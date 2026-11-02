#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace esphome {
namespace wago_netvar {

static constexpr size_t NETVAR_HEADER_SIZE = 20;
static constexpr size_t NETVAR_MAX_PACKET_SIZE = 1472;
static constexpr size_t NETVAR_MAX_PAYLOAD_SIZE = NETVAR_MAX_PACKET_SIZE - NETVAR_HEADER_SIZE;

class WagoNetVarComponent;

/// Platform independent UDP transport (Arduino / ESP-IDF). Implementations live in the .cpp file,
/// so no platform headers leak into the component header.
/// IPv4 addresses are passed as host-order integers: a.b.c.d -> (a << 24) | (b << 16) | (c << 8) | d.
class UdpTransport {
 public:
  virtual ~UdpTransport() = default;

  /// Opens the socket. port == 0 means "send only" (no fixed local port is bound).
  virtual bool begin(uint16_t port) = 0;

  /// Non-blocking receive of one datagram.
  /// Returns the datagram length (> capacity means the datagram is too large and `buffer` is invalid),
  /// 0 when nothing is pending, or a negative value on a socket error.
  virtual int receive(uint8_t *buffer, size_t capacity, uint32_t &remote_ip, uint16_t &remote_port) = 0;

  /// Sends one datagram. Returns true on success.
  virtual bool send(uint32_t ip, uint16_t port, const uint8_t *data, size_t length) = 0;
};

class WagoSwitch : public switch_::Switch {
 public:
  void set_parent(WagoNetVarComponent *parent, const std::string &var_name) {
    this->parent_ = parent;
    this->var_name_ = var_name;
  }

 protected:
  void write_state(bool state) override;

  WagoNetVarComponent *parent_{nullptr};
  std::string var_name_;
};

class WagoNumber : public number::Number {
 public:
  void set_parent(WagoNetVarComponent *parent, const std::string &var_name) {
    this->parent_ = parent;
    this->var_name_ = var_name;
  }

 protected:
  void control(float value) override;

  WagoNetVarComponent *parent_{nullptr};
  std::string var_name_;
};

enum class VariableType : uint8_t {
  BOOL,
  BYTE,
  USINT,
  SINT,
  WORD,
  UINT,
  INT,
  DWORD,
  UDINT,
  DINT,
  TIME,
  DATE,
  TIME_OF_DAY,
  DATE_AND_TIME,
  REAL,
  STRING
};

struct VarDef {
  std::string name;
  std::string type_name;
  VariableType type{VariableType::BOOL};
  size_t size{0};
  size_t byte_offset{0};  // resolved at compile time by the Python code generator

