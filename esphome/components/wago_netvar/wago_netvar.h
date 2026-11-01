#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif

#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif

namespace esphome {
namespace wago_netvar {

static constexpr size_t NETVAR_HEADER_SIZE = 20;
static constexpr size_t NETVAR_MAX_PACKET_SIZE = 1472;
static constexpr size_t NETVAR_MAX_PAYLOAD_SIZE =
    NETVAR_MAX_PACKET_SIZE - NETVAR_HEADER_SIZE;

class WagoNetVarComponent;

// Abstrakcja transportu UDP umożliwiająca obsługę zarówno Arduino, jak i ESP-IDF.
class UdpTransport {
 public:
  virtual ~UdpTransport() = default;

  virtual int begin(uint16_t port) = 0;
  virtual int parsePacket() = 0;
  virtual int read(uint8_t *data, size_t len) = 0;
  virtual int available() = 0;
  virtual IPAddress remoteIP() = 0;
  virtual uint16_t remotePort() = 0;
  virtual bool beginPacket(const IPAddress &ip, uint16_t port) = 0;
  virtual size_t write(const uint8_t *data, size_t len) = 0;
  virtual int endPacket() = 0;
};

#ifdef ARDUINO
#include <WiFiUdp.h>

class ArduinoUdpTransport : public UdpTransport {
 public:
  int begin(uint16_t port) override { return udp_.begin(port) ? 1 : 0; }
  int parsePacket() override { return udp_.parsePacket(); }
  int read(uint8_t *data, size_t len) override { return udp_.read(data, len); }
  int available() override { return udp_.available(); }
  IPAddress remoteIP() override { return udp_.remoteIP(); }
  uint16_t remotePort() override { return udp_.remotePort(); }
  bool beginPacket(const IPAddress &ip, uint16_t port) override {
    return udp_.beginPacket(ip, port);
  }
  size_t write(const uint8_t *data, size_t len) override {
    return udp_.write(data, len);
  }
  int endPacket() override { return udp_.endPacket(); }

 private:
  WiFiUDP udp_;
};
#endif

#ifdef USE_ESP_IDF
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <lwip/sockets.h>

class EspIdfUdpTransport : public UdpTransport {
 public:
  ~EspIdfUdpTransport() override {
    if (sockfd_ >= 0) {
      close(sockfd_);
    }
  }

  int begin(uint16_t port) override {
    if (sockfd_ >= 0) {
      close(sockfd_);
      sockfd_ = -1;
    }

    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ < 0) {
      ESP_LOGE("wago_netvar", "Failed to create UDP socket");
      return 0;
    }

    int flags = fcntl(sockfd_, F_GETFL, 0);
    if (flags >= 0) {
      fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(sockfd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
      ESP_LOGE("wago_netvar", "Failed to bind UDP socket to port %u", port);
      close(sockfd_);
      sockfd_ = -1;
      return 0;
    }

    local_port_ = port;
    return 1;
  }

  int parsePacket() override {
    if (sockfd_ < 0)
      return 0;

    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);
    memset(&sender, 0, sizeof(sender));

    int len = recvfrom(sockfd_, rx_buffer_, sizeof(rx_buffer_), MSG_PEEK | MSG_DONTWAIT,
                       reinterpret_cast<struct sockaddr *>(&sender), &sender_len);
    if (len < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return 0;
      ESP_LOGE("wago_netvar", "UDP recvfrom error");
      return 0;
    }

