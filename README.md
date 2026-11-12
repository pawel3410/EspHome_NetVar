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
| 4–7   | message_type      | Typ wiadomości (w dokumentacji 3S pole „ID”; nie mylić z list_ID): `0` = dane zmiennych sieciowych. Inne wartości oznaczają potwierdzenia, prośby bootup i wymianę parametrów (SDO). Przy odbiorze ramki z wartością różną od `0` są odrzucane, przy wysyłce `0` |
| 8–9   | list_ID           | Identyfikator listy (uint16 LE)                                                               |
| 10–11 | var_index         | Numer zmiennej w liście niepakowanej (lista pakowana: `0`)                                    |
| 12–13 | var_count         | Liczba zmiennych na liście (uint16 LE)                                                        |
| 14–15 | frame_size        | Całkowita długość ramki w bajtach (nagłówek + payload) (uint16 LE)                            |
| 16–17 | sequence_counter  | Licznik sekwencyjny (przy odbiorze odczytywany, nie walidowany; przy wysyłce zwiększany od 1). Lista pakowana: jeden licznik na listę, lista niepakowana: osobny licznik dla każdej zmiennej |
| 18    | flags             | Flagi (bit 0: żądanie potwierdzenia, bit 1: żądanie sprawdzenia sumy, bit 2: błędna suma); przy odbiorze nie walidowane, przy wysyłce `00` |
| 19    | checksum          | Suma kontrolna (algorytm nieznany); przy odbiorze nie walidowana, przy wysyłce `00`           |
| 20–N  | Payload           | Wartości zmiennych zgodnie z układem pamięci                                                  |

Maksymalny rozmiar ramki to 1472 bajty (nagłówek + 1452 bajty ładunku).

Lista **pakowana** (domyślnie) wysyła wszystkie zmienne w jednej ramce: `var_index = 0`, `var_count` równe liczbie zmiennych listy. Lista **niepakowana** (`pack_variables: false`) wysyła każdą zmienną w osobnej ramce: `var_index` to numer zmiennej, `var_count = 1`, a ładunek ma dokładnie rozmiar tej zmiennej.

## Obsługiwane typy danych

Wszystkie typy są little endian. LREAL nie jest obsługiwany przez NetVar.

| Typ NetVar | Rozmiar | Zakres / opis | `entity` (lista `read`) | `entity` (lista `write`) | Typ w Home Assistant |
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
| STRING(n) | n + 1 bajt | Tekst ASCII zakończony nullem (0x00), domyślnie 80 znaków, max 255 | `sensor` (ESPHome tworzy `text_sensor`) | `number` (liczba zapisana jako tekst) lub `set_variable_value()` | `sensor` / `number` |

W YAML typy `TIME_OF_DAY` i `DATE_AND_TIME` można zapisać również jako `TOD` i `DT`. Długość STRING podaje się jak w Home Assistant (`type: STRING`, `length: 20`) albo w nazwie typu (`STRING(20)`); zapis `STRING` bez długości oznacza `STRING(80)`. Typy czasowe w encji `sensor` są domyślnie pokazywane jako tekst IEC (patrz „Typy czasowe”).

## Konfiguracja ESPHome

Konfiguracja jest **taka sama jak w integracji Home Assistant** (te same nazwy parametrów, te same domyślne wartości). Rodzaj encji podaje się bezpośrednio przy zmiennej parametrem `entity` (`sensor`, `binary_sensor`, `switch`, `number`); osobnych list `sensors:`, `switches:` itd. nie ma, a pole `platform:` nie jest używane. Parametr `name` zmiennej jest nazwą encji.

```yaml
wago_netvar:
  - ip_address: "192.168.17.45"
    list_id: 124
    mode: read
    variables:
      - name: "Temperatura"
        unique_id: "temperatura"
        offset: 0
        type: REAL
        entity: sensor
        device_class: temperature
        state_class: measurement
        unit_of_measurement: "°C"
```

### Parametry listy

