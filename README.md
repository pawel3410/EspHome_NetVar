# WagoNetVar – komponent ESPHome dla protokołu NetVar (WAGO)

Komponent umożliwia wymianę danych między ESPHome a sterownikami WAGO (lub innymi urządzeniami) przy użyciu protokołu NetVar przez UDP. Dane przesyłane są w formacie little endian.

## Wymagania

- ESPHome oraz moduł z siecią Wi-Fi lub Ethernet (ESP32 / ESP8266).
- Wsparcie dla platform Arduino oraz ESP-IDF.
- Pojedyncza instancja działa w trybie `read` **albo** `write`. Można zdefiniować wiele instancji (np. jedną do odczytu, drugą do zapisu).
- Każda lista `read` nasłuchuje na własnym porcie UDP – dwie listy `read` nie mogą używać tego samego portu (sprawdza to walidacja konfiguracji). Listy `write` nie zajmują portu lokalnego, więc mogą używać tego samego numeru portu co lista `read`.

## Struktura ramki NetVar

Nagłówek ma stały rozmiar 20 bajtów i jest przesyłany w kolejności little endian. Po nagłówku następuje ładunek (payload) zawierający zdefiniowane zmienne.

| Bajty | Pole              | Opis                                                                                          |
|-------|-------------------|-----------------------------------------------------------------------------------------------|
| 0–3   | Magic bytes       | Sygnatura: `0x00 0x2D 0x53 0x33`                                                              |
| 4–7   | Zarezerwowane     | Zerowane                                                                                      |
| 8–9   | list_ID           | Identyfikator listy (uint16 LE)                                                               |
| 10–11 | var_index         | Indeks zmiennej w trybie niepakowanym (przy wysyłce 0)                                        |
| 12–13 | var_count         | Liczba zmiennych na liście (uint16 LE)                                                        |
| 14–15 | frame_size        | Całkowita długość ramki w bajtach (nagłówek + payload) (uint16 LE)                            |
| 16–17 | sequence_counter  | Licznik sekwencyjny (przy odbiorze odczytywany, nie walidowany; przy wysyłce zwiększany)      |
| 18    | checksum_present  | Flagi: `00` bez sumy kontrolnej, `02` z sumą kontrolną (przy wysyłce `00`)                    |
| 19    | checksum          | Suma kontrolna (przy odbiorze nie walidowana, przy wysyłce `00`)                              |
| 20–N  | Payload           | Wartości zmiennych zgodnie z układem pamięci                                                  |

Maksymalny rozmiar ramki to 1472 bajty (nagłówek + 1452 bajty ładunku).

## Obsługiwane typy danych

Wszystkie typy są little endian. LREAL nie jest obsługiwany przez NetVar.

| Typ NetVar | Rozmiar | Zakres / opis | Encja ESPHome (`read`) | Encja ESPHome (`write`) | Typ w Home Assistant |
|------------|---------|---------------|------------------------|-------------------------|----------------------|
| BOOL | 1 bajt | 0 = FAŁSZ, 1 = PRAWDA | `binary_sensor` | `switch` | `binary_sensor` / `switch` |
| BYTE | 1 bajt | 0–255 (maska bitowa) | `sensor` | `number` | `sensor` / `number` |
| USINT | 1 bajt | 0–255 | `sensor` | `number` | `sensor` / `number` |
| SINT | 1 bajt | -128–127 | `sensor` | `number` | `sensor` / `number` |
| WORD | 2 bajty | 0–65 535 (maska bitowa) | `sensor` | `number` | `sensor` / `number` |
| UINT | 2 bajty | 0–65 535 | `sensor` | `number` | `sensor` / `number` |
| INT | 2 bajty | -32 768–32 767 | `sensor` | `number` | `sensor` / `number` |
| DWORD | 4 bajty | 0–4 294 967 295 (maska bitowa) | `sensor` | `number` | `sensor` / `number` |
| UDINT | 4 bajty | 0–4 294 967 295 | `sensor` | `number` | `sensor` / `number` |
| DINT | 4 bajty | -2 147 483 648–2 147 483 647 | `sensor` | `number` | `sensor` / `number` |
| TIME | 4 bajty | Czas w ms (UDINT), T#0ms – T#49d17h2m47s295ms | `sensor` | `number` | `sensor` / `number` |
| DATE | 4 bajty | Data w s od 1970-01-01 (UDINT), do D#2106-02-07 | `sensor` | `number` | `sensor` / `number` |
| TIME_OF_DAY (TOD) | 4 bajty | Czas doby w ms (UDINT) | `sensor` | `number` | `sensor` / `number` |
| DATE_AND_TIME (DT) | 4 bajty | Data i czas w s od 1970-01-01 (UDINT) | `sensor` | `number` | `sensor` / `number` |
| REAL | 4 bajty | Liczba zmiennoprzecinkowa IEEE-754 | `sensor` | `number` | `sensor` / `number` |
| STRING(n) | n + 1 bajt | Tekst ASCII zakończony nullem (0x00), domyślnie 80 znaków, max 255 | `text_sensor` | `number` (liczba zapisana jako tekst) lub `set_variable_value()` | `text_sensor` / `number` |

