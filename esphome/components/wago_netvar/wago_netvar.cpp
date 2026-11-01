#include "wago_netvar.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>

namespace esphome {
namespace wago_netvar {

static const char *const TAG = "wago_netvar";

static constexpr uint8_t MAGIC_BYTES[4] = {
    0x00, 0x2D, 0x53, 0x33
};

static constexpr size_t MAX_PACKETS_PER_LOOP = 8;

#ifdef USE_SWITCH
void WagoSwitch::write_state(bool state) {
  if (parent_ == nullptr)
    return;

  parent_->set_variable_value(var_name_, state);
  publish_state(state);
}
#endif

#ifdef USE_NUMBER
void WagoNumber::control(float value) {
  if (parent_ == nullptr)
    return;

  parent_->set_variable_value(var_name_, value);
  publish_state(value);
}
#endif

bool WagoNetVarComponent::get_type_info(
    const std::string &type,
    VariableType &variable_type,
    size_t &size,
    size_t &alignment) {

  size = 0;
  alignment = 1;

  if (type == "BOOL") {
    variable_type = VariableType::BOOL;
    size = 1;
  } else if (type == "BYTE") {
    variable_type = VariableType::BYTE;
    size = 1;
  } else if (type == "USINT") {
    variable_type = VariableType::USINT;
    size = 1;
  } else if (type == "SINT") {
    variable_type = VariableType::SINT;
    size = 1;
  } else if (type == "WORD") {
    variable_type = VariableType::WORD;
    size = 2;
    alignment = 2;
  } else if (type == "UINT") {
    variable_type = VariableType::UINT;
    size = 2;
    alignment = 2;
  } else if (type == "INT") {
    variable_type = VariableType::INT;
    size = 2;
    alignment = 2;
  } else if (type == "DWORD") {
    variable_type = VariableType::DWORD;
    size = 4;
    alignment = 4;
  } else if (type == "UDINT") {
    variable_type = VariableType::UDINT;
    size = 4;
    alignment = 4;
  } else if (type == "DINT") {
    variable_type = VariableType::DINT;
    size = 4;
    alignment = 4;
  } else if (type == "TIME" || type == "DATE" || type == "TIME_OF_DAY" || type == "DATE_AND_TIME") {
    variable_type = VariableType::UDINT;
    size = 4;
    alignment = 4;
  } else if (type == "REAL") {
    variable_type = VariableType::REAL;
    size = 4;
    alignment = 2;
  } else if (type.rfind("STRING(", 0) == 0 &&
             type.back() == ')') {

    const std::string length_text =
        type.substr(7, type.size() - 8);

    if (length_text.empty())
      return false;

    char *end = nullptr;
    unsigned long length =
        std::strtoul(length_text.c_str(), &end, 10);

    if (end == nullptr || *end != '\0' ||
        length < 1 || length > 255)
      return false;

    variable_type = VariableType::STRING;
    size = static_cast<size_t>(length) + 1;
  } else {
    return false;
  }

  return true;
}

void WagoNetVarComponent::add_variable(
    const std::string &name,
    const std::string &type,
    int byte_offset) {

  if (name.empty()) {
    ESP_LOGE(TAG, "Variable name cannot be empty");
    mark_failed();
    return;
  }

  if (get_var(name) != nullptr) {
    ESP_LOGE(TAG, "Duplicate variable: %s", name.c_str());
    mark_failed();
    return;
  }

  VarDef var;
  var.name = name;
  var.type_name = type;

  if (!get_type_info(type, var.type, var.size, var.alignment)) {
    ESP_LOGE(TAG, "Unsupported variable type: %s (%s)", type.c_str(), name.c_str());
    mark_failed();
    return;
  }

  if (byte_offset < -1) {
    ESP_LOGE(TAG, "Invalid byte_offset for %s", name.c_str());
    mark_failed();
    return;
  }

  if (byte_offset >= 0) {
    var.has_manual_offset = true;
    var.byte_offset = static_cast<size_t>(byte_offset);
  }

  if (variables_.size() >= 255) {
    ESP_LOGE(TAG, "Too many NetVar variables");
    mark_failed();
    return;
  }

  variables_.push_back(var);
}

VarDef *WagoNetVarComponent::get_var(const std::string &name) {
  for (auto &var : variables_) {
    if (var.name == name)
      return &var;
  }
  return nullptr;
}

#ifdef USE_SENSOR
void WagoNetVarComponent::register_sensor(
    const std::string &name,
    sensor::Sensor *entity) {
  VarDef *var = get_var(name);
  if (var == nullptr || entity == nullptr) {
    ESP_LOGE(TAG, "Invalid sensor mapping: %s", name.c_str());
    mark_failed();
    return;
  }
  if (var->type == VariableType::BOOL || var->type == VariableType::STRING) {
    ESP_LOGE(TAG, "Invalid numeric sensor type: %s", name.c_str());
    mark_failed();
    return;
  }
  if (var->sensor_ptr != nullptr) {
    ESP_LOGE(TAG, "Duplicate sensor mapping: %s", name.c_str());
    mark_failed();
    return;
  }
  var->sensor_ptr = entity;
}
#endif

#ifdef USE_BINARY_SENSOR
void WagoNetVarComponent::register_binary_sensor(
    const std::string &name,
    binary_sensor::BinarySensor *entity) {
  VarDef *var = get_var(name);
  if (var == nullptr || entity == nullptr || var->type != VariableType::BOOL) {
    ESP_LOGE(TAG, "Invalid binary sensor mapping: %s", name.c_str());
    mark_failed();
    return;
  }
  if (var->binary_sensor_ptr != nullptr) {
    ESP_LOGE(TAG, "Duplicate binary sensor mapping: %s", name.c_str());
    mark_failed();
    return;
  }
  var->binary_sensor_ptr = entity;
}
#endif

#ifdef USE_TEXT_SENSOR
void WagoNetVarComponent::register_text_sensor(
    const std::string &name,
    text_sensor::TextSensor *entity) {
  VarDef *var = get_var(name);
  if (var == nullptr || entity == nullptr || var->type != VariableType::STRING) {
    ESP_LOGE(TAG, "Invalid text sensor mapping: %s", name.c_str());
    mark_failed();
    return;
  }
  if (var->text_sensor_ptr != nullptr) {
    ESP_LOGE(TAG, "Duplicate text sensor mapping: %s", name.c_str());
    mark_failed();
    return;
  }
  var->text_sensor_ptr = entity;
}
#endif

#ifdef USE_SWITCH
void WagoNetVarComponent::register_switch(
    const std::string &name,
    switch_::Switch *entity) {
  VarDef *var = get_var(name);
  if (var == nullptr || entity == nullptr || var->type != VariableType::BOOL) {
    ESP_LOGE(TAG, "Invalid switch mapping: %s", name.c_str());
    mark_failed();
    return;
  }
  if (var->switch_ptr != nullptr) {
    ESP_LOGE(TAG, "Duplicate switch mapping: %s", name.c_str());
    mark_failed();
    return;
  }
  var->switch_ptr = entity;
}
#endif

#ifdef USE_NUMBER
void WagoNetVarComponent::register_number(
    const std::string &name,
    number::Number *entity) {
  VarDef *var = get_var(name);
  if (var == nullptr || entity == nullptr ||
      var->type == VariableType::BOOL ||
      var->type == VariableType::STRING) {
    ESP_LOGE(TAG, "Invalid number mapping: %s", name.c_str());
    mark_failed();
    return;
  }
  if (var->number_ptr != nullptr) {
    ESP_LOGE(TAG, "Duplicate number mapping: %s", name.c_str());
    mark_failed();
    return;
  }
  var->number_ptr = entity;
}
#endif

bool WagoNetVarComponent::calculate_layout_() {
  size_t maximum_end = 0;

  for (size_t i = 0; i < variables_.size(); ++i) {
    VarDef &var = variables_[i];
    if (!var.has_manual_offset)
      continue;

    if (var.byte_offset > NETVAR_MAX_PAYLOAD_SIZE ||
        var.size > NETVAR_MAX_PAYLOAD_SIZE - var.byte_offset) {
      ESP_LOGE(TAG, "Variable %s exceeds payload size", var.name.c_str());
      return false;
    }

    const size_t end = var.byte_offset + var.size;
    for (size_t j = 0; j < i; ++j) {
      const VarDef &other = variables_[j];
      if (!other.has_manual_offset)
        continue;

      const size_t other_end = other.byte_offset + other.size;
      if (var.byte_offset < other_end && other.byte_offset < end) {
        ESP_LOGE(TAG, "Variables %s and %s overlap", var.name.c_str(), other.name.c_str());
        return false;
      }
    }

    if (end > maximum_end)
      maximum_end = end;
  }

  size_t cursor = 0;

  for (size_t i = 0; i < variables_.size(); ++i) {
    VarDef &var = variables_[i];
    if (var.has_manual_offset)
      continue;

    if (alignment_ && var.alignment > 1) {
      const size_t remainder = cursor % var.alignment;
      if (remainder != 0)
        cursor += var.alignment - remainder;
    }

    while (true) {
      bool collision = false;
      size_t next_cursor = cursor;

      if (cursor > NETVAR_MAX_PAYLOAD_SIZE ||
          var.size > NETVAR_MAX_PAYLOAD_SIZE - cursor)
        return false;

      const size_t end = cursor + var.size;

      for (size_t j = 0; j < variables_.size(); ++j) {
        if (j == i)
          continue;

        const VarDef &other = variables_[j];

        if (other.byte_offset + other.size <= cursor ||
            other.byte_offset >= end)
          continue;

        // Avoid collisions with manual offsets and with automatic variables
        // already placed earlier in the list.
        if (other.has_manual_offset || j < i) {
          collision = true;
          if (other.byte_offset + other.size > next_cursor)
            next_cursor = other.byte_offset + other.size;
        }
      }

      if (!collision)
        break;

      cursor = next_cursor;

      if (alignment_ && var.alignment > 1) {
        const size_t remainder = cursor % var.alignment;
        if (remainder != 0)
          cursor += var.alignment - remainder;
      }
    }

    var.byte_offset = cursor;
    cursor += var.size;

    if (cursor > maximum_end)
      maximum_end = cursor;
  }

  if (maximum_end > NETVAR_MAX_PAYLOAD_SIZE)
    return false;

  payload_size_ = maximum_end;
  return true;
}

void WagoNetVarComponent::setup() {
  if (variables_.empty()) {
    ESP_LOGE(TAG, "No NetVar variables configured");
    mark_failed();
    return;
  }

  if (!calculate_layout_()) {
    ESP_LOGE(TAG, "NetVar payload exceeds maximum size");
    mark_failed();
    return;
  }

  const uint16_t actual_var_count = static_cast<uint16_t>(variables_.size());
  if (var_count_ != 0 && var_count_ != actual_var_count) {
    ESP_LOGE(TAG, "var_count mismatch: configured=%u actual=%u", var_count_, actual_var_count);
    mark_failed();
    return;
  }
  var_count_ = actual_var_count;

  // Walidacja adresu IP nadawcy/odbiorcy
  IPAddress check_ip;
  if (!check_ip.fromString(ip_address_.c_str())) {
    ESP_LOGE(TAG, "Invalid IP address: %s", ip_address_.c_str());
    mark_failed();
    return;
  }

  if (enable_write_ && ip_address_ == "0.0.0.0") {
    ESP_LOGE(TAG, "Adres IP 0.0.0.0 jest niedozwolony w trybie write");
    mark_failed();
    return;
  }

#ifdef ARDUINO
  udp_ = std::make_unique<ArduinoUdpTransport>();
#elif defined(USE_ESP_IDF)
  udp_ = std::make_unique<EspIdfUdpTransport>();
#else
  udp_ = std::make_unique<ArduinoUdpTransport>();
#endif

  tx_buffer_.assign(payload_size_, 0);

  if (udp_->begin(port_) == 0) {
    ESP_LOGE(TAG, "Unable to initialize UDP on port %u", port_);
    mark_failed();
    return;
  }

  udp_started_ = true;

  ESP_LOGI(
      TAG,
      "NetVar started: IP=%s port=%u list_ID=%u variables=%u payload=%u direction=%s",
      ip_address_.c_str(),
      port_,
      list_id_,
      var_count_,
      static_cast<unsigned>(payload_size_),
      enable_read_ ? "read" : "write");
}

void WagoNetVarComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Wago NetVar:");
  ESP_LOGCONFIG(TAG, "  IP address: %s", ip_address_.c_str());
  ESP_LOGCONFIG(TAG, "  Port: %u", port_);
  ESP_LOGCONFIG(TAG, "  List ID: %u", list_id_);
  ESP_LOGCONFIG(TAG, "  Variable count: %u", var_count_);
  ESP_LOGCONFIG(TAG, "  Direction: %s", enable_read_ ? "read" : "write");
  ESP_LOGCONFIG(TAG, "  Alignment: %s", alignment_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Send on change: %s", send_on_change_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Min interval: %u ms", min_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Payload size: %u bytes", static_cast<unsigned>(payload_size_));
  for (const auto &var : variables_) {
    ESP_LOGCONFIG(TAG, "  Variable '%s': type=%s offset=%u size=%u",
                  var.name.c_str(), var.type_name.c_str(), static_cast<unsigned>(var.byte_offset), static_cast<unsigned>(var.size));
  }
}

uint16_t WagoNetVarComponent::read_u16_(const uint8_t *data) const {
  // Little Endian
  return static_cast<uint16_t>(data[0]) |
         (static_cast<uint16_t>(data[1]) << 8);
}

void WagoNetVarComponent::write_u16_(uint8_t *data, uint16_t value) const {
  // Little Endian
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
}

uint64_t WagoNetVarComponent::read_integer_(const uint8_t *data, size_t size) const {
  uint64_t result = 0;
  for (size_t i = 0; i < size; ++i) {
    result |= static_cast<uint64_t>(data[i]) << (i * 8);
  }
  return result;
}

bool WagoNetVarComponent::write_integer_(VarDef &var, uint64_t value) {
  if (var.byte_offset + var.size > tx_buffer_.size())
    return false;

  bool changed = false;
  for (size_t i = 0; i < var.size; ++i) {
    const uint8_t byte = static_cast<uint8_t>(value >> (i * 8));
    uint8_t &destination = tx_buffer_[var.byte_offset + i];

    if (destination != byte) {
      destination = byte;
      changed = true;
    }
  }

  if (changed && send_on_change_) {
    is_dirty_ = true;
    trigger_send_();
  }

  return true;
}

void WagoNetVarComponent::set_variable_value(const std::string &name, bool value) {
  VarDef *var = get_var(name);
  if (var == nullptr || var->type != VariableType::BOOL || !enable_write_)
    return;

  if (var->byte_offset >= tx_buffer_.size())
    return;

  const uint8_t updated = value ? 1 : 0;
  uint8_t &destination = tx_buffer_[var->byte_offset];

  if (destination != updated) {
    destination = updated;
    if (send_on_change_) {
      is_dirty_ = true;
      trigger_send_();
    }
  }
}

void WagoNetVarComponent::set_variable_value(const std::string &name, int value) {
  VarDef *var = get_var(name);
  if (var == nullptr || !enable_write_)
    return;

  if (var->type == VariableType::BOOL || var->type == VariableType::REAL || var->type == VariableType::STRING)
    return;

  write_integer_(*var, static_cast<uint64_t>(value));
}

void WagoNetVarComponent::set_variable_value(const std::string &name, float value) {
  VarDef *var = get_var(name);
  if (var == nullptr || !enable_write_ || !std::isfinite(value))
    return;

  if (var->type == VariableType::REAL) {
    uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    write_integer_(*var, raw);
    return;
  }

  write_integer_(*var, static_cast<uint64_t>(value));
}

void WagoNetVarComponent::set_variable_value(const std::string &name, const std::string &value) {
  VarDef *var = get_var(name);
  if (var == nullptr || !enable_write_)
    return;

  if (var->type == VariableType::BOOL) {
    if (value == "1" || value == "true" || value == "TRUE" ||
        value == "on" || value == "ON" || value == "yes" || value == "YES") {
      set_variable_value(name, true);
    } else {
      set_variable_value(name, false);
    }
    return;
  }

  if (var->type == VariableType::STRING) {
    for (char c : value) {
      if (static_cast<unsigned char>(c) > 127) {
        ESP_LOGE(TAG, "String value for %s contains non-ASCII character", name.c_str());
        return;
      }
    }

    if (var->byte_offset + var->size > tx_buffer_.size())
      return;

    bool changed = false;
    for (size_t i = 0; i < var->size; ++i) {
      const uint8_t byte = (i < value.size() && i < var->size - 1)
                               ? static_cast<uint8_t>(value[i])
                               : 0;
      uint8_t &destination = tx_buffer_[var->byte_offset + i];
      if (destination != byte) {
        destination = byte;
        changed = true;
      }
    }

    if (changed && send_on_change_) {
      is_dirty_ = true;
      trigger_send_();
    }
    return;
  }

  char *end = nullptr;
  const double numeric_value = std::strtod(value.c_str(), &end);
  if (end == value.c_str() || !std::isfinite(numeric_value)) {
    ESP_LOGE(TAG, "Nie można przekonwertować wartości '%s' na liczbę dla zmiennej %s", value.c_str(), name.c_str());
    return;
  }
  set_variable_value(name, static_cast<float>(numeric_value));
}

std::string WagoNetVarComponent::get_variable_value(const std::string &name) {
  VarDef *var = get_var(name);
  if (var == nullptr || var->byte_offset + var->size > tx_buffer_.size())
    return "";

  if (var->type == VariableType::BOOL) {
    return tx_buffer_[var->byte_offset] != 0 ? "1" : "0";
  }

  if (var->type == VariableType::STRING) {
    const char *data = reinterpret_cast<const char *>(&tx_buffer_[var->byte_offset]);
    size_t length = 0;
    while (length < var->size && data[length] != '\0')
      ++length;
    return std::string(data, length);
  }

  const uint64_t raw = read_integer_(&tx_buffer_[var->byte_offset], var->size);
  if (var->type == VariableType::REAL) {
    uint32_t bits = static_cast<uint32_t>(raw);
    float value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return std::to_string(value);
  }

  switch (var->type) {
    case VariableType::SINT:
      return std::to_string(static_cast<int8_t>(raw));
    case VariableType::INT:
      return std::to_string(static_cast<int16_t>(raw));
    case VariableType::DINT:
      return std::to_string(static_cast<int32_t>(raw));
    default:
      return std::to_string(raw);
  }
}

void WagoNetVarComponent::trigger_send_() {
  if (!enable_write_ || !send_on_change_ || !is_dirty_)
    return;

  const uint32_t now = millis();
  if (now - last_sent_time_ >= min_interval_ms_)
    send_packet_();
}

void WagoNetVarComponent::loop() {
  if (!udp_started_)
    return;

  if (enable_read_) {
    for (size_t count = 0; count < MAX_PACKETS_PER_LOOP; ++count) {
      const int packet_size = udp_->parsePacket();
      if (packet_size <= 0)
        break;

      const IPAddress remote_ip = udp_->remoteIP();
      const uint16_t remote_port = udp_->remotePort();

      if (static_cast<size_t>(packet_size) > NETVAR_MAX_PACKET_SIZE) {
        ++rejected_packets_;
        while (udp_->available())
          udp_->read();
        continue;
      }

      const int bytes_read = udp_->read(rx_packet_buffer_.data(), packet_size);
      if (bytes_read != packet_size) {
        ++rx_errors_;
        continue;
      }

      if (!validate_packet_(static_cast<size_t>(packet_size), remote_ip, remote_port)) {
        ++rejected_packets_;
        continue;
      }

      ++rx_packets_;
      unpack_payload_(
          rx_packet_buffer_.data() + NETVAR_HEADER_SIZE,
          static_cast<size_t>(packet_size) - NETVAR_HEADER_SIZE);
    }
  }

  if (enable_write_ && send_on_change_ && is_dirty_) {
    trigger_send_();
  }
}

bool WagoNetVarComponent::validate_packet_(
    size_t packet_size,
    const IPAddress &remote_ip,
    uint16_t remote_port) {

  (void)remote_port;  // The sender's source UDP port is not validated because the packet
                      // has already arrived at the socket bound to the configured port.
                      // The destination port is implicitly validated by the socket binding.

  if (packet_size < NETVAR_HEADER_SIZE || packet_size > NETVAR_MAX_PACKET_SIZE)
    return false;

  // If a specific IP (other than 0.0.0.0) is configured, validate the sender.
  // If 0.0.0.0 is specified, accept packets from any IP (e.g. broadcast / dynamic controller IP).
  if (ip_address_ != "0.0.0.0") {
    IPAddress configured_ip;
    configured_ip.fromString(ip_address_.c_str());
    if (configured_ip != remote_ip)
      return false;
  }

  const uint8_t *header = rx_packet_buffer_.data();
  for (size_t i = 0; i < 4; ++i) {
    if (header[i] != MAGIC_BYTES[i])
      return false;
  }

  const uint16_t packet_list_id = read_u16_(&header[8]);
  const uint16_t packet_var_count = read_u16_(&header[12]);
  const uint16_t declared_length = read_u16_(&header[14]);

  if (packet_list_id != list_id_ || packet_var_count != var_count_)
    return false;

  // sequence_counter (bytes 16-17) is read but not used for validation.
  const uint16_t packet_sequence_counter = read_u16_(&header[16]);
  // checksum (bytes 18-19) is read but not used for validation.
  const uint16_t packet_checksum = read_u16_(&header[18]);
  (void)packet_sequence_counter;
  (void)packet_checksum;

  // Validate frame_size field (bytes 14-15): declared frame length must match received packet size.
  if (declared_length != static_cast<uint16_t>(packet_size))
    return false;

  if (packet_size - NETVAR_HEADER_SIZE != payload_size_)
    return false;

  return true;
}

void WagoNetVarComponent::unpack_payload_(const uint8_t *payload, size_t length) {
  if (payload == nullptr || length != payload_size_)
    return;

  for (auto &var : variables_) {
    if (var.byte_offset + var.size > length)
      continue;
    publish_variable_(var, payload);
  }
}

void WagoNetVarComponent::publish_variable_(VarDef &var, const uint8_t *payload) {
  const uint8_t *data = payload + var.byte_offset;

  if (var.type == VariableType::BOOL) {
    const bool value = data[0] != 0;
#ifdef USE_BINARY_SENSOR
    if (var.binary_sensor_ptr != nullptr)
      var.binary_sensor_ptr->publish_state(value);
#endif
    return;
  }

  if (var.type == VariableType::STRING) {
    size_t length = 0;
    while (length < var.size && data[length] != '\0')
      ++length;

#ifdef USE_TEXT_SENSOR
    if (var.text_sensor_ptr != nullptr) {
      var.text_sensor_ptr->publish_state(
          std::string(reinterpret_cast<const char *>(data), length));
    }
#endif
    return;
  }

  const uint64_t raw = read_integer_(data, var.size);
  double value = 0;

  if (var.type == VariableType::REAL) {
    const uint32_t bits = static_cast<uint32_t>(raw);
    float converted = 0;
    std::memcpy(&converted, &bits, sizeof(converted));
    value = converted;
  } else {
    int64_t signed_value = static_cast<int64_t>(raw);
    switch (var.type) {
      case VariableType::SINT:
        signed_value = static_cast<int8_t>(raw);
        value = static_cast<double>(signed_value);
        break;
      case VariableType::INT:
        signed_value = static_cast<int16_t>(raw);
        value = static_cast<double>(signed_value);
        break;
      case VariableType::DINT:
        signed_value = static_cast<int32_t>(raw);
        value = static_cast<double>(signed_value);
        break;
      default:
        value = static_cast<double>(raw);
        break;
    }
  }

  if (!std::isfinite(value))
    return;

#ifdef USE_SENSOR
  if (var.sensor_ptr != nullptr)
    var.sensor_ptr->publish_state(static_cast<float>(value));
#endif
}

void WagoNetVarComponent::publish_statistics_() {
#ifdef USE_SENSOR
  if (rejected_packets_sensor_ != nullptr)
    rejected_packets_sensor_->publish_state(static_cast<float>(rejected_packets_));
  if (rx_packets_sensor_ != nullptr)
    rx_packets_sensor_->publish_state(static_cast<float>(rx_packets_));
  if (tx_packets_sensor_ != nullptr)
    tx_packets_sensor_->publish_state(static_cast<float>(tx_packets_));
  if (rx_errors_sensor_ != nullptr)
    rx_errors_sensor_->publish_state(static_cast<float>(rx_errors_));
  if (tx_errors_sensor_ != nullptr)
    tx_errors_sensor_->publish_state(static_cast<float>(tx_errors_));
#endif
}

void WagoNetVarComponent::update() {
  if (enable_write_ && !send_on_change_ && update_interval_ms_ > 0)
    send_packet_();
  publish_statistics_();
}

void WagoNetVarComponent::send_packet_() {
  if (!enable_write_ || !udp_started_ || payload_size_ == 0)
    return;

  IPAddress target_ip;
  if (!target_ip.fromString(ip_address_.c_str())) {
    ++tx_errors_;
    return;
  }

  const size_t total_length = NETVAR_HEADER_SIZE + payload_size_;
  if (total_length > NETVAR_MAX_PACKET_SIZE) {
    ++tx_errors_;
    return;
  }

  uint8_t header[NETVAR_HEADER_SIZE] = {};
  std::memcpy(header, MAGIC_BYTES, sizeof(MAGIC_BYTES));

  write_u16_(&header[8], list_id_);
  write_u16_(&header[12], var_count_);
  write_u16_(&header[14], static_cast<uint16_t>(total_length));
  write_u16_(&header[16], sequence_counter_);

  if (!udp_->beginPacket(target_ip, port_)) {
    ++tx_errors_;
    return;
  }

  udp_->write(header, NETVAR_HEADER_SIZE);
  udp_->write(tx_buffer_.data(), payload_size_);
  
  if (udp_->endPacket() != 1) {
    ++tx_errors_;
    return;
  }

  ++tx_packets_;
  ++sequence_counter_;
  last_sent_time_ = millis();
  is_dirty_ = false;
}

}  // namespace wago_netvar
}  // namespace esphome