| Opcja | Wymagana | Domyślna | Opis |
|-------|----------|----------|------|
| `id` | Nie | generowany | Identyfikator komponentu ESPHome. Potrzebny tylko wtedy, gdy odwołujesz się do listy w lambdzie lub akcji `wago_netvar.send`. |
| `ip_address` | Tak | – | Adres IPv4 nadawcy (`read`) albo odbiorcy (`write`). W trybie `read` może być `0.0.0.0` (dowolny nadawca); `255.255.255.255` jest tam niedozwolony. W trybie `write` nie może być `0.0.0.0` (dozwolony broadcast `255.255.255.255`). |
| `port` | Nie | `1202` | Port UDP: w trybie `read` port nasłuchu (może być wspólny dla wielu list `read`), w trybie `write` port docelowy. |
| `list_id` | Tak | – | Identyfikator listy NetVar (bajty 8–9 ramki). W trybie `read` rozdziela ramki między listy współdzielące port. |
| `mode` | Nie | `read` | `read` (odbiór) lub `write` (wysyłka). Alias: `direction`. |
| `pack_variables` | Nie | `true` | `true`: lista pakowana (jedna ramka z wszystkimi zmiennymi). `false`: lista niepakowana (każda zmienna w osobnej ramce z `var_index`, `var_count = 1`). |
| `var_count` | Nie | liczba zmiennych | Lista pakowana: liczba zmiennych na liście NetVar (bajty 12–13). Może być **większa** niż liczba zdefiniowanych zmiennych (część zmiennych listy jest pomijana), ale nie mniejsza. W liście niepakowanej ignorowana. |
| `payload_size` | Nie | – | Tylko lista pakowana: dokładny rozmiar ładunku w bajtach (nie mniejszy niż wynika z offsetów, max 1452). Przy odbiorze ramka o innym rozmiarze ładunku jest odrzucana; bez tej opcji akceptowane są ramki dłuższe. Przy wysyłce określa rozmiar ładunku. |
| `on_change` | Nie | `false` | Wysyłaj przy zmianie wartości (`write`). Patrz „Wysyłanie”. Alias: `send_on_change`. |
| `min_interval` | Nie | `500ms` | Minimalny odstęp między wysyłkami przy `on_change` (osobno dla każdej ramki: listy albo zmiennej). Liczba bez jednostki oznacza milisekundy; można podać też `"500ms"`, `"1s"`, `"2min"`. |
| `update_interval` | Nie | `5s` | Cykliczna wysyłka całej listy (`write`). `0`, `0s` albo `never` wyłącza. Liczba bez jednostki oznacza sekundy. W trybie `read` jest ignorowany. |
| `timeout` | Nie | `0s` | Tylko `read`: po tym czasie bez ramki encje zmiennej przechodzą w stan „unknown” (patrz „Timeout odbioru”). Liczba bez jednostki oznacza sekundy. `0` wyłącza. |
| `require_values` | Nie | `false` | Tylko `write`, lista pakowana: nie wysyłaj listy, dopóki **każda** zmienna nie ma wartości (patrz „Wartości początkowe i przywracanie”). W liście niepakowanej użycie jest błędem: służy do tego `require_value` przy zmiennych. |
| `variables` | Tak | – | Lista zmiennych z encjami. |

### Parametry zmiennej

