
#include "wago_netvar.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

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
  } else if (type == "TIME") {
    variable_type = VariableType::TIME;
    size = 4;
    alignment = 4;
  } else if (type == "REAL") {
    variable_type = VariableType::REAL;
    size = 4;
    alignment = 2;
  } else if (type.rfind("STRING(", 0) == 0 &&
             type.size() > 8 &&
             type.back() == ')') {

    const std::string length_text =
        type.substr(7, type.size() - 8);

    if (length_text.empty())
      return false;

    char *end = nullptr;
    const unsigned long length =
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
    const std::string &type) {

  if (name.empty()) {
    ESP_LOGE(TAG, "Variable name cannot be empty");
    mark_failed();
    return;
  }

  if (variable_indices_.find(name) != variable_indices_.end()) {
    ESP_LOGE(TAG, "Duplicate variable: %s", name.c_str());
    mark_failed();
    return;
  }

  VarDef var;
  var.name = name;
  var.type_name = type;

  if (!get_type_info(type, var.type, var.size, var.alignment)) {
    ESP_LOGE(
        TAG,
        "Unsupported variable type: %s (%s)",
        type.c_str(),
        name.c_str());
    mark_failed();
    return;
  }

  if (variables_.size() >= NETVAR_MAX_PAYLOAD_SIZE) {
    ESP_LOGE(TAG, "Too many NetVar variables");
    mark_failed();
    return;
  }

  const size_t index = variables_.size();
  variables_.push_back(var);
  variable_indices_.emplace(name, index);
}

VarDef *WagoNetVarComponent::get_var(
    const std::string &name) {

  const auto it = variable_indices_.find(name);

  if (it == variable_indices_.end())
    return nullptr;

  if (it->second >= variables_.size())
    return nullptr;

  return &variables_[it->second];
}

