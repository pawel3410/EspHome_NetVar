#include "wago_netvar.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace esphome {
namespace wago_netvar {

static const char *const TAG = "wago_netvar";

void WagoSwitch::write_state(bool state) {
    if (parent_ != nullptr) {
        parent_->set_variable_value(var_name_, state);
        publish_state(state);
    }
}

void WagoNumber::control(float value) {
    if (parent_ != nullptr) {
        parent_->set_variable_value(var_name_, value);
        publish_state(value);
    }
}

void WagoNetVarComponent::get_type_info(const std::string &type, size_t &size, size_t &align) {
    if (type == "BOOL") { size = 1; align = 1; }
    else if (type == "BYTE" || type == "USINT" || type == "SINT") { size = 1; align = 1; }
    else if (type == "WORD" || type == "UINT" || type == "INT") { size = 2; align = 2; }
    else if (type == "DWORD" || type == "UDINT" || type == "DINT" || type == "TIME") { size = 4; align = 4; }
    else if (type == "REAL") { size = 4; align = 2; }
    else if (type == "LREAL") { size = 8; align = 8; }
    else if (type.rfind("STRING", 0) == 0) {
        size_t start = type.find('(');
        size_t end = type.find(')');
        if (start != std::string::npos && end != std::string::npos) {
            int len = std::stoi(type.substr(start + 1, end - start - 1));
            size = len + 1;
        } else {
            size = 81;
        }
        align = 1;
    }
}

void WagoNetVarComponent::add_variable(const std::string &name, const std::string &type) {
    size_t size = 1, align = 1;
    get_type_info(type, size, align);
    variables_.push_back({name, type, size, align});
    if (var_values_.find(name) == var_values_.end()) {
        var_values_[name] = "0";
    }
}

void WagoNetVarComponent::set_variable_value(const std::string &name, float value) {
    std::string new_val = std::to_string(value);
    if (var_values_[name] != new_val) {
        var_values_[name] = new_val;
        is_dirty_ = true;
        trigger_send_();
    }
}

void WagoNetVarComponent::set_variable_value(const std::string &name, int value) {
    std::string new_val = std::to_string(value);
    if (var_values_[name] != new_val) {
        var_values_[name] = new_val;
        is_dirty_ = true;
        trigger_send_();
    }
}

void WagoNetVarComponent::set_variable_value(const std::string &name, bool value) {
    std::string new_val = value ? "1" : "0";
    if (var_values_[name] != new_val) {
        var_values_[name] = new_val;
        is_dirty_ = true;
        trigger_send_();
    }
}

void WagoNetVarComponent::set_variable_value(const std::string &name, const std::string &value) {
    if (var_values_[name] != value) {
        var_values_[name] = value;
        is_dirty_ = true;
        trigger_send_();
    }
}

std::string WagoNetVarComponent::get_variable_value(const std::string &name) { return var_values_[name]; }

uint16_t WagoNetVarComponent::read_u16(const uint8_t *ptr) {
    return big_endian_ ? ((ptr[0] << 8) | ptr[1]) : (ptr[0] | (ptr[1] << 8));
}

void WagoNetVarComponent::setup() {
    ESP_LOGI(TAG, "Start NetVar [Target IP: %s, COB-ID: %d, Port: %d, Read: %s, Write: %s, Alignment: %s]", 
             ip_address_.c_str(), cob_id_, port_, enable_read_ ? "TAK" : "NIE", enable_write_ ? "TAK" : "NIE", alignment_ ? "TAK" : "NIE");
    
    if (enable_read_) {
        udp_.begin(port_);
    }
}

void WagoNetVarComponent::trigger_send_() {
    if (!enable_write_ || !send_on_change_) return;

    uint32_t now = millis();
    if (now - last_sent_time_ >= min_interval_ms_) {
        send_packet_();
    }
}

void WagoNetVarComponent::loop() {
    if (enable_read_) {
        int packet_size = udp_.parsePacket();
        if (packet_size >= 20) {
            std::vector<uint8_t> buffer(packet_size);
            udp_.read(buffer.data(), packet_size);

            if (buffer[0] == 0x00 && buffer[1] == 0x2D && buffer[2] == 0x53 && buffer[3] == 0x33) {
                uint16_t pkt_cob_id = read_u16(&buffer[8]);
                uint16_t pkt_checksum = read_u16(&buffer[12]);

                if (pkt_cob_id == cob_id_ && (checksum_ == 0 || pkt_checksum == checksum_)) {
                    unpack_payload(buffer.data() + 20, packet_size - 20);
                }
            }
        }
    }

    if (enable_write_ && is_dirty_) {
        uint32_t now = millis();
        if (now - last_sent_time_ >= min_interval_ms_) {
            send_packet_();
        }
    }
}

void WagoNetVarComponent::update() {
    if (enable_write_) {
        send_packet_();
    }
}

void WagoNetVarComponent::unpack_payload(const uint8_t *payload, size_t len) {
    size_t offset = 0;
    uint8_t bool_bit_index = 0;
    uint8_t current_bool_byte = 0;

    for (const auto &var : variables_) {
        if (var.type != "BOOL" && bool_bit_index > 0) {
            offset++;
            bool_bit_index = 0;
        }

        if (var.type == "BOOL" && pack_bools_) {
            if (bool_bit_index == 0 && offset < len) {
                current_bool_byte = payload[offset];
            }
            bool val = (current_bool_byte >> bool_bit_index) & 0x01;
            var_values_[var.name] = val ? "1" : "0";
            ESP_LOGV(TAG, "Odczyt (pakowany BOOL) <- %s = %s", var.name.c_str(), var_values_[var.name].c_str());
            if (binary_sensors_.count(var.name) && binary_sensors_[var.name]->state != val) {
                binary_sensors_[var.name]->publish_state(val);
            }
            bool_bit_index++;
            if (bool_bit_index >= 8) {
                bool_bit_index = 0;
                offset++;
            }
        } else {
            if (alignment_ && var.align > 1) {
                size_t rem = offset % var.align;
                if (rem != 0) offset += (var.align - rem);
            }

            if (offset + var.size > len) break;

            if (var.type == "BOOL") {
                bool val = (payload[offset] != 0);
                var_values_[var.name] = val ? "1" : "0";
                ESP_LOGV(TAG, "Odczyt (BOOL) <- %s = %s", var.name.c_str(), var_values_[var.name].c_str());
                if (binary_sensors_.count(var.name) && binary_sensors_[var.name]->state != val) {
                    binary_sensors_[var.name]->publish_state(val);
                }
            } else if (var.type == "REAL") {
                uint32_t val_u = 0;
                for (int i = 0; i < 4; i++) {
                    if (big_endian_) val_u = (val_u << 8) | payload[offset + i];
                    else val_u |= ((uint32_t)payload[offset + i] << (i * 8));
                }
                float f;
                std::memcpy(&f, &val_u, sizeof(float));
                var_values_[var.name] = std::to_string(f);
                ESP_LOGV(TAG, "Odczyt (REAL) <- %s = %s", var.name.c_str(), var_values_[var.name].c_str());
                if (sensors_.count(var.name)) {
                    float cur = sensors_[var.name]->get_raw_state();
                    if (std::isnan(cur) || std::abs(f - cur) >= 0.001f) {
                        sensors_[var.name]->publish_state(f);
                    }
                }
            } else if (var.type.rfind("STRING", 0) == 0) {
                std::string str_val((const char*)(payload + offset), strnlen((const char*)(payload + offset), var.size));
                var_values_[var.name] = str_val;
                ESP_LOGV(TAG, "Odczyt (STRING) <- %s = %s", var.name.c_str(), var_values_[var.name].c_str());
            } else {
                int64_t val_i = 0;
                for (size_t i = 0; i < var.size; i++) {
                    if (big_endian_) val_i = (val_i << 8) | payload[offset + i];
                    else val_i |= ((int64_t)payload[offset + i] << (i * 8));
                }
                var_values_[var.name] = std::to_string(val_i);
                ESP_LOGV(TAG, "Odczyt (%s) <- %s = %s", var.type.c_str(), var.name.c_str(), var_values_[var.name].c_str());
                if (sensors_.count(var.name)) {
                    float f_val = static_cast<float>(val_i);
                    if (sensors_[var.name]->get_raw_state() != f_val) {
                        sensors_[var.name]->publish_state(f_val);
                    }
                }
            }
            offset += var.size;
        }
    }
}

std::vector<uint8_t> WagoNetVarComponent::pack_value(const VarDef &var, const std::string &val_str) {
    std::vector<uint8_t> bytes;

    if (var.type == "BOOL") {
        bool b = (val_str == "1" || val_str == "true" || val_str == "True");
        bytes.push_back(b ? 1 : 0);
    } else if (var.type == "REAL") {
        char *endptr = nullptr;
        float f = std::strtof(val_str.c_str(), &endptr);
        uint32_t val_u;
        std::memcpy(&val_u, &f, sizeof(float));
        for (int i = 0; i < 4; i++) {
            if (big_endian_) bytes.push_back((val_u >> (24 - i * 8)) & 0xFF);
            else bytes.push_back((val_u >> (i * 8)) & 0xFF);
        }
    } else if (var.type.rfind("STRING", 0) == 0) {
        for (size_t i = 0; i < var.size; i++) {
            if (i < val_str.length() && i < var.size - 1) {
                bytes.push_back(static_cast<uint8_t>(val_str[i]));
            } else {
                bytes.push_back(0x00);
            }
        }
    } else {
        char *endptr = nullptr;
        int64_t val_i = std::strtoll(val_str.c_str(), &endptr, 10);

        int64_t min_lim = 0, max_lim = 0;
        bool check_range = true;

        if (var.type == "BYTE" || var.type == "USINT") { min_lim = 0; max_lim = 255; }
        else if (var.type == "SINT") { min_lim = -128; max_lim = 127; }
        else if (var.type == "WORD" || var.type == "UINT") { min_lim = 0; max_lim = 65535; }
        else if (var.type == "INT") { min_lim = -32768; max_lim = 32767; }
        else if (var.type == "DWORD" || var.type == "UDINT") { min_lim = 0; max_lim = 4294967295LL; }
        else if (var.type == "DINT") { min_lim = -2147483648LL; max_lim = 2147483647LL; }
        else { check_range = false; }

        if (check_range && (val_i < min_lim || val_i > max_lim)) {
            ESP_LOGW(TAG, "Zmienna '%s' (%s) przekracza zakres [%lld, %lld]: %lld. Przycięto.",
                     var.name.c_str(), var.type.c_str(), (long long)min_lim, (long long)max_lim, (long long)val_i);
            val_i = std::clamp(val_i, min_lim, max_lim);
        }

        for (size_t i = 0; i < var.size; i++) {
            if (big_endian_) bytes.push_back((val_i >> ((var.size - 1 - i) * 8)) & 0xFF);
            else bytes.push_back((val_i >> (i * 8)) & 0xFF);
        }
    }
    return bytes;
}

void WagoNetVarComponent::send_packet_() {
    if (ip_address_.empty()) {
        ESP_LOGE(TAG, "Brak skonfigurowanego adresu IP.");
        return;
    }

    IPAddress target_ip;
    if (!target_ip.fromString(ip_address_.c_str())) {
        ESP_LOGE(TAG, "Nieprawidłowy format adresu IP: '%s'. Transmisja przerwana.", ip_address_.c_str());
        return;
    }

    std::vector<uint8_t> payload;
    uint8_t bool_bit_index = 0;
    uint8_t current_bool_byte = 0;

    for (const auto &var : variables_) {
        std::string val_str = var_values_[var.name];
        ESP_LOGV(TAG, "Wysyłanie -> %s = %s (typ: %s)", var.name.c_str(), val_str.c_str(), var.type.c_str());

        if (var.type != "BOOL" && bool_bit_index > 0) {
            payload.push_back(current_bool_byte);
            bool_bit_index = 0;
            current_bool_byte = 0;
        }

        if (var.type == "BOOL" && pack_bools_) {
            if (val_str == "1" || val_str == "true") current_bool_byte |= (1 << bool_bit_index);
            bool_bit_index++;
            if (bool_bit_index >= 8) {
                payload.push_back(current_bool_byte);
                bool_bit_index = 0;
                current_bool_byte = 0;
            }
        } else {
            if (alignment_ && var.align > 1) {
                size_t rem = payload.size() % var.align;
                if (rem != 0) {
                    for (size_t i = 0; i < (var.align - rem); i++) payload.push_back(0x00);
                }
            }
            std::vector<uint8_t> packed = pack_value(var, val_str);
            payload.insert(payload.end(), packed.begin(), packed.end());
        }
    }

    if (bool_bit_index > 0) payload.push_back(current_bool_byte);

    uint8_t header[20] = {0};
    header[0] = 0x00; header[1] = 0x2D; header[2] = 0x53; header[3] = 0x33;
    uint16_t total_len = 20 + payload.size();

    auto write_u16 = [this](uint8_t* ptr, uint16_t val) {
        if (big_endian_) { ptr[0] = (val >> 8) & 0xFF; ptr[1] = val & 0xFF; }
        else { ptr[0] = val & 0xFF; ptr[1] = (val >> 8) & 0xFF; }
    };

    write_u16(&header[8], cob_id_);
    write_u16(&header[12], checksum_);
    write_u16(&header[14], total_len);
    write_u16(&header[16], sequence_counter_++);

    if (!udp_.beginPacket(target_ip, port_)) {
        ESP_LOGE(TAG, "Błąd inicjalizacji UDP do %s:%d", ip_address_.c_str(), port_);
        return;
    }

    udp_.write(header, 20);
    if (!payload.empty()) {
        udp_.write(payload.data(), payload.size());
    }
    udp_.endPacket();

    last_sent_time_ = millis();
    is_dirty_ = false;
}

} // namespace wago_netvar
} // namespace esphome
