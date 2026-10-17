#include "wago_netvar.h"
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace esphome {
namespace wago_netvar {

static const char *const TAG = "wago_netvar";

// Pomocnicza funkcja do formatowania ciągu bajtów do postaci czytelnego ciągu HEX
static std::string format_hex(const uint8_t *data, size_t length) {
    std::string out;
    char buf[4];
    for (size_t i = 0; i < length; i++) {
        snprintf(buf, sizeof(buf), "%02X ", data[i]);
        out += buf;
    }
    if (!out.empty()) out.pop_back();
    return out;
}

// --- Wewnętrzne funkcje pomocnicze do szybkiej obsługi pamięci ---
static void write_bytes(uint8_t *dst, uint64_t val, size_t size, bool big_endian) {
    for (size_t i = 0; i < size; i++) {
        dst[i] = big_endian ? ((val >> ((size - 1 - i) * 8)) & 0xFF) : ((val >> (i * 8)) & 0xFF);
    }
}

static uint64_t read_bytes(const uint8_t *src, size_t size, bool big_endian) {
    uint64_t val = 0;
    for (size_t i = 0; i < size; i++) {
        if (big_endian) val = (val << 8) | src[i];
        else val |= ((uint64_t)src[i] << (i * 8));
    }
    return val;
}

#ifdef USE_SWITCH
void WagoSwitch::write_state(bool state) {
    if (parent_ != nullptr) {
        parent_->set_variable_value(var_name_, state);
        publish_state(state);
    }
}
#endif

#ifdef USE_NUMBER
void WagoNumber::control(float value) {
    if (parent_ != nullptr) {
        parent_->set_variable_value(var_name_, value);
        publish_state(value);
    }
}
#endif

void WagoNetVarComponent::get_type_info(const std::string &type, size_t &size, size_t &align) {
    size = 0;
    align = 1;
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
            size = std::stoi(type.substr(start + 1, end - start - 1)) + 1;
        } else {
            size = 81;
        }
        align = 1;
    }
}

void WagoNetVarComponent::add_variable(const std::string &name, const std::string &type) {
    VarDef v;
    v.name = name;
    v.type = type;
    get_type_info(type, v.size, v.align);
    if (v.size == 0) {
        ESP_LOGE(TAG, "Nieznany lub nieobsługiwany typ zmiennej: %s dla zmiennej %s", type.c_str(), name.c_str());
        return;
    }
    variables_.push_back(v);
}

VarDef* WagoNetVarComponent::get_var(const std::string &name) {
    for (auto &v : variables_) {
        if (v.name == name) return &v;
    }
    return nullptr;
}

void WagoNetVarComponent::setup() {
    ESP_LOGI(TAG, "Start NetVar [Target IP: %s, list_ID: %d, Var Count: %d, Port: %d, Direction: %s, Alignment: %s, Send on Change: %s]", 
             ip_address_.c_str(), list_id_, var_count_, port_, direction_.c_str(), alignment_ ? "TAK" : "NIE", send_on_change_ ? "TAK" : "NIE");
    
    size_t offset = 0;
    uint8_t bool_bit_index = 0;

    for (auto &var : variables_) {
        if (var.type != "BOOL" && bool_bit_index > 0) {
            offset++;
            bool_bit_index = 0;
        }

        if (var.type == "BOOL" && pack_bools_) {
            var.byte_offset = offset;
            var.bit_offset = bool_bit_index;
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
            var.byte_offset = offset;
            var.bit_offset = 0;
            offset += var.size;
        }
    }
    if (bool_bit_index > 0) offset++;
    
    payload_size_ = offset;
    
    tx_buffer_.resize(payload_size_, 0);
    rx_buffer_.resize(payload_size_, 0);
    rx_packet_buffer_.resize(1500, 0);

    udp_.begin(port_);
}

void WagoNetVarComponent::set_variable_value(const std::string &name, bool value) {
    VarDef *var = get_var(name);
    if (!var || var->type != "BOOL") return;
    
    uint8_t mask = 1 << var->bit_offset;
    uint8_t old_byte = tx_buffer_[var->byte_offset];
    uint8_t new_byte = value ? (old_byte | mask) : (old_byte & ~mask);
    
    if (old_byte != new_byte) {
        tx_buffer_[var->byte_offset] = new_byte;
        if (send_on_change_) {
            is_dirty_ = true;
            trigger_send_();
        }
    }
}

