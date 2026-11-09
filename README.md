# WagoNetVar – komponent ESPHome dla protokołu NetVar (WAGO)

Komponent umożliwia wymianę danych między ESPHome a sterownikami WAGO (lub innymi urządzeniami) przy użyciu protokołu NetVar przez UDP. Dane przesyłane są w formacie little endian.

## Wymagania

- ESPHome oraz moduł z siecią Wi-Fi lub Ethernet (ESP32 / ESP8266).
- Wsparcie dla platform Arduino oraz ESP-IDF.
- Pojedyncza instancja działa w trybie `read` **albo** `write`. Można zdefiniować wiele instancji (np. jedną do odczytu, drugą do zapisu).
- Wiele list `read` może nasłuchiwać na tym samym porcie UDP (np. listy 100 i 101 na porcie 1202). Komponent otwiera wtedy jedno współdzielone gniazdo na port, a odebrane ramki rozdziela między listy według `list_id` (oraz adresu nadawcy). Listy nie kolidują ze sobą, jeśli różnią się `list_id`, albo tym samym `list_id`, ale różnymi, konkretnymi adresami `ip_address` (nie `0.0.0.0`). Walidacja konfiguracji odrzuca jedynie listy nie do odróżnienia (ten sam port, `list_id` i nadawca).
- Listy `write` nie zajmują portu lokalnego (wysyłają z portu przydzielonego przez system), więc mogą używać tego samego numeru portu co listy `read`.

## Struktura ramki NetVar

Nagłówek ma stały rozmiar 20 bajtów i jest przesyłany w kolejności little endian. Po nagłówku następuje ładunek (payload) zawierający zdefiniowane zmienne.

| Bajty | Pole              | Opis                                                                                          |
|-------|-------------------|-----------------------------------------------------------------------------------------------|
| 0–3   | Magic bytes       | Sygnatura: `0x00 0x2D 0x53 0x33`                                                              |
| 4–7   | Zarezerwowane     | Zerowane                                                                                      |
| 8–9   | list_ID           | Identyfikator listy (uint16 LE)                                                               |
| 10–11 | var_index         | Numer zmiennej w liście niepakowanej (lista pakowana: `0`)                                    |
| 12–13 | var_count         | Liczba zmiennych na liście (uint16 LE)                                                        |
| 14–15 | frame_size        | Całkowita długość ramki w bajtach (nagłówek + payload) (uint16 LE)                            |
| 16–17 | sequence_counter  | Licznik sekwencyjny (przy odbiorze odczytywany, nie walidowany; przy wysyłce zwiększany)      |
| 18    | checksum_present  | Flagi: `00` bez sumy kontrolnej, `02` z sumą kontrolną (przy wysyłce `00`)                    |
| 19    | checksum          | Suma kontrolna (przy odbiorze nie walidowana, przy wysyłce `00`)                              |
| 20–N  | Payload           | Wartości zmiennych zgodnie z układem pamięci                                                  |

Maksymalny rozmiar ramki to 1472 bajty (nagłówek + 1452 bajty ładunku).