| Opcja | Dotyczy | Opis |
|-------|---------|------|
| `name` | wszystkie | Nazwa zmiennej i encji (unikalna w obrębie listy). |
| `unique_id` | wszystkie | Stabilny identyfikator zmiennej (domyślnie wyznaczany z nazwy: małe litery, cyfry i `_`). Używany m.in. do przywracania stanu (`restore_value`), więc zmiana nazwy encji nie gubi zapisanego stanu, oraz w lambdach (`set_variable_value("unique_id", ...)`). Powtórzony `unique_id` w liście jest błędem. |
| `entity` | wszystkie | `sensor`, `binary_sensor` (lista `read`), `switch`, `number` (lista `write`). |
| `type` | wszystkie | Typ NetVar (tabela typów). Zgodność typ–encja: BOOL tylko `binary_sensor`/`switch`, pozostałe typy `sensor`/`number`. |
| `length` | STRING | Długość tekstu w znakach (1..255), domyślnie 80. |
| `offset` | wszystkie | Pozycja w ładunku w bajtach (0–1451). Alias: `byte_offset`. **Lista pakowana:** wymagany (w `read` pozwala pominąć wybrane zmienne, w `write` bajty bez zmiennej pozostają zerami). **Lista niepakowana:** opcjonalny, to tylko miejsce w buforze (domyślnie za poprzednimi zmiennymi). |
| `index` | lista niepakowana | Numer zmiennej w liście sterownika (bajty 10–11). Domyślnie kolejna wolna liczba od `0`; powtórzony `index` jest błędem. |
| `format` | `sensor` typu TIME, DATE, TOD, DT | `text` (domyślnie): tekst IEC, `raw`: surowa liczba. |
| `initial_value` | `switch`, `number` | Wartość początkowa bufora i encji. Podanie go jawnie oznacza też, że zmienna „ma wartość” od startu. |
| `restore_value` | `switch`, `number` | `true`: po restarcie przywracany jest ostatni stan encji i wysyłany do PLC. Domyślnie `false`. |
| `require_value` | zmienne listy `write` niepakowanej | Nie wysyłaj tej zmiennej, dopóki nie ma wartości. W liście pakowanej jest błędem (użyj `require_values` listy). |
| `min`, `max`, `step` | `number` | Zakres i krok. Dla typów całkowitych domyślnie zakres typu (nie może go przekraczać), dla `REAL` i `STRING` `min` i `max` są wymagane; `step` domyślnie `1`. Aliasy: `min_value`, `max_value`. |
| `zero_as_unavailable` | `sensor` | Zero jest publikowane jako „unknown”. |
| `device_class`, `state_class`, `unit_of_measurement`, `icon`, `entity_category`, `force_update` | wg schematu encji | Jak w ESPHome (nazwy takie same jak w Home Assistant). |
| `suggested_display_precision` | `sensor` | Liczba miejsc po przecinku (w ESPHome: `accuracy_decimals`, nie podawaj obu). |
| `enabled_by_default` | wszystkie | `false` = encja domyślnie wyłączona (w ESPHome: `disabled_by_default`, odwrotnie; nie podawaj obu). |
| `inverted` | `binary_sensor`, `switch` | Odwraca wartość BOOL (dla `binary_sensor` jako filtr `invert`, dla `switch` natywne `inverted`). |
| `mode` | `number` | Sposób wyświetlania: `auto`, `box`, `slider`. |

Oprócz parametrów wspólnych zmienna przyjmuje **wszystkie parametry schematu encji ESPHome** danego rodzaju, np. `filters`, `on_value`, `on_raw_value`, `internal`, `web_server`, `accuracy_decimals`, `disabled_by_default`, `device_id` (dla `sensor`, `text_sensor`, `binary_sensor`, `switch`, `number`). Dla STRING i typów czasowych z `format: text` ESPHome tworzy `text_sensor`, więc obowiązuje jego schemat.

Układ pamięci jest sprawdzany raz, podczas walidacji konfiguracji (Python). Nakładające się zmienne i przekroczenie 1452 bajtów ładunku powodują błąd konfiguracji.

W lambdach zmienną wskazuje się przez `unique_id`, np. `id(plc_zapis).set_variable_value("temp_zadana", 21.5);`.

### Przykłady konfiguracji

#### Przykład 1: Odczyt (read) z ręcznymi offsetami

```yaml
wago_netvar:
  - ip_address: "0.0.0.0"       # akceptuj pakiety z dowolnego IP
    port: 1202
    list_id: 10
    mode: read
    var_count: 6                # lista w sterowniku ma 6 zmiennych, tu odczytujemy tylko 3
    timeout: 30s                # po 30 s bez ramki encje przechodzą w stan "unknown"
    variables:
      - name: "Temperatura"
        unique_id: temperatura
        offset: 0
        type: REAL
        entity: sensor
        device_class: temperature
        state_class: measurement
        unit_of_measurement: "°C"
        suggested_display_precision: 1
      - name: "Pompa pracuje"
        offset: 4
        type: BOOL
        entity: binary_sensor
        device_class: running
      - name: "Komunikat"
        offset: 6               # bajty 5 pomijamy
        type: STRING
        length: 20
        entity: sensor          # STRING w liście read: ESPHome tworzy text_sensor
```

#### Przykład 1b: Dwie listy odczytu na tym samym porcie

