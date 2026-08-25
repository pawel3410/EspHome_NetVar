#pragma once

#include "esphome.h"
#include <WiFiUdp.h>
#include <vector>
#include <string>

namespace esphome {
namespace wagonetvar {

struct VarDef {
    std::string name;
    std::string type;
    size_t size;
    size_t align;
};

class WagoNetVarComponent : public PollingComponent {
public:
    void set_ip(const std::string &ip) { ip_str_ = ip; }
    void set_port(uint16_t port) { port_ = port; }
    void set_cob_id(uint16_t cob_id) { cob_id_ = cob_id; }
    void set_checksum(uint16_t checksum) { checksum_ = checksum; }
    void set_big_endian(bool big_endian) { big_endian_ = big_endian; }
    void set_pack_bools(bool pack) { pack_bools_ = pack; }
    void set_alignment(bool align) { alignment_ = align; }

    void add_variable(const std::string &name, const std::string &type);

    void setup() override;
    void update() override;

private:
    std::string ip_str_;
    uint16_t port_;
    uint16_t cob_id_;
    uint16_t checksum_;
    bool big_endian_;
    bool pack_bools_;
    bool alignment_;

    WiFiUDP udp_;
    std::vector<VarDef> variables_;
    uint16_t sequence_counter_ = 1;

    void get_type_info(const std::string &type, size_t &size, size_t &align);
};

} // namespace wagonetvar
} // namespace esphome
