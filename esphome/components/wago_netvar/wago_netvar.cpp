#include "wago_netvar.h"
#include <cmath>
#include <cstring> // Niezbędne dla std::memcpy

namespace esphome {
namespace wago_netvar {

void WagoNetVarComponent::add_variable(const std::string &name, const std::string &type) {
    size_t size = 1;
    size_t align = 1;
    get_type_info(type, size, align);
    variables_.push_back({name, type, size, align});
    var_values_[name] = "0";
}

void WagoNetVarComponent::set_variable_value(const std::string &name, float value) {
    var_values_[name] = std::to_string(value);
}

void WagoNetVarComponent::set_variable_value(const std::string &name, bool value) {
    var_values_[name] = value ? "1" : "0";
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
            int len = stoi(type.substr(start + 1, end - start - 1));
            size = len + 1;
        } else {
            size = 81;
        }
        align = 1;
    }
}

std::vector<uint8_t> WagoNetVarComponent::pack_value(const VarDef &var, const std::string &val_str) {
    std::vector<uint8_t> bytes;
    if (var.type == "BOOL") {
        bool b = (val_str == "1" || val_str == "true" || val_str == "True");
        bytes.push_back(b ? 1 : 0);
    } else if (var.type == "REAL") {
        float f = std::stof(val_str);
        uint32_t val_u;
        std::memcpy(&val_u, &f, sizeof(float));
        for (int i = 0; i < 4; i++) {
            if (big_endian_) {
                bytes.push_back((val_u >> (24 - i * 8)) & 0xFF);
            } else {
                bytes.push_back((val_u >> (i * 8)) & 0xFF);
            }
        }
    } else {
        int32_t val_i = std::stoi(val_str);
        for (size_t i = 0; i < var.size; i++) {
            if (big_endian_) {
                bytes.push_back((val_i >> ((var.size - 1 - i) * 8)) & 0xFF);
            } else {
                bytes.push_back((val_i >> (i * 8)) & 0xFF);
            }
        }
    }
    return bytes;
}

void WagoNetVarComponent::setup() {
    ESP_LOGI("wago_netvar", "Inicjalizacja komponentu WagoNetVar (COB-ID: %d, Checksum: %d)", cob_id_, checksum_);
    udp_.begin(port_);
}

void WagoNetVarComponent::update() {
    std::vector<uint8_t> payload;
    uint8_t bool_bit_index = 0;
    uint8_t current_bool_byte = 0;

    for (const auto &var : variables_) {
        std::string val_str = var_values_[var.name];

        if (var.type != "BOOL" && bool_bit_index > 0) {
            payload.push_back(current_bool_byte);
            bool_bit_index = 0;
            current_bool_byte = 0;
        }

        if (var.type == "BOOL" && pack_bools_) {
            bool val_bool = (val_str == "1" || val_str == "true");
            if (val_bool) {
                current_bool_byte |= (1 << bool_bit_index);
            }
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
                    for (size_t i = 0; i < (var.align - rem); i++) {
                        payload.push_back(0x00);
                    }
                }
            }

            std::vector<uint8_t> packed = pack_value(var, val_str);
            payload.insert(payload.end(), packed.begin(), packed.end());
        }
    }

    if (bool_bit_index > 0) {
        payload.push_back(current_bool_byte);
    }

    uint8_t header[20] = {0};
    header[0] = 0x00; header[1] = 0x2D; header[2] = 0x53; header[3] = 0x33;
    
    uint16_t total_len = 20 + payload.size();

    auto write_u16 = [this](uint8_t* ptr, uint16_t val) {
        if (big_endian_) {
            ptr[0] = (val >> 8) & 0xFF;
            ptr[1] = val & 0xFF;
        } else {
            ptr[0] = val & 0xFF;
            ptr[1] = (val >> 8) & 0xFF;
        }
    };

    write_u16(&header[8], cob_id_);
    write_u16(&header[12], checksum_);
    write_u16(&header[14], total_len);
    write_u16(&header[16], sequence_counter_);
    sequence_counter_++;

    IPAddress target_ip;
    target_ip.fromString(ip_str_.c_str());

    udp_.beginPacket(target_ip, port_);
    udp_.write(header, 20);
    if (!payload.empty()) {
        udp_.write(payload.data(), payload.size());
    }
    udp_.endPacket();
}

} // namespace wago_netvar
} // namespace esphome