```yaml
wago_netvar:
  - ip_address: "0.0.0.0"
    list_id: 100
    mode: read
    variables:
      - {name: "Licznik A", offset: 0, type: UDINT, entity: sensor}
  - ip_address: "0.0.0.0"
    list_id: 101               # ten sam port 1202, inny list_id: jedno wspólne gniazdo
    mode: read
    variables:
      - {name: "Licznik B", offset: 0, type: UDINT, entity: sensor}
```

#### Przykład 2: Zapis (write) z wysyłką przy zmianie i cyklicznie

```yaml
wago_netvar:
  - id: plc_zapis
    ip_address: "192.168.17.45"
    list_id: 124
    mode: write
    on_change: true            # wysyłaj po zmianie wartości
    min_interval: 200ms        # ale nie częściej niż co 200 ms
    update_interval: 30s       # oraz cyklicznie co 30 s
    variables:
      - name: "Pompa"
        unique_id: pompa
        offset: 0
        type: BOOL
        entity: switch
        restore_value: true
      - name: "Temperatura zadana"
        unique_id: temp_zadana
        offset: 1
        type: REAL
        entity: number
        min: 5
        max: 35
        step: 0.5
        initial_value: 20
        restore_value: true
        unit_of_measurement: "°C"
```

#### Przykład 3: Wysyłka broadcast

```yaml
wago_netvar:
  - ip_address: "255.255.255.255"
    list_id: 200
    mode: write
    update_interval: 5s
    variables:
      - {name: "Licznik", offset: 0, type: UDINT, entity: number, min: 0, max: 4294967295}
```

#### Przykład 4: Zapis z lambdy

```yaml
interval:
  - interval: 10s
    then:
      - lambda: |-
          id(plc_zapis).set_variable_value("temp_zadana", 21.5);
          id(plc_zapis).set_variable_value("pompa", true);
```

#### Przykład 5: Lista niepakowana (odczyt i zapis)

Lista niepakowana odpowiada ustawieniu w sterowniku „Pack variables” wyłączone: każda zmienna jest osobną ramką.

```yaml
wago_netvar:
  - ip_address: "192.168.17.45"
    list_id: 131
    mode: read
    pack_variables: false
    variables:
      - {name: "Temperatura zadana", index: 2, type: INT, entity: sensor}     # index = numer w liście sterownika
      - {name: "Temperatura aktualna", index: 9, type: INT, entity: sensor}

  - ip_address: "192.168.17.45"
    list_id: 132
    mode: write
    pack_variables: false
    on_change: true            # wysyłana jest tylko zmieniona zmienna
    variables:
      - name: "Pompa"
        type: BOOL
        entity: switch
        require_value: true    # nie wysyłaj, dopóki pompa nie ma wartości
        restore_value: true
      - name: "Nastawa"
        type: INT
        entity: number
        min: 0
        max: 100
        initial_value: 20
```

#### Przykład 6: Typy czasowe

```yaml
wago_netvar:
  - ip_address: "192.168.17.45"
    list_id: 140
    mode: read
    variables:
      - {name: "Zegar PLC", offset: 0, type: DT, entity: sensor}                       # tekst, np. DT#2026-11-08-12:34:56
      - {name: "Czas pracy", offset: 4, type: TIME, entity: sensor}                    # tekst, np. T#1d2h3m4s500ms
      - {name: "Czas cyklu [ms]", offset: 8, type: TIME, entity: sensor, format: raw}  # surowa liczba milisekund
```

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

Akcja wysyła listę (w liście niepakowanej: wszystkie zmienne) niezależnie od `on_change` i `update_interval`, ale respektuje `require_value(s)`.

## Wysyłanie (`on_change`, `min_interval`, `update_interval`)

Lista `write` jest wysyłana w dwóch niezależnych trybach, które można włączyć osobno albo razem:

