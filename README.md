
│ WagoNetVar – komponent ESPHome dla protokołu NetVar (WAGO)                                                                                                                                │


Komponent umożliwia wymianę danych między ESPHome a sterownikami WAGO (lub innymi urządzeniami) przy użyciu protokołu NetVar przez UDP. Dane przesyłane są w formacie little endian.


Wymagania

 • ESPHome
 • Moduł z Wi-Fi (ESP32 / ESP8266)
 • Komponent działa w trybie read lub write na poziomie pojedynczej instancji. Można zdefiniować wiele instancji (np. jedną do odczytu, drugą do zapisu).


Struktura ramki NetVar

Nagłówek ma stały rozmiar 20 bajtów i jest przesyłany w kolejności little endian. Po nagłówku następuje ładunek (payload) zawierający zdefiniowane zmienne.


 Bajty  Pole              Opis
 ───────────────────────────────────────────────────────────────────────────────────────────
 0–3    Magic bytes       Stałe: 0x00 0x2D 0x53 0x33
 4–7    Zarezerwowane     Zerowane
 8–9    list_ID           Identyfikator listy (uint16 LE)
 10–11  Zarezerwowane     Zerowane
 12–13  var_count         Liczba zmiennych na liście (uint16 LE)
 14–15  frame_size        Całkowita długość ramki w bajtach (nagłówek + payload) (uint16 LE)
 16–17  sequence_counter  Licznik sekwencyjny (odczytywany, nie walidowany)
 18–19  checksum          Suma kontrolna (odczytywana, nie walidowana)
 20–N   Payload           Wartości zmiennych zgodnie z układem pamięci



Obsługiwane typy danych

Wszystkie typy są little endian. LREAL nie jest obsługiwany przez NetVar.


 Typ    Rozmiar  Zakres / opis
 ──────────────────────────────────────────────
 BOOL   1 bajt   0 = FAŁSZ, 1 = PRAWDA
 BYTE   1 bajt   0–255 (maska bitowa)
 USINT  1 bajt   0–255
 SINT   1 bajt   -128–127
 WORD                2 bajty     0–65 535 (maska bitowa)
 UINT                2 bajty     0–65 535
 INT                 2 bajty     -32 768–32 767
 DWORD               4 bajty     0–4 294 967 295 (maska bitowa)
 UDINT               4 bajty     0–4 294 967 295
 DINT                4 bajty     -2 147 483 648–2 147 483 647
 REAL                4 bajty     Liczba zmiennoprzecinkowa IEEE-754
 TIME                4 bajty     Czas w ms (UDINT)
 DATE                4 bajty     Data w s od 1970-01-01 (UDINT)
 TIME_OF_DAY (TOD)   4 bajty     Czas doby w ms (UDINT)
 DATE_AND_TIME (DT)  4 bajty     Data i czas w s od 1970-01-01 (UDINT)
 STRING(n)           n + 1 bajt  Tekst ASCII zakończony nullem (0x00), domyślnie 80 znaków, max 255



Konfiguracja ESPHome


wago_netvar:
  id: moje_netvar
  ip_address: "192.168.1.10"   # Adres IP nadawcy/odbiorcy. Użyj 0.0.0.0, aby akceptować pakiety z dowolnego IP (np. broadcast).
  port: 1202                    # Port UDP (domyślnie 1202)
  list_ID: 1                    # Identyfikator listy
  var_count: 0                  # 0 = automatycznie na podstawie liczby zmiennych
  direction: write              # "read" lub "write"
  alignment: false              # Wyrównanie zmiennych do granic typu
  send_on_change: false         # Wysyłaj tylko przy zmianie wartości
  min_interval: 100ms           # Minimalny odstęp między wysyłkami

  variables:
    - name: "zmienna_bool"
      type: "BOOL"
      byte_offset: 0

    - name: "zmienna_int"
      type: "INT"
      byte_offset: 1

    - name: "napis"
      type: "STRING(32)"
      byte_offset: 4

  # Przykład dla direction: read
  sensors:
    - platform: wago_netvar
      variable: "zmienna_int"
      name: "Czujnik INT"

  binary_sensors:
    - platform: wago_netvar
      variable: "zmienna_bool"
      name: "Czujnik BOOL"

  text_sensors:
    - platform: wago_netvar
      variable: "napis"
      name: "Czujnik tekstowy"

  # Przykład dla direction: write
  switches:
    - platform: wago_netvar
      variable: "zmienna_bool"
      name: "Przełącznik"

  numbers:
    - platform: wago_netvar
      variable: "zmienna_int"
      name: "Liczba"



Walidacja pakietów (tryb read)

Przy odbiorze ramki weryfikowane są:

 • Minimalny i maksymalny rozmiar pakietu.
 • Adres IP nadawcy (jeśli w konfiguracji ustawiono adres inny niż 0.0.0.0).
 • Magic bytes w nagłówku.
 • list_ID oraz var_count zgodne z konfiguracją.
 • Pole frame_size (bajty 14–15) zgodne z rzeczywistym rozmiarem otrzymanego pakietu.
 • Rozmiar payload zgodny z zadeklarowanym układem zmiennych.

Pola sequence_counter (bajty 16–17) oraz checksum (bajty 18–19) są odczytywane, ale nie wpływają na walidację.


Wysyłanie (tryb write)

Ramki są wysyłane na skonfigurowany adres IP i port. Obsługiwany jest broadcast (255.255.255.255). Licznik sequence_counter jest inkrementowany przy każdej wysłanej ramce.


Kompatybilność z ESP-IDF

Komponent został zaprojektowany z myślą o uruchomieniu pod ESPHome zarówno w frameworku Arduino, jak i ESP-IDF. Należy jednak pamiętać, że warstwa UDP może wymagać specyficznych zależności
w zależności od wybranej platformy budowania.


Uwagi

 • Lista może być wyłącznie typu read lub write w ramach jednej instancji komponentu.
 • Przy byte_offset można ręcznie określić położenie zmiennej w payloadzie; w przeciwnym razie układ jest obliczany automatycznie.
 • Przy send_on_change: true ramka jest wysyłana tylko gdy wartość się zmieniła, z ograniczeniem min_interval.