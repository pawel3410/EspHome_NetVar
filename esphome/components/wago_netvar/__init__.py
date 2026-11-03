import ipaddress
import re

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import binary_sensor, number, sensor, switch, text_sensor
from esphome.const import (
    CONF_ID,
    CONF_IP_ADDRESS,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_NAME,
    CONF_PORT,
    CONF_STEP,
    CONF_UPDATE_INTERVAL,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_TOTAL_INCREASING,
)

DOMAIN = "wago_netvar"

# "network" zamiast "wifi": komponent działa zarówno przez Wi-Fi, jak i Ethernet.
DEPENDENCIES = ["network"]
# Komponent tworzy encje wszystkich tych rodzajów (sensory statystyk powstają automatycznie).
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor", "switch", "number"]
MULTI_CONF = True

MAX_UDP_PACKET_SIZE = 1472
NETVAR_HEADER_SIZE = 20
MAX_PAYLOAD_SIZE = MAX_UDP_PACKET_SIZE - NETVAR_HEADER_SIZE
SCHEDULER_DONT_RUN = 4294967295  # wartość używana przez ESPHome dla "update_interval: never"

wago_netvar_ns = cg.esphome_ns.namespace("wago_netvar")

WagoNetVarComponent = wago_netvar_ns.class_("WagoNetVarComponent", cg.PollingComponent)
WagoSwitch = wago_netvar_ns.class_("WagoSwitch", switch.Switch)
WagoNumber = wago_netvar_ns.class_("WagoNumber", number.Number)

CONF_LIST_ID = "list_id"
CONF_VAR_COUNT = "var_count"
CONF_DIRECTION = "direction"
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

# klucz konfiguracji -> (nazwa settera w C++, domyślny przyrostek nazwy encji)
STATS_SENSORS = {
    CONF_REJECTED_PACKETS: ("set_rejected_packets_sensor", "rejected packets"),
    CONF_RX_PACKETS: ("set_rx_packets_sensor", "rx packets"),
    CONF_TX_PACKETS: ("set_tx_packets_sensor", "tx packets"),
    CONF_RX_ERRORS: ("set_rx_errors_sensor", "rx errors"),
    CONF_TX_ERRORS: ("set_tx_errors_sensor", "tx errors"),
}

TYPE_ALIASES = {
    "TOD": "TIME_OF_DAY",
    "DT": "DATE_AND_TIME",
}

# typ -> rozmiar w bajtach; LREAL celowo pominięty (nieobsługiwany przez NetVar)
TYPE_SIZES = {
    "BOOL": 1,
    "BYTE": 1,
    "USINT": 1,
    "SINT": 1,
    "WORD": 2,
    "UINT": 2,
    "INT": 2,
    "DWORD": 4,
    "UDINT": 4,
    "DINT": 4,
    "TIME": 4,
    "DATE": 4,
    "TIME_OF_DAY": 4,
    "DATE_AND_TIME": 4,
    "REAL": 4,
}

NUMERIC_TYPES = set(TYPE_SIZES) - {"BOOL"}

# Offset jest wymagany zawsze (zarówno dla "read", jak i "write"): dzięki temu w trybie "read"
# można pominąć wybrane zmienne, a w trybie "write" układ ramki jest jawny i zgodny ze sterownikiem.
VARIABLE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_VAR_NAME): cv.string_strict,
        cv.Required(CONF_VAR_TYPE): cv.string_strict,
        cv.Required(CONF_BYTE_OFFSET): cv.int_range(min=0, max=MAX_PAYLOAD_SIZE - 1),
    }
)

SENSOR_SCHEMA = sensor.sensor_schema().extend(
    {cv.Required(CONF_VARIABLE): cv.string_strict}
)

BINARY_SENSOR_SCHEMA = binary_sensor.binary_sensor_schema().extend(
    {cv.Required(CONF_VARIABLE): cv.string_strict}
)

TEXT_SENSOR_SCHEMA = text_sensor.text_sensor_schema().extend(
    {cv.Required(CONF_VARIABLE): cv.string_strict}
)

