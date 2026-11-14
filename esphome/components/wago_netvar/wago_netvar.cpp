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
#include <utility>

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

// Sumy liczników wszystkich list (wszystkie instancje komponentu w jednym urządzeniu).
struct Totals {
  uint32_t rejected_packets{0};
  uint32_t rx_packets{0};
  uint32_t tx_packets{0};
  uint32_t rx_errors{0};
  uint32_t tx_errors{0};
};
static Totals g_totals;

// ---------------------------------------------------------------------------------------------
// Transport UDP
// ---------------------------------------------------------------------------------------------
namespace {

#if defined(USE_ARDUINO)

class ArduinoUdpTransport : public UdpTransport {
 public:
  bool begin(uint16_t port) override {
    if (port == 0)
      return true;  // tylko wysyłanie: WiFiUDP::beginPacket() samo otwiera gniazdo
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

    // Zbyt duży datagram: odrzucamy go jawnie przez flush(). Na ESP32 parsePacket() zwraca 0, dopóki
    // poprzedni datagram nie zostanie w pełni odczytany, więc bez flush() odbiór zostałby zablokowany.
    if (static_cast<size_t>(size) > capacity) {
      this->udp_.flush();
      return size;
    }
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
    const int result = this->udp_.endPacket();  // pakiet zawsze kończymy
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

    // Pozwala wysyłać na adresy rozgłoszeniowe (np. 255.255.255.255).
    const int enable = 1;
    ::setsockopt(this->fd_, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));

    // port == 0 -> port lokalny przydzielony przez system (tylko wysyłanie), w innym razie nasłuch na porcie z konfiguracji.
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
      return -1;