| Ustawienie | Zachowanie |
|------------|------------|
| tylko `on_change: true` (`update_interval: never`) | Ramka jest wysyłana po zmianie wartości. Pierwsza wysyłka nie jest ograniczona; kolejna nie wcześniej niż `min_interval` po poprzedniej. Zmiana w trakcie odstępu jest wysyłana dopiero po jego upływie, a kilka zmian w tym czasie trafia do jednej ramki (ostatnie wartości). Ustawienie tej samej wartości nie jest zmianą i niczego nie wysyła. `min_interval: 0` wysyła każdą zmianę w następnej iteracji pętli. |
| tylko `update_interval` (`on_change: false`) | Co `update_interval` wysyłana jest cała lista (w liście niepakowanej: wszystkie zmienne), niezależnie od zmian. Zmiany same niczego nie wysyłają. |
| oba | Cykliczna wysyłka niesie aktualne wartości, czyli także niewysłane zmiany, i zeruje ich oczekiwanie. Każda wysyłka (także cykliczna) liczy się jako „poprzednia” dla `min_interval`, więc zmiana tuż po wysyłce cyklicznej czeka do upływu `min_interval`. |

**Lista pakowana:** zmiana dowolnej zmiennej powoduje wysłanie całej listy (jedna ramka); `min_interval` i `sequence_counter` dotyczą listy.

**Lista niepakowana:** po zmianie wysyłana jest **tylko ta zmienna, która zmieniła wartość** (własna ramka), a `min_interval` jest liczony osobno dla każdej zmiennej (zmiany różnych zmiennych nie opóźniają się wzajemnie). Każda zmienna ma własny `sequence_counter` (bajty 16–17), zwiększany przy każdej wysłanej ramce tej zmiennej.

Nieudana wysyłka (błąd gniazda) jest ponawiana najwyżej raz na `min_interval` i liczona w `tx_errors`. Blokady `require_value(s)` opisano w „Wartości początkowe i przywracanie”.

## Wartości początkowe i przywracanie

Dotyczy list `write`. Każda zmienna sterowana encją `switch` lub `number` może mieć:

- `initial_value` – wartość używana po starcie, gdy nie ma zapisanej,
- `restore_value: true` – po restarcie przywracany jest ostatni stan encji (zapisywany w pamięci nieulotnej przy każdej zmianie z poziomu encji), publikowany w Home Assistant i **wysyłany do sterownika**. Gdy zapisanego stanu nie ma, używane jest `initial_value`.

Wbudowane opcje przełącznika ESPHome (`restore_mode`) są ignorowane: używaj `restore_value`.

Domyślnie (bez `restore_value` i `initial_value`) bufor po starcie zawiera zera i zera są wysyłane. Żeby tego uniknąć, użyj blokady:

- **lista pakowana:** `require_values: true` – lista nie jest wysyłana (także cyklicznie i przez akcję `send`), dopóki każda zmienna nie ma wartości (z `initial_value`, przywróconej albo ustawionej w czasie pracy),
- **lista niepakowana:** `require_value: true` przy zmiennej – ramka tej zmiennej jest pomijana, dopóki zmienna nie ma wartości; pozostałe zmienne są wysyłane normalnie.

Powód różnicy: lista pakowana jest wysyłana jako jedna ramka, więc blokada musi dotyczyć całej listy, a lista niepakowana składa się z niezależnych ramek.

Zmienna ustawiona lambdą (`set_variable_value()`) ma wartość od pierwszego wywołania. Blokada jest zgłaszana w logu jednorazowo (ostrzeżenie).

## Timeout odbioru

Dotyczy list `read` z opcją `timeout`. Gdy przez podany czas nie nadejdzie ramka obejmująca zmienną (w liście pakowanej: żadna ramka listy; w niepakowanej: ramka tej zmiennej), jej encje przechodzą w stan „unknown” (sensor: NaN; binary_sensor i text_sensor: `invalidate_state()`, o ile dana wersja ESPHome je udostępnia, w przeciwnym razie zachowują ostatnią wartość). Komunikat w logu pojawia się jednorazowo, a po powrocie danych encje są odświeżane.

## Typy czasowe

Zmienne TIME, DATE, TIME_OF_DAY (TOD) i DATE_AND_TIME (DT) w encji `sensor` są domyślnie pokazywane jako tekst w zapisie IEC 61131-3 (`format: text`, ESPHome tworzy wtedy `text_sensor`, taki sam jak w integracji HA). Z `format: raw` sensor pokazuje surową liczbę (ms albo sekundy). Zapis zmiennych czasowych odbywa się surową liczbą przez `number`.

