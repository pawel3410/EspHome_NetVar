import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_IP_ADDRESS, CONF_PORT

wago_netvar_ns = cg.esphome_ns.namespace('wago_netvar')
WagoNetVarComponent = wago_netvar_ns.class_('WagoNetVarComponent', cg.PollingComponent)

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

VARIABLE_SCHEMA = cv.Schema({
    cv.Required(CONF_VAR_NAME): cv.string,
    cv.Required(CONF_VAR_TYPE): cv.string,
})

CONFIG_SCHEMA = cv.Schema({
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
}).extend(cv.POLLING_COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
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

    if CONF_VARIABLES in config:
        for v in config[CONF_VARIABLES]:
            cg.add(var.add_variable(v[CONF_VAR_NAME], v[CONF_VAR_TYPE]))
