import ipaddress
import re

import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import (
    binary_sensor,
    number,
    sensor,
    switch,
    text_sensor,
)
from esphome.const import (
    CONF_ID,
    CONF_IP_ADDRESS,
    CONF_PORT,
)

DEPENDENCIES = ["wifi"]
MULTI_CONF = True

MAX_UDP_PACKET_SIZE = 1472
NETVAR_HEADER_SIZE = 20
MAX_PAYLOAD_SIZE = MAX_UDP_PACKET_SIZE - NETVAR_HEADER_SIZE

wago_netvar_ns = cg.esphome_ns.namespace("wago_netvar")

WagoNetVarComponent = wago_netvar_ns.class_(
    "WagoNetVarComponent", cg.PollingComponent
)

WagoSwitch = wago_netvar_ns.class_("WagoSwitch", switch.Switch)
WagoNumber = wago_netvar_ns.class_("WagoNumber", number.Number)

CONF_LIST_ID = "list_id"
CONF_VAR_COUNT = "var_count"
CONF_DIRECTION = "direction"
CONF_ALIGNMENT = "alignment"
CONF_SEND_ON_CHANGE = "send_on_change"
CONF_MIN_INTERVAL = "min_interval"

CONF_VARIABLES = "variables"
CONF_VAR_NAME = "name"
CONF_VAR_TYPE = "type"
CONF_BYTE_OFFSET = "byte_offset"
CONF_VARIABLE = "variable"

CONF_SENSORS = "sensors"
CONF_BINARY_SENSORS = "binary_sensors"
CONF_TEXT_SENSORS = "text_sensors"
CONF_SWITCHES = "switches"
CONF_NUMBERS = "numbers"

CONF_REJECTED_PACKETS = "rejected_packets"
CONF_RX_PACKETS = "rx_packets"
CONF_TX_PACKETS = "tx_packets"
CONF_RX_ERRORS = "rx_errors"
CONF_TX_ERRORS = "tx_errors"

TYPE_ALIASES = {
    "TOD": "TIME_OF_DAY",
    "DT": "DATE_AND_TIME",
}

SUPPORTED_TYPES = {
    "BOOL",
    "BYTE",
    "USINT",
    "SINT",
    "WORD",
    "UINT",
    "INT",
    "DWORD",
    "UDINT",
    "DINT",
    "TIME",
    "DATE",
    "TIME_OF_DAY",
    "DATE_AND_TIME",
    "REAL",
}

INTEGER_TYPES = {
    "BYTE",
    "USINT",
    "SINT",
    "WORD",
    "UINT",
    "INT",
    "DWORD",
    "UDINT",
    "DINT",
    "TIME",
    "DATE",
    "TIME_OF_DAY",
    "DATE_AND_TIME",
}

FLOAT_TYPES = {"REAL"}

VARIABLE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_VAR_NAME): cv.string_strict,
        cv.Required(CONF_VAR_TYPE): cv.string_strict,
        cv.Optional(CONF_BYTE_OFFSET): cv.uint16_t,
    }
)

SENSOR_SCHEMA = sensor.sensor_schema().extend(
    {
        cv.Required(CONF_VARIABLE): cv.string_strict,
    }
)

BINARY_SENSOR_SCHEMA = binary_sensor.binary_sensor_schema().extend(
    {
        cv.Required(CONF_VARIABLE): cv.string_strict,
    }
)

TEXT_SENSOR_SCHEMA = text_sensor.text_sensor_schema().extend(
    {
        cv.Required(CONF_VARIABLE): cv.string_strict,
    }
)

SWITCH_SCHEMA = switch.switch_schema(WagoSwitch).extend(
    {
        cv.Required(CONF_VARIABLE): cv.string_strict,
    }
)

NUMBER_SCHEMA = number.number_schema(WagoNumber).extend(
    {
        cv.Required(CONF_VARIABLE): cv.string_strict,
    }
)

STATS_SENSOR_SCHEMA = sensor.sensor_schema()


def validate_variable_type(value):
    value = value.strip().upper()
    value = TYPE_ALIASES.get(value, value)

    if value in SUPPORTED_TYPES:
        return value

    match = re.fullmatch(r"STRING(?:\((\d+)\))?", value)

    if match:
        if match.group(1) is None:
            return "STRING(80)"

        length = int(match.group(1))

        if length < 1 or length > 255:
            raise cv.Invalid("STRING length must be between 1 and 255")

        return f"STRING({length})"

    raise cv.Invalid(f"Unsupported NetVar type: {value}")


