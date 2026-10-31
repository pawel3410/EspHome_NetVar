# WAGO NetVar - ESPHome

Komponent ESPHome umożliwiający komunikację z kontrolerami WAGO wykorzystującymi mechanizm NetVar przez UDP.

Komponent obsługuje niezależne listy zmiennych do odczytu i zapisu. Każda lista może mieć własny port UDP, konfigurację zmiennych oraz sposób wyznaczania ich położenia w payload.

## Funkcje

- Odczyt zmiennych NetVar przez UDP.
- Zapis zmiennych NetVar przez UDP.
- Niezależne listy `read` i `write`.
- Konfiguracja wielu zmiennych w jednej liście.
- Automatyczne wyznaczanie offsetów.
- Możliwość ręcznego określania offsetów bajtowych.
- Obsługa typów danych CODESYS 2.3.
- Obsługa BOOL jako pojedynczego bajtu w payload.
- Obsługa STRING o konfigurowanej długości.
- Publikowanie danych po odebraniu każdej poprawnej ramki.

## Kierunek komunikacji

Komponent obsługuje dwa kierunki:

| Kierunek | Opis |
|---|---|
| `read` | Odbieranie danych z kontrolera WAGO |
| `write` | Wysyłanie danych do kontrolera WAGO |

Każda lista ma własny parametr `port`.

- Dla `read` parametr `port` określa port UDP nasłuchu.
- Dla `write` parametr `port` określa docelowy port UDP.

Listy `read` i `write` są niezależne. Można skonfigurować wiele list, również z różnymi portami.

## Obsługiwane typy danych

Komponent obsługuje typy danych występujące w CODESYS 2.3.

| Typ CODESYS | Rozmiar w payload | Opis |
|---|---:|---|
| `BOOL` | 1 bajt | Wartość logiczna |
| `BYTE` | 1 bajt | Liczba 8-bitowa |
| `SINT` | 1 bajt | Liczba całkowita ze znakiem |
| `USINT` | 1 bajt | Liczba całkowita bez znaku |
| `WORD` | 2 bajty | Liczba 16-bitowa |
| `INT` | 2 bajty | Liczba całkowita ze znakiem |
| `UINT` | 2 bajty | Liczba całkowita bez znaku |
| `DWORD` | 4 bajty | Liczba 32-bitowa |
| `DINT` | 4 bajty | Liczba całkowita ze znakiem |
| `UDINT` | 4 bajty | Liczba całkowita bez znaku |
| `REAL` | 4 bajty | Liczba zmiennoprzecinkowa IEEE 754 |
| `TIME` | 4 bajty | Typ czasu |
| `DATE` | 4 bajty | Typ daty |
| `TIME_OF_DAY` | 4 bajty | Typ czasu w ciągu dnia |
| `DATE_AND_TIME` | 4 bajty | Typ daty i czasu |
| `STRING(n)` | n + 1 bajtów | Tekst zakończony bajtem NUL |

### BOOL

Ważne: mimo że w pamięci sterownika CODESYS zmienna `BOOL` zajmuje jeden bit, w komunikacji NetVar jest przesyłana jako **jeden pełny bajt**.

Kodowanie:

| Wartość | Bajt |
|---|---|
| `FALSE` | `0x00` |
| `TRUE` | wartość niezerowa |

Komponent nie pakuje zmiennych BOOL w pojedyncze bity payload. Każda zmienna BOOL zajmuje osobny bajt.

### STRING

Typ `STRING(n)` zajmuje:

- `n` bajtów na znaki,
- 1 dodatkowy bajt na terminator `NUL` (`0x00`).

Przykłady:

| Typ | Rozmiar |
|---|---:|
| `STRING(10)` | 11 bajtów |
| `STRING(20)` | 21 bajtów |
| `STRING(80)` | 81 bajtów |
| `STRING(100)` | 101 bajtów |

Jeżeli długość nie zostanie podana, przyjmowana jest domyślna długość `STRING(80)`.

### Typy DATE i TIME

Typy `TIME`, `DATE`, `TIME_OF_DAY` i `DATE_AND_TIME` zajmują po 4 bajty.