    rx_len_ = len;
    uint32_t ip = ntohl(sender.sin_addr.s_addr);
    remote_ip_ = IPAddress((ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    remote_port_ = ntohs(sender.sin_port);
    return len;
  }

  int read(uint8_t *data, size_t len) override {
    if (sockfd_ < 0 || rx_len_ <= 0)
      return 0;

    size_t to_copy = std::min<size_t>(len, static_cast<size_t>(rx_len_));
    memcpy(data, rx_buffer_, to_copy);
    rx_len_ = 0;
    return static_cast<int>(to_copy);
  }

  int available() override { return rx_len_; }

  IPAddress remoteIP() override { return remote_ip_; }
  uint16_t remotePort() override { return remote_port_; }

  bool beginPacket(const IPAddress &ip, uint16_t port) override {
    target_ip_ = ip;
    target_port_ = port;
    tx_buffer_.clear();
    return true;
  }

  size_t write(const uint8_t *data, size_t len) override {
    tx_buffer_.insert(tx_buffer_.end(), data, data + len);
    return len;
  }

  int endPacket() override {
    if (sockfd_ < 0)
      return 0;

    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(target_port_);

    uint32_t ip = (static_cast<uint32_t>(target_ip_[0]) << 24) |
                  (static_cast<uint32_t>(target_ip_[1]) << 16) |
                  (static_cast<uint32_t>(target_ip_[2]) << 8) |
                  static_cast<uint32_t>(target_ip_[3]);
    target.sin_addr.s_addr = htonl(ip);

    int sent = sendto(sockfd_, tx_buffer_.data(), tx_buffer_.size(), 0,
                      reinterpret_cast<struct sockaddr *>(&target), sizeof(target));
    tx_buffer_.clear();

    if (sent < 0) {
      ESP_LOGE("wago_netvar", "UDP sendto error");
      return 0;
    }
    return 1;
  }

 private:
  int sockfd_{-1};
  uint16_t local_port_{0};
  uint8_t rx_buffer_[NETVAR_MAX_PACKET_SIZE]{};
  int rx_len_{0};
  IPAddress remote_ip_;
  uint16_t remote_port_{0};
  IPAddress target_ip_;
  uint16_t target_port_{0};
  std::vector<uint8_t> tx_buffer_;
};
#endif

#ifdef USE_SWITCH
class WagoSwitch : public switch_::Switch {
 public:
  void set_parent(WagoNetVarComponent *parent,
                  const std::string &var_name) {
    parent_ = parent;
    var_name_ = var_name;
  }

 protected:
  void write_state(bool state) override;

  WagoNetVarComponent *parent_{nullptr};
  std::string var_name_;
};
#endif

#ifdef USE_NUMBER
class WagoNumber : public number::Number {
 public:
  void set_parent(WagoNetVarComponent *parent,
                  const std::string &var_name) {
    parent_ = parent;
    var_name_ = var_name;
  }

 protected:
  void control(float value) override;

  WagoNetVarComponent *parent_{nullptr};
  std::string var_name_;
};
#endif

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
  size_t alignment{1};

  size_t byte_offset{0};
  bool has_manual_offset{false};

#ifdef USE_SENSOR
  sensor::Sensor *sensor_ptr{nullptr};
#endif

#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *binary_sensor_ptr{nullptr};
#endif

#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *text_sensor_ptr{nullptr};
#endif

#ifdef USE_SWITCH
  switch_::Switch *switch_ptr{nullptr};
#endif

#ifdef USE_NUMBER
  number::Number *number_ptr{nullptr};
#endif
};

class WagoNetVarComponent : public PollingComponent {
 public:
  void set_ip_address(const std::string &ip) {
    ip_address_ = ip;
  }

  void set_port(uint16_t port) {
    port_ = port;
  }

  void set_list_id(uint16_t list_id) {
    list_id_ = list_id;
  }

  void set_var_count(uint16_t var_count) {
    var_count_ = var_count;
  }

  void set_direction(const std::string &direction) {
    enable_read_ = (direction == "read");
    enable_write_ = (direction == "write");
  }

  void set_alignment(bool align) {
    alignment_ = align;
  }

  void set_send_on_change(bool enable) {
    send_on_change_ = enable;
  }

  void set_min_interval(uint32_t ms) {
    min_interval_ms_ = ms;
  }

  void set_update_interval(uint32_t ms) {
    update_interval_ms_ = ms;
  }