Lista **pakowana** (domyślnie) wysyła wszystkie zmienne w jednej ramce: `var_index = 0`, `var_count` równe liczbie zmiennych listy. Lista **niepakowana** (`pack_variables: false`) wysyła każdą zmienną w osobnej ramce: `var_index` to numer zmiennej, `var_count = 1`, a ładunek ma dokładnie rozmiar tej zmiennej.

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
| `port` | Nie | `1202` | Port UDP: w trybie `read` port nasłuchu (może być wspólny dla wielu list `read`), w trybie `write` port docelowy. |
| `list_id` | Nie | `1` | Identyfikator listy NetVar (bajty 8–9 ramki). W trybie `read` służy do rozdzielania ramek między listy współdzielące port. |
| `var_count` | Nie | `0` | Liczba zmiennych na liście NetVar (bajty 12–13). Jeśli `0`, zostanie ustawiona na liczbę zdefiniowanych zmiennych. Może być **większa** niż liczba zdefiniowanych zmiennych (gdy część zmiennych listy jest pomijana), ale nie mniejsza. |
| `direction` | Nie | `write` | Kierunek komunikacji: `read` (odbiór) lub `write` (wysyłka). Alias: `mode` (nazwa używana w integracji Home Assistant). |
| `pack_variables` | Nie | `true` | `true`: lista pakowana (jedna ramka z wszystkimi zmiennymi). `false`: lista niepakowana (każda zmienna w osobnej ramce z `var_index`, `var_count = 1`). W trybie niepakowanym `var_count` i `payload_size` są ignorowane. |
| `payload_size` | Nie | – | Tylko lista pakowana: dokładny rozmiar ładunku w bajtach (nie mniejszy niż wynika z offsetów, max 1452). Przy odbiorze ramka o innym rozmiarze ładunku jest odrzucana; bez tej opcji akceptowane są ramki dłuższe (zmienne za ostatnią zdefiniowaną są pomijane). Przy wysyłce określa rozmiar ładunku. |
| `timeout` | Nie | `0s` | Tylko `read`: jeśli przez ten czas nie nadejdzie ramka ze zmienną, jej encje przechodzą w stan „unknown” (patrz „Timeout odbioru”). `0s` wyłącza. |
| `require_values` | Nie | `false` | Tylko `write`, lista pakowana: nie wysyłaj listy, dopóki **każda** zmienna nie ma wartości (patrz „Wartości początkowe i przywracanie”). W liście niepakowanej służy `require_value` przy zmiennych. |
| `on_change` | Nie | `false` | Czy wysyłać ramkę po zmianie wartości zmiennej (tryb `write`). Zmiany wykonane w tej samej iteracji pętli (np. kilka `set_variable_value()` w jednej lambdzie) trafiają do jednej ramki. Nazwa jest zgodna z integracją Home Assistant. Dotychczasowa nazwa `send_on_change` nadal działa, ale jest przestarzała (ostrzeżenie przy kompilacji). |
| `min_interval` | Nie | `500ms` | Minimalny odstęp czasu między wysyłkami przy `on_change: true`. |
| `update_interval` | Nie | `5s` | Interwał cyklicznej wysyłki ramki w trybie `write`. `0s` (lub `never`) wyłącza wysyłkę cykliczną. Działa równolegle z `on_change`. W trybie `read` jest ignorowany. |
| `variables` | Tak | – | Lista zdefiniowanych zmiennych NetVar. |
| `sensors` | Nie | – | Mapowanie zmiennych numerycznych na sensory (tylko `read`). |
| `binary_sensors` | Nie | – | Mapowanie zmiennych BOOL na binary_sensor (tylko `read`). |
| `text_sensors` | Nie | – | Mapowanie zmiennych STRING na text_sensor (tylko `read`). Zmienne czasowe (TIME, DATE, TIME_OF_DAY, DATE_AND_TIME) pokazują tekst IEC (patrz „Typy czasowe”). |
| `switches` | Nie | – | Mapowanie zmiennych BOOL na switch (tylko `write`). Opcje: `restore_value` (domyślnie `false`) i `initial_value`. |
| `numbers` | Nie | – | Mapowanie zmiennych numerycznych (lub STRING) na number (tylko `write`). Wymaga `min_value` i `max_value`, opcjonalnie `step` (domyślnie `1`), `restore_value` (domyślnie `false`) i `initial_value`. |
| `rejected_packets` | Nie | tworzony automatycznie | Sensor liczby odrzuconych ramek (błędna walidacja). |
| `rx_packets` | Nie | tworzony automatycznie | Sensor liczby poprawnie odebranych ramek. |
| `tx_packets` | Nie | tworzony automatycznie | Sensor liczby wysłanych ramek. |
| `rx_errors` | Nie | tworzony automatycznie | Sensor liczby błędów odbioru. |
| `tx_errors` | Nie | tworzony automatycznie | Sensor liczby błędów wysyłki. |

### Definicja zmiennych

Każda zmienna w sekcji `variables` wymaga:

- `name` – nazwa zmiennej (unikalna w obrębie listy).
- `type` – typ NetVar (np. `BOOL`, `UINT`, `REAL`, `STRING(20)`).
- `byte_offset` – offset w bajtach w ładunku (0–1451); alias: `offset` (nazwa używana w integracji Home Assistant). **Lista pakowana:** pole wymagane; w trybie `read` pozwala pominąć odczyt wybranych zmiennych (luki w ładunku), w trybie `write` bajty bez zmiennej pozostają zerami. **Lista niepakowana:** opcjonalny, to tylko miejsce w buforze komponentu (domyślnie bezpośrednio za poprzednimi zmiennymi).