W YAML typy `TIME_OF_DAY` i `DATE_AND_TIME` można zapisać również jako `TOD` i `DT`; zapis `STRING` bez długości oznacza `STRING(80)`.

## Konfiguracja ESPHome

Zmienne definiuje się w sekcji `variables:`. Encje (sensory, przełączniki, itp.) są **zagnieżdżone** w konfiguracji komponentu (listy `sensors`, `binary_sensors`, `text_sensors`, `switches`, `numbers`) i mapowane na zmienne przez pole `variable`. Nie używa się w nich pola `platform:`.

### Pełna lista opcji konfiguracyjnych

| Opcja | Wymagana | Domyślna | Opis |
|-------|----------|----------|------|
| `id` | Tak | – | Unikalny identyfikator komponentu ESPHome (używany też do nazw automatycznych sensorów statystyk). |
| `ip_address` | Tak | – | Adres IPv4 odbiorcy/nadawcy. W trybie `read` może być `0.0.0.0` (akceptuj pakiety z dowolnego IP); `255.255.255.255` jest tam niedozwolony. W trybie `write` nie może być `0.0.0.0` (dozwolony broadcast `255.255.255.255`). |
| `port` | Nie | `1202` | Port UDP: w trybie `read` port nasłuchu, w trybie `write` port docelowy. |
| `list_id` | Nie | `1` | Identyfikator listy NetVar (bajty 8–9 ramki). |
| `var_count` | Nie | `0` | Liczba zmiennych na liście NetVar (bajty 12–13). Jeśli `0`, zostanie ustawiona na liczbę zdefiniowanych zmiennych. Może być **większa** niż liczba zdefiniowanych zmiennych (gdy część zmiennych listy jest pomijana), ale nie mniejsza. |
| `direction` | Nie | `write` | Kierunek komunikacji: `read` (odbiór) lub `write` (wysyłka). |
| `alignment` | Nie | `false` | Czy zmiennym bez `byte_offset` przydzielać offsety wyrównane do rozmiaru typu (2 bajty dla WORD/UINT/INT, 4 bajty dla DWORD/UDINT/DINT/TIME/DATE/TOD/DT/REAL). |
| `send_on_change` | Nie | `false` | Czy wysyłać ramkę natychmiast po zmianie wartości zmiennej (tryb `write`). |
| `min_interval` | Nie | `500ms` | Minimalny odstęp czasu między wysyłkami przy `send_on_change: true`. |
| `update_interval` | Nie | `5s` | Interwał cyklicznej wysyłki ramki w trybie `write`. `0s` (lub `never`) wyłącza wysyłkę cykliczną. Działa równolegle z `send_on_change`. |
| `variables` | Tak | – | Lista zdefiniowanych zmiennych NetVar. |
| `sensors` | Nie | – | Mapowanie zmiennych numerycznych na sensory (tylko `read`). |
| `binary_sensors` | Nie | – | Mapowanie zmiennych BOOL na binary_sensor (tylko `read`). |
| `text_sensors` | Nie | – | Mapowanie zmiennych STRING na text_sensor (tylko `read`). |
| `switches` | Nie | – | Mapowanie zmiennych BOOL na switch (tylko `write`). |
| `numbers` | Nie | – | Mapowanie zmiennych numerycznych (lub STRING) na number (tylko `write`). Wymaga `min_value` i `max_value`, opcjonalnie `step` (domyślnie `1`). |
| `rejected_packets` | Nie | tworzony automatycznie | Sensor liczby odrzuconych ramek (błędna walidacja). |
| `rx_packets` | Nie | tworzony automatycznie | Sensor liczby poprawnie odebranych ramek. |
| `tx_packets` | Nie | tworzony automatycznie | Sensor liczby wysłanych ramek. |
| `rx_errors` | Nie | tworzony automatycznie | Sensor liczby błędów odbioru. |
| `tx_errors` | Nie | tworzony automatycznie | Sensor liczby błędów wysyłki. |