SWITCH_SCHEMA = switch.switch_schema(WagoSwitch).extend(
    {cv.Required(CONF_VARIABLE): cv.string_strict}
)

# number.new_number() wymaga min_value / max_value / step, więc muszą być częścią schematu.
NUMBER_SCHEMA = number.number_schema(WagoNumber).extend(
    {
        cv.Required(CONF_VARIABLE): cv.string_strict,
        cv.Required(CONF_MIN_VALUE): cv.float_,
        cv.Required(CONF_MAX_VALUE): cv.float_,
        cv.Optional(CONF_STEP, default=1.0): cv.positive_float,
    }
)

STATS_SENSOR_SCHEMA = sensor.sensor_schema(
    accuracy_decimals=0,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    state_class=STATE_CLASS_TOTAL_INCREASING,
)


def validate_variable_type(value):
    """Normalizuje nazwę typu (wielkość liter, aliasy, STRING bez długości = STRING(80))."""
    value = value.strip().upper()
    value = TYPE_ALIASES.get(value, value)

    if value in TYPE_SIZES:
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


def is_string_type(type_name):
    return type_name.startswith("STRING(")


def get_type_size(type_name):
    if is_string_type(type_name):
        return int(type_name[7:-1]) + 1  # + kończący bajt 0x00
    return TYPE_SIZES[type_name]


def ranges_overlap(start_a, size_a, start_b, size_b):
    return start_a < start_b + size_b and start_b < start_a + size_a


def validate_layout(variables):
    """Sprawdza ręcznie podane offsety: zakres ładunku oraz brak nakładania się zmiennych."""
    reserved = []  # (nazwa, offset, rozmiar)

    for variable in variables:
        name = variable[CONF_VAR_NAME]
        offset = variable[CONF_BYTE_OFFSET]
        size = get_type_size(variable[CONF_VAR_TYPE])

        if offset + size > MAX_PAYLOAD_SIZE:
            raise cv.Invalid(
                f"Variable '{name}' exceeds the maximum payload size of {MAX_PAYLOAD_SIZE} bytes"
            )
        for other_name, other_offset, other_size in reserved:
            if ranges_overlap(offset, size, other_offset, other_size):
                raise cv.Invalid(
                    f"Variables '{name}' and '{other_name}' occupy overlapping payload bytes"
                )
        reserved.append((name, offset, size))


def validate_ip_address(value):
    try:
        return str(ipaddress.IPv4Address(cv.string(value)))
    except ValueError as err:  # ipaddress.AddressValueError dziedziczy po ValueError
        raise cv.Invalid("A valid IPv4 address is required") from err


def allowed_for_mapping(mapping_name, variable_type):
    if mapping_name == CONF_SENSORS:
        return variable_type in NUMERIC_TYPES
    if mapping_name in (CONF_BINARY_SENSORS, CONF_SWITCHES):
        return variable_type == "BOOL"
    if mapping_name == CONF_TEXT_SENSORS:
        return is_string_type(variable_type)
    if mapping_name == CONF_NUMBERS:
        # liczba zapisywana do zmiennej STRING jest zamieniana na tekst
        return variable_type in NUMERIC_TYPES or is_string_type(variable_type)
    return False