    struct sockaddr_in sender {};
    socklen_t sender_len = sizeof(sender);
    // Datagram jest usuwany z kolejki gniazda (bez MSG_PEEK). Datagram dłuższy niż `capacity` zostaje
    // obcięty; wywołujący przekazuje capacity = max ramka + 1, więc obcięcie jest wykrywane jako "zbyt duży".
    const ssize_t size = ::recvfrom(this->fd_, buffer, capacity, MSG_DONTWAIT,
                                    reinterpret_cast<struct sockaddr *>(&sender), &sender_len);
    if (size < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
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
// Funkcje pomocnicze (bez stanu)
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
    size = static_cast<size_t>(length) + 1;  // + kończący bajt 0x00
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

// Zapis liczby całkowitej bez pośredniej konwersji na double (bez utraty precyzji).
std::string format_integer(int64_t value) {
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
  return buffer;
}

// Odczyt tekstu STRING: do pierwszego bajtu 0x00 (lub max_size). NetVar STRING to wyłącznie ASCII,
// więc bajty spoza ASCII (>127) są zamieniane na '?', aby publikowany tekst zawsze był poprawny.
std::string read_ascii_string(const uint8_t *data, size_t max_size) {
  std::string text;
  for (size_t i = 0; i < max_size && data[i] != '\0'; ++i)
    text.push_back(data[i] < 128 ? static_cast<char>(data[i]) : '?');
  return text;
}

// Liczba zapisywana do zmiennej STRING: wartości całkowite bez części ułamkowej i wykładnika
// (np. "1000000", a nie "1e+06"), pozostałe z dokładnością do 7 cyfr znaczących.
std::string format_decimal(double value) {
  char buffer[32];
  if (value == 0.0)
    return "0";
  if (value == std::floor(value) && std::fabs(value) < 1e15)
    std::snprintf(buffer, sizeof(buffer), "%.0f", value);
  else
    std::snprintf(buffer, sizeof(buffer), "%.7g", value);
  return buffer;
}

// Czy minął odstęp min_interval od ostatniej wysyłki (pierwsza wysyłka nie jest ograniczona).
inline bool interval_elapsed(bool sent_once, uint32_t last_sent, uint32_t min_interval) {
  return !sent_once || millis() - last_sent >= min_interval;
}

inline bool is_temporal_type(VariableType type) {
  return type == VariableType::TIME || type == VariableType::DATE || type == VariableType::TIME_OF_DAY ||
         type == VariableType::DATE_AND_TIME;
}

// Data cywilna z liczby dni od 1970-01-01 (algorytm Howarda Hinnanta).
void civil_from_days(int64_t days, int &year, unsigned &month, unsigned &day) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned day_of_era = static_cast<unsigned>(days - era * 146097);
  const unsigned year_of_era =
      (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
  const unsigned day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  const unsigned mp = (5 * day_of_year + 2) / 153;
  day = day_of_year - (153 * mp + 2) / 5 + 1;
  month = mp < 10 ? mp + 3 : mp - 9;
  year = static_cast<int>(year_of_era) + static_cast<int>(era) * 400 + (month <= 2 ? 1 : 0);
}

// Tekst IEC dla typów czasowych (identyczny jak w integracji Home Assistant):
// TIME: T#1d2h3m4s500ms (części zerowe pomijane, zero to T#0ms), DATE: D#2026-11-08,
// DATE_AND_TIME: DT#2026-11-08-12:34:56, TIME_OF_DAY: TOD#12:34:56 albo TOD#12:34:56.789.
// DATE i DATE_AND_TIME to sekundy od 1970-01-01 bez przeliczania strefy czasowej.
std::string format_temporal(VariableType type, uint32_t raw) {
  char buffer[48];
  if (type == VariableType::TIME) {
    const uint32_t days = raw / 86400000u;
    uint32_t rest = raw % 86400000u;
    const uint32_t hours = rest / 3600000u;
    rest %= 3600000u;
    const uint32_t minutes = rest / 60000u;
    rest %= 60000u;
    const uint32_t seconds = rest / 1000u;
    const uint32_t millis_part = rest % 1000u;
    std::string text = "T#";
    const struct {
      uint32_t value;
      const char *unit;
    } parts[] = {{days, "d"}, {hours, "h"}, {minutes, "m"}, {seconds, "s"}, {millis_part, "ms"}};
    for (const auto &part : parts) {
      if (part.value != 0)
        text += std::to_string(part.value) + part.unit;
    }
    return text.size() == 2 ? "T#0ms" : text;
  }
  if (type == VariableType::TIME_OF_DAY) {
    const uint32_t hours = raw / 3600000u;
    const uint32_t minutes = (raw % 3600000u) / 60000u;
    const uint32_t seconds = (raw % 60000u) / 1000u;
    const uint32_t millis_part = raw % 1000u;
    if (millis_part != 0)
      std::snprintf(buffer, sizeof(buffer), "TOD#%02u:%02u:%02u.%03u", static_cast<unsigned>(hours),
                    static_cast<unsigned>(minutes), static_cast<unsigned>(seconds), static_cast<unsigned>(millis_part));
    else
      std::snprintf(buffer, sizeof(buffer), "TOD#%02u:%02u:%02u", static_cast<unsigned>(hours),
                    static_cast<unsigned>(minutes), static_cast<unsigned>(seconds));
    return buffer;
  }
  int year = 1970;
  unsigned month = 1, day = 1;
  civil_from_days(raw / 86400u, year, month, day);
  if (type == VariableType::DATE) {
    std::snprintf(buffer, sizeof(buffer), "D#%04d-%02u-%02u", year, month, day);
  } else {
    const uint32_t seconds_of_day = raw % 86400u;
    std::snprintf(buffer, sizeof(buffer), "DT#%04d-%02u-%02u-%02u:%02u:%02u", year, month, day,
                  static_cast<unsigned>(seconds_of_day / 3600u), static_cast<unsigned>((seconds_of_day % 3600u) / 60u),
                  static_cast<unsigned>(seconds_of_day % 60u));
  }
  return buffer;
}

// Przeniesienie encji w stan "unknown" (timeout odbioru). Nie każda wersja ESPHome ma invalidate_state()
// dla wszystkich rodzajów encji, dlatego wywołanie jest wybierane w czasie kompilacji (SFINAE).
template<typename T> auto invalidate_entity(T *entity, int) -> decltype(entity->invalidate_state(), void()) {
  if (entity != nullptr)
    entity->invalidate_state();
}
template<typename T> void invalidate_entity(T *, long) {}

// Jak wyżej: has_state() jest używane tylko tam, gdzie encja je udostępnia.
template<typename T> auto source_has_state(T *entity, int) -> decltype(entity->has_state(), bool()) {
  return entity->has_state();
}
template<typename T> bool source_has_state(T *, long) {
  return false;
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
// Encje
// ---------------------------------------------------------------------------------------------

void WagoSwitch::write_state(bool state) {
  if (this->parent_ == nullptr)
    return;
  if (this->parent_->is_read_list()) {  // lista read: wartość pochodzi z PLC
    ESP_LOGW(TAG, "Switch '%s' belongs to a read list: command ignored", this->var_name_.c_str());
    this->parent_->republish_variable(this->var_name_);
    return;
  }
  this->parent_->set_variable_value(this->var_name_, state);
  this->parent_->save_restore_value(this->var_name_, state ? 1.0f : 0.0f);
  this->publish_state(state);
}

void WagoNumber::control(float value) {
  if (this->parent_ == nullptr)
    return;
  if (this->parent_->is_read_list()) {  // lista read: wartość pochodzi z PLC
    ESP_LOGW(TAG, "Number '%s' belongs to a read list: command ignored", this->var_name_.c_str());
    this->parent_->republish_variable(this->var_name_);
    return;
  }
  this->parent_->set_variable_value(this->var_name_, value);
  this->parent_->save_restore_value(this->var_name_, value);
  this->publish_state(value);
}

// ---------------------------------------------------------------------------------------------
// Współdzielone gniazdo odbiorcze portu
// ---------------------------------------------------------------------------------------------

/// Jedno gniazdo UDP na numer portu, współdzielone przez wszystkie listy "read" nasłuchujące na tym porcie
/// (np. listy 100 i 101 na porcie 1202). Każdy datagram jest odbierany raz i przekazywany do list o
/// pasującym list_ID; o przyjęciu decyduje pełna walidacja listy (IP nadawcy, var_count, frame_size, ...).
class PortReceiver {
 public:
  static PortReceiver *get(uint16_t port) {
    static std::vector<std::unique_ptr<PortReceiver>> registry;
    for (auto &receiver : registry) {
      if (receiver->port_ == port)
        return receiver.get();
    }
    registry.emplace_back(new PortReceiver(port));
    return registry.back().get();
  }

  void add_listener(WagoNetVarComponent *listener) { this->listeners_.push_back(listener); }

  /// Wywoływane z loop() każdej listy; kolejne wywołania w tej samej iteracji zastają pustą kolejkę.
  void poll() {
    if (!this->ensure_started_())
      return;

    for (size_t count = 0; count < MAX_PACKETS_PER_LOOP; ++count) {
      uint32_t remote_ip = 0;
      uint16_t remote_port = 0;
      const int size = this->udp_->receive(this->buffer_.data(), this->buffer_.size(), remote_ip, remote_port);
      if (size == 0)
        break;
      if (size < 0) {
        // Błąd gniazda: zliczamy go na wszystkich listach portu i otwieramy gniazdo ponownie (po odstępie).
        for (auto *listener : this->listeners_)
        {
          ++listener->rx_errors_;
          ++g_totals.rx_errors;
        }
        ESP_LOGW(TAG, "UDP receive error on port %u, socket will be reopened", static_cast<unsigned>(this->port_));
        this->started_ = false;
        this->attempted_ = true;
        this->last_attempt_ = millis();
        break;
      }
      this->dispatch_(static_cast<size_t>(size), remote_ip);
    }
  }

 protected:
  explicit PortReceiver(uint16_t port) : port_(port), buffer_(NETVAR_MAX_PACKET_SIZE + 1, 0), udp_(create_transport()) {}

  bool ensure_started_() {
    if (this->started_)
      return true;
    if (!network::is_connected())
      return false;

    const uint32_t now = millis();
    if (this->attempted_ && now - this->last_attempt_ < UDP_RETRY_INTERVAL_MS)
      return false;
    this->attempted_ = true;
    this->last_attempt_ = now;

    if (!this->udp_->begin(this->port_)) {
      ESP_LOGW(TAG, "Unable to open UDP port %u, retrying", static_cast<unsigned>(this->port_));
      return false;
    }
    this->started_ = true;
    ESP_LOGI(TAG, "Listening on UDP port %u (%u read list(s))", static_cast<unsigned>(this->port_),
             static_cast<unsigned>(this->listeners_.size()));
    return true;
  }

  // Zasady zliczania odrzuconych ramek (rejected_packets):
  //  - ramka niemożliwa do przypisania do listy (za duża, za krótka, zły magic) lub o list_ID, którego żadna
  //    lista tego portu nie obsługuje -> zliczana jako odrzucona na wszystkich listach portu,
  //  - ramka o list_ID obsługiwanym przez listy tego portu -> jeśli żadna z nich jej nie przyjmie (np. zły IP
  //    nadawcy, var_count, frame_size), jest zliczana jako odrzucona tylko na tych listach.
  void dispatch_(size_t size, uint32_t remote_ip) {
    const uint8_t *frame = this->buffer_.data();

    const bool identifiable = size >= NETVAR_HEADER_SIZE && size <= NETVAR_MAX_PACKET_SIZE &&
                              std::memcmp(frame, MAGIC_BYTES, sizeof(MAGIC_BYTES)) == 0;
    const uint16_t list_id = identifiable ? read_u16(&frame[8]) : 0;

    bool has_candidate = false;
    bool accepted = false;
    if (identifiable) {
      for (auto *listener : this->listeners_) {
        if (listener->list_id_ != list_id)
          continue;
        has_candidate = true;
        if (listener->accept_frame_(frame, size, remote_ip))
          accepted = true;  // nie przerywamy: listy o tym samym list_ID mogą różnić się adresem nadawcy
      }
    }
    if (accepted)
      return;

    for (auto *listener : this->listeners_) {
      if (!has_candidate || listener->list_id_ == list_id)
      {
        ++listener->rejected_packets_;
        ++g_totals.rejected_packets;
      }
    }
  }

  uint16_t port_;
  std::vector<uint8_t> buffer_;  // +1 bajt względem maksymalnej ramki pozwala wykryć zbyt duże datagramy
  std::unique_ptr<UdpTransport> udp_;
  std::vector<WagoNetVarComponent *> listeners_;
  bool started_{false};
  bool attempted_{false};
  uint32_t last_attempt_{0};
};

// ---------------------------------------------------------------------------------------------
// Konfiguracja
// ---------------------------------------------------------------------------------------------

// mark_failed() wywołane przed setup() jest tracone (stan komponentu jest zerowany na starcie setup()),
// dlatego błędy konfiguracji są zapamiętywane i zgłaszane z setup().
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

void WagoNetVarComponent::add_variable(const std::string &name, const std::string &type, uint16_t byte_offset,
                                       uint16_t index, bool require_value) {
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
  var.index = index;
  var.require_value = require_value;
  if (!parse_type(type, var.type, var.size)) {
    this->fail_config_("unsupported variable type", name + ": " + type);
    return;
  }
  this->variables_.push_back(std::move(var));
}

void WagoNetVarComponent::register_sensor(const std::string &name, sensor::Sensor *entity, bool zero_as_unavailable) {
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
  var->zero_as_unavailable = zero_as_unavailable;
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
  // STRING albo typ czasowy (tekst IEC)
  if (var == nullptr || entity == nullptr || (var->type != VariableType::STRING && !is_temporal_type(var->type))) {
    this->fail_config_("invalid text sensor mapping", name);
    return;
  }
  if (var->text_sensor_ptr != nullptr) {
    this->fail_config_("duplicate text sensor mapping", name);
    return;
  }
  var->text_sensor_ptr = entity;
}

void WagoNetVarComponent::register_switch(const std::string &name, switch_::Switch *entity, bool restore_value,
                                          bool has_initial, bool initial_value) {
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
  var->restore_value = restore_value;
  var->has_initial = has_initial;
  var->initial_value = initial_value ? 1.0 : 0.0;
}

// Number może sterować dowolną zmienną liczbową, a także STRING (liczba jest zapisywana jako tekst).
void WagoNetVarComponent::register_number(const std::string &name, number::Number *entity, bool restore_value,
                                          bool has_initial, float initial_value) {
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
  var->restore_value = restore_value;
  var->has_initial = has_initial;
  var->initial_value = initial_value;
}

// Offsety są walidowane przez kod Pythona; tutaj tylko je sprawdzamy (zabezpieczenie dodatkowe)
// i wyliczamy z nich rozmiar ładunku.
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
    if (!this->pack_variables_) {
      for (size_t j = 0; j < i; ++j) {
        if (this->variables_[j].index == var.index) {
          ESP_LOGE(TAG, "Variables '%s' and '%s' have the same index %u", var.name.c_str(),
                   this->variables_[j].name.c_str(), static_cast<unsigned>(var.index));
          return false;
        }
      }
    }
  }
  // Lista pakowana: jawny payload_size oznacza dokładny rozmiar ładunku (odbiór nie akceptuje dłuższych ramek).
  if (this->pack_variables_ && this->configured_payload_size_ > 0) {
    if (this->configured_payload_size_ < payload_size || this->configured_payload_size_ > NETVAR_MAX_PAYLOAD_SIZE) {
      ESP_LOGE(TAG, "payload_size (%u) is lower than the size resulting from variable offsets (%u) or too large",
               static_cast<unsigned>(this->configured_payload_size_), static_cast<unsigned>(payload_size));
      return false;
    }
    payload_size = this->configured_payload_size_;
    this->exact_payload_size_ = true;
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

  // var_count to liczba zmiennych na liście nadawcy (lista pakowana). Może być większa niż liczba zmiennych
  // zdefiniowanych tutaj (zmienne pomijane), ale nigdy mniejsza. W liście niepakowanej każda ramka ma var_count = 1.
  if (this->var_count_ == 0)
    this->var_count_ = static_cast<uint16_t>(this->variables_.size());
  if (this->pack_variables_ && this->var_count_ < this->variables_.size()) {
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
  std::fill(std::begin(this->published_stats_), std::end(this->published_stats_), STATS_UNPUBLISHED);
  std::fill(std::begin(this->published_totals_), std::end(this->published_totals_), STATS_UNPUBLISHED);

  if (this->enable_read_) {
    // Wszystkie listy "read" o tym samym porcie współdzielą jedno gniazdo (PortReceiver), a ramki są
    // rozdzielane między nie według list_ID. Gniazdo jest otwierane w loop(), gdy sieć jest dostępna.
    this->receiver_ = PortReceiver::get(this->port_);
    this->receiver_->add_listener(this);
  } else {
    size_t frame_payload = this->payload_size_;
    if (!this->pack_variables_) {
      frame_payload = 0;  // każda ramka niesie jedną zmienną
      for (const auto &var : this->variables_)
        frame_payload = std::max(frame_payload, var.size);
    }
    this->frame_.assign(NETVAR_HEADER_SIZE + frame_payload, 0);
    this->udp_ = create_transport();
    this->apply_startup_values_();  // initial_value i restore_value; wysyłka po otwarciu gniazda w loop()
    this->ready_ = true;            // od teraz callbacki opcji source mogą zapisywać do bufora
    this->apply_source_states_();   // stan źródeł, które opublikowały wartość przed setup()
    // Gniazdo wysyłające jest otwierane w loop() (patrz ensure_udp_()).
  }
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

  // Dotyczy tylko list "write": używają portu lokalnego przydzielonego przez system (port 0), więc nie
  // kolidują z listami "read" na tym samym numerze portu. Port z konfiguracji jest tylko portem docelowym.
  if (!this->udp_->begin(0)) {
    ESP_LOGW(TAG, "Unable to initialize UDP sender, retrying");
    return false;
  }

  this->udp_started_ = true;
  ESP_LOGI(TAG, "NetVar list %u started (write, target port %u, payload %u bytes)",
           static_cast<unsigned>(this->list_id_), static_cast<unsigned>(this->port_),
           static_cast<unsigned>(this->payload_size_));
  return true;
}

void WagoNetVarComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Wago NetVar:");
  ESP_LOGCONFIG(TAG, "  Direction: %s", this->enable_read_ ? "read" : "write");
  ESP_LOGCONFIG(TAG, "  IP address: %s", this->ip_address_.c_str());
  ESP_LOGCONFIG(TAG, "  Port: %u%s", static_cast<unsigned>(this->port_), this->enable_read_ ? " (shared by read lists)" : "");
  ESP_LOGCONFIG(TAG, "  List ID: %u", static_cast<unsigned>(this->list_id_));
  ESP_LOGCONFIG(TAG, "  Pack variables: %s", this->pack_variables_ ? "yes" : "no (one frame per variable)");
  if (this->pack_variables_) {
    ESP_LOGCONFIG(TAG, "  Variable count: %u", static_cast<unsigned>(this->var_count_));
    ESP_LOGCONFIG(TAG, "  Payload size: %u bytes%s", static_cast<unsigned>(this->payload_size_),
                  this->exact_payload_size_ ? " (exact)" : "");
  }
  if (this->enable_read_ && this->timeout_ms_ > 0)
    ESP_LOGCONFIG(TAG, "  Timeout: %u ms", static_cast<unsigned>(this->timeout_ms_));
  if (this->enable_write_ && this->pack_variables_)
    ESP_LOGCONFIG(TAG, "  Require values: %s", this->require_values_ ? "yes" : "no");
  if (this->enable_write_) {
    ESP_LOGCONFIG(TAG, "  On change: %s", this->send_on_change_ ? "yes" : "no");
    if (this->send_on_change_)
      ESP_LOGCONFIG(TAG, "  Min interval: %u ms", static_cast<unsigned>(this->min_interval_ms_));
    LOG_UPDATE_INTERVAL(this);
  }
  for (const auto &var : this->variables_) {
    ESP_LOGCONFIG(TAG, "  Variable '%s': type=%s offset=%u size=%u", var.name.c_str(), var.type_name.c_str(),
                  static_cast<unsigned>(var.byte_offset), static_cast<unsigned>(var.size));
    if (!this->pack_variables_)
      ESP_LOGCONFIG(TAG, "    Index: %u%s", static_cast<unsigned>(var.index), var.require_value ? " (require value)" : "");
    if (var.restore_value || var.has_initial)
      ESP_LOGCONFIG(TAG, "    Restore value: %s, initial value: %s", var.restore_value ? "yes" : "no",
                    var.has_initial ? "set" : "none");
    LOG_SENSOR("    ", "Sensor", var.sensor_ptr);
    LOG_BINARY_SENSOR("    ", "Binary sensor", var.binary_sensor_ptr);
    LOG_TEXT_SENSOR("    ", "Text sensor", var.text_sensor_ptr);
    LOG_SWITCH("    ", "Switch", var.switch_ptr);
    LOG_NUMBER("    ", "Number", var.number_ptr);
  }
  LOG_SENSOR("  ", "Rejected packets", this->rejected_packets_sensor_);
  LOG_SENSOR("  ", "RX packets", this->rx_packets_sensor_);
  LOG_SENSOR("  ", "TX packets", this->tx_packets_sensor_);
  LOG_SENSOR("  ", "RX errors", this->rx_errors_sensor_);
  LOG_SENSOR("  ", "TX errors", this->tx_errors_sensor_);
  LOG_SENSOR("  ", "Total rejected packets", this->total_rejected_packets_sensor_);
  LOG_SENSOR("  ", "Total RX packets", this->total_rx_packets_sensor_);
  LOG_SENSOR("  ", "Total TX packets", this->total_tx_packets_sensor_);
  LOG_SENSOR("  ", "Total RX errors", this->total_rx_errors_sensor_);
  LOG_SENSOR("  ", "Total TX errors", this->total_tx_errors_sensor_);
}

// ---------------------------------------------------------------------------------------------
// Główna pętla
// ---------------------------------------------------------------------------------------------

void WagoNetVarComponent::loop() {
  const uint32_t now = millis();
  if (now - this->last_stats_time_ >= STATS_PUBLISH_INTERVAL_MS) {
    this->last_stats_time_ = now;
    this->publish_statistics_();
  }

  if (this->enable_read_) {
    this->receiver_->poll();  // odbiera i rozdziela ramki także do pozostałych list tego portu
    this->check_timeouts_();
    return;
  }

  if (this->ensure_udp_())
    this->process_on_change_();
}

const VarDef *WagoNetVarComponent::find_by_index_(uint16_t index) const {
  for (const auto &var : this->variables_) {
    if (var.index == index)
      return &var;
  }
  return nullptr;
}

bool WagoNetVarComponent::accept_frame_(const uint8_t *frame, size_t packet_size, uint32_t remote_ip) {
  if (!this->validate_packet_(frame, packet_size, remote_ip))
    return false;
  ++this->rx_packets_;
  ++g_totals.rx_packets;

  const uint8_t *payload = frame + NETVAR_HEADER_SIZE;
  if (this->pack_variables_) {
    this->unpack_payload_(payload);
    return true;
  }

  // Lista niepakowana: ramka niesie jedną zmienną. Ramka zmiennej, której nie odczytujemy, jest poprawna,
  // ale pomijana (liczy się do rx_packets).
  auto *var = const_cast<VarDef *>(this->find_by_index_(read_u16(&frame[10])));
  if (var != nullptr) {
    std::memcpy(this->payload_.data() + var->byte_offset, payload, var->size);
    this->mark_received_(*var);
    this->publish_variable_(*var);
  }
  return true;
}

// Port docelowy jest walidowany niejawnie (gniazdo jest związane z portem z konfiguracji).
// Port źródłowy nadawcy nie ma znaczenia. sequence_counter i checksum są tylko odczytywane (log VERBOSE).
bool WagoNetVarComponent::validate_packet_(const uint8_t *header, size_t packet_size, uint32_t remote_ip) const {
  if (packet_size < NETVAR_HEADER_SIZE || packet_size > NETVAR_MAX_PACKET_SIZE)
    return false;

  // 0.0.0.0 = akceptuj dowolnego nadawcę.
  if (this->ip_ != 0 && this->ip_ != remote_ip)
    return false;

  if (std::memcmp(header, MAGIC_BYTES, sizeof(MAGIC_BYTES)) != 0)
    return false;

  // Typ wiadomości (bajty 4-7; w dokumentacji 3S pole "ID"): 0 = dane zmiennych sieciowych. Inne wartości to
  // potwierdzenia, prośby bootup i wymiana parametrów (SDO), które nie niosą wartości zmiennych.
  if (load_le(&header[4], 4) != 0)
    return false;

  if (read_u16(&header[8]) != this->list_id_)
    return false;
  if (read_u16(&header[14]) != packet_size)  // frame_size musi zgadzać się z rozmiarem odebranego datagramu
    return false;

  const size_t payload_length = packet_size - NETVAR_HEADER_SIZE;
  const uint16_t var_index = read_u16(&header[10]);
  const uint16_t var_count = read_u16(&header[12]);

  if (this->pack_variables_) {
    if (var_index != 0 || var_count != this->var_count_)
      return false;
    // Ramka musi obejmować wszystkie zdefiniowane zmienne. Bez jawnego payload_size może być dłuższa:
    // zmienne za ostatnią zdefiniowaną są po prostu pomijane.
    if (this->exact_payload_size_ ? payload_length != this->payload_size_ : payload_length < this->payload_size_)
      return false;
  } else {
    if (var_count != 1)
      return false;
    const VarDef *var = this->find_by_index_(var_index);
    if (var != nullptr && payload_length != var->size)
      return false;
  }

  ESP_LOGV(TAG, "List %u: frame ok, sequence counter %u, flags 0x%02X, checksum 0x%02X",
           static_cast<unsigned>(this->list_id_), static_cast<unsigned>(read_u16(&header[16])),
           static_cast<unsigned>(header[18]), static_cast<unsigned>(header[19]));
  return true;
}

void WagoNetVarComponent::unpack_payload_(const uint8_t *payload) {
  std::memcpy(this->payload_.data(), payload, this->payload_size_);
  for (auto &var : this->variables_) {
    this->mark_received_(var);
    this->publish_variable_(var);
  }
}

void WagoNetVarComponent::mark_received_(VarDef &var) {
  var.received = true;
  var.last_received = millis();
  if (var.expired) {
    var.expired = false;
    ESP_LOGI(TAG, "List %u: variable '%s' receives data again", static_cast<unsigned>(this->list_id_),
             var.name.c_str());
  }
}

// Watchdog odbioru: po timeout bez ramki encje zmiennej przechodzą w stan "unknown" (jednorazowo).
void WagoNetVarComponent::check_timeouts_() {
  if (this->timeout_ms_ == 0)
    return;
  const uint32_t now = millis();
  for (auto &var : this->variables_) {
    if (!var.received || var.expired || now - var.last_received <= this->timeout_ms_)
      continue;
    var.expired = true;
    ESP_LOGW(TAG, "List %u: no data for variable '%s' within %u ms", static_cast<unsigned>(this->list_id_),
             var.name.c_str(), static_cast<unsigned>(this->timeout_ms_));
    this->invalidate_variable_(var);
  }
}

void WagoNetVarComponent::invalidate_variable_(const VarDef &var) {
  if (var.sensor_ptr != nullptr)
    var.sensor_ptr->publish_state(NAN);  // NaN to "unknown" w ESPHome
  if (var.number_ptr != nullptr)
    var.number_ptr->publish_state(NAN);
  invalidate_entity(var.binary_sensor_ptr, 0);
  invalidate_entity(var.switch_ptr, 0);
  invalidate_entity(var.text_sensor_ptr, 0);
}

// Ponowna publikacja ostatniej wartości odebranej z PLC (np. po odrzuconym poleceniu z interfejsu).
void WagoNetVarComponent::republish_variable(const std::string &name) {
  const VarDef *var = this->get_var(name);
  if (var != nullptr && var->received)
    this->publish_variable_(*var);
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
    if (var.switch_ptr != nullptr && this->enable_read_)  // lista read: lustro wartości z PLC
      var.switch_ptr->publish_state(data[0] != 0);
    return;
  }

  if (var.type == VariableType::STRING) {
    if (var.text_sensor_ptr != nullptr)
      var.text_sensor_ptr->publish_state(read_ascii_string(data, var.size));
    return;
  }

  // Typy czasowe: text_sensor pokazuje tekst IEC, sensor surową liczbę.
  if (var.text_sensor_ptr != nullptr && is_temporal_type(var.type))
    var.text_sensor_ptr->publish_state(format_temporal(var.type, load_le(data, var.size)));

  // Lista read: number pokazuje wartość z PLC (NaN/Inf jako NaN, czyli "unknown").
  if (var.number_ptr != nullptr && this->enable_read_) {
    const double number_value = this->decode_number_(var, data);
    var.number_ptr->publish_state(std::isfinite(number_value) ? static_cast<float>(number_value) : NAN);
  }

  if (var.sensor_ptr == nullptr)
    return;
  // REAL nieskończony lub nieliczbowy (NaN/Inf) jest publikowany jako NaN, który ESPHome pokazuje jako "unknown".
  const double value = this->decode_number_(var, data);
  if (var.zero_as_unavailable && value == 0.0) {
    var.sensor_ptr->publish_state(NAN);  // zero oznacza brak wartości (zero_as_unavailable)
    return;
  }
  var.sensor_ptr->publish_state(std::isfinite(value) ? static_cast<float>(value) : NAN);
}

void WagoNetVarComponent::publish_statistics_() {
  publish_counter(this->rejected_packets_sensor_, this->rejected_packets_, this->published_stats_[0]);
  publish_counter(this->rx_packets_sensor_, this->rx_packets_, this->published_stats_[1]);
  publish_counter(this->tx_packets_sensor_, this->tx_packets_, this->published_stats_[2]);
  publish_counter(this->rx_errors_sensor_, this->rx_errors_, this->published_stats_[3]);
  publish_counter(this->tx_errors_sensor_, this->tx_errors_, this->published_stats_[4]);

  // Sumy wszystkich list publikuje tylko lista, która ma sensory total_* (sensory nie istnieją na pozostałych).
  publish_counter(this->total_rejected_packets_sensor_, g_totals.rejected_packets, this->published_totals_[0]);
  publish_counter(this->total_rx_packets_sensor_, g_totals.rx_packets, this->published_totals_[1]);
  publish_counter(this->total_tx_packets_sensor_, g_totals.tx_packets, this->published_totals_[2]);
  publish_counter(this->total_rx_errors_sensor_, g_totals.rx_errors, this->published_totals_[3]);
  publish_counter(this->total_tx_errors_sensor_, g_totals.tx_errors, this->published_totals_[4]);
}

// ---------------------------------------------------------------------------------------------
// Wysyłka
// ---------------------------------------------------------------------------------------------

void WagoNetVarComponent::update() {
  // Wysyłka cykliczna, działa równolegle z send_on_change. Nie jest wywoływana, gdy update_interval = never.
  if (this->enable_write_)
    this->send_packet_();
}

// Wysyłka przy zmianie (on_change), wywoływana w każdej iteracji pętli.
// - lista pakowana: gdy zmieniła się dowolna zmienna, wysyłana jest cała lista (jedna ramka),
// - lista niepakowana: wysyłane są tylko zmienne, które zmieniły wartość (każda własną ramką).
// min_interval to minimalny odstęp między kolejnymi wysyłkami tej samej ramki (listy albo zmiennej). Zmiana, która
// nastąpiła przed upływem odstępu, jest wysyłana dopiero po jego upływie (zmiany w tym czasie są scalane).
void WagoNetVarComponent::process_on_change_() {
  if (!this->enable_write_ || !this->send_on_change_)
    return;
  if (this->pack_variables_) {
    if (this->is_dirty_ && interval_elapsed(this->sent_once_, this->last_sent_time_, this->min_interval_ms_))
      this->send_list_();
    return;
  }
  for (auto &var : this->variables_) {
    if (var.dirty && interval_elapsed(var.sent_once, var.last_sent, this->min_interval_ms_))
      this->send_variable_(var);
  }
}

bool WagoNetVarComponent::all_values_set_() const {
  for (const auto &var : this->variables_) {
    if (!var.has_value)
      return false;
  }
  return true;
}

void WagoNetVarComponent::send_now() {
  if (this->enable_write_)
    this->send_packet_();
}

// Wysyła jedną ramkę: lista pakowana (var_index 0, var_count z konfiguracji, cały ładunek) albo jedna zmienna
// listy niepakowanej (var_index zmiennej, var_count 1). Pola flag i checksum pozostają 0.
bool WagoNetVarComponent::send_frame_(uint16_t var_index, uint16_t var_count, const uint8_t *data, size_t size,
                                      uint16_t &sequence) {
  const size_t total_length = NETVAR_HEADER_SIZE + size;
  uint8_t *frame = this->frame_.data();

  std::memset(frame, 0, NETVAR_HEADER_SIZE);
  std::memcpy(frame, MAGIC_BYTES, sizeof(MAGIC_BYTES));
  write_u16(&frame[8], this->list_id_);
  write_u16(&frame[10], var_index);
  write_u16(&frame[12], var_count);
  write_u16(&frame[14], static_cast<uint16_t>(total_length));
  write_u16(&frame[16], sequence);
  std::memcpy(frame + NETVAR_HEADER_SIZE, data, size);

  if (!this->udp_->send(this->ip_, this->port_, frame, total_length)) {
    ++this->tx_errors_;
    ++g_totals.tx_errors;
    return false;
  }
  ++this->tx_packets_;
  ++g_totals.tx_packets;
  ++sequence;
  return true;
}

// Cała lista pakowana w jednej ramce (var_index 0, var_count z konfiguracji). sequence_counter jest wspólny dla listy.
void WagoNetVarComponent::send_list_() {
  // require_values: lista nie jest wysyłana, dopóki każda zmienna nie ma wartości.
  if (this->require_values_ && !this->all_values_set_()) {
    if (!this->gate_warned_) {
      ESP_LOGW(TAG, "List %u: not sent, some variables have no value yet", static_cast<unsigned>(this->list_id_));
      this->gate_warned_ = true;
    }
    return;
  }
  this->gate_warned_ = false;
  const bool ok = this->send_frame_(0, this->var_count_, this->payload_.data(), this->payload_size_,
                                    this->sequence_counter_);
  // Czas jest zapisywany także przy błędzie, aby nieudana wysyłka była ponawiana najwyżej raz na min_interval.
  this->last_sent_time_ = millis();
  this->sent_once_ = true;
  if (ok)
    this->is_dirty_ = false;
}

// Jedna zmienna listy niepakowanej w osobnej ramce (var_index zmiennej, var_count 1), z własnym sequence_counter.
// Zmienna z require_value bez wartości jest pomijana.
void WagoNetVarComponent::send_variable_(VarDef &var) {
  if (var.require_value && !var.has_value)
    return;
  const bool ok = this->send_frame_(var.index, 1, this->payload_.data() + var.byte_offset, var.size, var.sequence);
  var.last_sent = millis();
  var.sent_once = true;
  if (ok)
    var.dirty = false;
}

// Wysyłka wszystkiego: cykliczna (update_interval) i na żądanie (send_now). Lista niepakowana: wszystkie zmienne.
void WagoNetVarComponent::send_packet_() {
  if (!this->enable_write_ || !this->udp_started_ || this->payload_size_ == 0)
    return;
  if (this->pack_variables_) {
    this->send_list_();
    return;
  }
  for (auto &var : this->variables_)
    this->send_variable_(var);
}

// ---------------------------------------------------------------------------------------------
// Zapis wartości
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

void WagoNetVarComponent::commit_(VarDef &var, const uint8_t *bytes) {
  // Pierwsze ustawienie zmiennej (nawet na wartość równą zeru w buforze) oznacza, że zmienna ma wartość;
  // przy on_change wymusza to wysyłkę, bo mogła na nią czekać blokada require_value(s).
  const bool first_value = !var.has_value;
  var.has_value = true;

  uint8_t *destination = this->payload_.data() + var.byte_offset;
  const bool changed = std::memcmp(destination, bytes, var.size) != 0;
  if (changed)
    std::memcpy(destination, bytes, var.size);
  if (!changed && !first_value)
    return;  // wartość bez zmiany: nic do wysłania

  // Tylko oznaczamy zmianę; faktyczna wysyłka następuje w loop(). Dzięki temu kilka zmian wykonanych
  // w jednej lambdzie trafia do jednej ramki (lista pakowana), a nie do ramki częściowej + opóźnionej o min_interval.
  if (this->send_on_change_) {
    if (this->pack_variables_)
      this->is_dirty_ = true;
    else
      var.dirty = true;  // lista niepakowana: wysłana zostanie tylko ta zmienna
  }

  // Encje sensor / binary_sensor / text_sensor listy write pokazują aktualnie zapisaną (wysyłaną) wartość.
  if (var.sensor_ptr != nullptr || var.binary_sensor_ptr != nullptr || var.text_sensor_ptr != nullptr)
    this->publish_variable_(var);
}

void WagoNetVarComponent::write_int64_(VarDef &var, int64_t value) {
  uint8_t bytes[4] = {};
  switch (var.type) {
    case VariableType::BOOL:
      bytes[0] = value != 0 ? 1 : 0;
      break;
    case VariableType::STRING:
      this->write_text_(var, format_integer(value));
      return;
    case VariableType::REAL: {
      const float real = static_cast<float>(value);
      std::memcpy(bytes, &real, sizeof(real));
      break;
    }
    default:
      // Przepełnienie: zachowywane są najmniej znaczące bajty, najbardziej znaczące są odrzucane.
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
      this->write_text_(var, format_decimal(value));
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
      // Zaokrąglenie do najbliższej liczby całkowitej; ograniczenie zakresu tylko po to, by konwersja
      // double -> int64 była zdefiniowana.
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

  uint8_t bytes[256] = {};  // STRING(n) zajmuje najwyżej 255 + 1 bajtów
  std::memcpy(bytes, text.data(), std::min(text.size(), var.size - 1));
  this->commit_(var, bytes);
}

void WagoNetVarComponent::set_variable_value(const std::string &name, bool value) {
  if (VarDef *var = this->prepare_write_(name))
    this->write_int64_(*var, value ? 1 : 0);
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

  // Typy liczbowe są parsowane bezpośrednio (bez pośredniej konwersji na float), a cały tekst musi zostać zużyty.
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
    number = static_cast<int64_t>(std::strtoull(begin, &end, 10));  // wzorzec bitowy, obcinany niżej

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
    case VariableType::STRING:
      return read_ascii_string(data, var->size);
    default:
      return format_number(this->decode_number_(*var, data), var->type == VariableType::REAL);
  }
}

// ---------------------------------------------------------------------------------------------
// Wartości początkowe i przywracanie
// ---------------------------------------------------------------------------------------------

void WagoNetVarComponent::apply_value_(VarDef &var, double value) {
  if (var.switch_ptr != nullptr) {
    this->write_int64_(var, value != 0.0 ? 1 : 0);
    var.switch_ptr->publish_state(value != 0.0);
  } else if (var.number_ptr != nullptr) {
    this->write_double_(var, value);
    var.number_ptr->publish_state(static_cast<float>(value));
  } else if (var.binary_sensor_ptr != nullptr) {
    this->write_int64_(var, value != 0.0 ? 1 : 0);  // stan encji publikuje commit_()
  } else if (var.sensor_ptr != nullptr || var.text_sensor_ptr != nullptr) {
    this->write_double_(var, value);
  }
}

void WagoNetVarComponent::set_initial_value(const std::string &name, double value) {
  VarDef *var = this->get_var(name);
  if (var == nullptr) {
    this->fail_config_("unknown variable (initial value)", name);
    return;
  }
  var->has_initial = true;
  var->initial_value = value;
}

// Opcja source: stan innej encji jest zapisywany do zmiennej (i wysyłany zgodnie z on_change / update_interval).
// Callback może zadziałać przed zakończeniem setup(), dlatego do tego czasu jest ignorowany, a aktualny stan
// źródła jest odczytywany w apply_source_states_().
void WagoNetVarComponent::set_source_sensor(const std::string &name, sensor::Sensor *source) {
  VarDef *var = this->get_var(name);
  if (var == nullptr || source == nullptr || !is_numeric_type(var->type)) {
    this->fail_config_("invalid source sensor", name);
    return;
  }
  var->source_sensor = source;
  source->add_on_state_callback([this, name](float value) {
    if (this->ready_ && std::isfinite(value))
      this->set_variable_value(name, static_cast<double>(value));
  });
}

void WagoNetVarComponent::set_source_binary_sensor(const std::string &name, binary_sensor::BinarySensor *source) {
  VarDef *var = this->get_var(name);
  if (var == nullptr || source == nullptr || var->type != VariableType::BOOL) {
    this->fail_config_("invalid source binary sensor", name);
    return;
  }
  var->source_binary = source;
  source->add_on_state_callback([this, name](bool state) {
    if (this->ready_)
      this->set_variable_value(name, state);
  });
}

void WagoNetVarComponent::set_source_number(const std::string &name, number::Number *source) {
  VarDef *var = this->get_var(name);
  if (var == nullptr || source == nullptr || !is_numeric_type(var->type)) {
    this->fail_config_("invalid source number", name);
    return;
  }
  var->source_number = source;
  source->add_on_state_callback([this, name](float value) {
    if (this->ready_ && std::isfinite(value))
      this->set_variable_value(name, static_cast<double>(value));
  });
}

void WagoNetVarComponent::set_source_switch(const std::string &name, switch_::Switch *source) {
  VarDef *var = this->get_var(name);
  if (var == nullptr || source == nullptr || var->type != VariableType::BOOL) {
    this->fail_config_("invalid source switch", name);
    return;
  }
  var->source_switch = source;
  source->add_on_state_callback([this, name](bool state) {
    if (this->ready_)
      this->set_variable_value(name, state);
  });
}

void WagoNetVarComponent::set_source_text_sensor(const std::string &name, text_sensor::TextSensor *source) {
  VarDef *var = this->get_var(name);
  if (var == nullptr || source == nullptr || var->type != VariableType::STRING) {
    this->fail_config_("invalid source text sensor", name);
    return;
  }
  var->source_text = source;
  source->add_on_state_callback([this, name](const std::string &text) {
    if (this->ready_)
      this->set_variable_value(name, text);
  });
}

void WagoNetVarComponent::apply_source_states_() {
  for (auto &var : this->variables_) {
    if (var.source_sensor != nullptr && var.source_sensor->has_state() && std::isfinite(var.source_sensor->state))
      this->set_variable_value(var.name, static_cast<double>(var.source_sensor->state));
    if (var.source_binary != nullptr && var.source_binary->has_state())
      this->set_variable_value(var.name, var.source_binary->state);
    // Number i text_sensor: stan sprzed setup() tylko, gdy wersja ESPHome udostępnia has_state()
    if (var.source_number != nullptr && source_has_state(var.source_number, 0) && std::isfinite(var.source_number->state))
      this->set_variable_value(var.name, static_cast<double>(var.source_number->state));
    if (var.source_text != nullptr && source_has_state(var.source_text, 0))
      this->set_variable_value(var.name, var.source_text->state);
    // Switch nie ma has_state(): jego stan trafia do zmiennej przy pierwszej publikacji (callback).
  }
}

// Kolejność: wartość przywrócona z pamięci (restore_value), a gdy jej brak - initial_value. Zmienna bez żadnej
// z nich nie ma wartości (ma znaczenie dla require_value(s)). Zmiana jest oznaczana jako "dirty" przy on_change,
// więc przywrócony stan jest wysyłany do PLC zaraz po otwarciu gniazda.
void WagoNetVarComponent::apply_startup_values_() {
  for (auto &var : this->variables_) {
    if (var.restore_value) {
      const std::string key = "wago_netvar:" + this->ip_address_ + ":" + std::to_string(this->port_) + ":" +
                              std::to_string(this->list_id_) + ":" + var.name;
      var.restore_pref = global_preferences->make_preference<float>(fnv1_hash(key));
      float saved = 0;
      if (var.restore_pref.load(&saved)) {
        this->apply_value_(var, saved);
        continue;
      }
    }
    if (var.has_initial)
      this->apply_value_(var, var.initial_value);
  }
}

void WagoNetVarComponent::save_restore_value(const std::string &name, float value) {
  VarDef *var = this->get_var(name);
  if (var != nullptr && var->restore_value)
    var->restore_pref.save(&value);
}

}  // namespace wago_netvar
}  // namespace esphome