W tej dokumentacji określony jest rozmiar danych przesyłanych w payload. Nie zakłada się dodatkowej konwersji na tekstową datę lub godzinę. Dane należy traktować jako wartości 32-bitowe zgodnie z reprezentacją używaną przez sterownik.

## Offsety zmiennych

Każda zmienna w liście ma określony offset, czyli pozycję w payload.

Komponent obsługuje dwa sposoby określania offsetów.

### Automatyczne offsety

Jeżeli offset nie zostanie podany, pozycja zmiennej jest wyznaczana na podstawie rozmiaru poprzednich zmiennych.

Przykład:

```yaml
variables:
  - name: status
    type: BOOL

  - name: temperature
    type: REAL

  - name: counter
    type: UDINT
```

Rozmieszczenie:

| Zmienna | Typ | Offset | Rozmiar |
|---|---|---:|---:|
| `status` | BOOL | 0 | 1 |
| `temperature` | REAL | 1 | 4 |
| `counter` | UDINT | 5 | 4 |

### Ręczne offsety

Można również określić pozycję każdej zmiennej samodzielnie.

Offset jest podawany w bajtach względem początku payload, a nie względem początku całego pakietu UDP.

Przykład:

```yaml
variables:
  - name: status
    type: BOOL
    offset: 0

  - name: temperature
    type: REAL
    offset: 4

  - name: counter
    type: UDINT
    offset: 8
```

W przypadku ręcznego określania offsetów użytkownik odpowiada za prawidłowe rozmieszczenie zmiennych i brak nakładania się ich obszarów pamięci.

## Maksymalny rozmiar pakietu

Maksymalny rozmiar datagramu UDP przyjęty przez komponent wynosi **1472 bajty**.

Przy założeniu 20-bajtowego nagłówka NetVar dostępna przestrzeń na payload wynosi:

| Element | Rozmiar |
|---|---:|
| Maksymalny datagram UDP | 1472 bajty |
| Nagłówek NetVar | 20 bajtów |
| Maksymalny payload | 1452 bajty |

Wszystkie zmienne znajdujące się w jednej liście muszą mieścić się w dostępnej przestrzeni payload.

Przy obliczaniu rozmiaru uwzględniane są rzeczywiste rozmiary typów, w tym dodatkowy bajt terminujący dla `STRING(n)`.

## Przykładowa konfiguracja

Poniżej znajduje się przykładowa konfiguracja z osobną listą do odczytu i osobną listą do zapisu.

```yaml
wago_netvar:
  read:
    - id: netvar_read
      port: 1202
      variables:
        - name: machine_running
          type: BOOL

        - name: temperature
          type: REAL

        - name: production_counter
          type: UDINT

        - name: machine_name
          type: STRING(80)

  write:
    - id: netvar_write
      port: 1202
      variables:
        - name: start_command
          type: BOOL

        - name: setpoint
          type: REAL
```

Powyższy przykład pokazuje strukturę list. Nazwy i typy zmiennych należy dopasować do konfiguracji sterownika WAGO.

## Uwagi dotyczące konfiguracji

1. Offsety są wyrażane w bajtach.
2. BOOL zajmuje jeden bajt w payload.
3. STRING(n) zajmuje n + 1 bajtów.
4. Typy 32-bitowe zajmują cztery bajty.
5. Maksymalny rozmiar datagramu UDP wynosi 1472 bajty.
6. Maksymalny payload przy 20-bajtowym nagłówku wynosi 1452 bajty.
7. Przy ręcznym określaniu offsetów należy zadbać o poprawne rozmieszczenie zmiennych.
8. Typy daty i czasu mają rozmiar 4 bajtów; sposób ich interpretacji powinien odpowiadać reprezentacji stosowanej przez sterownik.

## Ograniczenia

- Komponent nie zmienia konfiguracji ani programu sterownika WAGO.
- Nazwy, typy i kolejność zmiennych muszą odpowiadać konfiguracji NetVar po stronie sterownika.
- Nie należy przekraczać maksymalnego rozmiaru payload.
- Nie należy zakładać konwersji typów DATE i TIME do postaci tekstowej bez dodatkowej obsługi.