def validate_config(config):
    direction = config[CONF_DIRECTION]
    ip_address = config[CONF_IP_ADDRESS]
    variables = config[CONF_VARIABLES]

    if not variables:
        raise cv.Invalid("At least one variable must be defined")

    if direction == "write" and ip_address == "0.0.0.0":
        raise cv.Invalid("ip_address 0.0.0.0 is not allowed for direction 'write'")
    if direction == "read" and ip_address == "255.255.255.255":
        raise cv.Invalid(
            "ip_address 255.255.255.255 cannot be a sender address; use 0.0.0.0 to accept any sender"
        )

    names = set()
    normalized_variables = []
    for variable in variables:
        name = variable[CONF_VAR_NAME]
        if not name:
            raise cv.Invalid("Variable name cannot be empty")
        if name in names:
            raise cv.Invalid(f"Duplicate variable name: {name}")
        names.add(name)

        normalized_variables.append(
            {
                CONF_VAR_NAME: name,
                CONF_VAR_TYPE: validate_variable_type(variable[CONF_VAR_TYPE]),
                CONF_BYTE_OFFSET: variable[CONF_BYTE_OFFSET],
            }
        )

    validate_layout(normalized_variables)
    config[CONF_VARIABLES] = normalized_variables

    # var_count to liczba zmiennych na liście nadawcy/odbiorcy. Może być większa niż liczba
    # zmiennych zdefiniowanych tutaj (pominięte zmienne), ale nie mniejsza.
    defined = len(normalized_variables)
    if config[CONF_VAR_COUNT] == 0:
        config[CONF_VAR_COUNT] = defined
    elif config[CONF_VAR_COUNT] < defined:
        raise cv.Invalid(
            f"var_count ({config[CONF_VAR_COUNT]}) cannot be lower than the number of defined variables ({defined})"
        )

    variable_types = {v[CONF_VAR_NAME]: v[CONF_VAR_TYPE] for v in normalized_variables}
    mapped_variables = set()
    for mapping_name in (
        CONF_SENSORS,
        CONF_BINARY_SENSORS,
        CONF_TEXT_SENSORS,
        CONF_SWITCHES,
        CONF_NUMBERS,
    ):
        for entity in config.get(mapping_name, []):
            variable_name = entity[CONF_VARIABLE]
            if variable_name not in variable_types:
                raise cv.Invalid(
                    f"{mapping_name}: variable '{variable_name}' is not defined in variables"
                )
            if not allowed_for_mapping(mapping_name, variable_types[variable_name]):
                raise cv.Invalid(
                    f"{mapping_name}: variable '{variable_name}' has incompatible type '{variable_types[variable_name]}'"
                )
            if variable_name in mapped_variables:
                raise cv.Invalid(f"Variable '{variable_name}' is mapped more than once")
            mapped_variables.add(variable_name)

            if mapping_name == CONF_NUMBERS and entity[CONF_MIN_VALUE] >= entity[CONF_MAX_VALUE]:
                raise cv.Invalid(
                    f"numbers: min_value must be lower than max_value ('{variable_name}')"
                )

    read_entities = (
        config.get(CONF_SENSORS, [])
        + config.get(CONF_BINARY_SENSORS, [])
        + config.get(CONF_TEXT_SENSORS, [])
    )
    write_entities = config.get(CONF_SWITCHES, []) + config.get(CONF_NUMBERS, [])
    if direction == "read" and write_entities:
        raise cv.Invalid("direction 'read' cannot contain switches or numbers")
    if direction == "write" and read_entities:
        raise cv.Invalid("direction 'write' cannot contain sensors, binary_sensors or text_sensors")

    # update_interval steruje wyłącznie cykliczną wysyłką, więc w trybie "read" jest zbędny.
    # Wartość 0s oznacza "brak wysyłki cyklicznej"; ESPHome wywoływałby update() w każdej
    # iteracji pętli, dlatego 0s jest zamieniane na "never".
    interval = config[CONF_UPDATE_INTERVAL]
    if direction == "read" or getattr(interval, "total_milliseconds", interval) == 0:
        config[CONF_UPDATE_INTERVAL] = SCHEDULER_DONT_RUN

    # Sensory statystyk powstają automatycznie dla każdej listy; podanie ich w YAML tylko je dostosowuje.
    prefix = str(config[CONF_ID].id).replace("_", " ")
    for key, (_setter, suffix) in STATS_SENSORS.items():
        if key not in config:
            config[key] = STATS_SENSOR_SCHEMA({CONF_NAME: f"{prefix} {suffix}"})

    return config


