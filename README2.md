Wago NetVar Component (Dokumentacja)
Komponent wago_netvar umożliwia dwukierunkową komunikację ze sterownikami PLC firmy Wago przy użyciu protokołu zmiennych sieciowych (NetVar przez UDP). Pozwala na wymianę danych w czasie rzeczywistym pomiędzy ESPHome a sterownikiem PLC.

Konfiguracja YAML
YAML
wago_netvar:
  - id: my_wago_netvars
    ip_address: "192.168.1.10"
    port: 1202
    cob_id: 1
    enable_read: true
    enable_write: true
    big_endian: false
    pack_bools: false
    alignment: true
    send_on_change: true
    min_interval: 100ms
    variables:
      - name: "SuszarkaTemperatura"
        type: "REAL"
      - name: "WiFi_RSSI"
        type: "INT"
      - name: "ESP_IP"
        type: "STRING(15)"
Opcje konfiguracji
Nazwa	Typ	Wymagane	Domyślna	Opis
id	ID	Tak	-	Unikalny identyfikator komponentu używany w lambdach.
ip_address	string	Tak	-	Adres IP docelowego sterownika Wago PLC.
port	int	Nie	1202	Port UDP używany do transmisji pakietów NetVar.
cob_id	int	Nie	1	Identyfikator COB-ID pakietu sieciowego.
checksum	int	Nie	0	Suma kontrolna pakietu (0 wyłącza weryfikację).
enable_read	boolean	Nie	false	Włącza nasłuch i odbiór danych ze sterownika PLC.
enable_write	boolean	Nie	true	Włącza wysyłanie danych z ESPHome do PLC.
big_endian	boolean	Nie	false	Określa kolejność bajtów (Big-Endian / Little-Endian).
pack_bools	boolean	Nie	false	Pakowanie zmiennych typu BOOL w pojedyncze bajty.
alignment	boolean	Nie	true	Wymuszenie wyrównania pamięci struktur (Memory alignment), domyślne dla CoDeSys/Wago.
send_on_change	boolean	Nie	true	Wysyłanie pakietu natychmiast po zmianie wartości dowolnej zmiennej.
min_interval	time	Nie	100ms	Minimalny odstęp czasowy między kolejnymi wysyłanymi pakietami.
variables	list	Tak	-	Lista zmiennych PLC definiująca ich dokładną kolejność i typy zgodne z programem w PLC.
Obsługiwane typy zmiennych PLC
Komponent wspiera standardowe typy danych systemów CoDeSys / Wago:

BOOL – wartość logiczna (true / false, 1 / 0)

BYTE, USINT, SINT – liczby 8-bitowe

WORD, UINT, INT – liczby 16-bitowe

DWORD, UDINT, DINT, TIME – liczby 32-bitowe

REAL – liczba zmiennoprzecinkowa 32-bitowa (float)

LREAL – liczba zmiennoprzecinkowa 64-bitowa (double)

STRING(n) – ciąg znaków o zadanej maksymalnej długości n (np. STRING(15))

Przykłady zaawansowane
Aktualizacja danych IP i czujników w lambdzie
Przesyłanie aktualnego adresu IP oraz siły sygnału Wi-Fi do PLC:

YAML
text_sensor:
  - platform: wifi_info
    ip_address:
      id: device_ip
      internal: true

sensor:
  - platform: wifi_signal
    name: "WiFi RSSI"
    id: wifi_RSSI
    update_interval: 10s
    on_value:
      then:
        - lambda: |-
            id(my_wago_netvars).set_variable_value("WiFi_RSSI", (int)x);

interval:
  - interval: 10s
    then:
      - lambda: |-
          std::string ip = id(device_ip).state;
          if (!ip.empty() && ip != "0.0.0.0") {
            id(my_wago_netvars).set_variable_value("ESP_IP", ip);
          }

##################################################################################################################################

Opcje konfiguracji
Nazwa	Typ	Wymagane	Domyślna	Opis
id	ID	Tak	-	Unikalny identyfikator komponentu używany w lambdach.
ip_address	string	Tak	-	Adres IP docelowego sterownika Wago PLC.
port	int	Nie	1202	Port UDP używany do transmisji pakietów NetVar.
cob_id	int	Nie	1	Identyfikator COB-ID pakietu sieciowego.
checksum	int	Nie	0	Suma kontrolna pakietu (0 wyłącza weryfikację).
direction	string	Nie	write	Kierunek transmisji: read, write lub both.
big_endian	boolean	Nie	false	Określa kolejność bajtów (Big-Endian / Little-Endian).
pack_bools	boolean	Nie	false	Pakowanie zmiennych typu BOOL w pojedyncze bajty.
alignment	boolean	Nie	true	Wymuszenie wyrównania pamięci struktur (Memory alignment), domyślne dla CoDeSys/Wago.
send_on_change	boolean	Nie	true	Wysyłanie pakietu natychmiast po zmianie wartości dowolnej zmiennej.
min_interval	time	Nie	100ms	Minimalny odstęp czasowy między kolejnymi wysyłanymi pakietami.
variables	list	Tak	-	Lista zmiennych PLC definiująca ich dokładną kolejność i typy zgodne z programem w PLC.
Obsługiwane typy zmiennych PLC
Komponent wspiera standardowe typy danych systemów CoDeSys / Wago:

BOOL – wartość logiczna (true / false, 1 / 0)

BYTE, USINT, SINT – liczby 8-bitowe

WORD, UINT, INT – liczby 16-bitowe

DWORD, UDINT, DINT, TIME – liczby 32-bitowe

REAL – liczba zmiennoprzecinkowa 32-bitowa (float)

LREAL – liczba zmiennoprzecinkowa 64-bitowa (double)

STRING(n) – ciąg znaków o zadanej maksymalnej długości n (np. STRING(15))

Przykłady zaawansowane
Aktualizacja danych IP i czujników w lambdzie
Przesyłanie aktualnego adresu IP oraz siły sygnału Wi-Fi do PLC:

YAML
text_sensor:
  - platform: wifi_info
    ip_address:
      id: device_ip
      internal: true

sensor:
  - platform: wifi_signal
    name: "WiFi RSSI"
    id: wifi_RSSI
    update_interval: 10s
    on_value:
      then:
        - lambda: |-
            id(my_wago_netvars).set_variable_value("WiFi_RSSI", (int)x);

interval:
  - interval: 10s
    then:
      - lambda: |-
          std::string ip = id(device_ip).state;
          if (!ip.empty() && ip != "0.0.0.0") {
            id(my_wago_netvars).set_variable_value("ESP_IP", ip);
          }