#ifdef USE_SENSOR
void WagoNetVarComponent::register_sensor(
    const std::string &name,
    sensor::Sensor *entity) {

  VarDef *var = get_var(name);

  if (var == nullptr || entity == nullptr ||
      var->type == VariableType::BOOL ||
      var->type == VariableType::STRING) {
    ESP_LOGE(TAG, "Invalid numeric sensor mapping: %s", name.c_str());
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

  if (var == nullptr || entity == nullptr ||
      var->type != VariableType::BOOL) {
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

  if (var == nullptr || entity == nullptr ||
      var->type != VariableType::STRING) {
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

  if (var == nullptr || entity == nullptr ||
      var->type != VariableType::BOOL) {
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
  size_t offset = 0;
  uint8_t bool_bit_index = 0;

  for (auto &var : variables_) {
    if (var.type != VariableType::BOOL && bool_bit_index > 0) {
      ++offset;
      bool_bit_index = 0;
    }

    if (var.type == VariableType::BOOL && pack_bools_) {
      var.byte_offset = offset;
      var.bit_offset = bool_bit_index;

      ++bool_bit_index;

      if (bool_bit_index >= 8) {
        bool_bit_index = 0;
        ++offset;
      }

      continue;
    }

    if (alignment_ && var.alignment > 1) {
      const size_t remainder = offset % var.alignment;

      if (remainder != 0)
        offset += var.alignment - remainder;
    }

    var.byte_offset = offset;
    var.bit_offset = 0;

    if (offset > NETVAR_MAX_PAYLOAD_SIZE ||
        var.size > NETVAR_MAX_PAYLOAD_SIZE - offset)
      return false;

    offset += var.size;
  }

  if (bool_bit_index > 0)
    ++offset;

  if (offset > NETVAR_MAX_PAYLOAD_SIZE)
    return false;

  payload_size_ = offset;
  return true;
}

void WagoNetVarComponent::setup() {
  if (variables_.empty()) {
    ESP_LOGE(TAG, "No NetVar variables configured");
    mark_failed();
    return;
  }

  if (enable_read_ == enable_write_) {
    ESP_LOGE(TAG, "Invalid communication direction");
    mark_failed();
    return;
  }

  if (!target_ip_.fromString(ip_address_.c_str())) {
    ESP_LOGE(TAG, "Invalid target IPv4 address");
    mark_failed();
    return;
  }

  if (!calculate_layout_()) {
    ESP_LOGE(
        TAG,
        "NetVar payload exceeds maximum size of %u bytes",
        static_cast<unsigned>(NETVAR_MAX_PAYLOAD_SIZE));
    mark_failed();
    return;
  }

  if (variables_.size() > UINT16_MAX ||
      var_count_ != variables_.size()) {
    ESP_LOGE(
        TAG,
        "var_count mismatch: configured=%u actual=%u",
        var_count_,
        static_cast<unsigned>(variables_.size()));
    mark_failed();
    return;
  }

  tx_buffer_.assign(payload_size_, 0);

  if (tx_buffer_.size() != payload_size_) {
    ESP_LOGE(TAG, "Unable to allocate NetVar TX buffer");
    mark_failed();
    return;
  }

  if (udp_.begin(port_) == 0) {
    ESP_LOGE(TAG, "Unable to initialize UDP on port %u", port_);
    mark_failed();
    return;
  }

  udp_started_ = true;

  ESP_LOGI(
      TAG,
      "NetVar started: IP=%s port=%u list_ID=%u variables=%u "
      "payload=%u direction=%s endian=%s",
      ip_address_.c_str(),
      port_,
      list_id_,
      var_count_,
      static_cast<unsigned>(payload_size_),
      enable_read_ ? "read" : "write",
      big_endian_ ? "big" : "little");
}

uint16_t WagoNetVarComponent::read_u16_(
    const uint8_t *data) const {

  if (big_endian_)
    return (static_cast<uint16_t>(data[0]) << 8) | data[1];

  return static_cast<uint16_t>(data[0]) |
         (static_cast<uint16_t>(data[1]) << 8);
}

void WagoNetVarComponent::write_u16_(
    uint8_t *data,
    uint16_t value) const {

  if (big_endian_) {
    data[0] = static_cast<uint8_t>(value >> 8);
    data[1] = static_cast<uint8_t>(value);
  } else {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8);
  }
}

uint64_t WagoNetVarComponent::read_integer_(
    const uint8_t *data,
    size_t size) const {

  uint64_t result = 0;

  for (size_t i = 0; i < size; ++i) {
    if (big_endian_) {
      result = (result << 8) | data[i];
    } else {
      result |= static_cast<uint64_t>(data[i]) << (i * 8);
    }
  }

  return result;
}

bool WagoNetVarComponent::write_integer_(
    VarDef &var,
    uint64_t value) {

  if (var.byte_offset > tx_buffer_.size() ||
      var.size > tx_buffer_.size() - var.byte_offset)
    return false;

  bool changed = false;

  for (size_t i = 0; i < var.size; ++i) {
    const size_t shift_index =
        big_endian_ ? var.size - 1 - i : i;

    const uint8_t byte =
        static_cast<uint8_t>(value >> (shift_index * 8));

    uint8_t &destination =
        tx_buffer_[var.byte_offset + i];

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

void WagoNetVarComponent::set_variable_value(
    const std::string &name,
    bool value) {

  VarDef *var = get_var(name);

  if (var == nullptr || var->type != VariableType::BOOL ||
      !enable_write_)
    return;

  if (var->byte_offset >= tx_buffer_.size())
    return;

  const uint8_t mask =
      static_cast<uint8_t>(1U << var->bit_offset);

  uint8_t &destination = tx_buffer_[var->byte_offset];

  const uint8_t updated =
      value ? destination | mask : destination & static_cast<uint8_t>(~mask);

  if (destination != updated) {
    destination = updated;

    if (send_on_change_) {
      is_dirty_ = true;
      trigger_send_();
    }
  }
}

void WagoNetVarComponent::set_variable_value(
    const std::string &name,
    int value) {

  VarDef *var = get_var(name);

  if (var == nullptr || !enable_write_ ||
      var->type == VariableType::BOOL ||
      var->type == VariableType::REAL ||
      var->type == VariableType::STRING)
    return;

  int64_t signed_min = 0;
  int64_t signed_max = 0;
  uint64_t unsigned_max = 0;
  bool is_signed = false;

  switch (var->type) {
    case VariableType::SINT:
      is_signed = true;
      signed_min = INT8_MIN;
      signed_max = INT8_MAX;
      break;

    case VariableType::INT:
      is_signed = true;
      signed_min = INT16_MIN;
      signed_max = INT16_MAX;
      break;

    case VariableType::DINT:
      is_signed = true;
      signed_min = INT32_MIN;
      signed_max = INT32_MAX;
      break;

    default:
      unsigned_max =
          var->size == 4 ? UINT32_MAX :
          var->size == 2 ? UINT16_MAX :
                           UINT8_MAX;
      break;
  }

  if (is_signed) {
    if (static_cast<int64_t>(value) < signed_min ||
        static_cast<int64_t>(value) > signed_max) {
      ESP_LOGW(TAG, "Value out of range for %s", name.c_str());
      return;
    }
  } else {
    if (value < 0 ||
        static_cast<uint64_t>(value) > unsigned_max) {
      ESP_LOGW(TAG, "Value out of range for %s", name.c_str());
      return;
    }
  }

  write_integer_(*var, static_cast<uint64_t>(
      static_cast<int64_t>(value)));
}

void WagoNetVarComponent::set_variable_value(
    const std::string &name,
    float value) {

  VarDef *var = get_var(name);

  if (var == nullptr || !enable_write_ ||
      !std::isfinite(value))
    return;

  if (var->type == VariableType::REAL) {
    uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    write_integer_(*var, raw);
    return;
  }

  if (var->type == VariableType::BOOL ||
      var->type == VariableType::STRING)
    return;

  const double numeric_value = static_cast<double>(value);

  const bool signed_type =
      var->type == VariableType::SINT ||
      var->type == VariableType::INT ||
      var->type == VariableType::DINT;

  const double minimum =
      var->type == VariableType::SINT ? INT8_MIN :
      var->type == VariableType::INT ? INT16_MIN :
      var->type == VariableType::DINT ? INT32_MIN : 0.0;

  const double maximum =
      var->type == VariableType::SINT ? INT8_MAX :
      var->type == VariableType::INT ? INT16_MAX :
      var->type == VariableType::DINT ? INT32_MAX :
      var->size == 1 ? UINT8_MAX :
      var->size == 2 ? UINT16_MAX : UINT32_MAX;

  if (numeric_value < minimum || numeric_value > maximum) {
    ESP_LOGW(TAG, "Value out of range for %s", name.c_str());
    return;
  }

  if (signed_type) {
    write_integer_(
        *var,
        static_cast<uint64_t>(static_cast<int64_t>(value)));
  } else {
    write_integer_(*var, static_cast<uint64_t>(value));
  }
}

void WagoNetVarComponent::set_variable_value(
    const std::string &name,
    const std::string &value) {

  VarDef *var = get_var(name);

  if (var == nullptr || !enable_write_)
    return;

  if (var->type == VariableType::BOOL) {
    if (value == "1" || value == "true" || value == "TRUE") {
      set_variable_value(name, true);
    } else if (value == "0" || value == "false" ||
               value == "FALSE") {
      set_variable_value(name, false);
    } else {
      ESP_LOGW(TAG, "Invalid BOOL value for %s", name.c_str());
    }

    return;
  }

  if (var->type == VariableType::STRING) {
    if (var->byte_offset > tx_buffer_.size() ||
        var->size > tx_buffer_.size() - var->byte_offset)
      return;

    bool changed = false;
    const size_t max_chars = var->size - 1;
    const size_t copy_length =
        value.size() < max_chars ? value.size() : max_chars;

    for (size_t i = 0; i < var->size; ++i) {
      const uint8_t byte =
          i < copy_length ? static_cast<uint8_t>(value[i]) : 0;

      uint8_t &destination =
          tx_buffer_[var->byte_offset + i];

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

  if (end == value.c_str() || end == nullptr ||
      *end != '\0' || !std::isfinite(numeric_value)) {
    ESP_LOGW(TAG, "Invalid numeric value for %s", name.c_str());
    return;
  }

  if (numeric_value < -2147483648.0 ||
      numeric_value > 4294967295.0) {
    ESP_LOGW(TAG, "Value out of range for %s", name.c_str());
    return;
  }

  set_variable_value(name, static_cast<float>(numeric_value));
}

std::string WagoNetVarComponent::get_variable_value(
    const std::string &name) {

  VarDef *var = get_var(name);

  if (var == nullptr)
    return "";

  if (var->byte_offset > tx_buffer_.size() ||
      var->size > tx_buffer_.size() - var->byte_offset)
    return "";

  if (var->type == VariableType::BOOL) {
    return (tx_buffer_[var->byte_offset] &
            (1U << var->bit_offset)) ? "1" : "0";
  }

  if (var->type == VariableType::STRING) {
    const char *data =
        reinterpret_cast<const char *>(
            &tx_buffer_[var->byte_offset]);

    size_t length = 0;

    while (length < var->size && data[length] != '\0')
      ++length;

    return std::string(data, length);
  }

  const uint64_t raw =
      read_integer_(&tx_buffer_[var->byte_offset], var->size);

  if (var->type == VariableType::REAL) {
    const uint32_t bits = static_cast<uint32_t>(raw);
    float value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return std::to_string(value);
  }

  int64_t signed_value = static_cast<int64_t>(raw);

  switch (var->type) {
    case VariableType::SINT:
      signed_value = static_cast<int8_t>(raw);
      break;

    case VariableType::INT:
      signed_value = static_cast<int16_t>(raw);
      break;

    case VariableType::DINT:
      signed_value = static_cast<int32_t>(raw);
      break;

    default:
      break;
  }

  switch (var->type) {
    case VariableType::BYTE:
    case VariableType::USINT:
    case VariableType::WORD:
    case VariableType::UINT:
    case VariableType::DWORD:
    case VariableType::UDINT:
    case VariableType::TIME:
      return std::to_string(raw);

    default:
      return std::to_string(signed_value);
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
      const int packet_size = udp_.parsePacket();

      if (packet_size <= 0)
        break;

      const IPAddress remote_ip = udp_.remoteIP();
      const uint16_t remote_port = udp_.remotePort();

      if (static_cast<size_t>(packet_size) >
          NETVAR_MAX_PACKET_SIZE) {

        ++rejected_packets_;

        while (udp_.available())
          udp_.read();

        ESP_LOGW(TAG, "Rejected oversized UDP packet: %d bytes",
                 packet_size);
        continue;
      }

      const int bytes_read =
          udp_.read(rx_packet_buffer_, static_cast<size_t>(packet_size));

      if (bytes_read != packet_size) {
        ++rx_errors_;
        ESP_LOGW(TAG, "Incomplete UDP packet read");
        continue;
      }

      if (!validate_packet_(
              static_cast<size_t>(packet_size),
              remote_ip,
              remote_port)) {
        ++rejected_packets_;
        continue;
      }

      ++rx_packets_;

      unpack_payload_(
          rx_packet_buffer_ + NETVAR_HEADER_SIZE,
          static_cast<size_t>(packet_size) - NETVAR_HEADER_SIZE);
    }
  }

  if (enable_write_ && send_on_change_ && is_dirty_) {
    const uint32_t now = millis();

    if (now - last_sent_time_ >= min_interval_ms_)
      send_packet_();
  }
}

bool WagoNetVarComponent::validate_packet_(
    size_t packet_size,
    const IPAddress &remote_ip,
    uint16_t remote_port) {

  if (packet_size < NETVAR_HEADER_SIZE ||
      packet_size > NETVAR_MAX_PACKET_SIZE) {
    ESP_LOGW(TAG, "Invalid UDP packet size");
    return false;
  }

  if (remote_ip != target_ip_ || remote_port != port_) {
    ESP_LOGW(
        TAG,
        "Rejected packet from %s:%u",
        remote_ip.toString().c_str(),
        remote_port);
    return false;
  }

  const uint8_t *header = rx_packet_buffer_;

  for (size_t i = 0; i < 4; ++i) {
    if (header[i] != MAGIC_BYTES[i]) {
      ESP_LOGW(TAG, "Invalid NetVar magic bytes");
      return false;
    }
  }

  const uint16_t packet_list_id = read_u16_(&header[8]);
  const uint16_t packet_var_count = read_u16_(&header[12]);
  const uint16_t declared_length = read_u16_(&header[14]);

  if (packet_list_id != list_id_) {
    ESP_LOGW(
        TAG,
        "Invalid list_ID: received=%u expected=%u",
        packet_list_id,
        list_id_);
    return false;
  }

  if (packet_var_count != var_count_) {
    ESP_LOGW(
        TAG,
        "Invalid var_count: received=%u expected=%u",
        packet_var_count,
        var_count_);
    return false;
  }

  if (declared_length != packet_size) {
    ESP_LOGW(
        TAG,
        "Invalid declared packet length: received=%u actual=%u",
        declared_length,
        static_cast<unsigned>(packet_size));
    return false;
  }

  if (packet_size - NETVAR_HEADER_SIZE != payload_size_) {
    ESP_LOGW(
        TAG,
        "Invalid payload length: received=%u expected=%u",
        static_cast<unsigned>(packet_size - NETVAR_HEADER_SIZE),
        static_cast<unsigned>(payload_size_));
    return false;
  }

  return true;
}

void WagoNetVarComponent::unpack_payload_(
    const uint8_t *payload,
    size_t length) {

  if (payload == nullptr || length != payload_size_)
    return;

  for (auto &var : variables_) {
    if (var.byte_offset > length ||
        var.size > length - var.byte_offset)
      continue;

    publish_variable_(var, payload);
  }
}

void WagoNetVarComponent::publish_variable_(
    VarDef &var,
    const uint8_t *payload) {

  const uint8_t *data = payload + var.byte_offset;

  if (var.type == VariableType::BOOL) {
    const bool value =
        (data[0] & (1U << var.bit_offset)) != 0;

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
          std::string(
              reinterpret_cast<const char *>(data),
              length));
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

void WagoNetVarComponent::update() {
  if (enable_write_)
    send_packet_();
}

void WagoNetVarComponent::send_packet_() {
  if (!enable_write_ || !udp_started_ ||
      payload_size_ == 0 ||
      payload_size_ > NETVAR_MAX_PAYLOAD_SIZE)
    return;

  const size_t total_length =
      NETVAR_HEADER_SIZE + payload_size_;

  if (total_length > NETVAR_MAX_PACKET_SIZE ||
      total_length > UINT16_MAX) {
    ++tx_errors_;
    ESP_LOGE(TAG, "NetVar packet exceeds maximum size");
    return;
  }

  uint8_t header[NETVAR_HEADER_SIZE] = {};

  std::memcpy(header, MAGIC_BYTES, sizeof(MAGIC_BYTES));

  write_u16_(&header[8], list_id_);
  write_u16_(&header[12], var_count_);
  write_u16_(&header[14], static_cast<uint16_t>(total_length));
  write_u16_(&header[16], sequence_counter_);

  if (!udp_.beginPacket(target_ip_, port_)) {
    ++tx_errors_;
    ESP_LOGE(TAG, "UDP beginPacket failed");
    return;
  }

  const size_t header_written =
      udp_.write(header, NETVAR_HEADER_SIZE);

  const size_t payload_written =
      udp_.write(tx_buffer_.data(), payload_size_);

  const int result = udp_.endPacket();

  if (header_written != NETVAR_HEADER_SIZE ||
      payload_written != payload_size_ ||
      result != 1) {
    ++tx_errors_;
    ESP_LOGE(TAG, "UDP transmission failed");
    return;
  }

  ++tx_packets_;
  ++sequence_counter_;

  last_sent_time_ = millis();
  is_dirty_ = false;

  ESP_LOGD(
      TAG,
      "TX %s:%u ID=%u variables=%u size=%u",
      ip_address_.c_str(),
      port_,
      list_id_,
      var_count_,
      static_cast<unsigned>(total_length));
}

}  // namespace wago_netvar
}  // namespace esphome
