WAGO / CODESYS Network Variable List (NVL) ComponentKomponent wago_netvar umożliwia wydajną, dwukierunkową wymianę danych z regulatorami i sterownikami PLC (WAGO, Beckhoff, CODESYS 2.3 / 3.5) za pomocą natywnego protokołu Network Variable List (NVL) przesyłanego przez sieć UDP.Zapewnia automatyczne pakowanie bitów (pack_bools), wyrównanie pamięci (alignment), obsługę zmiennych stało- i zmiennoprzecinkowych, a także dwukierunkową synchronizację z systemem Home Assistant.

Konfiguracja komponentu

Wymagane i opcjonalne klucze główne
Parametr  Typ  Domyślnie  Opis 
ip_address  string  wymagany   Adres IP sterownika PLC (dla nadawania) lub docelowy IP sieciowy.
port            int      1202      Port UDP wykorzystywany przez wymianę danych NVL.
cob_id          int      1         Identyfikator obiektu COB-ID zgadzający się z konfiguracją w CODESYS.
checksum        int      0         Suma kontrolna listy zmiennych (0 wyłącza weryfikację sumy).
direction       string   write     Kierunek pracy: read (odczyt z PLC), write (wysyłanie do PLC), both (dwukierunkowy).
endian          string   little    Kolejność bajtów: little (Little-Endian) lub big (Big-Endian).
pack_bools      boolean  false     Gdy true, sąsiadujące zmienne BOOL są pakowane bitowo w pojedyncze bajty.
alignment       boolean  true      Wyrównywanie adresów pamięci do granic typów (np. WORD na adresy parzyste).
send_on_change  boolean  true      Wysłanie pakietu UDP natychmiast po zmianie stanu dowolnej encji.
min_interval    Time     100ms     Minimalny odstęp czasu pomiędzy kolejnymi wysłanymi pakietami UDP.
update_interval  Time    10s       Częstotliwość wymuszonego (cyklicznego) wysyłania ramki UDP.

Obsługiwane typy danych CODESYSTyp 
CODESYSTyp    ESPHome         / OpisRozmiar           (Bajty)        Wyrównanie (Align)
BOOL          Binary Sensor   / Switch1               (lub 1 bit)    1
BYTE, USINT   Sensor          / Number (bez znaku)      1            1
SINT          Sensor          / Number (ze znakiem)     1            1 
WORD, UINT    Sensor          / Number (bez znaku)      2            2
INT           Sensor          / Number (ze znakiem)     2            2
DWORD, UDINT, TIMESensor      / Number (bez znaku)      4            4
DINT          Sensor          / Number (ze znakiem)     4            4
REAL          Sensor          / Number (Float 32-bit)   4            2
LREAL         Sensor          / Number (Float 64-bit)   8            8
STRING(N)    Wewnętrzny bufor tekstowy                  N + 1        1

[!NOTE]Kolejność zdeklarowanych zmiennych w sekcji variables musi być identyczna z kolejnością zmiennych zdefiniowanych w pamięci listy NVL po stronie CODESYS / WAGO.

Przykłady konfiguracji
 Odczyt danych ze sterownika PLC (direction: read)
 Poniższy przykład odczytuje ze sterownika PLC stany wejść binarnych oraz wartości czujników analogowych (temperatura, ciśnienie).
 YAML
external_components:
  - source:
      type: local
      path: components

wago_netvar:
  id: plc_reader
  ip_address: "192.168.1.50"
  port: 1202
  cob_id: 1
  direction: read
  endian: little
  pack_bools: true
  alignment: true
  variables:
    - name: "bPumpStatus"
      type: "BOOL"
    - name: "bAlarmActive"
      type: "BOOL"
    - name: "iErrorCode"
      type: "INT"
    - name: "rWaterTemp"
      type: "REAL"
    - name: "rPressure"
      type: "LREAL"

binary_sensor:
  - platform: wago_netvar
    variable: "bPumpStatus"
    name: "Stan Pompy"
    device_class: running

  - platform: wago_netvar
    variable: "bAlarmActive"
    name: "Alarm Zbiornika"
    device_class: problem

