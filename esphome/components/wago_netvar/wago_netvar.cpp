#include "wago_netvar.h"

#include "esphome/components/network/util.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(USE_ARDUINO)
#include <WiFiUdp.h>
#elif defined(USE_ESP_IDF)
#include <arpa/inet.h>
#include <fcntl.h>
#include <lwip/sockets.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#error "wago_netvar supports only the Arduino and ESP-IDF frameworks"
#endif

namespace esphome {
namespace wago_netvar {

static const char *const TAG = "wago_netvar";

static constexpr uint8_t MAGIC_BYTES[4] = {0x00, 0x2D, 0x53, 0x33};
static constexpr size_t MAX_PACKETS_PER_LOOP = 8;
static constexpr uint32_t UDP_RETRY_INTERVAL_MS = 5000;
static constexpr uint32_t STATS_PUBLISH_INTERVAL_MS = 1000;
static constexpr uint32_t STATS_UNPUBLISHED = 0xFFFFFFFFu;

// ---------------------------------------------------------------------------------------------
// UDP transports
// ---------------------------------------------------------------------------------------------
namespace {

#if defined(USE_ARDUINO)

class ArduinoUdpTransport : public UdpTransport {
 public:
  bool begin(uint16_t port) override {
    if (port == 0)
      return true;  // send only: WiFiUDP::beginPacket() opens its own socket
    return this->udp_.begin(port) != 0;
  }

  int receive(uint8_t *buffer, size_t capacity, uint32_t &remote_ip, uint16_t &remote_port) override {
    const int size = this->udp_.parsePacket();
    if (size <= 0)
      return 0;

    const IPAddress ip = this->udp_.remoteIP();
    remote_ip = (static_cast<uint32_t>(ip[0]) << 24) | (static_cast<uint32_t>(ip[1]) << 16) |
                (static_cast<uint32_t>(ip[2]) << 8) | static_cast<uint32_t>(ip[3]);
    remote_port = this->udp_.remotePort();

    // Too large: leave the payload alone, the next parsePacket() drops the rest of this datagram.
    if (static_cast<size_t>(size) > capacity)
      return size;
    if (this->udp_.read(buffer, static_cast<size_t>(size)) != size)
      return -1;
    return size;
  }

  bool send(uint32_t ip, uint16_t port, const uint8_t *data, size_t length) override {
    const IPAddress target(static_cast<uint8_t>(ip >> 24), static_cast<uint8_t>(ip >> 16),
                           static_cast<uint8_t>(ip >> 8), static_cast<uint8_t>(ip));
    if (!this->udp_.beginPacket(target, port))
      return false;
    const size_t written = this->udp_.write(data, length);
    const int result = this->udp_.endPacket();  // always finish the packet
    return written == length && result == 1;
  }

 private:
  WiFiUDP udp_;
};

std::unique_ptr<UdpTransport> create_transport() { return std::make_unique<ArduinoUdpTransport>(); }

#elif defined(USE_ESP_IDF)

class IdfUdpTransport : public UdpTransport {
 public:
  ~IdfUdpTransport() override { this->close_(); }

  bool begin(uint16_t port) override {
    this->close_();

    this->fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (this->fd_ < 0) {
      ESP_LOGE(TAG, "Failed to create UDP socket (errno %d)", errno);
      return false;
    }

    const int flags = ::fcntl(this->fd_, F_GETFL, 0);
    if (flags < 0 || ::fcntl(this->fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
      ESP_LOGE(TAG, "Failed to set UDP socket non-blocking (errno %d)", errno);
      this->close_();
      return false;
    }

    // Allow sending to broadcast addresses (e.g. 255.255.255.255).
    const int enable = 1;
    ::setsockopt(this->fd_, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));

    // port == 0 -> ephemeral local port (send only), otherwise listen on the configured port.
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(this->fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
      ESP_LOGE(TAG, "Failed to bind UDP socket to port %u (errno %d)", static_cast<unsigned>(port), errno);
      this->close_();
      return false;
    }
    return true;
  }