  void add_variable(const std::string &name,
                    const std::string &type,
                    int byte_offset);

#ifdef USE_SENSOR
  void register_sensor(const std::string &name,
                       sensor::Sensor *entity);
  void set_rejected_packets_sensor(sensor::Sensor *sensor) {
    rejected_packets_sensor_ = sensor;
  }
  void set_rx_packets_sensor(sensor::Sensor *sensor) {
    rx_packets_sensor_ = sensor;
  }
  void set_tx_packets_sensor(sensor::Sensor *sensor) {
    tx_packets_sensor_ = sensor;
  }
  void set_rx_errors_sensor(sensor::Sensor *sensor) {
    rx_errors_sensor_ = sensor;
  }
  void set_tx_errors_sensor(sensor::Sensor *sensor) {
    tx_errors_sensor_ = sensor;
  }
#endif

#ifdef USE_BINARY_SENSOR
  void register_binary_sensor(const std::string &name,
                              binary_sensor::BinarySensor *entity);
#endif

#ifdef USE_TEXT_SENSOR
  void register_text_sensor(const std::string &name,
                            text_sensor::TextSensor *entity);
#endif

#ifdef USE_SWITCH
  void register_switch(const std::string &name,
                       switch_::Switch *entity);
#endif

#ifdef USE_NUMBER
  void register_number(const std::string &name,
                       number::Number *entity);
#endif

  void set_variable_value(const std::string &name, bool value);
  void set_variable_value(const std::string &name, float value);
  void set_variable_value(const std::string &name, int value);
  void set_variable_value(const std::string &name,
                          const std::string &value);

  std::string get_variable_value(const std::string &name) const;

  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;

  float get_setup_priority() const override {
    return setup_priority::AFTER_WIFI;
  }

 protected:
  std::string ip_address_;

  uint16_t port_{1202};
  uint16_t list_id_{1};
  uint16_t var_count_{0};

  bool enable_read_{false};
  bool enable_write_{true};

  bool alignment_{false};
  bool send_on_change_{false};

  uint32_t min_interval_ms_{500};
  uint32_t update_interval_ms_{5000};
  uint32_t last_sent_time_{0};

  bool is_dirty_{false};
  bool udp_started_{false};

  uint16_t sequence_counter_{1};

  uint32_t rx_packets_{0};
  uint32_t tx_packets_{0};
  uint32_t rx_errors_{0};
  uint32_t tx_errors_{0};
  uint32_t rejected_packets_{0};

  std::unique_ptr<UdpTransport> udp_;

  std::vector<VarDef> variables_;
  size_t payload_size_{0};

  std::vector<uint8_t> tx_buffer_;
  std::array<uint8_t, NETVAR_MAX_PACKET_SIZE> rx_packet_buffer_{};

#ifdef USE_SENSOR
  sensor::Sensor *rejected_packets_sensor_{nullptr};
  sensor::Sensor *rx_packets_sensor_{nullptr};
  sensor::Sensor *tx_packets_sensor_{nullptr};
  sensor::Sensor *rx_errors_sensor_{nullptr};
  sensor::Sensor *tx_errors_sensor_{nullptr};
#endif

  VarDef *get_var(const std::string &name);
  const VarDef *get_var(const std::string &name) const;

  bool get_type_info(const std::string &type,
                     VariableType &variable_type,
                     size_t &size,
                     size_t &alignment);

  bool calculate_layout_();

  bool validate_packet_(size_t packet_size,
                        const IPAddress &remote_ip,
                        uint16_t remote_port);

  void unpack_payload_(const uint8_t *payload,
                       size_t length);

  void send_packet_();
  void trigger_send_();
  void publish_statistics_();

  bool write_integer_(VarDef &var, uint64_t value);
  uint64_t read_integer_(const uint8_t *data, size_t size) const;

  void publish_variable_(VarDef &var, const uint8_t *payload);

  uint16_t read_u16_(const uint8_t *data) const;
  void write_u16_(uint8_t *data, uint16_t value) const;
};

}  // namespace wago_netvar
}  // namespace esphome