  sensor::Sensor *sensor_ptr{nullptr};
  binary_sensor::BinarySensor *binary_sensor_ptr{nullptr};
  text_sensor::TextSensor *text_sensor_ptr{nullptr};
  switch_::Switch *switch_ptr{nullptr};
  number::Number *number_ptr{nullptr};
};

/// update_interval (PollingComponent) drives the cyclic transmission in "write" mode.
/// update_interval: never (also "0s", converted by the Python validator) disables it.
class WagoNetVarComponent : public PollingComponent {
 public:
  // ---- configuration (called from generated code) ----
  void set_ip_address(const std::string &ip) { this->ip_address_ = ip; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_list_id(uint16_t list_id) { this->list_id_ = list_id; }
  void set_var_count(uint16_t var_count) { this->var_count_ = var_count; }
  void set_direction(const std::string &direction) {
    this->enable_read_ = (direction == "read");
    this->enable_write_ = (direction == "write");
  }
  void set_alignment(bool alignment) { this->alignment_ = alignment; }
  void set_send_on_change(bool enable) { this->send_on_change_ = enable; }
  void set_min_interval(uint32_t ms) { this->min_interval_ms_ = ms; }

  void add_variable(const std::string &name, const std::string &type, uint16_t byte_offset);

  void register_sensor(const std::string &name, sensor::Sensor *entity);
  void register_binary_sensor(const std::string &name, binary_sensor::BinarySensor *entity);
  void register_text_sensor(const std::string &name, text_sensor::TextSensor *entity);
  void register_switch(const std::string &name, switch_::Switch *entity);
  void register_number(const std::string &name, number::Number *entity);

  void set_rejected_packets_sensor(sensor::Sensor *sensor) { this->rejected_packets_sensor_ = sensor; }
  void set_rx_packets_sensor(sensor::Sensor *sensor) { this->rx_packets_sensor_ = sensor; }
  void set_tx_packets_sensor(sensor::Sensor *sensor) { this->tx_packets_sensor_ = sensor; }
  void set_rx_errors_sensor(sensor::Sensor *sensor) { this->rx_errors_sensor_ = sensor; }
  void set_tx_errors_sensor(sensor::Sensor *sensor) { this->tx_errors_sensor_ = sensor; }

  // ---- runtime API (usable from lambdas, switches and numbers); "write" lists only ----
  // Values are converted to the NetVar type of the variable. On integer overflow the least significant
  // bytes are kept. Numbers written to a STRING variable are converted to text.
  void set_variable_value(const std::string &name, bool value);
  void set_variable_value(const std::string &name, int value);
  void set_variable_value(const std::string &name, float value);
  void set_variable_value(const std::string &name, const std::string &value);
  // Without this overload a string literal would silently select the bool overload.
  void set_variable_value(const std::string &name, const char *value) {
    this->set_variable_value(name, std::string(value));
  }

  /// Last value of a variable (sent value in "write" mode, last decoded value in "read" mode).
  std::string get_variable_value(const std::string &name) const;

  // ---- Component ----
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

 protected:
  static constexpr size_t STATS_COUNT = 5;

  // helpers: configuration
  VarDef *get_var(const std::string &name);
  const VarDef *get_var(const std::string &name) const;
  void fail_config_(const char *what, const std::string &name);
  bool finalize_layout_();

  // helpers: transport
  bool ensure_udp_();
  void receive_packets_();
  bool validate_packet_(size_t packet_size, uint32_t remote_ip);
  void unpack_payload_(const uint8_t *payload);
  void publish_variable_(const VarDef &var);
  void publish_statistics_();

  void trigger_send_();
  void send_packet_();

  // helpers: value conversion
  VarDef *prepare_write_(const std::string &name);
  void commit_(const VarDef &var, const uint8_t *bytes);
  void write_int64_(VarDef &var, int64_t value);
  void write_double_(VarDef &var, double value);
  void write_text_(VarDef &var, const std::string &text);
  double decode_number_(const VarDef &var, const uint8_t *data) const;

  // configuration
  std::string ip_address_;
  uint32_t ip_{0};
  uint16_t port_{1202};
  uint16_t list_id_{1};
  uint16_t var_count_{0};
  bool enable_read_{false};
  bool enable_write_{true};
  bool alignment_{false};
  bool send_on_change_{false};
  uint32_t min_interval_ms_{500};
  bool config_error_{false};

  // state
  std::vector<VarDef> variables_;
  size_t payload_size_{0};
  std::vector<uint8_t> payload_;  // current variable values
  std::vector<uint8_t> frame_;    // scratch buffer: RX datagram (read) or TX frame (write)
  std::unique_ptr<UdpTransport> udp_;
  bool udp_started_{false};
  bool udp_attempted_{false};
  uint32_t last_udp_attempt_{0};

  bool is_dirty_{false};
  uint32_t last_sent_time_{0};
  uint16_t sequence_counter_{1};

  uint32_t rx_packets_{0};
  uint32_t tx_packets_{0};
  uint32_t rx_errors_{0};
  uint32_t tx_errors_{0};
  uint32_t rejected_packets_{0};
  uint32_t published_stats_[STATS_COUNT];
  uint32_t last_stats_time_{0};

  sensor::Sensor *rejected_packets_sensor_{nullptr};
  sensor::Sensor *rx_packets_sensor_{nullptr};
  sensor::Sensor *tx_packets_sensor_{nullptr};
  sensor::Sensor *rx_errors_sensor_{nullptr};
  sensor::Sensor *tx_errors_sensor_{nullptr};
};

}  // namespace wago_netvar
}  // namespace esphome
