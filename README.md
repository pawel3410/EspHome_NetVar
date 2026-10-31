Oto zaktualizowana treść pliku README.md, która uwzględnia usunięcie flagi big_endian, obsługę adresu IP 0.0.0.0 dla trybu nasłuchu oraz podział na niezależne tryby read lub write (bez możliwości łączenia obu kierunków w ramach jednej listy).WAGO NetVar - ESPHomeKomponent ESPHome umożliwiający komunikację z kontrolerami WAGO wykorzystującymi mechanizm NetVar przez UDP.   Komponent obsługuje niezależne listy zmiennych do odczytu (read) lub zapisu (write). Każda lista ma własny port UDP, konfigurację zmiennych oraz sposób wyznaczania ich położenia w payload.   FunkcjeOdczyt zmiennych NetVar przez UDP.   Zapis zmiennych NetVar przez UDP.   Niezależne listy typu read lub write (jedna rola na listę).Konfiguracja wielu zmiennych w jednej liście.   Automatyczne wyznaczanie offsetów.   Możliwość ręcznego określania offsetów bajtowych.   Obsługa typów danych CODESYS 2.3 (w formacie Little Endian).Obsługa BOOL jako pojedynczego bajtu w payload.   Obsługa STRING o konfigurowanej długości.   Publikowanie danych po odebraniu każdej poprawnej ramki.   Kierunek komunikacji (direction)Każda lista realizuje dokładnie jeden kierunek:KierunekOpisreadOdbieranie danych z kontrolera WAGO (w tym obsługa broadcastu / dowolnego nadawcy przy IP 0.0.0.0)   writeWysyłanie danych do kontrolera WAGO   W trybie read podanie adresu IP 0.0.0.0 pozwala na nasłuchiwanie i akceptowanie ramek wysyłanych przez sterownik ze zmiennego adresu lub w trybie broadcast.Dla read parametr ip_address służy jako opcjonalny filtr (lub 0.0.0.0 dla nasłuchu ogólnego), a port określa port UDP nasłuchu.   Dla write parametr ip_address oraz port określają docelowy adres i port UDP (np. adres sterownika lub broadcast 255.255.255.255).   Obsługiwane typy danychKomponent obsługuje typy danych występujące w CODESYS 2.3. Transmisja danych odbywa się w formacie Little Endian.   Typ CODESYSRozmiar w payloadOpisBOOL1 bajtWartość logicznaBYTE1 bajtLiczba 8-bitowaSINT1 bajtLiczba całkowita ze znakiemUSINT1 bajtLiczba całkowita bez znakuWORD2 bajtyLiczba 16-bitowaINT2 bajtyLiczba całkowita ze znakiemUINT2 bajtyLiczba całkowita bez znakuDWORD4 bajtyLiczba 32-bitowaDINT4 bajtyLiczba całkowita ze znakiemUDINT4 bajtyLiczba całkowita bez znakuREAL4 bajtyLiczba zmiennoprzecinkowa IEEE 754TIME4 bajtyTyp czasu (mapowany jako UDINT)DATE4 bajtyTyp daty (mapowany jako UDINT)TIME_OF_DAY4 bajtyTyp czasu w ciągu dnia (mapowany jako UDINT)DATE_AND_TIME4 bajtyTyp daty i czasu (mapowany jako UDINT)STRING(n)n + 1 bajtówTekst zakończony bajtem NUL   BOOLWażne: mimo że w pamięci sterownika CODESYS zmienna BOOL zajmuje jeden bit, w komunikacji NetVar jest przesyłana jako jeden pełny bajt.   Kodowanie:WartośćBajtFALSE0x00   TRUEwartość niezerowa (np. 0x01)STRINGTyp STRING(n) zajmuje:n bajtów na znaki,   1 dodatkowy bajt na terminator NUL (0x00).   Jeżeli długość nie zostanie podana, przyjmowana jest domyślna długość STRING(80).   Offsety zmiennychKażda zmienna w liście ma określony offset, czyli pozycję w payload. Dostępne są dwa sposoby określania offsetów: automatyczny (sekwencyjny) oraz ręczny za pomocą parametru byte_offset.   Maksymalny rozmiar pakietuMaksymalny rozmiar datagramu UDP przyjęty przez komponent wynosi 1472 bajty.
Przy 20-bajtowym nagłówku NetVar, maksymalny rozmiar payload wynosi 1452 bajty.   Przykładowa konfiguracjaPoniżej znajduje się przykładowa konfiguracja z osobną listą do odczytu (z nasłuchem na wszystkich interfejsach 0.0.0.0) oraz osobną listą do zapisu.   YAMLwago_netvar:
  - direction: read
    ip_address: "0.0.0.0"
    port: 1202
    list_id: 1
    variables:
      - name: machine_running
        type: BOOL
      - name: temperature
        type: REAL
      - name: production_counter
        type: UDINT
      - name: machine_name
        type: STRING(80)
    binary_sensors:
      - variable: machine_running
        name: "Stan maszyny"
    sensors:
      - variable: temperature
        name: "Temperatura"
      - variable: production_counter
        name: "Licznik produkcji"
    text_sensors:
      - variable: machine_name
        name: "Nazwa maszyny"

  - direction: write
    ip_address: "192.168.1.10"
    port: 1202
    list_id: 2
    variables:
      - name: start_command
        type: BOOL
      - name: setpoint
        type: REAL
    switches:
      - variable: start_command
        name: "Komenda Start"
    numbers:
      - variable: setpoint
        name: "Wartość zadana"
        min: 0
        max: 100
OgraniczeniaKomponent nie zmienia konfiguracji ani programu sterownika WAGO.   Nazwy, typy i kolejność zmiennych muszą odpowiadać konfiguracji NetVar po stronie sterownika.   Każda lista konfiguruje wyłącznie jeden kierunek (read lub write).