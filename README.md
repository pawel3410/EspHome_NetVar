# WagoNetVar – komponent ESPHome dla protokołu NetVar (WAGO)

Komponent umożliwia wymianę danych między ESPHome a sterownikami WAGO (lub innymi urządzeniami) przy użyciu protokołu NetVar przez UDP. Dane przesyłane są w formacie little endian.

## Wymagania

- ESPHome
- Moduł z Wi-Fi (ESP32 / ESP8266)
- Komponent działa w trybie `read` lub `write` na poziomie pojedynczej instancji. Można zdefiniować wiele instancji (np. jedną do odczytu, drugą do zapisu).
- Wsparcie dla platform Arduino oraz ESP-IDF.

## Struktura ramki NetVar

Nagłówek ma stały rozmiar 20 bajtów i jest przesyłany w kolejności little endian. Po nagłówku następuje ładunek (payload) zawierający zdefiniowane zmienne.

| Bajty | Pole              | Opis                                                                                                                               |
|-------|-------------------|------------------------------------------------------------------------------------------------------------------------------------|
| 0–3   | Magic bytes       | Stałe: 0x00 0x2D 0x53 0x33                                                                                                         |
| 4–7   | Zarezerwowane     | Zerowane                                                                                                                           |
| 8–9   | list_ID           | Identyfikator listy (uint16 LE)                                                                                                    |
| 10–11 | Zarezerwowane     | Zerowane                                                                                                                           |
| 12–13 | var_count         | Liczba zmiennych na liście (uint16 LE)                                                                                             |
| 14–15 | frame_size        | Całkowita długość ramki w bajtach (nagłówek + payload) (uint16 LE)                                                                 |
| 16–17 | sequence_counter  | Licznik sekwencyjny (odczytywany, nie walidowany)                                                                                  |
| 18–19 | checksum          | Suma kontrolna (odczytywana, nie walidowana; przy wysyłce = 0)                                                                     |
| 20–N  | Payload           | Wartości zmiennych zgodnie z układem pamięci                                                                                       |

## Obsługiwane typy danych

Wszystkie typy są little endian. LREAL nie jest obsługiwany przez NetVar.

| Typ NetVar | Rozmiar | Zakres / opis | Encja ESPHome | Typ w Home Assistant |
|------------|---------|---------------|---------------|----------------------|
| BOOL | 1 bajt | 0 = FAŁSZ, 1 = PRAWDA | `binary_sensor` | `binary_sensor` |
| BYTE | 1 bajt | 0–255 (maska bitowa) | `sensor` | `sensor` |
| USINT | 1 bajt | 0–255 | `sensor` | `sensor` |
| SINT | 1 bajt | -128–127 | `sensor` | `sensor` |
| WORD | 2 bajty | 0–65 535 (maska bitowa) | `sensor` | `sensor` |
| UINT | 2 bajty | 0–65 535 | `sensor` | `sensor` |
| INT | 2 bajty | -32 768–32 767 | `sensor` | `sensor` |
| DWORD | 4 bajty | 0–4 294 967 295 (maska bitowa) | `sensor` | `sensor` |
| UDINT | 4 bajty | 0–4 294 967 295 | `sensor` | `sensor` |
| DINT | 4 bajty | -2 147 483 648–2 147 483 647 | `sensor` | `sensor` |
| TIME | 4 bajty | Czas w ms (UDINT) | `sensor` | `sensor` |
| DATE | 4 bajty | Data w s od 1970-01-01 (UDINT) | `sensor` | `sensor` |
| TIME_OF_DAY (TOD) | 4 bajty | Czas doby w ms (UDINT) | `sensor` | `sensor` |
| DATE_AND_TIME (DT) | 4 bajty | Data i czas w s od 1970-01-01 (UDINT) | `sensor` | `sensor` |
| REAL | 4 bajty | Liczba zmiennoprzecinkowa IEEE-754 | `sensor` | `sensor` |
| STRING(n) | n + 1 bajt | Tekst ASCII zakończony nullem (0x00), domyślnie 80 znaków, max 255 | `text_sensor` | `text_sensor` |

## Konfiguracja ESPHome

Zmienne definiuje się bezpośrednio w sekcji `wago_netvar:`. Encje (sensory, przełączniki, itp.) są mapowane na zmienne przez pole `variable` i muszą być zdefiniowane na poziomie głównym konfiguracji ESPHome.

### Pełna lista opcji konfiguracyjnych

