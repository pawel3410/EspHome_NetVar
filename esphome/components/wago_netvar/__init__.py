
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

MAX_UDP_PACKET_SIZE = 1500
NETVAR_HEADER_SIZE = 20
MAX_PAYLOAD_SIZE = MAX_UDP_PACKET_SIZE - NETVAR_HEADER_SIZE

wago_netvar_ns = cg.esphome_ns.namespace("wago_netvar")

WagoNetVarComponent = wago_netvar_ns.class_(
    "WagoNetVarComponent", cg.PollingComponent
)

WagoSwitch = wago_netvar_ns.class_("WagoSwitch", switch.Switch)
WagoNumber = wago_netvar_ns.class_("WagoNumber", number.Number)

CONF_LIST_ID = "list_ID"
CONF_VAR_COUNT = "var_count"
CONF_DIRECTION = "direction"
CONF_ENDIAN = "endian"
CONF_PACK_BOOLS = "pack_bools"
CONF_ALIGNMENT = "alignment"
CONF_SEND_ON_CHANGE = "send_on_change"
CONF_MIN_INTERVAL = "min_interval"

CONF_VARIABLES = "variables"
CONF_VAR_NAME = "name"
CONF_VAR_TYPE = "type"
CONF_VARIABLE = "variable"

CONF_SENSORS = "sensors"
CONF_BINARY_SENSORS = "binary_sensors"
CONF_TEXT_SENSORS = "text_sensors"
CONF_SWITCHES = "switches"
CONF_NUMBERS = "numbers"

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
    "REAL",
    "LREAL",
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
}

FLOAT_TYPES = {"REAL", "LREAL"}

VARIABLE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_VAR_NAME): cv.string_strict,
        cv.Required(CONF_VAR_TYPE): cv.string_strict,
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


def validate_variable_type(value):
    value = value.strip().upper()

    if value in SUPPORTED_TYPES:
        return value

    match = re.fullmatch(r"STRING(?:\((\d+)\))?", value)

    if match:
        if match.group(1) is None:
            return "STRING(80)"

        length = int(match.group(1))

        if length < 1 or length > 255:
            raise cv.Invalid(
                "STRING length must be between 1 and 255"
            )

        return f"STRING({length})"

    raise cv.Invalid(
        f"Unsupported NetVar type: {value}"
    )


def get_type_size(type_name):
    if type_name == "BOOL":
        return 1

    if type_name in {"BYTE", "USINT", "SINT"}:
        return 1

    if type_name in {"WORD", "UINT", "INT"}:
        return 2

    if type_name in {
        "DWORD",
        "UDINT",
        "DINT",
        "TIME",
        "REAL",
    }:
        return 4

    if type_name == "LREAL":
        return 8

    if type_name.startswith("STRING("):
        return int(type_name[7:-1]) + 1

    raise cv.Invalid(f"Unsupported type: {type_name}")


def get_type_alignment(type_name):
    if type_name in {"BOOL", "BYTE", "USINT", "SINT"}:
        return 1

    if type_name in {"WORD", "UINT", "INT"}:
        return 2

    if type_name in {"DWORD", "UDINT", "DINT", "TIME"}:
        return 4

    if type_name == "REAL":
        return 2

    if type_name == "LREAL":
        return 8

    return 1


