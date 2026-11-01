# WagoNetVar – komponent ESPHome dla protokołu NetVar (WAGO)

Komponent umożliwia wymianę danych między ESPHome a sterownikami WAGO (lub innymi urządzeniami) przy użyciu protokołu NetVar przez UDP. Dane przesyłane są w formacie little endian.

## Wymagania

- ESPHome
- Moduł z Wi-Fi (ESP32 / ESP8266)
- Komponent działa w trybie `read` lub `write` na poziomie pojedynczej instancji. Można zdefiniować wiele instancji (np. jedną do odczytu, drugą do zapisu).

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