| Opcja | Wymagana | Domyślna | Opis |
|-------|----------|----------|------|
| `id` | Tak | – | Unikalny identyfikator komponentu ESPHome. |
| `ip_address` | Tak | – | Adres IP odbiorcy/nadawcy. W trybie `read` może być `0.0.0.0`, aby akceptować pakiety z dowolnego IP. W trybie `write` nie może być `0.0.0.0` (dozwolony broadcast `255.255.255.255`).
|
| `port` | Nie | `1202` | Port UDP. Różne instancje mogą używać różnych portów. |
| `list_id` | Nie | `1` | Identyfikator listy NetVar (bajty 8–9 ramki). |
| `var_count` | Nie | `0` | Liczba zmiennych na liście. Jeśli `0`, zostanie ustawiona automatycznie na podstawie zdefiniowanych zmiennych. |
| `direction` | Nie | `write` | Kierunek komunikacji: `read` (odbiór) lub `write` (wysyłka). |
| `alignment` | Nie | `false` | Czy wyrównywać zmienne automatycznie zgodnie z wymaganiami typu (np. 2 bajty dla WORD, 4 bajty dla DWORD). |
| `send_on_change` | Nie | `false` | Czy wysyłać ramkę natychmiast po zmianie wartości zmiennej. |
| `min_interval` | Nie | `500ms` | Minimalny odstęp czasu między wysyłkami przy `send_on_change: true`. |
| `update_interval` | Nie | `5s` | Interwał cyklicznej wysyłki ramki w trybie `write`. Ustawienie `0s` wyłącza wysyłkę cykliczną. Działa równolegle z `send_on_change`. |
| `variables` | Tak | – | Lista zdefiniowanych zmiennych NetVar. |
| `sensors` | Nie | – | Mapowanie zmiennych numerycznych na sensory ESPHome. |
| `binary_sensors` | Nie | – | Mapowanie zmiennych BOOL na binary_sensor. |
| `text_sensors` | Nie | – | Mapowanie zmiennych STRING na text_sensor. |
| `switches` | Nie | – | Mapowanie zmiennych BOOL na switch (tylko tryb `write`). |
| `numbers` | Nie | – | Mapowanie zmiennych numerycznych na number (tylko tryb `write`). |
| `rejected_packets` | Nie | – | Sensor liczby odrzuconych ramek (błędna walidacja). |
| `rx_packets` | Nie | – | Sensor liczby poprawnie odebranych ramek. |
| `tx_packets` | Nie | – | Sensor liczby wysłanych ramek. |
| `rx_errors` | Nie | – | Sensor liczby błędów odbioru. |
| `tx_errors` | Nie | – | Sensor liczby błędów wysyłki. |

### Definicja zmiennych

Każda zmienna w sekcji `variables` wymaga:

- `name` – nazwa zmiennej (unikalna w obrębie listy).
- `type` – typ NetVar (np. `BOOL`, `UINT`, `REAL`, `STRING(20)`).
- `byte_offset` (opcjonalnie) – ręczny offset w bajcie w ładunku. W trybie `read` jest wymagany dla każdej zmiennej. W trybie `write` może być pominięty, wtedy offset jest przydzielany automatycznie.

### Przykłady konfiguracji

#### Przykład 1: Odczyt (read) z ręcznymi offsetami

```yaml
wago_netvar:
  id: wago_read
  ip_address: "0.0.0.0"        # Akceptuj pakiety z dowolnego IP
  port: 1202
  list_id: 10
  direction: read
  alignment: false
  variables:
    - name: "temp"
      type: "REAL"
      byte_offset: 0
    - name: "status"
      type: "BOOL"
      byte_offset: 4
    - name: "alarm"
      type: "BOOL"
      byte_offset: 5
  sensors:
    - platform: wago_netvar
      variable: "temp"
      name: "Temperatura"
  binary_sensors:
    - platform: wago_netvar
      variable: "status"
      name: "Status pracy"
  rejected_packets:
    name: "Odrzucone ramki"
  rx_packets:
    name: "Odebrane ramki"


Przykład 2: Zapis (write) z automatycznymi offsetami i wysyłką przy zmianie


wago_netvar:
  id: wago_write
  ip_address: "192.168.1.50"   # Adres sterownika WAGO
  port: 1202
  list_id: 20
  direction: write
  send_on_change: true
  min_interval: "1000ms"
  update_interval: "5s"
  alignment: true
  variables:
    - name: "setpoint"
      type: "REAL"
    - name: "pump_on"
      type: "BOOL"
  switches:
    - platform: wago_netvar
      variable: "pump_on"
      name: "Pompa"
  numbers:
    - platform: wago_netvar
      variable: "setpoint"
      name: "Nastawa temperatury"
      min_value: 0
      max_value: 100
  tx_packets:
    name: "Wysłane ramki"


Przykład 3: Wysyłka broadcast


wago_netvar:
  id: wago_broadcast
  ip_address: "255.255.255.255"
  port: 1202
  list_id: 30
  direction: write
  variables:
    - name: "heartbeat"
      type: "BOOL"



Walidacja ramek (tryb read)

Każda przychodząca ramka jest walidowana pod kątem:

 • adresu IP nadawcy (jeśli ip_address różne od 0.0.0.0),
 • portu docelowego (niejawnie przez bindowanie gniazda UDP),
 • list_ID (bajty 8–9),
 • var_count (bajty 12–13) zgodnie z konfiguracją,
 • frame_size (bajty 14–15) zgodnie z rozmiarem otrzymanego pakietu,
 • rozmiaru ładunku zgodnie z obliczonym payload_size.

Pola sequence_counter (bajty 16–17) oraz checksum (bajty 18–19) są odczytywane, ale nie walidowane. Przy wysyłce checksum jest zerowane.


Automatyczne sensory statystyk

Komponent może automatycznie utworzyć sensory ESPHome z licznikami:

 • rejected_packets – ramki odrzucone przy walidacji,
 • rx_packets – poprawnie odebrane ramki,
 • tx_packets – wysłane ramki,
 • rx_errors – błędy odbioru,
 • tx_errors – błędy wysyłki.


Uwagi dotyczące typów danych i konwersji

 • Wszystkie liczby są przesyłane w formacie little endian.
 • Przy przepełnieniu typu całkowitego mniej znaczące bity są obcinane.
 • Przy wysyłce wartości typu STRING liczby są konwertowane na tekst ASCII.
 • W trybie read offsety zmiennych muszą być podane ręcznie (byte_offset), co pozwala pominąć odczyt niektórych zmiennych poprzez pozostawienie luk w ładunku.
 • W trybie write offsety mogą być przydzielane automatycznie (jeśli byte_offset nie jest podany).


Wsparcie platform

Komponent działa zarówno na Arduino, jak i ESP-IDF. Warstwa transportu UDP jest abstrakcyjna (UdpTransport), co umożliwia łatwe dodanie kolejnych platform w przyszłości. Komponent implementuje dump_config() i
inicjalizuje się po Wi-Fi (AFTER_WIFI).