| Typ | Surowa wartość | Przykład tekstu |
|-----|----------------|-----------------|
| TIME | milisekundy | `T#0ms`, `T#1s500ms`, `T#49d17h2m47s295ms` (części zerowe pomijane) |
| DATE | sekundy od 1970-01-01 | `D#2026-11-08` |
| TIME_OF_DAY | milisekundy od północy | `TOD#12:34:56`, `TOD#12:34:56.789` (milisekundy tylko gdy niezerowe) |
| DATE_AND_TIME | sekundy od 1970-01-01 | `DT#2026-11-08-12:34:56` |

DATE i DT są wyświetlane bez przeliczania strefy czasowej (tak, jak je przechowuje sterownik).

## Walidacja ramek (tryb read)

Wszystkie listy `read` na jednym porcie korzystają z jednego gniazda. Każdy datagram jest odbierany raz, a następnie przekazywany do list o zgodnym `list_id` (bajty 8–9); ramka jest dekodowana przez listę, która przejdzie pełną walidację. Każda lista walidowana jest pod kątem:

- adresu IP nadawcy (jeśli `ip_address` różne od `0.0.0.0`),
- portu docelowego (niejawnie przez bindowanie gniazda UDP do `port`),
- magic bytes (bajty 0–3),
- message_type (bajty 4–7) równy `0` (dane zmiennych; potwierdzenia, prośby bootup i SDO są odrzucane),
- list_ID (bajty 8–9),
- frame_size (bajty 14–15) zgodnie z rozmiarem otrzymanego pakietu,
- **lista pakowana:** `var_index = 0`, var_count (bajty 12–13) zgodny z konfiguracją, rozmiar ładunku obejmujący wszystkie zdefiniowane zmienne (ramka może być dłuższa – zmienne za ostatnią zdefiniowaną są pomijane; z opcją `payload_size` rozmiar musi być dokładny),
- **lista niepakowana:** `var_count = 1`; jeżeli zmienna o danym `var_index` jest zdefiniowana, ładunek musi mieć dokładnie jej rozmiar. Ramka zmiennej niezdefiniowanej jest poprawna, ale pomijana (liczy się do `rx_packets`).

Zliczanie odrzuconych ramek (`rejected_packets`) przy współdzielonym porcie:

- ramka, której nie da się przypisać do listy (za duża, za krótka, błędne magic bytes) lub o `list_id`, którego żadna lista tego portu nie obsługuje – jest zliczana na wszystkich listach `read` tego portu,
- ramka o `list_id` obsługiwanym przez listy tego portu, której żadna z nich nie przyjęła (np. zły adres nadawcy, `var_count`, `frame_size`) – jest zliczana tylko na listach o tym `list_id`.

Pola sequence_counter (bajty 16–17), flags (bajt 18) i checksum (bajt 19) są odczytywane, ale nie walidowane. Przy wysyłce flags i checksum są zerowane. Ramki o `message_type` różnym od `0` zwiększają `rejected_packets` (np. prośby bootup, które sterownik rozsyła po restarcie). Każda ramka, która przejdzie walidację, jest w całości dekodowana i publikowana do zmapowanych encji; pozostałe są liczone w `rejected_packets`.

## Automatyczne sensory statystyk

Sensory diagnostyczne tworzone są automatycznie (kategoria encji: diagnostyczna), nazwy zawierają `list_id`:

| Lista | Sensory |
|-------|---------|
| `read` | `rx_packets_<list_id>` (poprawnie odebrane ramki), `rx_errors_<list_id>` (błędy odbioru, błąd gniazda), `rejected_packets_<list_id>` (ramki odrzucone przy walidacji, w tym zbyt duże datagramy) |
| `write` | `tx_packets_<list_id>` (wysłane ramki; w liście niepakowanej każda zmienna to osobna ramka), `tx_errors_<list_id>` (błędy wysyłki) |

Dodatkowo powstaje jeden zestaw sum po wszystkich listach: `total_rx_packets`, `total_rx_errors`, `total_rejected_packets`, `total_tx_packets`, `total_tx_errors` (suma liczników wszystkich list; sensory należą do pierwszej listy w konfiguracji). Gdy dwie listy tego samego kierunku mają ten sam `list_id`, do nazwy drugiej dopisywany jest adres IP (np. `rx_packets_124_10_0_0_9`).