sensor:
  - platform: wago_netvar
    variable: "iErrorCode"
    name: "Kod Błędu PLC"

  - platform: wago_netvar
    variable: "rWaterTemp"
    name: "Temperatura Wody"
    unit_of_measurement: "°C"
    device_class: temperature
    accuracy_decimals: 1

  - platform: wago_netvar
    variable: "rPressure"
    name: "Ciśnienie Instalacji"
    unit_of_measurement: "bar"
    device_class: pressure
    accuracy_decimals: 2
    
2. Wysyłanie sterowania do PLC (direction: write)W tym trybie ESPHome działa jako nadajnik rozkazów i nastaw.
   Przełączniki (switch) oraz zadajniki liczb (number) z Home Assistant wysyłają ramki UDP do sterownika WAGO.
   YAML
  wago_netvar:
  id: plc_writer
  ip_address: "192.168.1.50"
  port: 1202
  cob_id: 2
  direction: write
  endian: little
  send_on_change: true
  min_interval: 50ms
  update_interval: 5s
  variables:
    - name: "bStartCmd"
      type: "BOOL"
    - name: "bResetCmd"
      type: "BOOL"
    - name: "iTargetSpeed"
      type: "INT"
    - name: "rSetPointTemp"
      type: "REAL"

switch:
  - platform: wago_netvar
    variable: "bStartCmd"
    name: "Start Układu"
    icon: "mdi:power"

  - platform: wago_netvar
    variable: "bResetCmd"
    name: "Reset Błędów"
    icon: "mdi:restart"

number:
  - platform: wago_netvar
    variable: "iTargetSpeed"
    name: "Zadana Prędkość Wentylatora"
    min_value: 0
    max_value: 100
    step: 1
    unit_of_measurement: "%"

  - platform: wago_netvar
    variable: "rSetPointTemp"
    name: "Nastawa Temperatury"
    min_value: 15.0
    max_value: 30.0
    step: 0.5
    unit_of_measurement: "°C"

3. Komunikacja Pełny-Dupleks / Dwukierunkowa (direction: both)Tryb both pozwala na ciągłą synchronizację dwukierunkową.
   Jeśli zmiana nastąpi w PLC – stan encji w Home Assistant zostanie zaktualizowany.
   Jeśli użytkownik zmieni przełącznik lub nastawę w Home Assistant – nowa wartość zostanie przesłana do PLC.
   YAML
  wago_netvar:
  id: plc_bidirectional
  ip_address: "192.168.1.50"
  port: 1202
  cob_id: 10
  direction: both
  endian: little
  pack_bools: false
  alignment: true
  variables:
    - name: "bLightSwitch"
      type: "BOOL"
    - name: "rDimmerValue"
      type: "REAL"
    - name: "rOutdoorTemp"
      type: "REAL"

switch:
  - platform: wago_netvar
    variable: "bLightSwitch"
    name: "Oświetlenie Ogrodu"

number:
  - platform: wago_netvar
    variable: "rDimmerValue"
    name: "Poziom Ściemniacza"
    min_value: 0
    max_value: 100
    step: 1

sensor:
  - platform: wago_netvar
    variable: "rOutdoorTemp"
    name: "Temperatura Zewnętrzna"
    unit_of_measurement: "°C"
    
Diagnostyka i Rozwiązywanie Problemów[!TIP]Zgodność ze sterownikami CODESYS 2.3 vs 3.5CODESYS v2.3 (WAGO 750-88x): Zazwyczaj domyślnie wykorzystuje endian: little oraz nakłada wyrównanie pamięci (alignment: true).CODESYS v3.5: W ustawieniach listy zmiennych sieciowych (NVL) w projekcie upewnij się, że ustawiony COB-ID oraz ewentualna suma kontrolna Checksum odpowiadają wartościom skonfigurowanym w ESPHome.Pakiet odebrany, ale stan się nie zmienia: Sprawdź, czy cob_id w konfiguracji ESPHome pokrywa się co do bitu z identyfikatorem zadeklarowanym w liście odbiorczej PLC.Przesunięte / zniekształcone dane: Jeśli odczytywane wartości liczbowe są drastycznie zawyżone lub niepoprawne, zmień ustawienie endian (z little na big) lub upewnij się, że opcja pack_bools w ESPHome jest ustawiona dokładnie tak samo, jak w konfiguracji exportera listy w PLC.
