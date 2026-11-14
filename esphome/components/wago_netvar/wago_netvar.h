#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace esphome {
namespace wago_netvar {

static constexpr size_t NETVAR_HEADER_SIZE = 20;
static constexpr size_t NETVAR_MAX_PACKET_SIZE = 1472;
static constexpr size_t NETVAR_MAX_PAYLOAD_SIZE = NETVAR_MAX_PACKET_SIZE - NETVAR_HEADER_SIZE;

class WagoNetVarComponent;
class PortReceiver;  // współdzielone gniazdo odbiorcze jednego portu UDP (definicja w pliku .cpp)

/// Niezależny od platformy transport UDP (Arduino / ESP-IDF). Implementacje znajdują się w pliku .cpp,
/// dzięki czemu nagłówki platformowe nie przedostają się do nagłówka komponentu.
/// Adresy IPv4 są przekazywane jako liczby w kolejności hosta: a.b.c.d -> (a << 24) | (b << 16) | (c << 8) | d.
class UdpTransport {
 public:
  virtual ~UdpTransport() = default;

  /// Otwiera gniazdo. port == 0 oznacza tryb "tylko wysyłanie" (bez stałego portu lokalnego).
  virtual bool begin(uint16_t port) = 0;

  /// Nieblokujący odbiór jednego datagramu.
  /// Zwraca długość datagramu (> capacity oznacza zbyt duży datagram, a `buffer` jest nieprawidłowy),
  /// 0 gdy nic nie czeka albo wartość ujemną przy błędzie gniazda.
  virtual int receive(uint8_t *buffer, size_t capacity, uint32_t &remote_ip, uint16_t &remote_port) = 0;

  /// Wysyła jeden datagram. Zwraca true przy powodzeniu.
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
  size_t byte_offset{0};  // podawany ręcznie w YAML (lista niepakowana: domyślnie wyliczany), zwalidowany w Pythonie
  uint16_t index{0};      // numer zmiennej w liście PLC (tylko lista niepakowana)

  // Wartości początkowe i przywracanie (lista "write")
  bool require_value{false};  // lista niepakowana: nie wysyłaj zmiennej, dopóki nie ma wartości
  bool has_value{false};      // zmienna ma wartość: initial_value, przywrócona albo ustawiona w czasie pracy
  bool zero_as_unavailable{false};  // sensor: zero jest publikowany jako "unknown"

  // Lista "write": encja sensor / binary_sensor może dostawać wartość z innej encji (opcja source)
  sensor::Sensor *source_sensor{nullptr};
  binary_sensor::BinarySensor *source_binary{nullptr};
  number::Number *source_number{nullptr};
  switch_::Switch *source_switch{nullptr};
  text_sensor::TextSensor *source_text{nullptr};

  // Wysyłka (lista "write" niepakowana): każda zmienna ma własny stan zmiany, odstęp min_interval i licznik sequence
  bool dirty{false};       // wartość zmieniła się od ostatniej wysyłki (tylko przy on_change)
  bool sent_once{false};
  uint32_t last_sent{0};
  uint16_t sequence{1};    // sequence_counter ramek tej zmiennej (bajty 16-17)
  bool restore_value{false};
  bool has_initial{false};
  double initial_value{0};
  ESPPreferenceObject restore_pref;

  // Watchdog odbioru (lista "read", opcja timeout)
  uint32_t last_received{0};
  bool received{false};
  bool expired{false};

  sensor::Sensor *sensor_ptr{nullptr};
  binary_sensor::BinarySensor *binary_sensor_ptr{nullptr};
  text_sensor::TextSensor *text_sensor_ptr{nullptr};
  switch_::Switch *switch_ptr{nullptr};
  number::Number *number_ptr{nullptr};
};

/// update_interval (PollingComponent) steruje cykliczną wysyłką w trybie "write".
/// update_interval: never (także "0s", zamieniane przez walidator Pythona) wyłącza ją.
class WagoNetVarComponent : public PollingComponent {
  friend class PortReceiver;  // PortReceiver rozdziela odebrane ramki między listy "read" tego samego portu

