# EspHome_NetVar

## Opis

**EspHome_NetVar** to komponent ESPHome do zarządzania zmiennymi sieciowymi na urządzeniach ESP8266/ESP32. Projekt umożliwia komunikację z urządzeniami Wago poprzez protokół UDP, przesyłając dane zmiennych o różnych typach (BOOL, INT, REAL, STRING, itp.) z uwzględnieniem konfiguracji endianności, wyrównania pamięci i pakowania wartości boolowskich.

## Cechy

- 🌐 **Obsługa zmiennych sieciowych** - zarządzanie wieloma zmiennymi o różnych typach danych
- 🔌 **Komunikacja UDP** - wysyłanie danych do urządzeń Wago
- 📱 **Integracja z ESPHome** - pełna integracja z ekosystemem ESPHome
- 🔧 **Elastyczna konfiguracja** - obsługa endianności, wyrównania, pakowania boolów
- 📡 **Obsługa wielu typów danych** - BOOL, BYTE, WORD, DWORD, INT, UINT, REAL, LREAL, STRING
- 🏠 **Idealne do automatyki domowej** - integracja z Home Assistant i innymi systemami
- 🌡️ **Integracja z czujnikami** - obsługa czujników (np. DS18B20) z automatycznym przesłaniem danych

## Wymagania

- **ESPHome** v1.18+
- **Urządzenie** ESP8266 lub ESP32
- **Python** 3.6+
- **Home Assistant** (opcjonalnie)
- **Dostęp do sieci** do komunikacji z urządzeniami Wago

## Instalacja

### 1. Klonuj repozytorium

```bash
git clone https://github.com/pawel3410/EspHome_NetVar.git
cd EspHome_NetVar
```

### 2. Skonfiguruj ESPHome

Dodaj komponent jako `external_components` w pliku YAML ESPHome:

```yaml
external_components:
  - source: github://pawel3410/EspHome_NetVar
    components: [ wagonetvar ]
```

## Konfiguracja

### Parametry główne

| Parametr | Typ | Wymagany | Opis |
|----------|-----|----------|------|
| `ip` | string | ✅ | Adres IP urządzenia Wago |
| `port` | int | ✅ | Port UDP (np. 1202) |
| `cob_id` | int | ✅ | COB-ID dla komunikacji |
| `checksum` | int | ✅ | Checksum dla komunikacji |
| `endian` | string | ❌ | `little` (domyślnie) lub `big` |
| `pack_bools` | bool | ❌ | Pakowanie wartości BOOL (domyślnie: false) |
| `alignment` | bool | ❌ | Wyrównanie pamięci (domyślnie: false) |
| `update_interval` | string | ❌ | Interwał aktualizacji (domyślnie: 1s) |

### Obsługiwane typy zmiennych

- **BOOL** - wartość boolowska (1 bit)
- **BYTE, USINT, SINT** - 1 bajt
- **WORD, UINT, INT** - 2 bajty
- **DWORD, UDINT, DINT, TIME** - 4 bajty
- **REAL** - liczba zmiennoprzecinkowa (4 bajty)
- **LREAL** - liczba zmiennoprzecinkowa o podwyższonej precyzji (8 bajtów)
- **STRING(n)** - ciąg znaków o długości n+1

### Przykładowa konfiguracja

```yaml
esphome:
  name: wago-netvar-esp32

esp32:
  board: esp32dev

wifi:
  ssid: "TwojeWiFi"
  password: "TwojePassword"

external_components:
  - source: github://pawel3410/EspHome_NetVar
    components: [ wagonetvar ]

wago_netvar:
  id: my_wago_netvars
  ip: "192.168.17.44"
  port: 1202
  cob_id: 124
  checksum: 6
  endian: "little"
  pack_bools: false
  alignment: false
  update_interval: 1s
  variables:
    - name: "Pompa_CWi"
      type: "BOOL"
    - name: "Temperatura"
      type: "REAL"
    - name: "Wilgotność"
      type: "INT"
```

## Użycie

### Aktualizacja wartości zmiennych

Wartości zmiennych można aktualizować za pomocą metod `set_variable_value()`:

```yaml
# W konfiguracji lambda
lambda: |-
  id(my_wago_netvars).set_variable_value("Temperatura", 25.5);
  id(my_wago_netvars).set_variable_value("Pompa_CWi", true);
```

### Integracja z czujnikami

Przykład integracji z czujnikiem temperatury DS18B20:

```yaml
dallas:
  - pin: GPIO23
    id: my_dallas

sensor:
  - platform: dallas
    dallas_id: my_dallas
    address: 0x1234567890AB
    name: "Temperatura DS18B20"
    id: temp_ds18b20
    on_value:
      then:
        - lambda: |-
            id(my_wago_netvars).set_variable_value("Temperatura", x);
```

## Struktura projektu

```
.
├── README.md                 # Dokumentacja projektu
├── example.yaml             # Przykładowy plik konfiguracji
├── __init__.py              # Plik konfiguracji ESPHome
├── wagonetvar.h             # Nagłówek komponentu C++
└── wagonetvar.cpp           # Implementacja komponentu C++
```

## Główne komponenty kodu

### wagonetvar.h
Definiuje:
- Strukturę `VarDef` - definicję zmiennej sieciowej
- Klasę `WagoNetVarComponent` - główny komponent do zarządzania zmiennymi
- Metody do konfiguracji IP, portu, COB-ID, checksum
- Metody do aktualizacji wartości zmiennych (`set_variable_value()`)

### wagonetvar.cpp
Implementuje:
- Funkcje konwersji typów danych
- Funkcje pakowania wartości dla różnych typów
- Obsługę endianności (little-endian i big-endian)
- Pakowanie boolów do jednego bajtu
- Wyrównanie pamięci
- Wysyłanie danych UDP z nagłówkiem

### __init__.py
Definiuje:
- Schemat konfiguracji YAML
- Walidację parametrów
- Integrację z systemem ESPHome

## Najnowsze zmiany

### Ostatnie commity (sierpień 2026)
- ✅ **Metoda do aktualizacji wartości zmiennych** - dodana funkcja `set_variable_value()` dla typów `float` i `bool`
- ✅ **Mapa przechowywania wartości** - zmienna `var_values_` do przechowywania aktualnych wartości zmiennych
- ✅ **Integracja z DS18B20** - przykład konfiguracji z czujnikiem temperatury
- ✅ **Konfiguracja WagoNetVar** - pełna konfiguracja komponentu w pliku `example.yaml`

## Wkład

Zapraszam do zgłaszania błędów i propozycji ulepszeń poprzez GitHub Issues oraz pull requests.

## Licencja

Projekt jest dostępny na licencji MIT. Szczegóły znajdują się w pliku [LICENSE](./LICENSE).

## Kontakt

Jeśli masz pytania lub chciałbyś się skontaktować, otwórz issue na GitHubie.

---

**Ostatnia aktualizacja:** sierpień 2026  
**Język główny:** C++ (komponent ESPHome)  
**Współautorzy:** pawel3410