def get_type_size(type_name):
    if type_name in {"BOOL", "BYTE", "USINT", "SINT"}:
        return 1
    if type_name in {"WORD", "UINT", "INT"}:
        return 2
    if type_name in {
        "DWORD",
        "UDINT",
        "DINT",
        "TIME",
        "DATE",
        "TIME_OF_DAY",
        "DATE_AND_TIME",
        "REAL",
    }:
        return 4
    if type_name.startswith("STRING("):
        return int(type_name[7:-1]) + 1

    raise cv.Invalid(f"Unsupported type: {type_name}")


def get_type_alignment(type_name):
    if type_name in {"BOOL", "BYTE", "USINT", "SINT"}:
        return 1
    if type_name in {"WORD", "UINT", "INT"}:
        return 2
    if type_name in {
        "DWORD",
        "UDINT",
        "DINT",
        "TIME",
        "DATE",
        "TIME_OF_DAY",
        "DATE_AND_TIME",
    }:
        return 4
    if type_name == "REAL":
        return 2
    return 1


def get_cpp_type(type_name):
    if type_name in {
        "DATE",
        "TIME_OF_DAY",
        "DATE_AND_TIME",
    }:
        return "UDINT"
    return type_name


def ranges_overlap(start_a, size_a, start_b, size_b):
    return (
        start_a < start_b + size_b
        and start_b < start_a + size_a
    )


def calculate_layout(config, variables):
    reserved = []
    layout = {}
    payload_size = 0

    for variable in variables:
        if CONF_BYTE_OFFSET not in variable:
            continue

        name = variable[CONF_VAR_NAME]
        offset = variable[CONF_BYTE_OFFSET]
        size = get_type_size(variable[CONF_VAR_TYPE])

        if offset + size > MAX_PAYLOAD_SIZE:
            raise cv.Invalid(
                f"Variable '{name}' exceeds maximum payload size of {MAX_PAYLOAD_SIZE} bytes"
            )

        for other_name, other_offset, other_size in reserved:
            if ranges_overlap(offset, size, other_offset, other_size):
                raise cv.Invalid(
                    f"Variables '{name}' and '{other_name}' occupy overlapping payload bytes"
                )

        reserved.append((name, offset, size))
        layout[name] = offset
        payload_size = max(payload_size, offset + size)

    cursor = 0

    for variable in variables:
        name = variable[CONF_VAR_NAME]

        if name in layout:
            continue

        variable_type = variable[CONF_VAR_TYPE]
        size = get_type_size(variable_type)
        alignment = (
            get_type_alignment(variable_type)
            if config[CONF_ALIGNMENT]
            else 1
        )

        while True:
            remainder = cursor % alignment
            if remainder:
                cursor += alignment - remainder

            collision = None
            for other_name, other_offset, other_size in reserved:
                if ranges_overlap(cursor, size, other_offset, other_size):
                    collision = (other_offset, other_size)
                    break

            if collision is None:
                break

            cursor = collision[0] + collision[1]

        if cursor + size > MAX_PAYLOAD_SIZE:
            raise cv.Invalid(
                f"Variable '{name}' exceeds maximum payload size of {MAX_PAYLOAD_SIZE} bytes"
            )

        layout[name] = cursor
        reserved.append((name, cursor, size))
        payload_size = max(payload_size, cursor + size)
        cursor += size

    if payload_size > MAX_PAYLOAD_SIZE:
        raise cv.Invalid(f"NetVar payload exceeds {MAX_PAYLOAD_SIZE} bytes")

    return layout, payload_size