### Definicja zmiennych

Każda zmienna w sekcji `variables` wymaga:

- `name` – nazwa zmiennej (unikalna w obrębie listy).
- `type` – typ NetVar (np. `BOOL`, `UINT`, `REAL`, `STRING(20)`).
- `byte_offset` (opcjonalnie) – offset w bajtach w ładunku (0–1451). W trybie `read` jest wymagany dla każdej zmiennej. W trybie `write` może być pominięty – wtedy offset jest przydzielany automatycznie (zmienne z ręcznym offsetem mają pierwszeństwo, a przydzielane automatycznie omijają zajęte bajty).

Układ pamięci jest wyliczany raz, podczas walidacji konfiguracji (Python), a do kodu C++ trafiają gotowe offsety. Nakładające się zmienne i przekroczenie 1452 bajtów ładunku powodują błąd konfiguracji.

### Przykłady konfiguracji

#### Przykład 1: Odczyt (read) z ręcznymi offsetami

```yaml
wago_netvar:
  id: wago_read
  ip_address: "0.0.0.0"        # Akceptuj pakiety z dowolnego IP
  port: 1202
  list_id: 10
  var_count: 6                 # lista w sterowniku ma 6 zmiennych, tu odczytujemy tylko 3
  direction: read
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
    - variable: "temp"
      name: "Temperatura"
      unit_of_measurement: "°C"
  binary_sensors:
    - variable: "status"
      name: "Status pracy"
  rejected_packets:
    name: "Odrzucone ramki"    # opcjonalnie: własna nazwa/ustawienia sensora statystyk
```

#### Przykład 2: Zapis (write) z automatycznymi offsetami i wysyłką przy zmianie

```yaml
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
    - variable: "pump_on"
      name: "Pompa"
  numbers:
    - variable: "setpoint"
      name: "Nastawa temperatury"
      min_value: 0
      max_value: 100
      step: 0.5
```

#### Przykład 3: Wysyłka broadcast

```yaml
wago_netvar:
  id: wago_broadcast
  ip_address: "255.255.255.255"
  port: 1202
  list_id: 30
  direction: write
  variables:
    - name: "heartbeat"
      type: "BOOL"
```

#### Przykład 4: Zapis z lambdy

```yaml
interval:
  - interval: 10s
    then:
      - lambda: |-
          id(wago_write).set_variable_value("setpoint", 21.5f);
          id(wago_write).set_variable_value("pump_on", true);
```

Dostępne przeciążenia `set_variable_value(nazwa, wartość)`: `bool`, `int`, `float`, `std::string` oraz `const char *`. `get_variable_value(nazwa)` zwraca ostatnią wartość zmiennej jako tekst.

## Walidacja ramek (tryb read)

Każda przychodząca ramka jest walidowana pod kątem:

