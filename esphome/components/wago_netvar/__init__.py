import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_UPDATE_INTERVAL

DEPENDENCIES = ['network']
AUTO_LOAD = []

wago_netvar_ns = cg.esphome_ns.namespace('wago_netvar')
WagoNetVarComponent = wago_netvar_ns.class_('WagoNetVarComponent', cg.PollingComponent)

# Jawna definicja kluczy konfiguracyjnych jako stringi
CONF_IP = 'ip'
CONF_PORT = 'port'
CONF_COB_ID = 'cob_id'
CONF_CHECKSUM = 'checksum'
CONF_ENDIAN = 'endian'
CONF_PACK_BOOLS = 'pack_bools'
CONF_ALIGNMENT = 'alignment'
CONF_VARIABLES = 'variables'
CONF_VAR_NAME = 'name'
CONF_VAR_TYPE = 'type'

VARIABLE_SCHEMA = cv.Schema({
    cv.Required(CONF_VAR_NAME): cv.string,
    cv.Required(CONF_VAR_TYPE): cv.string,
})

CONFIG_SCHEMA = cv.schema_extender(cv.polling_component_schema('1s'))(cv.Schema({
    cv.GenerateID(): cv.declare_id(WagoNetVarComponent),
    cv.Required(CONF_IP): cv.ipv4,
    cv.Required(CONF_PORT): cv.port,
    cv.Required(CONF_COB_ID): cv.int_,
    cv.Required(CONF_CHECKSUM): cv.int_,
    cv.Optional(CONF_ENDIAN, default='little'): cv.one_of('little', 'big', lower=True),
    cv.Optional(CONF_PACK_BOOLS, default=False): cv.boolean,
    cv.Optional(CONF_ALIGNMENT, default=False): cv.boolean,
    cv.Required(CONF_VARIABLES): cv.ensure_list(VARIABLE_SCHEMA),
}))

async def to_code(config):
    var = cg.new_variable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_ip(str(config[CONF_IP])))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_cob_id(config[CONF_COB_ID]))
    cg.add(var.set_checksum(config[CONF_CHECKSUM]))
    cg.add(var.set_big_endian(config[CONF_ENDIAN] == 'big'))
    cg.add(var.set_pack_bools(config[CONF_PACK_BOOLS]))
    cg.add(var.set_alignment(config[CONF_ALIGNMENT]))

    for v in config[CONF_VARIABLES]:
        cg.add(var.add_variable(v[CONF_VAR_NAME], v[CONF_VAR_TYPE]))