def validate_config(config):
    direction = config[CONF_DIRECTION]
    variables = config.get(CONF_VARIABLES, [])

    if not variables:
        raise cv.Invalid("At least one variable must be defined")

    names = set()
    normalized_variables = []

    for variable in variables:
        name = variable[CONF_VAR_NAME]
        if not name:
            raise cv.Invalid("Variable name cannot be empty")
        if name in names:
            raise cv.Invalid(f"Duplicate variable name: {name}")

        names.add(name)
        variable_type = validate_variable_type(variable[CONF_VAR_TYPE])

        normalized = {
            CONF_VAR_NAME: name,
            CONF_VAR_TYPE: variable_type,
        }

        if CONF_BYTE_OFFSET in variable:
            normalized[CONF_BYTE_OFFSET] = variable[CONF_BYTE_OFFSET]

        normalized_variables.append(normalized)

    config[CONF_VARIABLES] = normalized_variables
    calculate_layout(config, normalized_variables)

    # In 'read' direction, byte_offset must be specified manually for each variable
    # (some variables can be skipped by leaving gaps).
    if direction == "read":
        for variable in normalized_variables:
            if CONF_BYTE_OFFSET not in variable:
                raise cv.Invalid(
                    f"direction 'read' requires manual byte_offset for variable '{variable[CONF_VAR_NAME]}'"
                )

    variable_map = {
        variable[CONF_VAR_NAME]: variable[CONF_VAR_TYPE]
        for variable in normalized_variables
    }

    mappings = (
        (CONF_SENSORS, FLOAT_TYPES | INTEGER_TYPES),
        (CONF_BINARY_SENSORS, {"BOOL"}),
        # Text sensors require STRING type, so the set of allowed types is empty.
        # Validation is performed in the special if branch below.
        (CONF_TEXT_SENSORS, set()),
        (CONF_SWITCHES, {"BOOL"}),
        (CONF_NUMBERS, FLOAT_TYPES | INTEGER_TYPES),
    )

    mapped_variables = {}

    for mapping_name, allowed_types in mappings:
        for entity in config.get(mapping_name, []):
            variable_name = entity[CONF_VARIABLE]

            if variable_name not in variable_map:
                raise cv.Invalid(
                    f"{mapping_name}: variable '{variable_name}' is not defined in variables"
                )

            variable_type = variable_map[variable_name]

            if mapping_name == CONF_TEXT_SENSORS:
                if not variable_type.startswith("STRING("):
                    raise cv.Invalid(f"Text sensor '{variable_name}' requires STRING type")
            elif variable_type not in allowed_types:
                raise cv.Invalid(
                    f"Entity '{variable_name}' has incompatible type '{variable_type}' in {mapping_name}"
                )

            if variable_name in mapped_variables:
                raise cv.Invalid(f"Variable '{variable_name}' is mapped more than once")

            mapped_variables[variable_name] = mapping_name

    read_entities = (
        config.get(CONF_SENSORS, [])
        + config.get(CONF_BINARY_SENSORS, [])
        + config.get(CONF_TEXT_SENSORS, [])
    )

    write_entities = (
        config.get(CONF_SWITCHES, [])
        + config.get(CONF_NUMBERS, [])
    )

    if direction == "read" and write_entities:
        raise cv.Invalid("direction 'read' cannot contain switches or numbers")

    if direction == "write" and read_entities:
        raise cv.Invalid("direction 'write' cannot contain sensors or text_sensors")

    if config[CONF_VAR_COUNT] > 0:
        if config[CONF_VAR_COUNT] != len(normalized_variables):
            raise cv.Invalid("var_count must match the number of defined variables")

    if config[CONF_VAR_COUNT] == 0:
        config[CONF_VAR_COUNT] = len(normalized_variables)

    return config