Pola dodatkowe:

- `index` – tylko lista niepakowana: numer zmiennej w liście sterownika (bajty 10–11 ramki). Domyślnie kolejna wolna liczba od `0`; powtórzony `index` jest błędem. W liście pakowanej jest ignorowany.
- `require_value` – tylko lista `write` niepakowana: nie wysyłaj tej zmiennej, dopóki nie ma wartości (patrz „Wartości początkowe i przywracanie”).

Układ pamięci jest sprawdzany raz, podczas walidacji konfiguracji (Python). Nakładające się zmienne i przekroczenie 1452 bajtów ładunku powodują błąd konfiguracji.

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

#### Przykład 1b: Dwie listy odczytu na tym samym porcie

```yaml
wago_netvar:
  - id: wago_read_100
    ip_address: "192.168.1.50"
    port: 1202
    list_id: 100
    direction: read
    variables:
      - name: "temp"
        type: "REAL"
        byte_offset: 0
    sensors:
      - variable: "temp"
        name: "Temperatura"

  - id: wago_read_101
    ip_address: "192.168.1.50"
    port: 1202                 # ten sam port co lista 100 – nie koliduje, ramki rozdziela list_id
    list_id: 101
    direction: read
    variables:
      - name: "pressure"
        type: "REAL"
        byte_offset: 0
    sensors:
      - variable: "pressure"
        name: "Ciśnienie"
```

#### Przykład 2: Zapis (write) z wysyłką przy zmianie

```yaml
wago_netvar:
  id: wago_write
  ip_address: "192.168.1.50"   # Adres sterownika WAGO
  port: 1202
  list_id: 20
  direction: write
  on_change: true
  min_interval: "1000ms"
  update_interval: "5s"
  variables:
    - name: "setpoint"
      type: "REAL"
      byte_offset: 0
    - name: "pump_on"
      type: "BOOL"
      byte_offset: 4
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
      byte_offset: 0
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

Dostępne przeciążenia `set_variable_value(nazwa, wartość)`: `bool`, dowolny typ arytmetyczny (`int`, `uint32_t`, `float`, `double`, …), `std::string` oraz `const char *`. `get_variable_value(nazwa)` zwraca ostatnią wartość zmiennej jako tekst.

#### Przykład 5: Lista niepakowana (odczyt i zapis)

Lista niepakowana odpowiada ustawieniu w sterowniku „Pack variables” wyłączone: każda zmienna jest osobną ramką.

```yaml
wago_netvar:
  - id: plc_niepakowana_odczyt
    ip_address: "192.168.17.45"
    list_id: 131
    direction: read
    pack_variables: false
    variables:
      - name: temp_zadana
        type: INT
        index: 2        # numer zmiennej w liście sterownika
      - name: temp_aktualna
        type: INT
        index: 9
    sensors:
      - name: "Temperatura zadana"
        variable: temp_zadana
      - name: "Temperatura aktualna"
        variable: temp_aktualna

  - id: plc_niepakowana_zapis
    ip_address: "192.168.17.45"
    list_id: 132
    direction: write
    pack_variables: false
    on_change: true
    variables:
      - name: pompa
        type: BOOL
        require_value: true   # nie wysyłaj, dopóki pompa nie ma wartości
      - name: nastawa
        type: INT
    switches:
      - name: "Pompa"
        variable: pompa
        restore_value: true
    numbers:
      - name: "Nastawa"
        variable: nastawa
        min_value: 0
        max_value: 100
        initial_value: 20
