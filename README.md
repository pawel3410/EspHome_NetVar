# EspHome_NetVar

## Opis

EspHome_NetVar to projekt umożliwiający zarządzanie zmiennymi sieciowymi na urządzeniach ESP8266/ESP32 za pomocą ESPHome. Projekt pozwala na tworzenie i obsługę zmiennych dostępnych przez sieć, co ułatwia integrację z innymi systemami automatyki domowej.

## Cechy

- 🌐 Obsługa zmiennych sieciowych (Network Variables)
- 📱 Integracja z ESPHome
- 🔧 Łatwa konfiguracja
- 📡 Komunikacja przez sieć
- 🏠 Idealne do automatyki domowej

## Wymagania

- ESPHome v1.18+
- Urządzenie ESP8266 lub ESP32
- Python 3.6+
- Home Assistant (opcjonalnie)

## Instalacja

1. Klonuj repozytorium:
```bash
git clone https://github.com/pawel3410/EspHome_NetVar.git
cd EspHome_NetVar
```

2. Zainstaluj wymagane zależności:
```bash
pip install -r requirements.txt
```

3. Skonfiguruj plik YAML dla ESPHome:
```yaml
esphome:
  name: my_device
  platform: esp8266
  board: d1_mini

# Dodaj swoje zmienne sieciowe tutaj
```

## Użycie

### Konfiguracja zmiennych

Przykładowa konfiguracja w pliku YAML:

```yaml
network_variables:
  - name: temperature
    type: float
    persistence: true
  - name: humidity
    type: float
    persistence: true
```

### API

Zmienne są dostępne przez prosty interfejs REST API dostępny na urządzeniu.

## Struktura projektu

```
.
├── README.md
├── requirements.txt
├── src/
│   ├── network_var.py
│   └── config.py
├── examples/
│   └── sample_config.yaml
└── tests/
    └── test_network_var.py
```

## Przykłady

### Odczyt zmiennej

```bash
curl http://device_ip/api/variables/temperature
```

### Zapis zmiennej

```bash
curl -X POST http://device_ip/api/variables/humidity -d "value=65.5"
```

## Dokumentacja

Szczegółowa dokumentacja znajduje się w pliku [DOCUMENTATION.md](./DOCUMENTATION.md).

## Wkład

Zapraszam do zgłaszania błędów i propozycji ulepszeń poprzez GitHub Issues.

## Licencja

Projekt jest dostępny na licencji MIT. Szczegóły znajdują się w pliku [LICENSE](./LICENSE).

## Kontakt

Jeśli masz pytania lub chciałbyś się skontaktować, otwórz issue na GitHubie.

---

**Ostatnia aktualizacja:** sierpień 2026