def validate_ip_address(value):
    try:
        return str(ipaddress.IPv4Address(value))
    except ipaddress.AddressValueError as err:
        raise cv.Invalid("A valid IPv4 address is required") from err


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(WagoNetVarComponent),
            cv.Required(CONF_IP_ADDRESS): validate_ip_address,
            cv.Optional(CONF_PORT, default=1202): cv.port,
            cv.Optional(CONF_LIST_ID, default=1): cv.uint16_t,
            cv.Optional(CONF_VAR_COUNT, default=0): cv.uint16_t,
            cv.Optional(
                CONF_DIRECTION,
                default="write",
            ): cv.one_of("read", "write", lower=True),
            cv.Optional(
                CONF_ALIGNMENT,
                default=False,
            ): cv.boolean,
            cv.Optional(
                CONF_SEND_ON_CHANGE,
                default=False,
            ): cv.boolean,
            cv.Optional(
                CONF_MIN_INTERVAL,
                default="500ms",
            ): cv.positive_time_period_milliseconds,
            cv.Required(CONF_VARIABLES): cv.ensure_list(VARIABLE_SCHEMA),
            cv.Optional(CONF_SENSORS): cv.ensure_list(SENSOR_SCHEMA),
            cv.Optional(CONF_BINARY_SENSORS): cv.ensure_list(BINARY_SENSOR_SCHEMA),
            cv.Optional(CONF_TEXT_SENSORS): cv.ensure_list(TEXT_SENSOR_SCHEMA),
            cv.Optional(CONF_SWITCHES): cv.ensure_list(SWITCH_SCHEMA),
            cv.Optional(CONF_NUMBERS): cv.ensure_list(NUMBER_SCHEMA),
            cv.Optional(CONF_REJECTED_PACKETS): STATS_SENSOR_SCHEMA,
            cv.Optional(CONF_RX_PACKETS): STATS_SENSOR_SCHEMA,
            cv.Optional(CONF_TX_PACKETS): STATS_SENSOR_SCHEMA,
            cv.Optional(CONF_RX_ERRORS): STATS_SENSOR_SCHEMA,
            cv.Optional(CONF_TX_ERRORS): STATS_SENSOR_SCHEMA,
        }
    ).extend(cv.polling_component_schema("5s")),
    validate_config,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await cg.register_component(var, config)

    cg.add(var.set_ip_address(config[CONF_IP_ADDRESS]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_list_id(config[CONF_LIST_ID]))
    cg.add(var.set_var_count(config[CONF_VAR_COUNT]))
    cg.add(var.set_direction(config[CONF_DIRECTION]))
    cg.add(var.set_alignment(config[CONF_ALIGNMENT]))
    cg.add(var.set_send_on_change(config[CONF_SEND_ON_CHANGE]))
    cg.add(var.set_min_interval(config[CONF_MIN_INTERVAL]))

    for variable in config[CONF_VARIABLES]:
        byte_offset = variable.get(CONF_BYTE_OFFSET, -1)
        cpp_type = get_cpp_type(variable[CONF_VAR_TYPE])

        cg.add(
            var.add_variable(
                variable[CONF_VAR_NAME],
                cpp_type,
                byte_offset,
            )
        )

    for entity_config in config.get(CONF_SENSORS, []):
        entity = await sensor.new_sensor(entity_config)
        cg.add(var.register_sensor(entity_config[CONF_VARIABLE], entity))

    for entity_config in config.get(CONF_BINARY_SENSORS, []):
        entity = await binary_sensor.new_binary_sensor(entity_config)
        cg.add(var.register_binary_sensor(entity_config[CONF_VARIABLE], entity))

    for entity_config in config.get(CONF_TEXT_SENSORS, []):
        entity = await text_sensor.new_text_sensor(entity_config)
        cg.add(var.register_text_sensor(entity_config[CONF_VARIABLE], entity))

    for entity_config in config.get(CONF_SWITCHES, []):
        entity = await switch.new_switch(entity_config)
        cg.add(entity.set_parent(var, entity_config[CONF_VARIABLE]))
        cg.add(var.register_switch(entity_config[CONF_VARIABLE], entity))

    for entity_config in config.get(CONF_NUMBERS, []):
        entity = await number.new_number(entity_config)
        cg.add(entity.set_parent(var, entity_config[CONF_VARIABLE]))
        cg.add(var.register_number(entity_config[CONF_VARIABLE], entity))

    if CONF_REJECTED_PACKETS in config:
        sens = await sensor.new_sensor(config[CONF_REJECTED_PACKETS])
        cg.add(var.set_rejected_packets_sensor(sens))
    if CONF_RX_PACKETS in config:
        sens = await sensor.new_sensor(config[CONF_RX_PACKETS])
        cg.add(var.set_rx_packets_sensor(sens))
    if CONF_TX_PACKETS in config:
        sens = await sensor.new_sensor(config[CONF_TX_PACKETS])
        cg.add(var.set_tx_packets_sensor(sens))
    if CONF_RX_ERRORS in config:
        sens = await sensor.new_sensor(config[CONF_RX_ERRORS])
        cg.add(var.set_rx_errors_sensor(sens))
    if CONF_TX_ERRORS in config:
        sens = await sensor.new_sensor(config[CONF_TX_ERRORS])
        cg.add(var.set_tx_errors_sensor(sens))
