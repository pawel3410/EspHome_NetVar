# ESPHome Wago NetVar Component

Komponent niestandardowy dla **ESPHome** umożliwiający szybką, dwukierunkową komunikację ze sterownikami PLC firmy **Wago** oraz środowiskiem **CoDeSys** przy użyciu protokołu zmiennych sieciowych (**NetVar over UDP**).

Komponent wykorzystuje płaskie buforowanie pamięci oraz blokowy zrzut pakietów, co zapewnia maksymalną wydajność i minimalizuje narzut na procesor ESP32/ESP8266.

---

## ⚙️ Parametry Konfiguracyjne

| Parametr | Typ | Domyślnie | Opis |
| --- | --- | --- | --- |
| **`id`** | GenerationID | Wymagane | Wewnętrzny identyfikator komponentu ESPHome. |
| **`ip_address`** | String | Wymagane | Adres IP docelowego sterownika Wago PLC. |
| **`port`** | Port | `1202` | Port UDP używany do transmisji NetVar. |
| **`list_ID`** | Integer | `1` | Identyfikator listy zmiennych sieciowych (`list_ID` / NetVar ID w CoDeSys). |
| **`var_count`** | Integer | `0` | Liczba zmiennych w ramce (0 = brak weryfikacji liczby). |
| **`direction`** | String | `"write"` | Kierunek komunikacji: `"read"`, `"write"` lub `"both"`. |
| **`endian`** | String | `"little"` | Kolejność bajtów: `"little"` (domyślnie dla CoDeSys) lub `"big"`. |
| **`pack_bools`** | Boolean | `false` | Czy pakować zmienne typu `BOOL` bitowo w bajtach. |
| **`alignment`** | Boolean | `false` | Wyrównanie pamięci (alignment) do rozmiaru typów. |
| **`update_interval`** | Time | `"10s"` | Cykliczny interwał wysyłania pełnej ramki (heartbeat / stały czas). |
| **`send_on_change`** | Boolean | `false` | Czy wysyłać ramkę natychmiast po zmianie wartości zmiennej. |
| **`min_interval`** | Time | `"100ms"` | Minimalny czas odstępu między ramkami przy włączonym `send_on_change`. |
| **`variables`** | List | Opcjonalne | Lista definicji zmiennych (nazwa i typ IEC 61131-3). |
| **`sensors`** | List | Opcjonalne | Mappowanie zmiennych numerycznych na czujniki ESPHome (`sensor`). |
| **`binary_sensors`** | List | Opcjonalne | Mappowanie zmiennych binarnych na czujniki binarne (`binary_sensor`). |
| **`switches`** | List | Opcjonalne | Mappowanie zmiennych na przełączniki (`switch`). |
| **`numbers`** | List | Opcjonalne | Mappowanie zmiennych na slidery/wprowadzanie liczbowe (`number`). |

---

## 📦 Struktura Ramki NetVar (UDP Packet Layout)

Każda ramka UDP wysyłana lub odbierana przez komponent składa się z **20-bajtowego nagłówka** oraz następującego po nim bloku danych (**payload**):

| Offset (bajty) | Rozmiar | Pole | Opis / Wartość |
| --- | --- | --- | --- |
| `0 - 3` | 4 bajty | Magic Bytes | Stała nagłówka protokołu: `0x00, 0x2D, 0x53, 0x33` |
| `4 - 7` | 4 bajty | Reserved | Zarezerwowane / wypełnione zerami |
| `8 - 9` | 2 bajty | `list_ID` | Identyfikator listy zmiennych sieciowych |
| `10 - 11` | 2 bajty | Reserved | Zarezerwowane / wypełnione zerami |
| `12 - 13` | 2 bajty | `var_count` | Liczba zmiennych w pakiecie |
| `14 - 15` | 2 bajty | Total Length | Całkowity rozmiar ramki w bajtach (`20 + payload_size_`) |
| `16 - 17` | 2 bajty | Sequence Counter | Licznik sekwencji pakietów (automatycznie inkrementowany) |
| `18 - 19` | 2 bajty | Reserved | Zarezerwowane / wypełnione zerami |
| `20+` | Zmienna | Payload | Ciągły blok danych zmiennych (zgodny z ustawieniami `endian` i `alignment`) |

---

## 📋 Obsługiwane Typy Zmiennych (IEC 61131-3)

* **Binarne:** `BOOL`
* **Całkowite (8-bit):** `BYTE`, `USINT`, `SINT`
* **Całkowite (16-bit):** `WORD`, `UINT`, `INT`
* **Całkowite (32-bit/Czas):** `DWORD`, `UDINT`, `DINT`, `TIME`
* **Zmiennoprzecinkowe:** `REAL` (32-bit), `LREAL` (64-bit)
* **Tekstowe:** `STRING(n)` (np. `STRING(20)`)

---

## 📚 Przykłady Użycia

### Przykład 1: Podstawowa konfiguracja (Little Endian, domyślne parametry)

```yaml
esphome:
  name: wago-gateway

esp32:
  board: esp32dev

wifi:
  ssid: "MojeWiFi"
  password: "HasloDoWiFi"

logger:

wago_netvar:
  ip_address: "192.168.1.10"
  port: 1202
  list_ID: 1
  direction: "write"
  update_interval: 5s
  variables:
    - name: "bLamp1"
      type: "BOOL"
    - name: "nSetpoint"
      type: "INT"
  switches:
    - variable: "bLamp1"
      name: "Salon Światło"
  numbers:
    - variable: "nSetpoint"
      name: "Temperatura Zadana"
      min: 15
      max: 30
      step: 0.5

```

---

### Przykład 2: Zaawansowana konfiguracja (`send_on_change` + Heartbeat)

```yaml
wago_netvar:
  ip_address: "192.168.1.50"
  port: 1202
  list_ID: 2
  var_count: 5
  direction: "both"
  endian: "little"
  pack_bools: false
  alignment: false
  
  update_interval: 10s       # Cykliczny heartbeat
  send_on_change: true       # Wysyłaj natychmiast po zmianie
  min_interval: 200ms        # Antyflood dla trybu on_change
  
  variables:
    - name: "Status_Pump1"
      type: "BOOL"
    - name: "Cmd_Pump1"
      type: "BOOL"
    - name: "Actual_Temp"
      type: "REAL"
    - name: "Flow_Rate"
      type: "REAL"
    - name: "Operating_Hours"
      type: "DINT"

  binary_sensors:
    - variable: "Status_Pump1"
      name: "Pompa 1 - Status"

  switches:
    - variable: "Cmd_Pump1"
      name: "Pompa 1 - Sterowanie"

  sensors:
    - variable: "Actual_Temp"
      name: "Temperatura Aktualna"
      unit_of_measurement: "°C"
      device_class: "temperature"
    - variable: "Flow_Rate"
      name: "Przepływ"
      unit_of_measurement: "m³/h"

```