Wartości są publikowane co najwyżej raz na sekundę i tylko po zmianie. Nazw ani ustawień tych sensorów nie można zmieniać w YAML.

## Uwagi dotyczące typów danych i konwersji

- Wszystkie liczby są przesyłane w formacie little endian.
- Wysyłana wartość jest konwertowana na typ zmiennej. Przy przepełnieniu typu całkowitego zachowywane są najmniej znaczące bajty, a nadmiarowe, najbardziej znaczące, są odrzucane (np. `70000` zapisane do INT daje `4464`). Wartości zmiennoprzecinkowe zapisywane do typów całkowitych są zaokrąglane do najbliższej liczby całkowitej.
- Liczby zapisywane do zmiennej STRING są zamieniane na tekst ASCII: wartości całkowite bez części ułamkowej i wykładnika (`12`, `1000000`), pozostałe z dokładnością do 7 cyfr znaczących (`7.5`). Tekst dłuższy niż `n` znaków jest ucinany (ostrzeżenie w logu), a tekst zawierający znaki spoza ASCII jest w całości odrzucany (ostrzeżenie w logu).
- Odczytany tekst STRING kończy się na pierwszym bajcie `0x00`. Bajty spoza ASCII (>127) są zamieniane na `?`, dzięki czemu `text_sensor` zawsze dostaje poprawny tekst.
- Tekst zapisywany do zmiennej liczbowej musi być w całości poprawną liczbą (np. `12abc` jest odrzucane). Dla BOOL akceptowane są: `1/0`, `true/false`, `on/off`, `yes/no` (bez rozróżniania wielkości liter).
- **Ograniczenie precyzji:** sensory i number ESPHome używają liczb `float` (24 bity mantysy). Wartości typów 32-bitowych (DWORD, UDINT, DINT, TIME, DATE, TIME_OF_DAY, DATE_AND_TIME) powyżej 16 777 216 są w encji `sensor` zaokrąglane (np. UDINT `16777217` pokaże `16777216`). Typy BOOL, BYTE, USINT, SINT, WORD, UINT, INT oraz REAL (32-bitowy float) nie są tym dotknięte. Pełną wartość można odczytać w lambdzie przez `get_variable_value()`, a dla typów czasowych pełną dokładność daje `text_sensor`. Integracja Home Assistant nie ma tego ograniczenia (liczby całkowite są dokładne).
- Nieskończona lub nieliczbowa wartość REAL (NaN/Inf) jest publikowana jako NaN (stan „unknown”).
- Offsety zmiennych (`offset`) w liście pakowanej zawsze podaje się ręcznie – w trybie `read` pozwala to pominąć odczyt niektórych zmiennych (luki w ładunku), w trybie `write` odwzorować układ listy zmiennych sterownika.
- W trybie `write` bufor po starcie zawiera zera, o ile zmienna nie ma `initial_value` ani przywróconego stanu. Patrz „Wartości początkowe i przywracanie” (opcje `restore_value`, `require_value(s)`).
- Wysyłka i odbiór działają dopiero po uzyskaniu połączenia sieciowego; gniazdo UDP jest otwierane automatycznie (z ponawianiem co 5 s, także po błędzie odbioru).

## Wsparcie platform

Komponent działa zarówno na Arduino, jak i ESP-IDF. Warstwa transportu UDP jest abstrakcyjna (`UdpTransport`), a implementacje platformowe znajdują się wyłącznie w pliku `.cpp`, co ułatwia dodanie kolejnych platform. Komponent implementuje `dump_config()` i inicjalizuje się po sieci (`AFTER_WIFI`).

## Różnice względem integracji Home Assistant

Konfiguracja listy i zmiennych (nazwy, domyślne wartości, reguły) jest taka sama w obu platformach, a ramki, walidacja (w tym `rejected_packets`), konwersje wartości, `on_change`/`min_interval`/`update_interval`, restore i blokady wysyłki działają tak samo. Pozostałe różnice wynikają z platform:

