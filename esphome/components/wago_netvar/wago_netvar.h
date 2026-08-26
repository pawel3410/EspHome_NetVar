#pragma once

#include "esphome.h"
#include <WiFiUdp.h>
#include <vector>
#include <string>

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif

#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif

namespace esphome {
namespace wago_netvar {

class WagoNetVarComponent;

#ifdef USE_SWITCH
class WagoSwitch : public switch_::Switch {
public:
    void set_parent(WagoNetVarComponent *parent, const std::string &var_name) { parent_ = parent; var_name_ = var_name; }
protected:
    void write_state(bool state) override;
    WagoNetVarComponent *parent_{nullptr};
    std::string var_name_;
};
#endif

#ifdef USE_NUMBER
class WagoNumber : public number::Number {
public:
    void set_parent(WagoNetVarComponent *parent, const std::string &var_name) { parent_ = parent; var_name_ = var_name; }
protected:
    void control(float value) override;
    WagoNetVarComponent *parent_{nullptr};
    std::string var_name_;
};
#endif

struct VarDef {
    std::string name;
    std::string type;
    size_t size;
    size_t align;
    size_t byte_offset{0};
    uint8_t bit_offset{0};

#ifdef USE_SENSOR
    sensor::Sensor* sensor_ptr{nullptr};
#endif
#ifdef USE_BINARY_SENSOR
    binary_sensor::BinarySensor* binary_sensor_ptr{nullptr};
#endif
#ifdef USE_SWITCH
    switch_::Switch* switch_ptr{nullptr};
#endif
#ifdef USE_NUMBER
    number::Number* number_ptr{nullptr};
#endif
};

class WagoNetVarComponent : public PollingComponent {
public:
    void set_ip_address(const std::string &ip) { ip_address_ = ip; }
    void set_port(uint16_t port) { port_ = port; }
    void set_cob_id(uint16_t cob_id) { cob_id_ = cob_id; }
    void set_checksum(uint16_t checksum) { checksum_ = checksum; }

    void set_direction(const std::string &direction) {
        direction_ = direction;
        std::string dir = direction;
        std::transform(dir.begin(), dir.end(), dir.begin(), ::tolower);
        enable_read_ = (dir == "read" || dir == "both");
        enable_write_ = (dir == "write" || dir == "both");
    }

    void set_big_endian(bool big_endian) { big_endian_ = big_endian; }
    void set_pack_bools(bool pack) { pack_bools_ = pack; }
    void set_alignment(bool align) { alignment_ = align; }
    void set_send_on_change(bool enable) { send_on_change_ = enable; }
    void set_min_interval(uint32_t ms) { min_interval_ms_ = ms; }

    void add_variable(const std::string &name, const std::string &type);

#ifdef USE_SENSOR
    void register_sensor(const std::string &var_name, sensor::Sensor *s) {
        VarDef *v = get_var(var_name);
        if (v) v->sensor_ptr = s;
    }
#endif
#ifdef USE_BINARY_SENSOR
    void register_binary_sensor(const std::string &var_name, binary_sensor::BinarySensor *bs) {
        VarDef *v = get_var(var_name);
        if (v) v->binary_sensor_ptr = bs;
    }
#endif
#ifdef USE_SWITCH
    void register_switch(const std::string &var_name, switch_::Switch *sw) {
        VarDef *v = get_var(var_name);
        if (v) v->switch_ptr = sw;
    }
#endif
#ifdef USE_NUMBER
    void register_number(const std::string &var_name, number::Number *num) {
        VarDef *v = get_var(var_name);
        if (v) v->number_ptr = num;
    }
#endif

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
    std::string direction_{"write"};

    bool enable_read_{false};
    bool enable_write_{true};
    bool big_endian_{false};
    bool pack_bools_{false};
    bool alignment_{true};

    bool send_on_change_{true};
    uint32_t min_interval_ms_{100};
    uint32_t last_sent_time_{0};
    bool is_dirty_{false};
    bool first_rx_done_{false};

    WiFiUDP udp_;
    std::vector<VarDef> variables_;
    
    // Płaskie bufory pamięci zastępujące mapy stringów!
    size_t payload_size_{0};
    std::vector<uint8_t> rx_buffer_;
    std::vector<uint8_t> tx_buffer_;
    std::vector<uint8_t> rx_packet_buffer_;

    uint16_t sequence_counter_{1};

    void trigger_send_();
    void send_packet_();
    void get_type_info(const std::string &type, size_t &size, size_t &align);
    void unpack_payload(const uint8_t *payload, size_t len);
    VarDef* get_var(const std::string &name);
};

} // namespace wago_netvar
} // namespace esphome