 public:
  // ---- konfiguracja (wywoływana z wygenerowanego kodu) ----
  void set_ip_address(const std::string &ip) { this->ip_address_ = ip; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_list_id(uint16_t list_id) { this->list_id_ = list_id; }
  void set_var_count(uint16_t var_count) { this->var_count_ = var_count; }
  void set_direction(const std::string &direction) {
    this->enable_read_ = (direction == "read");
    this->enable_write_ = !this->enable_read_;
  }
  void set_send_on_change(bool enable) { this->send_on_change_ = enable; }
  void set_min_interval(uint32_t ms) { this->min_interval_ms_ = ms; }
  /// false = lista niepakowana: każda zmienna w osobnej ramce (var_index w nagłówku, var_count = 1).
  void set_pack_variables(bool pack) { this->pack_variables_ = pack; }
  /// Lista pakowana: dokładny rozmiar ładunku (0 = wynika z offsetów zmiennych, odbiór akceptuje dłuższe ramki).
  void set_payload_size(uint16_t size) { this->configured_payload_size_ = size; }
  /// Lista "read": po tym czasie (ms) bez ramki encje zmiennej przechodzą w stan "unknown" (0 = wyłączone).
  void set_timeout(uint32_t ms) { this->timeout_ms_ = ms; }
  /// Lista "write" pakowana: nie wysyłaj, dopóki wszystkie zmienne nie mają wartości.
  void set_require_values(bool enable) { this->require_values_ = enable; }

  void add_variable(const std::string &name, const std::string &type, uint16_t byte_offset, uint16_t index = 0,
                    bool require_value = false);

  void register_sensor(const std::string &name, sensor::Sensor *entity, bool zero_as_unavailable = false);
  void register_binary_sensor(const std::string &name, binary_sensor::BinarySensor *entity);
  void register_text_sensor(const std::string &name, text_sensor::TextSensor *entity);
  void register_switch(const std::string &name, switch_::Switch *entity, bool restore_value, bool has_initial,
                       bool initial_value);
  void register_number(const std::string &name, number::Number *entity, bool restore_value, bool has_initial,
                       float initial_value);

  /// Lista "write": wartość początkowa zmiennej z encją sensor / binary_sensor (switch i number ustawia register_*).
  void set_initial_value(const std::string &name, double value);
  /// Lista "write": wysyłaj stan podanego sensora / binary_sensora (opcja source).
  void set_source_sensor(const std::string &name, sensor::Sensor *source);
  void set_source_binary_sensor(const std::string &name, binary_sensor::BinarySensor *source);
  void set_source_number(const std::string &name, number::Number *source);
  void set_source_switch(const std::string &name, switch_::Switch *source);
  void set_source_text_sensor(const std::string &name, text_sensor::TextSensor *source);

  /// Lista "read": switch i number tylko pokazują wartość z PLC; polecenia z interfejsu są odrzucane
  /// (encja wraca do wartości odebranej z PLC).
  bool is_read_list() const { return this->enable_read_; }
  void republish_variable(const std::string &name);

  /// Natychmiastowa wysyłka listy "write" (akcja wago_netvar.send). Respektuje require_value(s).
  void send_now();
  /// Zapisuje wartość encji do pamięci nieulotnej (opcja restore_value); wywoływane przez switch i number.
  void save_restore_value(const std::string &name, float value);

  void set_rejected_packets_sensor(sensor::Sensor *sensor) { this->rejected_packets_sensor_ = sensor; }
  void set_rx_packets_sensor(sensor::Sensor *sensor) { this->rx_packets_sensor_ = sensor; }
  void set_tx_packets_sensor(sensor::Sensor *sensor) { this->tx_packets_sensor_ = sensor; }
  void set_rx_errors_sensor(sensor::Sensor *sensor) { this->rx_errors_sensor_ = sensor; }
  void set_tx_errors_sensor(sensor::Sensor *sensor) { this->tx_errors_sensor_ = sensor; }

  // Sumy liczników wszystkich list (tworzone tylko na jednej liście; patrz total_* w README).
  void set_total_rejected_packets_sensor(sensor::Sensor *sensor) { this->total_rejected_packets_sensor_ = sensor; }
  void set_total_rx_packets_sensor(sensor::Sensor *sensor) { this->total_rx_packets_sensor_ = sensor; }
  void set_total_tx_packets_sensor(sensor::Sensor *sensor) { this->total_tx_packets_sensor_ = sensor; }
  void set_total_rx_errors_sensor(sensor::Sensor *sensor) { this->total_rx_errors_sensor_ = sensor; }
  void set_total_tx_errors_sensor(sensor::Sensor *sensor) { this->total_tx_errors_sensor_ = sensor; }

  // ---- API czasu działania (lambdy, switch, number); tylko listy "write" ----
  // Wartość jest konwertowana na typ NetVar zmiennej. Przy przepełnieniu typu całkowitego zachowywane są
  // najmniej znaczące bajty. Liczby zapisywane do zmiennej STRING są zamieniane na tekst.
  void set_variable_value(const std::string &name, bool value);
  void set_variable_value(const std::string &name, const std::string &value);
  // Bez tego przeciążenia literał tekstowy wybrałby przeciążenie bool.
  void set_variable_value(const std::string &name, const char *value) {
    this->set_variable_value(name, std::string(value));
  }
  /// Dowolny typ arytmetyczny (int, uint32_t, float, double, ...). Zwykły zestaw przeciążeń bool/int/float
  /// powodowałby niejednoznaczność przy wywołaniu np. z double lub uint32_t.
  template<typename T, typename = typename std::enable_if<std::is_arithmetic<T>::value &&
                                                          !std::is_same<T, bool>::value>::type>
  void set_variable_value(const std::string &name, T value) {
    VarDef *var = this->prepare_write_(name);
    if (var == nullptr)
      return;
    if (std::is_floating_point<T>::value)
      this->write_double_(*var, static_cast<double>(value));
    else
      this->write_int64_(*var, static_cast<int64_t>(value));
  }

  /// Ostatnia wartość zmiennej (wysłana w trybie "write", ostatnio zdekodowana w trybie "read").
  std::string get_variable_value(const std::string &name) const;

  // ---- Component ----
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

 protected:
  static constexpr size_t STATS_COUNT = 5;

  // pomocnicze: konfiguracja
  VarDef *get_var(const std::string &name);
  const VarDef *get_var(const std::string &name) const;
  void fail_config_(const char *what, const std::string &name);
  bool finalize_layout_();

  // pomocnicze: transport
  bool ensure_udp_();
  const VarDef *find_by_index_(uint16_t index) const;
  void apply_startup_values_();
  void apply_source_states_();
  void apply_value_(VarDef &var, double value);
  void mark_received_(VarDef &var);
  void check_timeouts_();
  void invalidate_variable_(const VarDef &var);
  bool all_values_set_() const;
  bool send_frame_(uint16_t var_index, uint16_t var_count, const uint8_t *data, size_t size, uint16_t &sequence);
  void send_list_();
  void send_variable_(VarDef &var);
  /// Waliduje ramkę i, jeśli jest poprawna, dekoduje ją i zlicza (rx_packets). Zwraca true, gdy ramka została przyjęta.
  bool accept_frame_(const uint8_t *frame, size_t packet_size, uint32_t remote_ip);
  bool validate_packet_(const uint8_t *frame, size_t packet_size, uint32_t remote_ip) const;
  void unpack_payload_(const uint8_t *payload);
  void publish_variable_(const VarDef &var);
  void publish_statistics_();

  void process_on_change_();
  void send_packet_();

  // pomocnicze: konwersja wartości
  VarDef *prepare_write_(const std::string &name);
  void commit_(VarDef &var, const uint8_t *bytes);
  void write_int64_(VarDef &var, int64_t value);
  void write_double_(VarDef &var, double value);
  void write_text_(VarDef &var, const std::string &text);
  double decode_number_(const VarDef &var, const uint8_t *data) const;

  // konfiguracja
  std::string ip_address_;
  uint32_t ip_{0};
  uint16_t port_{1202};
  uint16_t list_id_{1};
  uint16_t var_count_{0};
  bool enable_read_{false};
  bool enable_write_{true};
  bool send_on_change_{false};
  uint32_t min_interval_ms_{500};
  bool pack_variables_{true};
  uint16_t configured_payload_size_{0};
  bool exact_payload_size_{false};
  uint32_t timeout_ms_{0};
  bool require_values_{false};
  bool gate_warned_{false};
  bool ready_{false};  // setup() zakończony: od tego momentu callbacki source mogą zapisywać do bufora
  bool config_error_{false};

  // stan
  std::vector<VarDef> variables_;
  size_t payload_size_{0};
  std::vector<uint8_t> payload_;  // bieżące wartości zmiennych
  std::vector<uint8_t> frame_;    // bufor ramki TX (tylko tryb write)
  std::unique_ptr<UdpTransport> udp_;  // gniazdo wysyłające (tylko tryb write)
  PortReceiver *receiver_{nullptr};    // współdzielone gniazdo odbiorcze portu (tylko tryb read)
  bool udp_started_{false};
  bool udp_attempted_{false};
  uint32_t last_udp_attempt_{0};

  // Lista pakowana: jeden stan zmiany, jeden odstęp min_interval i jeden sequence_counter na całą listę.
  // Lista niepakowana: te same informacje są w VarDef (osobno dla każdej zmiennej).
  bool is_dirty_{false};
  bool sent_once_{false};
  uint32_t last_sent_time_{0};
  uint16_t sequence_counter_{1};

  uint32_t rx_packets_{0};
  uint32_t tx_packets_{0};
  uint32_t rx_errors_{0};
  uint32_t tx_errors_{0};
  uint32_t rejected_packets_{0};
  uint32_t published_stats_[STATS_COUNT];
  uint32_t published_totals_[STATS_COUNT];
  uint32_t last_stats_time_{0};

  sensor::Sensor *rejected_packets_sensor_{nullptr};
  sensor::Sensor *rx_packets_sensor_{nullptr};
  sensor::Sensor *tx_packets_sensor_{nullptr};
  sensor::Sensor *rx_errors_sensor_{nullptr};
  sensor::Sensor *tx_errors_sensor_{nullptr};

  sensor::Sensor *total_rejected_packets_sensor_{nullptr};
  sensor::Sensor *total_rx_packets_sensor_{nullptr};
  sensor::Sensor *total_tx_packets_sensor_{nullptr};
  sensor::Sensor *total_rx_errors_sensor_{nullptr};
  sensor::Sensor *total_tx_errors_sensor_{nullptr};
};

}  // namespace wago_netvar
}  // namespace esphome