| Obszar | ESPHome | Home Assistant |
|--------|---------|----------------|
| Identyfikator listy | opcjonalne `id` (do lambd i akcji) | brak |
| Dodatkowe parametry encji | cały schemat encji ESPHome (`filters`, `on_value`, `internal`, `web_server`, ...) | `visible_by_default`, `suggested_unit_of_measurement` |
| Wysyłka na żądanie | akcja `wago_netvar.send` lub `send_now()` | usługa `wago_netvar.send_list` |
| Przywracanie stanu | pamięć nieulotna ESPHome (klucz: `unique_id`) | `RestoreEntity` Home Assistant |
| Precyzja liczb 32-bitowych | `float` (do 16 777 216 dokładnie) | dokładna (liczby całkowite Pythona) |
| REAL będący NaN/Inf przy odbiorze | stan „unknown” (NaN) | ramka przyjęta, stan encji bez zmian, `rx_errors` +1 |
| Własny broadcast na ten sam port | nie dotyczy (inne urządzenie) | rozpoznawany i pomijany przez listy `read` |
| Walidacja konfiguracji | błąd odrzuca całą konfigurację (kompilacja) | błędna lista jest pomijana, reszta działa |
| Stan „niedostępny” po `timeout` | `unknown` (`binary_sensor`/`text_sensor`: o ile wersja ESPHome ma `invalidate_state()`) | `unavailable` |

## Zmiany

- Ramki z typem wiadomości (bajty 4–7, w dokumentacji 3S pole „ID”) różnym od `0` są odrzucane i liczone w `rejected_packets`; opis pól 4–7 i 18–19 ujednolicono z dokumentacją protokołu.

- Opcja `send_on_change` została przemianowana na `on_change` (zgodnie z założeniami projektu i integracją HA); stara nazwa działa jako alias.
- Odczyt STRING: bajty spoza ASCII są zamieniane na `?`.
- Zapis liczby do STRING: wartości całkowite są zapisywane bez części ułamkowej (`12`, a nie `12.0`) i bez notacji wykładniczej.
- Dodano listy niepakowane (`pack_variables: false`, `index`), opcje `payload_size`, `timeout`, `require_values`, `require_value`, `initial_value`, `restore_value`, akcję `wago_netvar.send` oraz tekstowy zapis typów czasowych (`text_sensor`). Przy ramkach pakowanych sprawdzane jest teraz `var_index = 0` (zgodnie z integracją HA).
- Aliasy nazw zgodne z integracją HA: `mode` (= `direction`) i `offset` (= `byte_offset`).
- Opis ograniczenia precyzji liczb 32-bitowych (sekcja „Uwagi dotyczące typów danych i konwersji”).
- **Ujednolicona konfiguracja z integracją HA (zmiana niezgodna wstecz):** rodzaj encji podaje się przy zmiennej (`entity`), usunięto listy `sensors`, `binary_sensors`, `text_sensors`, `switches`, `numbers` i pole `variable`. Nazwy parametrów są takie same jak w HA: `mode` (zamiast `direction`, domyślnie `read`), `offset`, `type` + `length`, `min`/`max`/`step`, `suggested_display_precision`, `enabled_by_default`, `unique_id`, `inverted`, `zero_as_unavailable`; `list_id` jest wymagane, `id` opcjonalne. Dotychczasowe nazwy `direction`, `byte_offset`, `min_value`/`max_value`, `send_on_change` działają jako aliasy.
- Zmienna w lambdach jest wskazywana przez `unique_id` (domyślnie slug nazwy).
- Typy czasowe w `sensor` są domyślnie tekstem IEC (`format: text`); surowa liczba przez `format: raw`.
- `on_change` w liście niepakowanej wysyła tylko zmienioną zmienną; `min_interval` jest liczony osobno dla każdej zmiennej, a `sequence_counter` osobno dla każdej zmiennej (lista pakowana: jeden na listę). Ustawienie tej samej wartości nie wywołuje wysyłki.
- Sensory statystyk: `rx_packets_<list_id>`, `rx_errors_<list_id>`, `rejected_packets_<list_id>` dla list `read` oraz `tx_packets_<list_id>`, `tx_errors_<list_id>` dla list `write`; sumy `total_*`. Klucze `rx_packets`, `tx_packets` itd. w YAML zostały usunięte.
- Nowe parametry zmiennej: `zero_as_unavailable`, `inverted`, `suggested_display_precision`, `enabled_by_default`.
