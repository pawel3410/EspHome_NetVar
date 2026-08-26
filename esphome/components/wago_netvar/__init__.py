import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, number, sensor, switch
from esphome.const import (
    CONF_ID,
    CONF_IP_ADDRESS,
    CONF_PORT,
)

# Flaga pozwalająca na tworzenie wielu instancji komponentu
MULTI_CONF = True

# Definicja przestrzeni nazw i klas C++
wago_netvar_ns = cg.esphome_ns.namespace("wago_netvar")
WagoNetVarComponent = wago_netvar_ns.class_(
    "WagoNetVarComponent", cg.PollingComponent
)
WagoSwitch = wago_netvar_ns.class_("WagoSwitch", switch.Switch)
WagoNumber = wago_netvar_ns.class_("WagoNumber", number.Number)

# Klucze konfiguracji Huba
CONF_COB_ID = "cob_id"
CONF_CHECKSUM = "checksum"
CONF_ENABLE_READ = "enable_read"
CONF_ENABLE_WRITE = "enable_write"
CONF_BIG_ENDIAN = "big_endian"
CONF_PACK_BOOLS = "pack_bools"
CONF_ALIGNMENT = "alignment"
CONF_SEND_ON_CHANGE = "send_on_change"
CONF_MIN_INTERVAL = "min_interval"
CONF_VARIABLES = "variables"
CONF_VAR_NAME = "name"
CONF_VAR_TYPE = "type"

# Klucze pod-platform
CONF_VARIABLE = "variable"
CONF_SENSORS = "sensors"
CONF_BINARY_SENSORS = "binary_sensors"
CONF_SWITCHES = "switches"
CONF_NUMBERS = "numbers"

# Schematy dla zmiennych oraz pod-encji
VARIABLE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_VAR_NAME): cv.string,
        cv.Required(CONF_VAR_TYPE): cv.string,
    }
)

SENSOR_SCHEMA = sensor.sensor_schema().extend(
    {
        cv.Required(CONF_VARIABLE): cv.string,
    }
)

BINARY_SENSOR_SCHEMA = binary_sensor.binary_sensor_schema().extend(
    {
        cv.Required(CONF_VARIABLE): cv.string,
    }
)

SWITCH_SCHEMA = switch.switch_schema(WagoSwitch).extend(
    {
        cv.Required(CONF_VARIABLE): cv.string,
    }
)

NUMBER_SCHEMA = number.number_schema(WagoNumber).extend(
    {
        cv.Required(CONF_VARIABLE): cv.string,
    }
)

# Główny schemat konfiguracji YAML
CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(WagoNetVarComponent),
            cv.Required(CONF_IP_ADDRESS): cv.string,
            cv.Optional(CONF_PORT, default=1202): cv.port,
            cv.Optional(CONF_COB_ID, default=1): cv.uint16_t,
            cv.Optional(CONF_CHECKSUM, default=0): cv.uint16_t,
            cv.Optional(CONF_ENABLE_READ, default=False): cv.boolean,
            cv.Optional(CONF_ENABLE_WRITE, default=True): cv.boolean,
            cv.Optional(CONF_BIG_ENDIAN, default=False): cv.boolean,
            cv.Optional(CONF_PACK_BOOLS, default=False): cv.boolean,
            cv.Optional(CONF_ALIGNMENT, default=True): cv.boolean,
            cv.Optional(CONF_SEND_ON_CHANGE, default=True): cv.boolean,
            cv.Optional(CONF_MIN_INTERVAL, default="100ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_VARIABLES): cv.ensure_list(VARIABLE_SCHEMA),
            cv.Optional(CONF_SENSORS): cv.ensure_list(SENSOR_SCHEMA),
            cv.Optional(CONF_BINARY_SENSORS): cv.ensure_list(BINARY_SENSOR_SCHEMA),
            cv.Optional(CONF_SWITCHES): cv.ensure_list(SWITCH_SCHEMA),
            cv.Optional(CONF_NUMBERS): cv.ensure_list(NUMBER_SCHEMA),
        }
    )
    .extend(cv.polling_component_schema("10s"))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Ustawienia główne
    cg.add(var.set_ip_address(config[CONF_IP_ADDRESS]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_cob_id(config[CONF_COB_ID]))
    cg.add(var.set_checksum(config[CONF_CHECKSUM]))
    cg.add(var.set_enable_read(config[CONF_ENABLE_READ]))
    cg.add(var.set_enable_write(config[CONF_ENABLE_WRITE]))
    cg.add(var.set_big_endian(config[CONF_BIG_ENDIAN]))
    cg.add(var.set_pack_bools(config[CONF_PACK_BOOLS]))
    cg.add(var.set_alignment(config[CONF_ALIGNMENT]))
    cg.add(var.set_send_on_change(config[CONF_SEND_ON_CHANGE]))
    cg.add(var.set_min_interval(config[CONF_MIN_INTERVAL]))

    # Deklaracje zmiennych PLC
    if CONF_VARIABLES in config:
        for v in config[CONF_VARIABLES]:
            cg.add(var.add_variable(v[CONF_VAR_NAME], v[CONF_VAR_TYPE]))

    # Rejestracja czujników (sensors)
    if CONF_SENSORS in config:
        for s_conf in config[CONF_SENSORS]:
            s = await sensor.new_sensor(s_conf)
            cg.add(var.register_sensor(s_conf[CONF_VARIABLE], s))

    # Rejestracja czujników binarnych (binary_sensors)
    if CONF_BINARY_SENSORS in config:
        for bs_conf in config[CONF_BINARY_SENSORS]:
            bs = await binary_sensor.new_binary_sensor(bs_conf)
            cg.add(var.register_binary_sensor(bs_conf[CONF_VARIABLE], bs))

    # Rejestracja przełączników (switches)
    if CONF_SWITCHES in config:
        for sw_conf in config[CONF_SWITCHES]:
            sw = await switch.new_switch(sw_conf)
            cg.add(sw.set_parent(var, sw_conf[CONF_VARIABLE]))

    # Rejestracja suwaków / wartości liczbowych (numbers)
    if CONF_NUMBERS in config:
        for num_conf in config[CONF_NUMBERS]:
            num = await number.new_number(num_conf)
            cg.add(num.set_parent(var, num_conf[CONF_VARIABLE]))