def validate_config(config):
    direction = config[CONF_DIRECTION]

    variables = config.get(CONF_VARIABLES, [])

    if not variables:
        raise cv.Invalid(
            "At least one variable must be defined"
        )

    names = set()
    normalized_variables = []

    for variable in variables:
        name = variable[CONF_VAR_NAME]

        if not name:
            raise cv.Invalid("Variable name cannot be empty")

        if name in names:
            raise cv.Invalid(
                f"Duplicate variable name: {name}"
            )

        names.add(name)

        variable_type = validate_variable_type(
            variable[CONF_VAR_TYPE]
        )

        normalized_variables.append(
            {
                CONF_VAR_NAME: name,
                CONF_VAR_TYPE: variable_type,
            }
        )

    config[CONF_VARIABLES] = normalized_variables

    offset = 0
    bool_bit_index = 0

    for variable in normalized_variables:
        variable_type = variable[CONF_VAR_TYPE]

        if variable_type != "BOOL" and bool_bit_index > 0:
            offset += 1
            bool_bit_index = 0

        if variable_type == "BOOL" and config[CONF_PACK_BOOLS]:
            bool_bit_index += 1

            if bool_bit_index >= 8:
                bool_bit_index = 0
                offset += 1

            continue

        if config[CONF_ALIGNMENT]:
            alignment = get_type_alignment(variable_type)
            remainder = offset % alignment

            if remainder:
                offset += alignment - remainder

        offset += get_type_size(variable_type)

    if bool_bit_index:
        offset += 1

    if offset + NETVAR_HEADER_SIZE > MAX_UDP_PACKET_SIZE:
        raise cv.Invalid(
            f"NetVar packet exceeds {MAX_UDP_PACKET_SIZE} bytes"
        )

    variable_map = {
        variable[CONF_VAR_NAME]: variable[CONF_VAR_TYPE]
        for variable in normalized_variables
    }

    mappings = (
        (CONF_SENSORS, FLOAT_TYPES | INTEGER_TYPES),
        (CONF_BINARY_SENSORS, {"BOOL"}),
        (CONF_TEXT_SENSORS, set()),
        (CONF_SWITCHES, {"BOOL"}),
        (CONF_NUMBERS, FLOAT_TYPES | INTEGER_TYPES),
    )

    for mapping_name, allowed_types in mappings:
        for entity in config.get(mapping_name, []):
            variable_name = entity[CONF_VARIABLE]

            if variable_name not in variable_map:
                raise cv.Invalid(
                    f"{mapping_name}: variable '{variable_name}' "
                    "is not defined in variables"
                )

            variable_type = variable_map[variable_name]

            if mapping_name == CONF_TEXT_SENSORS:
                if not variable_type.startswith("STRING("):
                    raise cv.Invalid(
                        f"Text sensor '{variable_name}' "
                        "requires STRING type"
                    )
            elif variable_type not in allowed_types:
                raise cv.Invalid(
                    f"Entity '{variable_name}' has incompatible "
                    f"type '{variable_type}' in {mapping_name}"
                )

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
        raise cv.Invalid(
            "direction 'read' cannot contain switches or numbers"
        )

    if direction == "write" and read_entities:
        raise cv.Invalid(
            "direction 'write' cannot contain sensors or text_sensors"
        )

    if config[CONF_VAR_COUNT] > 0:
        if config[CONF_VAR_COUNT] != len(normalized_variables):
            raise cv.Invalid(
                "var_count must match the number of defined variables"
            )

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
                CONF_ENDIAN,
                default="little",
            ): cv.one_of("little", "big", lower=True),

            cv.Optional(
                CONF_PACK_BOOLS,
                default=False,
            ): cv.boolean,

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
                default="100ms",
            ): cv.positive_time_period_milliseconds,

            cv.Required(CONF_VARIABLES): cv.ensure_list(
                VARIABLE_SCHEMA
            ),

            cv.Optional(CONF_SENSORS): cv.ensure_list(
                SENSOR_SCHEMA
            ),

            cv.Optional(CONF_BINARY_SENSORS): cv.ensure_list(
                BINARY_SENSOR_SCHEMA
            ),

            cv.Optional(CONF_TEXT_SENSORS): cv.ensure_list(
                TEXT_SENSOR_SCHEMA
            ),

            cv.Optional(CONF_SWITCHES): cv.ensure_list(
                SWITCH_SCHEMA
            ),

            cv.Optional(CONF_NUMBERS): cv.ensure_list(
                NUMBER_SCHEMA
            ),
        }
    ).extend(cv.polling_component_schema("10s")),
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
    cg.add(var.set_big_endian(config[CONF_ENDIAN] == "big"))

    cg.add(var.set_pack_bools(config[CONF_PACK_BOOLS]))
    cg.add(var.set_alignment(config[CONF_ALIGNMENT]))

    cg.add(var.set_send_on_change(config[CONF_SEND_ON_CHANGE]))
    cg.add(var.set_min_interval(config[CONF_MIN_INTERVAL]))

    for variable in config[CONF_VARIABLES]:
        cg.add(
            var.add_variable(
                variable[CONF_VAR_NAME],
                variable[CONF_VAR_TYPE],
            )
        )

    for entity_config in config.get(CONF_SENSORS, []):
        entity = await sensor.new_sensor(entity_config)

        cg.add(
            var.register_sensor(
                entity_config[CONF_VARIABLE],
                entity,
            )
        )

    for entity_config in config.get(CONF_BINARY_SENSORS, []):
        entity = await binary_sensor.new_binary_sensor(entity_config)

        cg.add(
            var.register_binary_sensor(
                entity_config[CONF_VARIABLE],
                entity,
            )
        )

    for entity_config in config.get(CONF_TEXT_SENSORS, []):
        entity = await text_sensor.new_text_sensor(entity_config)

        cg.add(
            var.register_text_sensor(
                entity_config[CONF_VARIABLE],
                entity,
            )
        )

    for entity_config in config.get(CONF_SWITCHES, []):
        entity = await switch.new_switch(entity_config)

        cg.add(
            entity.set_parent(
                var,
                entity_config[CONF_VARIABLE],
            )
        )

        cg.add(
            var.register_switch(
                entity_config[CONF_VARIABLE],
                entity,
            )
        )

    for entity_config in config.get(CONF_NUMBERS, []):
        entity = await number.new_number(entity_config)

        cg.add(
            entity.set_parent(
                var,
                entity_config[CONF_VARIABLE],
            )
        )

        cg.add(
            var.register_number(
                entity_config[CONF_VARIABLE],
                entity,
            )
        )