void WagoNetVarComponent::set_variable_value(const std::string &name, float value) {
    VarDef *var = get_var(name);
    if (!var) return;

    std::vector<uint8_t> temp(var->size, 0);
    if (var->type == "REAL") {
        uint32_t val_u;
        std::memcpy(&val_u, &value, 4);
        write_bytes(temp.data(), val_u, 4, big_endian_);
    } else if (var->type == "LREAL") {
        double d = value;
        uint64_t val_u;
        std::memcpy(&val_u, &d, 8);
        write_bytes(temp.data(), val_u, 8, big_endian_);
    } else {
        int64_t val_i = static_cast<int64_t>(value);
        write_bytes(temp.data(), static_cast<uint64_t>(val_i), var->size, big_endian_);
    }

    bool changed = false;
    for (size_t i = 0; i < var->size; i++) {
        if (tx_buffer_[var->byte_offset + i] != temp[i]) {
            tx_buffer_[var->byte_offset + i] = temp[i];
            changed = true;
        }
    }
    if (changed && send_on_change_) {
        is_dirty_ = true;
        trigger_send_();
    }
}

void WagoNetVarComponent::set_variable_value(const std::string &name, int value) {
    VarDef *var = get_var(name);
    if (!var) return;
    
    std::vector<uint8_t> temp(var->size, 0);
    write_bytes(temp.data(), static_cast<uint64_t>(value), var->size, big_endian_);
    
    bool changed = false;
    for (size_t i = 0; i < var->size; i++) {
        if (tx_buffer_[var->byte_offset + i] != temp[i]) {
            tx_buffer_[var->byte_offset + i] = temp[i];
            changed = true;
        }
    }
    if (changed && send_on_change_) {
        is_dirty_ = true;
        trigger_send_();
    }
}

void WagoNetVarComponent::set_variable_value(const std::string &name, const std::string &value) {
    VarDef *var = get_var(name);
    if (!var) return;

    if (var->type.rfind("STRING", 0) == 0) {
        std::vector<uint8_t> temp(var->size, 0);
        for (size_t i = 0; i < var->size; i++) {
            temp[i] = (i < value.length() && i < var->size - 1) ? value[i] : 0x00;
        }

        bool changed = false;
        for (size_t i = 0; i < var->size; i++) {
            if (tx_buffer_[var->byte_offset + i] != temp[i]) {
                tx_buffer_[var->byte_offset + i] = temp[i];
                changed = true;
            }
        }
        if (changed && send_on_change_) {
            is_dirty_ = true;
            trigger_send_();
        }
    } else if (var->type == "BOOL") {
        set_variable_value(name, (value == "1" || value == "true" || value == "True"));
    } else if (var->type == "REAL" || var->type == "LREAL") {
        set_variable_value(name, std::strtof(value.c_str(), nullptr));
    } else {
        set_variable_value(name, static_cast<int>(std::strtol(value.c_str(), nullptr, 10)));
    }
}