  int receive(uint8_t *buffer, size_t capacity, uint32_t &remote_ip, uint16_t &remote_port) override {
    if (this->fd_ < 0)
      return 0;

    struct sockaddr_in sender {};
    socklen_t sender_len = sizeof(sender);
    // The datagram is removed from the socket queue (no MSG_PEEK). A datagram longer than `capacity`
    // is truncated; the caller passes capacity = max frame + 1, so truncation is detected as "too large".
    const ssize_t size = ::recvfrom(this->fd_, buffer, capacity, MSG_DONTWAIT,
                                    reinterpret_cast<struct sockaddr *>(&sender), &sender_len);
    if (size < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return 0;
      return -1;
    }
    remote_ip = ntohl(sender.sin_addr.s_addr);
    remote_port = ntohs(sender.sin_port);
    return static_cast<int>(size);
  }

  bool send(uint32_t ip, uint16_t port, const uint8_t *data, size_t length) override {
    if (this->fd_ < 0)
      return false;

    struct sockaddr_in target {};
    target.sin_family = AF_INET;
    target.sin_addr.s_addr = htonl(ip);
    target.sin_port = htons(port);
    const ssize_t sent = ::sendto(this->fd_, data, length, 0, reinterpret_cast<struct sockaddr *>(&target),
                                  sizeof(target));
    return sent >= 0 && static_cast<size_t>(sent) == length;
  }

 private:
  void close_() {
    if (this->fd_ >= 0) {
      ::close(this->fd_);
      this->fd_ = -1;
    }
  }

  int fd_{-1};
};

std::unique_ptr<UdpTransport> create_transport() { return std::make_unique<IdfUdpTransport>(); }

#endif

// ---------------------------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------------------------

bool parse_ipv4(const std::string &text, uint32_t &result) {
  uint32_t address = 0;
  size_t pos = 0;
  for (int octet = 0; octet < 4; ++octet) {
    if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos])))
      return false;
    uint32_t value = 0;
    size_t digits = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
      value = value * 10 + static_cast<uint32_t>(text[pos] - '0');
      if (++digits > 3 || value > 255)
        return false;
      ++pos;
    }
    address = (address << 8) | value;
    if (octet < 3) {
      if (pos >= text.size() || text[pos] != '.')
        return false;
      ++pos;
    }
  }
  if (pos != text.size())
    return false;
  result = address;
  return true;
}

bool parse_type(const std::string &type, VariableType &variable_type, size_t &size) {
  struct TypeInfo {
    const char *name;
    VariableType type;
    size_t size;
  };
  static const TypeInfo TYPES[] = {
      {"BOOL", VariableType::BOOL, 1},           {"BYTE", VariableType::BYTE, 1},
      {"USINT", VariableType::USINT, 1},         {"SINT", VariableType::SINT, 1},
      {"WORD", VariableType::WORD, 2},           {"UINT", VariableType::UINT, 2},
      {"INT", VariableType::INT, 2},             {"DWORD", VariableType::DWORD, 4},
      {"UDINT", VariableType::UDINT, 4},         {"DINT", VariableType::DINT, 4},
      {"TIME", VariableType::TIME, 4},           {"DATE", VariableType::DATE, 4},
      {"TIME_OF_DAY", VariableType::TIME_OF_DAY, 4},
      {"DATE_AND_TIME", VariableType::DATE_AND_TIME, 4},
      {"REAL", VariableType::REAL, 4},
  };

  for (const auto &info : TYPES) {
    if (type == info.name) {
      variable_type = info.type;
      size = info.size;
      return true;
    }
  }

  if (type.size() > 8 && type.compare(0, 7, "STRING(") == 0 && type.back() == ')') {
    const std::string digits = type.substr(7, type.size() - 8);
    char *end = nullptr;
    const unsigned long length = std::strtoul(digits.c_str(), &end, 10);
    if (end == digits.c_str() || *end != '\0' || length < 1 || length > 255)
      return false;
    variable_type = VariableType::STRING;
    size = static_cast<size_t>(length) + 1;  // + terminating 0x00
    return true;
  }
  return false;
}

inline bool is_numeric_type(VariableType type) { return type != VariableType::BOOL && type != VariableType::STRING; }

uint32_t load_le(const uint8_t *data, size_t size) {
  uint32_t result = 0;
  for (size_t i = 0; i < size && i < 4; ++i)
    result |= static_cast<uint32_t>(data[i]) << (8 * i);
  return result;
}

