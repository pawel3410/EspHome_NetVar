import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, binary_sensor, number, switch
from esphome.const import (
    CONF_ID, CONF_PORT, CONF_IP_ADDRESS,
    CONF_UNIT_OF_MEASUREMENT, CONF_ACCURACY_DECIMALS
)

DEPENDENCIES = ['network']
AUTO_LOAD = ['sensor', 'binary_sensor', 'number', 'switch']

wago_netvar_ns = cg.esphome_ns.namespace('wago_netvar')
WagoNetVarComponent = wago_netvar_ns.class_('WagoNetVarComponent', cg.PollingComponent)

CONF_COB_ID = 'cob_id'
CONF_CHECKSUM = 'checksum'
CONF_DIRECTION = 'direction'
CONF_ENDIAN = 'endian'
CONF_PACK_BOOLS = 'pack_bools'
CONF_ALIGNMENT = 'alignment'
CONF_SEND_ON_CHANGE = 'send_on_change'
CONF_MIN_INTERVAL = 'min_interval'

CONF_VARIABLES = 'variables'
CONF_VAR_NAME = 'name'
CONF_VAR_TYPE = 'type'
CONF_SENSOR_NAME = 'sensor_name'
CONF_MIN_VALUE = 'min_value'
CONF_MAX_VALUE = 'max_value'
CONF_STEP = 'step'

VARIABLE_SCHEMA = cv.Schema({
    cv.Required(CONF_VAR_NAME): cv.string,
    cv.Required(CONF_VAR_TYPE): cv.string,
    cv.Optional(CONF_SENSOR_NAME): cv.string,
    cv.Optional(CONF_UNIT_OF_MEASUREMENT): cv.string,
    cv.Optional(CONF_ACCURACY_DECIMALS): cv.int_,
    cv.Optional(CONF_MIN_VALUE): cv.float_,
    cv.Optional(CONF_MAX_VALUE): cv.float_,
    cv.Optional(CONF_STEP, default=1.0): cv.float_,
})

CONFIG_SCHEMA = cv.polling_component_schema('1s').extend({
    cv.GenerateID(): cv.declare_id(WagoNetVarComponent),
    
    cv.Optional(CONF_IP_ADDRESS, default="255.255.255.255"): cv.string,
    cv.Optional(CONF_PORT, default=1202): cv.port,
    cv.Optional(CONF_COB_ID, default=1): cv.int_,
    cv.Optional(CONF_CHECKSUM, default=0): cv.int_,
    cv.Optional(CONF_DIRECTION, default='write'): cv.one_of('read', 'write', 'both', 'read_write', lower=True),

    cv.Optional(CONF_ENDIAN, default='little'): cv.one_of('little', 'big', lower=True),
    cv.Optional(CONF_PACK_BOOLS, default=False): cv.boolean,
    cv.Optional(CONF_ALIGNMENT, default=False): cv.boolean,
    
    cv.Optional(CONF_SEND_ON_CHANGE, default=True): cv.boolean,
    cv.Optional(CONF_MIN_INTERVAL, default="100ms"): cv.positive_time_period_milliseconds,

    cv.Optional(CONF_VARIABLES, default=[]): cv.ensure_list(VARIABLE_SCHEMA),
})

async def to_code(config):
    var = cg.Pvariable(config[CONF_ID], WagoNetVarComponent.new())
    await cg.register_component(var, config)

    cg.add(var.set_ip_address(str(config[CONF_IP_ADDRESS])))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_cob_id(config[CONF_COB_ID]))
    cg.add(var.set_checksum(config[CONF_CHECKSUM]))

    direction = config[CONF_DIRECTION]
    is_read = direction in ['read', 'both', 'read_write']
    is_write = direction in ['write', 'both', 'read_write']

    cg.add(var.set_enable_read(is_read))
    cg.add(var.set_enable_write(is_write))

    cg.add(var.set_big_endian(config[CONF_ENDIAN] == 'big'))
    cg.add(var.set_pack_bools(config[CONF_PACK_BOOLS]))
    cg.add(var.set_alignment(config[CONF_ALIGNMENT]))

    cg.add(var.set_send_on_change(config[CONF_SEND_ON_CHANGE]))
    cg.add(var.set_min_interval(config[CONF_MIN_INTERVAL].total_milliseconds))

    for v in config[CONF_VARIABLES]:
        var_name = v[CONF_VAR_NAME]
        var_type = v[CONF_VAR_TYPE]
        cg.add(var.add_variable(var_name, var_type))

        if CONF_SENSOR_NAME in v:
            s_name = v[CONF_SENSOR_NAME]
            
            if is_read:
                if var_type == "BOOL":
                    bs = cg.new_Pvariable(cg.Template(binary_sensor.BinarySensor))
                    cg.add(bs.set_name(s_name))
                    await binary_sensor.register_binary_sensor(bs, {})
                    cg.add(var.register_binary_sensor(var_name, bs))
                else:
                    s = cg.new_Pvariable(cg.Template(sensor.Sensor))
                    cg.add(s.set_name(s_name))
                    if CONF_UNIT_OF_MEASUREMENT in v:
                        cg.add(s.set_unit_of_measurement(v[CONF_UNIT_OF_MEASUREMENT]))
                    if CONF_ACCURACY_DECIMALS in v:
                        cg.add(s.set_accuracy_decimals(v[CONF_ACCURACY_DECIMALS]))
                    await sensor.register_sensor(s, {})
                    cg.add(var.register_sensor(var_name, s))

            elif is_write:
                if var_type == "BOOL":
                    sw = cg.new_Pvariable(wago_netvar_ns.class_('WagoSwitch', switch.Switch))
                    cg.add(sw.set_name(s_name))
                    cg.add(sw.set_parent(var, var_name))
                    await switch.register_switch(sw, {})
                else:
                    num = cg.new_Pvariable(wago_netvar_ns.class_('WagoNumber', number.Number))
                    cg.add(num.set_name(s_name))
                    cg.add(num.set_parent(var, var_name))
                    
                    min_v = v.get(CONF_MIN_VALUE, 0.0)
                    max_v = v.get(CONF_MAX_VALUE, 100.0)
                    step_v = v.get(CONF_STEP, 1.0)
                    
                    cg.add(num.set_min_value(min_v))
                    cg.add(num.set_max_value(max_v))
                    cg.add(num.set_step(step_v))
                    
                    if CONF_UNIT_OF_MEASUREMENT in v:
                        cg.add(num.set_unit_of_measurement(v[CONF_UNIT_OF_MEASUREMENT]))
                    await number.register_number(num, {})