std::string WagoNetVarComponent::get_variable_value(const std::string &name) {
    VarDef *var = get_var(name);
    if (!var) return "";

    if (var->type == "BOOL") {
        return (tx_buffer_[var->byte_offset] & (1 << var->bit_offset)) ? "1" : "0";
    } else if (var->type == "REAL") {
        uint32_t val_u = read_bytes(&tx_buffer_[var->byte_offset], 4, big_endian_);
        float f; std::memcpy(&f, &val_u, 4);
        return std::to_string(f);
    } else if (var->type == "LREAL") {
        uint64_t val_u = read_bytes(&tx_buffer_[var->byte_offset], 8, big_endian_);
        double d; std::memcpy(&d, &val_u, 8);
        return std::to_string(d);
    } else if (var->type.rfind("STRING", 0) == 0) {
        return std::string((const char*)&tx_buffer_[var->byte_offset], strnlen((const char*)&tx_buffer_[var->byte_offset], var->size));
    } else {
        int64_t val_i = static_cast<int64_t>(read_bytes(&tx_buffer_[var->byte_offset], var->size, big_endian_));
        if (var->type == "SINT") val_i = static_cast<int8_t>(val_i);
        else if (var->type == "INT") val_i = static_cast<int16_t>(val_i);
        else if (var->type == "DINT") val_i = static_cast<int32_t>(val_i);
        return std::to_string(val_i);
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
        if (packet_size > 0) {
            if ((size_t)packet_size > rx_packet_buffer_.size()) {
                udp_.flush();
            } else {
                IPAddress remote_ip = udp_.remoteIP();
                uint16_t remote_port = udp_.remotePort();
                udp_.read(rx_packet_buffer_.data(), packet_size);

                uint16_t pkt_list_id = 0;
                uint16_t pkt_var_count = 0;
                if (packet_size >= 20 && rx_packet_buffer_[0] == 0x00 && rx_packet_buffer_[1] == 0x2D && 
                    rx_packet_buffer_[2] == 0x53 && rx_packet_buffer_[3] == 0x33) {
                    pkt_list_id = big_endian_ ? ((rx_packet_buffer_[8] << 8) | rx_packet_buffer_[9]) : (rx_packet_buffer_[8] | (rx_packet_buffer_[9] << 8));
                    pkt_var_count = big_endian_ ? ((rx_packet_buffer_[12] << 8) | rx_packet_buffer_[13]) : (rx_packet_buffer_[12] | (rx_packet_buffer_[13] << 8));
                }

                // Logowanie w formacie RX IP:PORT (ID: X, var: Y, size: Z B)
                ESP_LOGI(TAG, "RX %s:%d (ID: %d, var: %d, size: %d B)", 
                         remote_ip.toString().c_str(), remote_port, pkt_list_id, pkt_var_count, packet_size);

                // Podgląd HEX w logu DEBUG
                ESP_LOGD(TAG, "RX HEX: %s", 
                         format_hex(rx_packet_buffer_.data(), packet_size).c_str());

                if (packet_size >= 20 && rx_packet_buffer_[0] == 0x00 && rx_packet_buffer_[1] == 0x2D && 
                    rx_packet_buffer_[2] == 0x53 && rx_packet_buffer_[3] == 0x33) {
                    
                    if (pkt_list_id == list_id_ && (var_count_ == 0 || pkt_var_count == var_count_)) {
                        unpack_payload(rx_packet_buffer_.data() + 20, packet_size - 20);
                    } else {
                        ESP_LOGW(TAG, "Odrzucono pakiet NetVar: niezgodny list_ID (%d != %d) lub var_count (%d != %d)", 
                                 pkt_list_id, list_id_, pkt_var_count, var_count_);
                    }
                } else {
                    ESP_LOGW(TAG, "Odrzucono pakiet: niepoprawny nagłówek NetVar Magic Bytes");
                }
            }
        }
    }

    if (enable_write_ && send_on_change_ && is_dirty_) {
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
    if (len < payload_size_) return;

    for (auto &var : variables_) {
        bool changed = false;
        
        if (var.type == "BOOL") {
            uint8_t mask = 1 << var.bit_offset;
            if ((rx_buffer_[var.byte_offset] & mask) != (payload[var.byte_offset] & mask)) changed = true;
        } else {
            if (std::memcmp(&rx_buffer_[var.byte_offset], &payload[var.byte_offset], var.size) != 0) changed = true;
        }

        if (changed || !first_rx_done_) {
            if (var.type == "BOOL") {
                if (payload[var.byte_offset] & (1 << var.bit_offset)) {
                    rx_buffer_[var.byte_offset] |= (1 << var.bit_offset);
                    tx_buffer_[var.byte_offset] |= (1 << var.bit_offset);
                } else {
                    rx_buffer_[var.byte_offset] &= ~(1 << var.bit_offset);
                    tx_buffer_[var.byte_offset] &= ~(1 << var.bit_offset);
                }
            } else {
                std::memcpy(&rx_buffer_[var.byte_offset], &payload[var.byte_offset], var.size);
                std::memcpy(&tx_buffer_[var.byte_offset], &payload[var.byte_offset], var.size);
            }

            if (var.type == "BOOL") {
                bool val = (payload[var.byte_offset] >> var.bit_offset) & 0x01;
#ifdef USE_BINARY_SENSOR
                if (var.binary_sensor_ptr) var.binary_sensor_ptr->publish_state(val);
#endif
#ifdef USE_SWITCH
                if (var.switch_ptr) var.switch_ptr->publish_state(val);
#endif
            } else if (var.type == "REAL") {
                uint32_t val_u = read_bytes(&payload[var.byte_offset], 4, big_endian_);
                float f; std::memcpy(&f, &val_u, 4);
#ifdef USE_SENSOR
                if (var.sensor_ptr) var.sensor_ptr->publish_state(f);
#endif
#ifdef USE_NUMBER
                if (var.number_ptr) var.number_ptr->publish_state(f);
#endif
            } else if (var.type == "LREAL") {
                uint64_t val_u = read_bytes(&payload[var.byte_offset], 8, big_endian_);
                double d; std::memcpy(&d, &val_u, 8);
                float f = static_cast<float>(d);
#ifdef USE_SENSOR
                if (var.sensor_ptr) var.sensor_ptr->publish_state(f);
#endif
#ifdef USE_NUMBER
                if (var.number_ptr) var.number_ptr->publish_state(f);
#endif
            } else if (var.type.rfind("STRING", 0) != 0) {
                int64_t val_i = static_cast<int64_t>(read_bytes(&payload[var.byte_offset], var.size, big_endian_));
                if (var.type == "SINT") val_i = static_cast<int8_t>(val_i);
                else if (var.type == "INT") val_i = static_cast<int16_t>(val_i);
                else if (var.type == "DINT") val_i = static_cast<int32_t>(val_i);
                float f = static_cast<float>(val_i);
#ifdef USE_SENSOR
                if (var.sensor_ptr) var.sensor_ptr->publish_state(f);
#endif
#ifdef USE_NUMBER
                if (var.number_ptr) var.number_ptr->publish_state(f);
#endif
            }
        }
    }
    first_rx_done_ = true;
}

void WagoNetVarComponent::send_packet_() {
    if (ip_address_.empty() || payload_size_ == 0) return;

    IPAddress target_ip;
    if (!target_ip.fromString(ip_address_.c_str())) return;

    uint8_t header[20] = {0};
    header[0] = 0x00; header[1] = 0x2D; header[2] = 0x53; header[3] = 0x33;
    uint16_t total_len = 20 + payload_size_;

    auto write_u16 = [this](uint8_t* ptr, uint16_t val) {
        if (big_endian_) { ptr[0] = (val >> 8) & 0xFF; ptr[1] = val & 0xFF; }
        else { ptr[0] = val & 0xFF; ptr[1] = (val >> 8) & 0xFF; }
    };

    write_u16(&header[8], list_id_);
    write_u16(&header[12], var_count_);
    write_u16(&header[14], total_len);
    write_u16(&header[16], sequence_counter_++);

    if (udp_.beginPacket(target_ip, port_)) {
        udp_.write(header, 20);
        udp_.write(tx_buffer_.data(), payload_size_);
        udp_.endPacket();

        // Logowanie w formacie TX IP:PORT (ID: X, var: Y, size: Z B)
        ESP_LOGI(TAG, "TX %s:%d (ID: %d, var: %d, size: %d B)", 
                 ip_address_.c_str(), port_, list_id_, var_count_, total_len);

        // Podgląd HEX w logu DEBUG
        std::vector<uint8_t> full_packet(header, header + 20);
        full_packet.insert(full_packet.end(), tx_buffer_.begin(), tx_buffer_.end());

        ESP_LOGD(TAG, "TX HEX: %s", 
                 format_hex(full_packet.data(), full_packet.size()).c_str());
    } else {
        ESP_LOGE(TAG, "Błąd podczas rozpoczynania wysyłania pakietu UDP do IP: %s", ip_address_.c_str());
    }

    last_sent_time_ = millis();
    is_dirty_ = false;
}

} // namespace wago_netvar
} // namespace esphome