uint16_t read_u16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0] | (static_cast<uint16_t>(data[1]) << 8));
}

void write_u16(uint8_t *data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
}

std::string format_number(double value, bool is_real) {
  char buffer[32];
  if (is_real)
    std::snprintf(buffer, sizeof(buffer), "%.7g", value);
  else
    std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
  return buffer;
}

std::string to_lower(std::string text) {
  for (auto &c : text)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

bool only_whitespace_left(const char *end) {
  while (*end != '\0') {
    if (!std::isspace(static_cast<unsigned char>(*end)))
      return false;
    ++end;
  }
  return true;
}

void publish_counter(sensor::Sensor *sensor, uint32_t value, uint32_t &last_published) {
  if (sensor == nullptr || value == last_published)
    return;
  sensor->publish_state(static_cast<float>(value));
  last_published = value;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------------------------

void WagoSwitch::write_state(bool state) {
  if (this->parent_ == nullptr)
    return;
  this->parent_->set_variable_value(this->var_name_, state);
  this->publish_state(state);
}

void WagoNumber::control(float value) {
  if (this->parent_ == nullptr)
    return;
  this->parent_->set_variable_value(this->var_name_, value);
  this->publish_state(value);
}

// ---------------------------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------------------------

// mark_failed() called before setup() is lost (the component state is reset when setup starts),
// therefore configuration problems are remembered and reported from setup().
void WagoNetVarComponent::fail_config_(const char *what, const std::string &name) {
  ESP_LOGE(TAG, "Configuration error: %s (%s)", what, name.c_str());
  this->config_error_ = true;
}

VarDef *WagoNetVarComponent::get_var(const std::string &name) {
  for (auto &var : this->variables_) {
    if (var.name == name)
      return &var;
  }
  return nullptr;
}

const VarDef *WagoNetVarComponent::get_var(const std::string &name) const {
  for (const auto &var : this->variables_) {
    if (var.name == name)
      return &var;
  }
  return nullptr;
}

void WagoNetVarComponent::add_variable(const std::string &name, const std::string &type, uint16_t byte_offset) {
  if (name.empty()) {
    this->fail_config_("variable name cannot be empty", name);
    return;
  }
  if (this->get_var(name) != nullptr) {
    this->fail_config_("duplicate variable", name);
    return;
  }

  VarDef var;
  var.name = name;
  var.type_name = type;
  var.byte_offset = byte_offset;
  if (!parse_type(type, var.type, var.size)) {
    this->fail_config_("unsupported variable type", name + ": " + type);
    return;
  }
  this->variables_.push_back(std::move(var));
}

void WagoNetVarComponent::register_sensor(const std::string &name, sensor::Sensor *entity) {
  VarDef *var = this->get_var(name);
  if (var == nullptr || entity == nullptr || !is_numeric_type(var->type)) {
    this->fail_config_("invalid sensor mapping", name);
    return;
  }
  if (var->sensor_ptr != nullptr) {
    this->fail_config_("duplicate sensor mapping", name);
    return;
  }
  var->sensor_ptr = entity;
}

void WagoNetVarComponent::register_binary_sensor(const std::string &name, binary_sensor::BinarySensor *entity) {
  VarDef *var = this->get_var(name);
  if (var == nullptr || entity == nullptr || var->type != VariableType::BOOL) {
    this->fail_config_("invalid binary sensor mapping", name);
    return;
  }
  if (var->binary_sensor_ptr != nullptr) {
    this->fail_config_("duplicate binary sensor mapping", name);
    return;
  }
  var->binary_sensor_ptr = entity;
}

void WagoNetVarComponent::register_text_sensor(const std::string &name, text_sensor::TextSensor *entity) {
  VarDef *var = this->get_var(name);
  if (var == nullptr || entity == nullptr || var->type != VariableType::STRING) {
    this->fail_config_("invalid text sensor mapping", name);
    return;
  }
  if (var->text_sensor_ptr != nullptr) {
    this->fail_config_("duplicate text sensor mapping", name);
    return;
  }
  var->text_sensor_ptr = entity;
}

void WagoNetVarComponent::register_switch(const std::string &name, switch_::Switch *entity) {
  VarDef *var = this->get_var(name);
  if (var == nullptr || entity == nullptr || var->type != VariableType::BOOL) {
    this->fail_config_("invalid switch mapping", name);
    return;
  }
  if (var->switch_ptr != nullptr) {
    this->fail_config_("duplicate switch mapping", name);
    return;
  }
  var->switch_ptr = entity;
}

// A number may drive any numeric variable and also a STRING (the number is written as text).
void WagoNetVarComponent::register_number(const std::string &name, number::Number *entity) {
  VarDef *var = this->get_var(name);
  if (var == nullptr || entity == nullptr || var->type == VariableType::BOOL) {
    this->fail_config_("invalid number mapping", name);
    return;
  }
  if (var->number_ptr != nullptr) {
    this->fail_config_("duplicate number mapping", name);
    return;
  }
  var->number_ptr = entity;
}

// Offsets are resolved by the Python code generator; here they are only verified (defence in depth)
// and the payload size is derived from them.
bool WagoNetVarComponent::finalize_layout_() {
  size_t payload_size = 0;
  for (size_t i = 0; i < this->variables_.size(); ++i) {
    const VarDef &var = this->variables_[i];
    if (var.byte_offset > NETVAR_MAX_PAYLOAD_SIZE || var.size > NETVAR_MAX_PAYLOAD_SIZE - var.byte_offset) {
      ESP_LOGE(TAG, "Variable '%s' exceeds the maximum payload size (%u bytes)", var.name.c_str(),
               static_cast<unsigned>(NETVAR_MAX_PAYLOAD_SIZE));
      return false;
    }
    const size_t end = var.byte_offset + var.size;
    for (size_t j = 0; j < i; ++j) {
      const VarDef &other = this->variables_[j];
      if (var.byte_offset < other.byte_offset + other.size && other.byte_offset < end) {
        ESP_LOGE(TAG, "Variables '%s' and '%s' overlap", var.name.c_str(), other.name.c_str());
        return false;
      }
    }
    payload_size = std::max(payload_size, end);
  }
  this->payload_size_ = payload_size;
  return true;
}

void WagoNetVarComponent::setup() {
  if (this->config_error_ || this->variables_.empty()) {
    ESP_LOGE(TAG, "Invalid configuration, component disabled");
    this->mark_failed();
    return;
  }

  if (!this->finalize_layout_()) {
    this->mark_failed();
    return;
  }

  // var_count is the number of variables on the sender's list. It may be larger than the number of
  // variables defined here (variables that are skipped), but never smaller.
  if (this->var_count_ == 0)
    this->var_count_ = static_cast<uint16_t>(this->variables_.size());
  if (this->var_count_ < this->variables_.size()) {
    ESP_LOGE(TAG, "var_count (%u) is lower than the number of defined variables (%u)",
             static_cast<unsigned>(this->var_count_), static_cast<unsigned>(this->variables_.size()));
    this->mark_failed();
    return;
  }

  if (!parse_ipv4(this->ip_address_, this->ip_)) {
    ESP_LOGE(TAG, "Invalid IP address: %s", this->ip_address_.c_str());
    this->mark_failed();
    return;
  }
  if (this->enable_write_ && this->ip_ == 0) {
    ESP_LOGE(TAG, "IP address 0.0.0.0 is not allowed in write mode");
    this->mark_failed();
    return;
  }

  this->payload_.assign(this->payload_size_, 0);
  if (this->enable_read_)
    this->frame_.assign(NETVAR_MAX_PACKET_SIZE + 1, 0);  // +1 lets us detect oversized datagrams
  else
    this->frame_.assign(NETVAR_HEADER_SIZE + this->payload_size_, 0);

  std::fill(std::begin(this->published_stats_), std::end(this->published_stats_), STATS_UNPUBLISHED);

  this->udp_ = create_transport();
  // The socket itself is opened in loop() as soon as the network is up (see ensure_udp_()).
}

bool WagoNetVarComponent::ensure_udp_() {
  if (this->udp_started_)
    return true;
  if (this->udp_ == nullptr || !network::is_connected())
    return false;

  const uint32_t now = millis();
  if (this->udp_attempted_ && now - this->last_udp_attempt_ < UDP_RETRY_INTERVAL_MS)
    return false;
  this->udp_attempted_ = true;
  this->last_udp_attempt_ = now;

  // Only "read" lists listen on the configured port; "write" lists use an ephemeral local port, so a
  // read and a write list can share the same port number.
  if (!this->udp_->begin(this->enable_read_ ? this->port_ : 0)) {
    ESP_LOGW(TAG, "Unable to initialize UDP (port %u), retrying", static_cast<unsigned>(this->port_));
    return false;
  }

  this->udp_started_ = true;
  ESP_LOGI(TAG, "NetVar list %u started (%s, port %u, payload %u bytes)", static_cast<unsigned>(this->list_id_),
           this->enable_read_ ? "read" : "write", static_cast<unsigned>(this->port_),
           static_cast<unsigned>(this->payload_size_));
  return true;
}

void WagoNetVarComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Wago NetVar:");
  ESP_LOGCONFIG(TAG, "  Direction: %s", this->enable_read_ ? "read" : "write");
  ESP_LOGCONFIG(TAG, "  IP address: %s", this->ip_address_.c_str());
  ESP_LOGCONFIG(TAG, "  Port: %u", static_cast<unsigned>(this->port_));
  ESP_LOGCONFIG(TAG, "  List ID: %u", static_cast<unsigned>(this->list_id_));
  ESP_LOGCONFIG(TAG, "  Variable count: %u", static_cast<unsigned>(this->var_count_));
  ESP_LOGCONFIG(TAG, "  Payload size: %u bytes", static_cast<unsigned>(this->payload_size_));
  if (this->enable_write_) {
    ESP_LOGCONFIG(TAG, "  Alignment: %s", this->alignment_ ? "yes" : "no");
    ESP_LOGCONFIG(TAG, "  Send on change: %s", this->send_on_change_ ? "yes" : "no");
    if (this->send_on_change_)
      ESP_LOGCONFIG(TAG, "  Min interval: %u ms", static_cast<unsigned>(this->min_interval_ms_));
    LOG_UPDATE_INTERVAL(this);
  }
  for (const auto &var : this->variables_) {
    ESP_LOGCONFIG(TAG, "  Variable '%s': type=%s offset=%u size=%u", var.name.c_str(), var.type_name.c_str(),
                  static_cast<unsigned>(var.byte_offset), static_cast<unsigned>(var.size));
  }
  LOG_SENSOR("  ", "Rejected packets", this->rejected_packets_sensor_);
  LOG_SENSOR("  ", "RX packets", this->rx_packets_sensor_);
  LOG_SENSOR("  ", "TX packets", this->tx_packets_sensor_);
  LOG_SENSOR("  ", "RX errors", this->rx_errors_sensor_);
  LOG_SENSOR("  ", "TX errors", this->tx_errors_sensor_);
}

// ---------------------------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------------------------

void WagoNetVarComponent::loop() {
  const uint32_t now = millis();
  if (now - this->last_stats_time_ >= STATS_PUBLISH_INTERVAL_MS) {
    this->last_stats_time_ = now;
    this->publish_statistics_();
  }

  if (!this->ensure_udp_())
    return;

  if (this->enable_read_)
    this->receive_packets_();
  else
    this->trigger_send_();
}

void WagoNetVarComponent::receive_packets_() {
  for (size_t count = 0; count < MAX_PACKETS_PER_LOOP; ++count) {
    uint32_t remote_ip = 0;
    uint16_t remote_port = 0;
    const int size = this->udp_->receive(this->frame_.data(), this->frame_.size(), remote_ip, remote_port);
    if (size == 0)
      break;
    if (size < 0) {
      ++this->rx_errors_;
      break;
    }

    if (static_cast<size_t>(size) > NETVAR_MAX_PACKET_SIZE ||
        !this->validate_packet_(static_cast<size_t>(size), remote_ip)) {
      ++this->rejected_packets_;
      continue;
    }

    ++this->rx_packets_;
    this->unpack_payload_(this->frame_.data() + NETVAR_HEADER_SIZE);
  }
}

// The destination port is validated implicitly (the socket is bound to the configured port).
// The sender's source port is irrelevant. sequence_counter and checksum are not evaluated.
bool WagoNetVarComponent::validate_packet_(size_t packet_size, uint32_t remote_ip) {
  if (packet_size < NETVAR_HEADER_SIZE || packet_size > NETVAR_MAX_PACKET_SIZE)
    return false;

  // 0.0.0.0 = accept any sender.
  if (this->ip_ != 0 && this->ip_ != remote_ip)
    return false;

  const uint8_t *header = this->frame_.data();
  if (std::memcmp(header, MAGIC_BYTES, sizeof(MAGIC_BYTES)) != 0)
    return false;

  if (read_u16(&header[8]) != this->list_id_)
    return false;
  if (read_u16(&header[12]) != this->var_count_)
    return false;
  if (read_u16(&header[14]) != packet_size)  // frame_size must match the received datagram
    return false;

  // The frame must cover every defined variable. It may be longer: variables beyond the last defined
  // one are simply skipped.
  if (packet_size - NETVAR_HEADER_SIZE < this->payload_size_)
    return false;

  ESP_LOGV(TAG, "List %u: frame ok, sequence counter %u", static_cast<unsigned>(this->list_id_),
           static_cast<unsigned>(read_u16(&header[16])));
  return true;
}

void WagoNetVarComponent::unpack_payload_(const uint8_t *payload) {
  std::memcpy(this->payload_.data(), payload, this->payload_size_);
  for (const auto &var : this->variables_)
    this->publish_variable_(var);
}

double WagoNetVarComponent::decode_number_(const VarDef &var, const uint8_t *data) const {
  const uint32_t raw = load_le(data, var.size);
  switch (var.type) {
    case VariableType::SINT:
      return static_cast<int8_t>(static_cast<uint8_t>(raw));
    case VariableType::INT:
      return static_cast<int16_t>(static_cast<uint16_t>(raw));
    case VariableType::DINT:
      return static_cast<int32_t>(raw);
    case VariableType::REAL: {
      float value = 0;
      std::memcpy(&value, &raw, sizeof(value));
      return value;
    }
    default:
      return static_cast<double>(raw);
  }
}

void WagoNetVarComponent::publish_variable_(const VarDef &var) {
  const uint8_t *data = this->payload_.data() + var.byte_offset;

  if (var.type == VariableType::BOOL) {
    if (var.binary_sensor_ptr != nullptr)
      var.binary_sensor_ptr->publish_state(data[0] != 0);
    return;
  }

  if (var.type == VariableType::STRING) {
    if (var.text_sensor_ptr != nullptr) {
      size_t length = 0;
      while (length < var.size && data[length] != '\0')
        ++length;
      var.text_sensor_ptr->publish_state(std::string(reinterpret_cast<const char *>(data), length));
    }
    return;
  }

  if (var.sensor_ptr == nullptr)
    return;
  // A non-finite REAL (NaN/Inf) is published as NaN, which ESPHome shows as "unknown".
  const double value = this->decode_number_(var, data);
  var.sensor_ptr->publish_state(std::isfinite(value) ? static_cast<float>(value) : NAN);
}

void WagoNetVarComponent::publish_statistics_() {
  publish_counter(this->rejected_packets_sensor_, this->rejected_packets_, this->published_stats_[0]);
  publish_counter(this->rx_packets_sensor_, this->rx_packets_, this->published_stats_[1]);
  publish_counter(this->tx_packets_sensor_, this->tx_packets_, this->published_stats_[2]);
  publish_counter(this->rx_errors_sensor_, this->rx_errors_, this->published_stats_[3]);
  publish_counter(this->tx_errors_sensor_, this->tx_errors_, this->published_stats_[4]);
}

// ---------------------------------------------------------------------------------------------
// Transmission
// ---------------------------------------------------------------------------------------------

void WagoNetVarComponent::update() {
  // Cyclic transmission, runs in parallel with send_on_change. Never called when update_interval is "never".
  if (this->enable_write_)
    this->send_packet_();
}

void WagoNetVarComponent::trigger_send_() {
  if (!this->enable_write_ || !this->send_on_change_ || !this->is_dirty_)
    return;
  if (millis() - this->last_sent_time_ >= this->min_interval_ms_)
    this->send_packet_();
}

void WagoNetVarComponent::send_packet_() {
  if (!this->enable_write_ || !this->udp_started_ || this->payload_size_ == 0)
    return;

  const size_t total_length = NETVAR_HEADER_SIZE + this->payload_size_;
  uint8_t *frame = this->frame_.data();

  std::memset(frame, 0, NETVAR_HEADER_SIZE);  // reserved, var_index, flags and checksum stay 0
  std::memcpy(frame, MAGIC_BYTES, sizeof(MAGIC_BYTES));
  write_u16(&frame[8], this->list_id_);
  write_u16(&frame[12], this->var_count_);
  write_u16(&frame[14], static_cast<uint16_t>(total_length));
  write_u16(&frame[16], this->sequence_counter_);
  std::memcpy(frame + NETVAR_HEADER_SIZE, this->payload_.data(), this->payload_size_);

  // Also set on failure, so that a failing send is retried at most once per min_interval.
  this->last_sent_time_ = millis();

  if (!this->udp_->send(this->ip_, this->port_, frame, total_length)) {
    ++this->tx_errors_;
    return;
  }

  ++this->tx_packets_;
  ++this->sequence_counter_;
  this->is_dirty_ = false;
}

// ---------------------------------------------------------------------------------------------
// Writing values
// ---------------------------------------------------------------------------------------------

VarDef *WagoNetVarComponent::prepare_write_(const std::string &name) {
  if (!this->enable_write_) {
    ESP_LOGW(TAG, "Cannot set '%s': this list is not in write mode", name.c_str());
    return nullptr;
  }
  VarDef *var = this->get_var(name);
  if (var == nullptr || var->byte_offset + var->size > this->payload_.size()) {
    ESP_LOGW(TAG, "Cannot set '%s': unknown variable or component not ready", name.c_str());
    return nullptr;
  }
  return var;
}

void WagoNetVarComponent::commit_(const VarDef &var, const uint8_t *bytes) {
  uint8_t *destination = this->payload_.data() + var.byte_offset;
  if (std::memcmp(destination, bytes, var.size) == 0)
    return;
  std::memcpy(destination, bytes, var.size);

  if (this->send_on_change_) {
    this->is_dirty_ = true;
    this->trigger_send_();  // honours min_interval, otherwise loop() sends later
  }
}

void WagoNetVarComponent::write_int64_(VarDef &var, int64_t value) {
  uint8_t bytes[4] = {};
  switch (var.type) {
    case VariableType::BOOL:
      bytes[0] = value != 0 ? 1 : 0;
      break;
    case VariableType::STRING:
      this->write_text_(var, format_number(static_cast<double>(value), false));
      return;
    case VariableType::REAL: {
      const float real = static_cast<float>(value);
      std::memcpy(bytes, &real, sizeof(real));
      break;
    }
    default:
      // Overflow: the least significant bytes are kept, the most significant ones are discarded.
      for (size_t i = 0; i < var.size; ++i)
        bytes[i] = static_cast<uint8_t>(static_cast<uint64_t>(value) >> (8 * i));
      break;
  }
  this->commit_(var, bytes);
}

void WagoNetVarComponent::write_double_(VarDef &var, double value) {
  if (!std::isfinite(value)) {
    ESP_LOGW(TAG, "Ignoring non-finite value for '%s'", var.name.c_str());
    return;
  }

  switch (var.type) {
    case VariableType::BOOL:
      this->write_int64_(var, value != 0.0 ? 1 : 0);
      return;
    case VariableType::STRING:
      this->write_text_(var, format_number(value, true));
      return;
    case VariableType::REAL: {
      if (std::fabs(value) > FLT_MAX) {
        ESP_LOGW(TAG, "Value out of REAL range for '%s'", var.name.c_str());
        return;
      }
      const float real = static_cast<float>(value);
      uint8_t bytes[4];
      std::memcpy(bytes, &real, sizeof(real));
      this->commit_(var, bytes);
      return;
    }
    default: {
      // Round to the nearest integer; clamp only to keep the double -> int64 conversion defined.
      const double limit = 9.0e18;
      this->write_int64_(var, static_cast<int64_t>(std::llround(std::max(-limit, std::min(limit, value)))));
      return;
    }
  }
}

void WagoNetVarComponent::write_text_(VarDef &var, const std::string &text) {
  for (const char c : text) {
    if (static_cast<unsigned char>(c) > 127) {
      ESP_LOGW(TAG, "String value for '%s' contains a non-ASCII character", var.name.c_str());
      return;
    }
  }
  if (text.size() > var.size - 1)
    ESP_LOGW(TAG, "String value for '%s' truncated to %u characters", var.name.c_str(),
             static_cast<unsigned>(var.size - 1));

  uint8_t bytes[256] = {};  // STRING(n) has at most 255 + 1 bytes
  std::memcpy(bytes, text.data(), std::min(text.size(), var.size - 1));
  this->commit_(var, bytes);
}

void WagoNetVarComponent::set_variable_value(const std::string &name, bool value) {
  if (VarDef *var = this->prepare_write_(name))
    this->write_int64_(*var, value ? 1 : 0);
}

void WagoNetVarComponent::set_variable_value(const std::string &name, int value) {
  if (VarDef *var = this->prepare_write_(name))
    this->write_int64_(*var, value);
}

void WagoNetVarComponent::set_variable_value(const std::string &name, float value) {
  if (VarDef *var = this->prepare_write_(name))
    this->write_double_(*var, static_cast<double>(value));
}

void WagoNetVarComponent::set_variable_value(const std::string &name, const std::string &value) {
  VarDef *var = this->prepare_write_(name);
  if (var == nullptr)
    return;

  if (var->type == VariableType::STRING) {
    this->write_text_(*var, value);
    return;
  }

  if (var->type == VariableType::BOOL) {
    const std::string text = to_lower(value);
    if (text == "1" || text == "true" || text == "on" || text == "yes")
      this->write_int64_(*var, 1);
    else if (text == "0" || text == "false" || text == "off" || text == "no")
      this->write_int64_(*var, 0);
    else
      ESP_LOGW(TAG, "Cannot convert '%s' to BOOL for '%s'", value.c_str(), name.c_str());
    return;
  }

  // Numeric types are parsed directly (no detour through float) and the whole text must be consumed.
  const char *begin = value.c_str();
  char *end = nullptr;
  errno = 0;

  if (var->type == VariableType::REAL) {
    const double number = std::strtod(begin, &end);
    if (end == begin || !only_whitespace_left(end) || errno == ERANGE) {
      ESP_LOGW(TAG, "Cannot convert '%s' to REAL for '%s'", value.c_str(), name.c_str());
      return;
    }
    this->write_double_(*var, number);
    return;
  }

  const bool is_signed =
      var->type == VariableType::SINT || var->type == VariableType::INT || var->type == VariableType::DINT;
  int64_t number = 0;
  if (is_signed)
    number = static_cast<int64_t>(std::strtoll(begin, &end, 10));
  else
    number = static_cast<int64_t>(std::strtoull(begin, &end, 10));  // bit pattern, truncated below

  if (end == begin || !only_whitespace_left(end) || errno == ERANGE) {
    ESP_LOGW(TAG, "Cannot convert '%s' to %s for '%s'", value.c_str(), var->type_name.c_str(), name.c_str());
    return;
  }
  this->write_int64_(*var, number);
}

std::string WagoNetVarComponent::get_variable_value(const std::string &name) const {
  const VarDef *var = this->get_var(name);
  if (var == nullptr || var->byte_offset + var->size > this->payload_.size())
    return "";

  const uint8_t *data = this->payload_.data() + var->byte_offset;
  switch (var->type) {
    case VariableType::BOOL:
      return data[0] != 0 ? "1" : "0";
    case VariableType::STRING: {
      size_t length = 0;
      while (length < var->size && data[length] != '\0')
        ++length;
      return std::string(reinterpret_cast<const char *>(data), length);
    }
    default:
      return format_number(this->decode_number_(*var, data), var->type == VariableType::REAL);
  }
}

}  // namespace wago_netvar
}  // namespace esphome