- adresu IP nadawcy (jeśli `ip_address` różne od `0.0.0.0`),
- portu docelowego (niejawnie przez bindowanie gniazda UDP do `port`),
- magic bytes (bajty 0–3),
- list_ID (bajty 8–9),
- var_count (bajty 12–13) zgodnie z konfiguracją,
- frame_size (bajty 14–15) zgodnie z rozmiarem otrzymanego pakietu,
- rozmiaru ładunku: musi obejmować wszystkie zdefiniowane zmienne (ramka może być dłuższa – zmienne za ostatnią zdefiniowaną są pomijane).

Pola sequence_counter (bajty 16–17) oraz checksum (bajty 18–19) są odczytywane, ale nie walidowane. Przy wysyłce checksum jest zerowane. Każda ramka, która przejdzie walidację, jest w całości dekodowana i publikowana do zmapowanych encji; pozostałe są liczone w `rejected_packets`.

## Automatyczne sensory statystyk

Dla każdej listy komponent automatycznie tworzy (kategoria encji: diagnostyczna) sensory z licznikami, nazwane `<id listy> <opis>`. Podanie w YAML klucza o tej samej nazwie pozwala zmienić nazwę i pozostałe ustawienia sensora:

- `rejected_packets` – ramki odrzucone przy walidacji (w tym zbyt duże datagramy),
- `rx_packets` – poprawnie odebrane ramki,
- `tx_packets` – wysłane ramki,
- `rx_errors` – błędy odbioru (błąd gniazda),
- `tx_errors` – błędy wysyłki.

Wartości są publikowane co najwyżej raz na sekundę i tylko po zmianie.

## Uwagi dotyczące typów danych i konwersji

- Wszystkie liczby są przesyłane w formacie little endian.
- Wysyłana wartość jest konwertowana na typ zmiennej. Przy przepełnieniu typu całkowitego zachowywane są najmniej znaczące bajty, a nadmiarowe, najbardziej znaczące, są odrzucane (np. `70000` zapisane do INT daje `4464`). Wartości zmiennoprzecinkowe zapisywane do typów całkowitych są zaokrąglane do najbliższej liczby całkowitej.
- Liczby zapisywane do zmiennej STRING są zamieniane na tekst ASCII; tekst dłuższy niż `n` znaków jest ucinany, a znaki spoza ASCII są odrzucane.
- Tekst zapisywany do zmiennej liczbowej musi być w całości poprawną liczbą (np. `12abc` jest odrzucane). Dla BOOL akceptowane są: `1/0`, `true/false`, `on/off`, `yes/no` (bez rozróżniania wielkości liter).
- Sensory ESPHome używają liczb `float` (24 bity mantysy), więc wartości 32-bitowych typów całkowitych powyżej ok. 16 777 216 są w encji zaokrąglane. Pełna wartość jest dostępna przez `get_variable_value()`.
- Nieskończona lub nieliczbowa wartość REAL (NaN/Inf) jest publikowana jako NaN (stan „unknown”).
- W trybie `read` offsety zmiennych muszą być podane ręcznie (`byte_offset`), co pozwala pominąć odczyt niektórych zmiennych poprzez pozostawienie luk w ładunku.
- W trybie `write` bufor po starcie zawiera zera – do czasu ustawienia wartości (np. przez `switch`/`number`) wysyłane są zera. Stan encji `switch`/`number` nie jest odtwarzany po restarcie.
- Wysyłka i odbiór działają dopiero po uzyskaniu połączenia sieciowego; gniazdo UDP jest otwierane automatycznie (z ponawianiem co 5 s).

## Wsparcie platform

Komponent działa zarówno na Arduino, jak i ESP-IDF. Warstwa transportu UDP jest abstrakcyjna (`UdpTransport`), a implementacje platformowe znajdują się wyłącznie w pliku `.cpp`, co ułatwia dodanie kolejnych platform. Komponent implementuje `dump_config()` i inicjalizuje się po sieci (`AFTER_WIFI`).
