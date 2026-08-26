#pragma once

#include "esphome.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include <WiFiUdp.h>
#include <vector>
#include <string>
#include <map>

namespace esphome {
namespace wago_netvar {

class WagoNetVarComponent;

class WagoSwitch : public switch_::Switch {
public:
    void set_parent(WagoNetVarComponent *parent, const std::string &var_name) { parent_ = parent; var_name_ = var_name; }
protected:
    void write_state(bool state) override;
    WagoNetVarComponent *parent_{nullptr};
    std::string var_name_;
};

class WagoNumber : public number::Number {
public:
    void set_parent(WagoNetVarComponent *parent, const std::string &var_name) { parent_ = parent; var_name_ = var_name; }
protected:
    void control(float value) override;
    WagoNetVarComponent *parent_{nullptr};
    std::string var_name_;
};

struct VarDef {
    std::string name;
    std::string type;
    size_t size;
    size_t align;
};

class WagoNetVarComponent : public PollingComponent {
public:
    void set_ip_address(const std::string &ip) { ip_address_ = ip; }
    void set_port(uint16_t port) { port_ = port; }
    void set_cob_id(uint16_t cob_id) { cob_id_ = cob_id; }
    void set_checksum(uint16_t checksum) { checksum_ = checksum; }

    void set_enable_read(bool enable) { enable_read_ = enable; }
    void set_enable_write(bool enable) { enable_write_ = enable; }

    void set_big_endian(bool big_endian) { big_endian_ = big_endian; }
    void set_pack_bools(bool pack) { pack_bools_ = pack; }
    void set_alignment(bool align) { alignment_ = align; }

    void set_send_on_change(bool enable) { send_on_change_ = enable; }
    void set_min_interval(uint32_t ms) { min_interval_ms_ = ms; }

    void add_variable(const std::string &name, const std::string &type);

    void register_sensor(const std::string &var_name, sensor::Sensor *s) { sensors_[var_name] = s; }
    void register_binary_sensor(const std::string &var_name, binary_sensor::BinarySensor *bs) { binary_sensors_[var_name] = bs; }

    void set_variable_value(const std::string &name, float value);
    void set_variable_value(const std::string &name, int value);
    void set_variable_value(const std::string &name, bool value);
    void set_variable_value(const std::string &name, const std::string &value);

    std::string get_variable_value(const std::string &name);

    void setup() override;
    void loop() override;
    void update() override;

private:
    std::string ip_address_;
    uint16_t port_{1202};
    uint16_t cob_id_{1};
    uint16_t checksum_{0};

    bool enable_read_{false};
    bool enable_write_{true};

    bool big_endian_{false};
    bool pack_bools_{false};
    bool alignment_{true};

    bool send_on_change_{true};
    uint32_t min_interval_ms_{100};
    uint32_t last_sent_time_{0};
    bool is_dirty_{false};

    WiFiUDP udp_;

    std::vector<VarDef> variables_;
    std::map<std::string, std::string> var_values_;
    std::map<std::string, sensor::Sensor*> sensors_;
    std::map<std::string, binary_sensor::BinarySensor*> binary_sensors_;

    uint16_t sequence_counter_{1};

    void trigger_send_();
    void send_packet_();
    void get_type_info(const std::string &type, size_t &size, size_t &align);
    std::vector<uint8_t> pack_value(const VarDef &var, const std::string &val_str);
    void unpack_payload(const uint8_t *payload, size_t len);
    uint16_t read_u16(const uint8_t *ptr);
};

} // namespace wago_netvar
} // namespace esphome