```

#### Przykład 6: Odczyt z timeoutem i typami czasowymi

```yaml
wago_netvar:
  - id: plc_czas
    ip_address: "192.168.17.45"
    list_id: 140
    direction: read
    timeout: 30s            # po 30 s bez ramki encje przechodzą w stan "unknown"
    variables:
      - name: data_czas
        type: DT
        byte_offset: 0
      - name: czas_pracy
        type: TIME
        byte_offset: 4
      - name: licznik
        type: UDINT
        byte_offset: 8
    text_sensors:
      - name: "Zegar PLC"           # np. DT#2026-11-08-12:34:56
        variable: data_czas
      - name: "Czas pracy"          # np. T#1d2h3m4s500ms
        variable: czas_pracy
    sensors:
      - name: "Licznik"             # zmienna liczbowa jako surowa liczba
        variable: licznik
```

Jedna zmienna może mieć tylko jedną encję: `variable` nie może powtórzyć się między `sensors` i `text_sensors`.

#### Przykład 7: Wysyłka na żądanie (akcja `wago_netvar.send`)

```yaml
button:
  - platform: template
    name: "Wyślij listę teraz"
    on_press:
      - wago_netvar.send: plc_zapis      # id listy write

# albo w lambdzie:
#   id(plc_zapis).send_now();
```

Akcja wysyła listę niezależnie od `on_change` i `update_interval`, ale respektuje `require_value(s)`.

## Wartości początkowe i przywracanie

Dotyczy list `write`. Każda zmienna sterowana encją `switch` lub `number` może mieć:

- `initial_value` – wartość używana po starcie, gdy nie ma zapisanej,
- `restore_value: true` – po restarcie przywracany jest ostatni stan encji (zapisywany w pamięci nieulotnej przy każdej zmianie z poziomu encji), publikowany w Home Assistant i **wysyłany do sterownika**. Gdy zapisanego stanu nie ma, używane jest `initial_value`.

Wbudowane opcje przełącznika ESPHome (`restore_mode`) są ignorowane: używaj `restore_value`.

Domyślnie (bez `restore_value` i `initial_value`) bufor po starcie zawiera zera i zera są wysyłane. Żeby tego uniknąć, użyj blokady:

- **lista pakowana:** `require_values: true` – lista nie jest wysyłana (także cyklicznie i przez akcję `send`), dopóki każda zmienna nie ma wartości (z `initial_value`, przywróconej albo ustawionej w czasie pracy),
- **lista niepakowana:** `require_value: true` przy zmiennej – ramka tej zmiennej jest pomijana, dopóki zmienna nie ma wartości; pozostałe zmienne są wysyłane normalnie.

Powód różnicy: lista pakowana jest wysyłana jako jedna ramka, więc blokada musi dotyczyć całej listy, a lista niepakowana składa się z niezależnych ramek.

Zmienne bez encji (ustawiane lambdą) mają wartość od pierwszego `set_variable_value()`. Blokada jest zgłaszana w logu jednorazowo (ostrzeżenie).

## Timeout odbioru

Dotyczy list `read` z opcją `timeout`. Gdy przez podany czas nie nadejdzie ramka obejmująca zmienną (w liście pakowanej: żadna ramka listy; w niepakowanej: ramka tej zmiennej), jej encje przechodzą w stan „unknown” (sensor: NaN; binary_sensor i text_sensor: `invalidate_state()`, o ile dana wersja ESPHome je udostępnia, w przeciwnym razie zachowują ostatnią wartość). Komunikat w logu pojawia się jednorazowo, a po powrocie danych encje są odświeżane.

## Typy czasowe

Zmienne TIME, DATE, TIME_OF_DAY (TOD) i DATE_AND_TIME (DT) można mapować na `sensor` (surowa liczba: ms albo sekundy) lub na `text_sensor` (tekst w zapisie IEC 61131-3, taki sam jak w integracji HA):

| Typ | Surowa wartość | Przykład tekstu |
|-----|----------------|-----------------|
| TIME | milisekundy | `T#0ms`, `T#1s500ms`, `T#49d17h2m47s295ms` (części zerowe pomijane) |
| DATE | sekundy od 1970-01-01 | `D#2026-11-08` |
| TIME_OF_DAY | milisekundy od północy | `TOD#12:34:56`, `TOD#12:34:56.789` (milisekundy tylko gdy niezerowe) |
| DATE_AND_TIME | sekundy od 1970-01-01 | `DT#2026-11-08-12:34:56` |

DATE i DT są wyświetlane bez przeliczania strefy czasowej (tak, jak je przechowuje sterownik). Zapis zmiennych czasowych odbywa się surową liczbą przez `number`.

