Oto szczegółowy plik **`README.md`** dla Twojego komponentu ESPHome `wago_netvar`, zawierający pełną dokumentację, opis parametrów, obsługiwane typy oraz bogaty zestaw przykładów konfiguracyjnych.

---

# ESPHome Wago NetVar Component

Komponent niestandardowy dla **ESPHome** umożliwiający szybką, dwukierunkową komunikację z sterownikami PLC firmy **Wago** oraz środowiskiem **CoDeSys** przy użyciu protokołu zmiennych sieciowych (**NetVar over UDP**).

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
| **`endian`** | String | `"little"` | Kolejność bajtów: `"little"` (np. ESP32) lub `"big"` (standard CoDeSys/Wago). |
| **`pack_bools`** | Boolean | `false` | Czy pakować zmienne typu `BOOL` bitowo w bajtach (zgodnie z CoDeSys). |
| **`alignment`** | Boolean | `true` | Wyrównanie pamięci (alignment) do rozmiaru typów. |
| **`min_interval`** | Time | `"100ms"` | Interwał czasowy cyklicznego wysyłania/odświeżania pakietów UDP. |
| **`variables`** | List | Opcjonalne | Lista definicji zmiennych (nazwa i typ IEC 61131-3). |
| **`sensors`** | List | Opcjonalne | Mappowanie zmiennych numerycznych na czujniki ESPHome (`sensor`). |
| **`binary_sensors`** | List | Opcjonalne | Mappowanie zmiennych binarnych na czujniki binarne (`binary_sensor`). |
| **`switches`** | List | Opcjonalne | Mappowanie zmiennych na przełączniki (`switch`). |
| **`numbers`** | List | Opcjonalne | Mappowanie zmiennych na slidery/wprowadzanie liczbowe (`number`). |

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

### Przykład 1: Podstawowa konfiguracja (Zapis do sterownika, Little Endian)

Najprostszy przykład sterowania wyjściami i przesyłania prostych wartości do sterownika Wago z interwałem cyklicznym co 1 sekundę.

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
  min_interval: 1s
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

### Przykład 2: Komunikacja dwukierunkowa (Big Endian, Pakowanie Bitek, Odczyt i Zapis)

Standardowa konfiguracja dla sterowników Wago/CoDeSys, gdzie wymagany jest big-endian, pakowanie zmiennych `BOOL` w bajty (`pack_bools: true`) oraz jednoczesny odczyt i zapis (`direction: both`).

```yaml
wago_netvar:
  ip_address: "192.168.1.50"
  port: 1202
  list_ID: 2
  var_count: 6
  direction: "both"
  endian: "big"
  pack_bools: true
  alignment: true
  min_interval: 500ms
  
  variables:
    # Bity spakowane w jeden bajt
    - name: "Status_Pump1"
      type: "BOOL"
    - name: "Cmd_Pump1"
      type: "BOOL"
    - name: "Status_Valve"
      type: "BOOL"
      
    # Zmienne numeryczne
    - name: "Actual_Temp"
      type: "REAL"
    - name: "Flow_Rate"
      type: "REAL"
    - name: "Operating_Hours"
      type: "DINT"

  binary_sensors:
    - variable: "Status_Pump1"
      name: "Pompa 1 - Status"
    - variable: "Status_Valve"
      name: "Zawór - Status"

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
    - variable: "Operating_Hours"
      name: "Czas Pracy"
      unit_of_measurement: "h"

```

---

### Przykład 3: Monitorowanie (Tylko odczyt ze sterownika PLC)

Konfiguracja nastawiona wyłącznie na odbiór danych z PLC (`direction: read`), idealna do wizualizacji parametrów maszyn lub stacji pogodowej na sterowniku Wago bez wysyłania komend sterujących z Home Assistant.

```yaml
wago_netvar:
  ip_address: "192.168.1.100"
  port: 1202
  list_ID: 10
  direction: "read"
  min_interval: 2s
  
  variables:
    - name: "Weather_Temp"
      type: "REAL"
    - name: "Weather_Wind"
      type: "REAL"
    - name: "Is_Raining"
      type: "BOOL"
    - name: "System_Message"
      type: "STRING(40)"

  sensors:
    - variable: "Weather_Temp"
      name: "Pogoda - Temperatura"
      unit_of_measurement: "°C"
    - variable: "Weather_Wind"
      name: "Pogoda - Wiatr"
      unit_of_measurement: "km/h"

  binary_sensors:
    - variable: "Is_Raining"
      name: "Czujnik Deszczu"
      device_class: moisture

```

---

### Przykład 4: Zaawansowane typy danych (LREAL, SINT, TIME, STRING)

Przykład demonstrujący obsługę pełnego wachlarza typów liczbowych oraz ciągów znaków.

```yaml
wago_netvar:
  ip_address: "192.168.1.15"
  port: 1202
  list_ID: 5
  direction: "both"
  min_interval: 1s
  
  variables:
    - name: "PreciseValue"
      type: "LREAL"     # 64-bit float
    - name: "SmallInt"
      type: "SINT"      # -128 do 127
    - name: "CycleTime"
      type: "TIME"      # Czas w ms
    - name: "DeviceName"
      type: "STRING(25)"

  sensors:
    - variable: "PreciseValue"
      name: "Wartość Precyzyjna"
    - variable: "SmallInt"
      name: "Mała Liczba"
    - variable: "CycleTime"
      name: "Czas Cyklu PLC"
      unit_of_measurement: "ms"

```

---

## 🛠️ Wskazówki konfiguracji w CoDeSys / Wago e!Cockpit

1. W projekcie PLC utwórz **Network Variables List (Sender/Receiver)** odpowiadającą kierunkiem konfiguracji w ESPHome.
2. Upewnij się, że **NetVar ID** (`list_ID`) oraz **Port UDP** po stronie PLC są dokładnie takie same jak w pliku konfiguracyjnym ESPHome YAML.
3. Jeśli używasz struktur lub tablic zmiennych z `pack_bools: true`, pamiętaj o wyrównaniu pamięci w CoDeSys (struktury mogą wymagać atrybutu `ALIGN` lub odpowiedniej kolejności pól).