def final_validate(config):
    # Listy "read" mogą współdzielić port UDP (jedno gniazdo, ramki rozdzielane według list_ID i IP nadawcy).
    # Niedozwolone są tylko listy, których nie da się odróżnić: ten sam port, ten sam list_ID oraz
    # ten sam adres nadawcy albo któryś z nich równy 0.0.0.0 (akceptuje dowolnego nadawcę).
    if config[CONF_DIRECTION] != "read":
        return config
    for other in fv.full_config.get().get(DOMAIN, []):
        if other[CONF_ID].id == config[CONF_ID].id or other[CONF_DIRECTION] != "read":
            continue
        if other[CONF_PORT] != config[CONF_PORT] or other[CONF_LIST_ID] != config[CONF_LIST_ID]:
            continue
        ips = (config[CONF_IP_ADDRESS], other[CONF_IP_ADDRESS])
        if ips[0] == ips[1] or "0.0.0.0" in ips:
            raise cv.Invalid(
                f"Read lists '{config[CONF_ID].id}' and '{other[CONF_ID].id}' share UDP port "
                f"{config[CONF_PORT]} and list_id {config[CONF_LIST_ID]} and cannot be told apart; "
                f"use a different list_id or a different (non-0.0.0.0) ip_address"
            )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.declare_id(WagoNetVarComponent),
            cv.Required(CONF_IP_ADDRESS): validate_ip_address,
            cv.Optional(CONF_PORT, default=1202): cv.port,
            cv.Optional(CONF_LIST_ID, default=1): cv.uint16_t,
            cv.Optional(CONF_VAR_COUNT, default=0): cv.uint16_t,
            cv.Optional(CONF_DIRECTION, default="write"): cv.one_of("read", "write", lower=True),
            cv.Optional(CONF_SEND_ON_CHANGE, default=False): cv.boolean,
            cv.Optional(
                CONF_MIN_INTERVAL, default="500ms"
            ): cv.positive_time_period_milliseconds,
            cv.Required(CONF_VARIABLES): cv.ensure_list(VARIABLE_SCHEMA),
            cv.Optional(CONF_SENSORS): cv.ensure_list(SENSOR_SCHEMA),
            cv.Optional(CONF_BINARY_SENSORS): cv.ensure_list(BINARY_SENSOR_SCHEMA),
            cv.Optional(CONF_TEXT_SENSORS): cv.ensure_list(TEXT_SENSOR_SCHEMA),
            cv.Optional(CONF_SWITCHES): cv.ensure_list(SWITCH_SCHEMA),
            cv.Optional(CONF_NUMBERS): cv.ensure_list(NUMBER_SCHEMA),
            **{cv.Optional(key): STATS_SENSOR_SCHEMA for key in STATS_SENSORS},
        }
    ).extend(cv.polling_component_schema("5s")),
    validate_config,
)

FINAL_VALIDATE_SCHEMA = final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    # register_component() ustawia także update_interval komponentu PollingComponent.
    await cg.register_component(var, config)

    cg.add(var.set_ip_address(config[CONF_IP_ADDRESS]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_list_id(config[CONF_LIST_ID]))
    cg.add(var.set_var_count(config[CONF_VAR_COUNT]))
    cg.add(var.set_direction(config[CONF_DIRECTION]))
    cg.add(var.set_send_on_change(config[CONF_SEND_ON_CHANGE]))
    cg.add(var.set_min_interval(config[CONF_MIN_INTERVAL]))

    for variable in config[CONF_VARIABLES]:
        cg.add(
            var.add_variable(
                variable[CONF_VAR_NAME],
                variable[CONF_VAR_TYPE],
                variable[CONF_BYTE_OFFSET],
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
        entity = await number.new_number(
            entity_config,
            min_value=entity_config[CONF_MIN_VALUE],
            max_value=entity_config[CONF_MAX_VALUE],
            step=entity_config[CONF_STEP],
        )
        cg.add(entity.set_parent(var, entity_config[CONF_VARIABLE]))
        cg.add(var.register_number(entity_config[CONF_VARIABLE], entity))

    for key, (setter, _suffix) in STATS_SENSORS.items():
        sens = await sensor.new_sensor(config[key])
        cg.add(getattr(var, setter)(sens))