## Walidacja ramek (tryb read)

Wszystkie listy `read` na jednym porcie korzystają z jednego gniazda. Każdy datagram jest odbierany raz, a następnie przekazywany do list o zgodnym `list_id` (bajty 8–9); ramka jest dekodowana przez listę, która przejdzie pełną walidację. Każda lista walidowana jest pod kątem:

- adresu IP nadawcy (jeśli `ip_address` różne od `0.0.0.0`),
- portu docelowego (niejawnie przez bindowanie gniazda UDP do `port`),
- magic bytes (bajty 0–3),
- list_ID (bajty 8–9),
- frame_size (bajty 14–15) zgodnie z rozmiarem otrzymanego pakietu,
- **lista pakowana:** `var_index = 0`, var_count (bajty 12–13) zgodny z konfiguracją, rozmiar ładunku obejmujący wszystkie zdefiniowane zmienne (ramka może być dłuższa – zmienne za ostatnią zdefiniowaną są pomijane; z opcją `payload_size` rozmiar musi być dokładny),
- **lista niepakowana:** `var_count = 1`; jeżeli zmienna o danym `var_index` jest zdefiniowana, ładunek musi mieć dokładnie jej rozmiar. Ramka zmiennej niezdefiniowanej jest poprawna, ale pomijana (liczy się do `rx_packets`).

Zliczanie odrzuconych ramek (`rejected_packets`) przy współdzielonym porcie:

- ramka, której nie da się przypisać do listy (za duża, za krótka, błędne magic bytes) lub o `list_id`, którego żadna lista tego portu nie obsługuje – jest zliczana na wszystkich listach `read` tego portu,
- ramka o `list_id` obsługiwanym przez listy tego portu, której żadna z nich nie przyjęła (np. zły adres nadawcy, `var_count`, `frame_size`) – jest zliczana tylko na listach o tym `list_id`.

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
- Liczby zapisywane do zmiennej STRING są zamieniane na tekst ASCII: wartości całkowite bez części ułamkowej i wykładnika (`12`, `1000000`), pozostałe z dokładnością do 7 cyfr znaczących (`7.5`). Tekst dłuższy niż `n` znaków jest ucinany (ostrzeżenie w logu), a tekst zawierający znaki spoza ASCII jest w całości odrzucany (ostrzeżenie w logu).
- Odczytany tekst STRING kończy się na pierwszym bajcie `0x00`. Bajty spoza ASCII (>127) są zamieniane na `?`, dzięki czemu `text_sensor` zawsze dostaje poprawny tekst.
- Tekst zapisywany do zmiennej liczbowej musi być w całości poprawną liczbą (np. `12abc` jest odrzucane). Dla BOOL akceptowane są: `1/0`, `true/false`, `on/off`, `yes/no` (bez rozróżniania wielkości liter).
- **Ograniczenie precyzji:** sensory i number ESPHome używają liczb `float` (24 bity mantysy). Wartości typów 32-bitowych (DWORD, UDINT, DINT, TIME, DATE, TIME_OF_DAY, DATE_AND_TIME) powyżej 16 777 216 są w encji `sensor` zaokrąglane (np. UDINT `16777217` pokaże `16777216`). Typy BOOL, BYTE, USINT, SINT, WORD, UINT, INT oraz REAL (32-bitowy float) nie są tym dotknięte. Pełną wartość można odczytać w lambdzie przez `get_variable_value()`, a dla typów czasowych pełną dokładność daje `text_sensor`. Integracja Home Assistant nie ma tego ograniczenia (liczby całkowite są dokładne).
- Nieskończona lub nieliczbowa wartość REAL (NaN/Inf) jest publikowana jako NaN (stan „unknown”).
- Offsety zmiennych (`byte_offset`) w liście pakowanej zawsze podaje się ręcznie – w trybie `read` pozwala to pominąć odczyt niektórych zmiennych (luki w ładunku), w trybie `write` odwzorować układ listy zmiennych sterownika.
- W trybie `write` bufor po starcie zawiera zera, o ile zmienna nie ma `initial_value` ani przywróconego stanu. Patrz „Wartości początkowe i przywracanie” (opcje `restore_value`, `require_value(s)`).
- Wysyłka i odbiór działają dopiero po uzyskaniu połączenia sieciowego; gniazdo UDP jest otwierane automatycznie (z ponawianiem co 5 s, także po błędzie odbioru).

## Wsparcie platform

Komponent działa zarówno na Arduino, jak i ESP-IDF. Warstwa transportu UDP jest abstrakcyjna (`UdpTransport`), a implementacje platformowe znajdują się wyłącznie w pliku `.cpp`, co ułatwia dodanie kolejnych platform. Komponent implementuje `dump_config()` i inicjalizuje się po sieci (`AFTER_WIFI`).

## Różnice względem integracji Home Assistant

Obie implementacje stosują te same reguły ramki, walidacji (w tym zliczania `rejected_packets`) i konwersji wartości. Różnice wynikają z platform:

| Obszar | ESPHome | Home Assistant |
|--------|---------|----------------|
| Konfiguracja | YAML ESPHome, zmienne i encje w jednym bloku | ręczny YAML w `configuration.yaml`, bez interfejsu graficznego |
| Kierunek listy | `direction: read/write` (alias `mode`) | `mode: read/write` (alias `direction`) |
| Wysyłka przy zmianie | `on_change: true/false` | `on_change: true/false` |
| Offset zmiennej | `byte_offset` (alias `offset`) | `offset` (alias `byte_offset`) |
| Jednostki czasu | tekst z jednostką: `"500ms"`, `"5s"` | tekst z jednostką (`"500ms"`, `"5s"`, `"2min"`, `"never"`) albo liczba: `min_interval` w ms, `update_interval` i `timeout` w s |
| Typ encji | `sensors`, `binary_sensors`, `text_sensors`, `switches`, `numbers` | pole `entity` przy każdej zmiennej |
| STRING | odczyt: `text_sensor`, zapis: `number` | odczyt: `sensor`, zapis: `number` |
| Typy czasowe jako tekst IEC | `text_sensor` (surowa liczba: `sensor`) | `sensor` z `format: text` (domyślnie) albo `format: raw` |
| Listy niepakowane (`pack_variables: false`) | obsługiwane | obsługiwane |
| `payload_size`, `timeout`, `require_values`, `require_value`, `initial_value`, `restore_value` | obsługiwane | obsługiwane |
| Wysyłka na żądanie | akcja `wago_netvar.send` lub `send_now()` | usługa `wago_netvar.send_list` |
| Przywracanie stanu | pamięć nieulotna ESPHome (`restore_value`) | `RestoreEntity` Home Assistant (`restore_value`) |
| Port lokalny listy `write` | port przydzielony przez system | port przydzielony przez system (osobne gniazdo wysyłające) |
| Własny broadcast na ten sam port | nie dotyczy (inne urządzenie) | rozpoznawany i pomijany przez listy `read` |
| Precyzja liczb 32-bitowych | `float` (do 16 777 216 dokładnie) | dokładna (liczby całkowite Pythona) |
| REAL będący NaN/Inf przy odbiorze | stan „unknown” (NaN) | ramka przyjęta, stan encji bez zmian, `rx_errors` +1 |

## Zmiany

- Opcja `send_on_change` została przemianowana na `on_change` (zgodnie z założeniami projektu i integracją HA); stara nazwa działa jako alias.
- Odczyt STRING: bajty spoza ASCII są zamieniane na `?`.
- Zapis liczby do STRING: wartości całkowite są zapisywane bez części ułamkowej (`12`, a nie `12.0`) i bez notacji wykładniczej.
- Dodano listy niepakowane (`pack_variables: false`, `index`), opcje `payload_size`, `timeout`, `require_values`, `require_value`, `initial_value`, `restore_value`, akcję `wago_netvar.send` oraz tekstowy zapis typów czasowych (`text_sensor`). Przy ramkach pakowanych sprawdzane jest teraz `var_index = 0` (zgodnie z integracją HA).
- Aliasy nazw zgodne z integracją HA: `mode` (= `direction`) i `offset` (= `byte_offset`).
- Opis ograniczenia precyzji liczb 32-bitowych (sekcja „Uwagi dotyczące typów danych i konwersji”).
