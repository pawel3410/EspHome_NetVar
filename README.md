# Wago NetVar dla ESPHome

Komponent umożliwia komunikację z kontrolerami WAGO wykorzystującymi
protokół CoDeSys NetVar UDP (.S3).

Obsługiwane są niezależne listy zmiennych NetVar. Każda lista może mieć
własną konfigurację, identyfikator, port UDP oraz kierunek komunikacji.

## Funkcje

- Odczyt zmiennych z kontrolera WAGO.
- Zapis zmiennych do kontrolera WAGO.
- Obsługa wielu niezależnych list NetVar.
- Automatyczne rozmieszczanie zmiennych w payload.
- Możliwość ręcznego określania offsetów bajtowych.
- Obsługa typów BOOL, BYTE, USINT, SINT, WORD, UINT, INT,
  DWORD, UDINT, DINT, TIME, REAL oraz STRING.
- Obsługa kolejności bajtów BIG ENDIAN i LITTLE ENDIAN.
- Publikowanie odczytanych wartości po odebraniu ramki.
- Kontrola rozmiaru pakietu UDP.

## Kierunek komunikacji

Każda lista może pracować w jednym z dwóch trybów:

- `read` - odbieranie danych z kontrolera WAGO.
- `write` - wysyłanie danych do kontrolera WAGO.

Parametr `port` określa port UDP używany przez daną listę:
- w trybie `read` jest to port nasłuchujący,
- w trybie `write` jest to port docelowy.

Nie ma osobnego parametru portu lokalnego i zdalnego.

## Konfiguracja

Każda lista NetVar jest konfigurowana niezależnie. Możliwe jest
skonfigurowanie kilku list korzystających z tego samego adresu IP
i portu UDP, ale posiadających różne identyfikatory.

Podstawowe parametry:

| Parametr | Opis |
|---|---|
| `ip_address` | Adres IP kontrolera WAGO |
| `port` | Port UDP |
| `list_ID` | Identyfikator listy NetVar |
| `var_count` | Liczba zmiennych w liście |
| `direction` | Kierunek komunikacji: `read` lub `write` |
| `endian` | Kolejność bajtów danych |
| `alignment` | Wyrównanie rozmieszczenia zmiennych |
| `variables` | Definicje zmiennych |

## Obsługiwane typy danych

| Typ | Rozmiar | Opis |
|---|---:|---|
| BOOL | 1 bajt | Wartość logiczna |
| BYTE | 1 bajt | Liczba 8-bitowa bez znaku |
| USINT | 1 bajt | Liczba 8-bitowa bez znaku |
| SINT | 1 bajt | Liczba 8-bitowa ze znakiem |
| WORD | 2 bajty | Liczba 16-bitowa bez znaku |
| UINT | 2 bajty | Liczba 16-bitowa bez znaku |
| INT | 2 bajty | Liczba 16-bitowa ze znakiem |
| DWORD | 4 bajty | Liczba 32-bitowa bez znaku |
| UDINT | 4 bajty | Liczba 32-bitowa bez znaku |
| DINT | 4 bajty | Liczba 32-bitowa ze znakiem |
| TIME | 4 bajty | Wartość czasu |
| REAL | 4 bajty | Liczba zmiennoprzecinkowa IEEE 754 |
| STRING(n) | n bajtów | Ciąg znaków o określonej długości |

Typ LREAL nie jest obsługiwany.

## Typ BOOL

Wartość BOOL jest zawsze przesyłana i odbierana jako jeden cały bajt.

- `0x00` oznacza `false`.
- Wartość różna od `0x00` oznacza `true`.

BOOL zajmuje dokładnie jeden bajt w payload.

Nie jest stosowane adresowanie bitowe. Parametr `bit_offset` nie jest
potrzebny i nie należy go konfigurować.

Przykładowo:

| Zmienna | Offset | Rozmiar |
|---|---:|---:|
| REAL | 0 | 4 bajty |
| BOOL | 4 | 1 bajt |
| REAL | 6 | 4 bajty |

W powyższym przykładzie BOOL zajmuje bajt o indeksie 4. Kolejna zmienna
REAL zaczyna się pod offsetem 6, pozostawiając bajt o indeksie 5
niewykorzystany.

## Rozmieszczanie zmiennych w payload

Dostępne są dwa sposoby określania położenia zmiennych.

### Automatyczne rozmieszczanie

Jeżeli nie podano `byte_offset`, położenie zmiennej jest wyznaczane
automatycznie na podstawie kolejności definicji zmiennych, ich rozmiarów
oraz ustawionego wyrównania.

Dzięki temu nie trzeba ręcznie obliczać offsetów dla każdej zmiennej.

### Ręczne rozmieszczanie

Można również określić `byte_offset` dla poszczególnych zmiennych.

Offset jest liczony od początku payload, a nie od początku całego
pakietu UDP.

Przykład:

| Zmienna | Typ | Offset | Rozmiar |
|---|---|---:|---:|
| temperatura | REAL | 0 | 4 bajty |
| alarm | BOOL | 4 | 1 bajt |
| licznik | UINT | 6 | 2 bajty |

W przypadku ręcznego rozmieszczania należy zadbać o to, aby zakresy
bajtów poszczególnych zmiennych nie nakładały się na siebie.

## Kolejność bajtów

Parametr `endian` określa kolejność bajtów wartości zapisanych
w payload.

Dostępne ustawienia:

- `little` - LITTLE ENDIAN.
- `big` - BIG ENDIAN.

Kolejność bajtów musi odpowiadać konfiguracji kontrolera WAGO
i pozostałych urządzeń komunikujących się z daną listą.

## Rozmiar pakietu UDP

Maksymalny rozmiar datagramu UDP przy standardowym MTU Ethernet 1500
bajtów, bez dodatkowych nagłówków IP, wynosi:

**1472 bajty.**

Obliczenie:

| Element | Rozmiar |
|---|---:|
| MTU Ethernet | 1500 bajtów |
| Nagłówek IPv4 | 20 bajtów |
| Nagłówek UDP | 8 bajtów |
| Maksymalny datagram UDP | 1472 bajty |

Pakiet NetVar zawiera dodatkowo własny nagłówek protokołu.

Przy założeniu, że nagłówek NetVar ma 20 bajtów, maksymalna przestrzeń
na dane zmiennych (payload) wynosi:

**1452 bajty.**

| Element | Rozmiar |
|---|---:|
| Maksymalny datagram UDP | 1472 bajty |
| Nagłówek NetVar | 20 bajtów |
| Maksymalny payload | 1452 bajty |

Limit 1472 bajtów zapobiega przekroczeniu standardowego rozmiaru
datagramu UDP dla MTU 1500 bajtów i ogranicza ryzyko fragmentacji
pakietów IP.

W przypadku innego MTU, dodatkowych nagłówków lub tunelowania
rzeczywisty bezpieczny limit może być mniejszy.

## Przykład konfiguracji

Poniższy przykład przedstawia dwie niezależne listy NetVar.

```yaml
wago_netvar:
  - id: netvar_read
    ip_address: 192.168.1.10
    port: 1202
    list_ID: 126
    var_count: 3
    direction: read
    endian: little
    variables:
      - name: temperatura
        type: REAL
        byte_offset: 0
      - name: alarm
        type: BOOL
        byte_offset: 4
      - name: cisnienie
        type: REAL
        byte_offset: 6

  - id: netvar_write
    ip_address: 192.168.1.10
    port: 1202
    list_ID: 127
    var_count: 2
    direction: write
    endian: little
    variables:
      - name: zadana_temperatura
        type: REAL
        byte_offset: 0
      - name: zadane_cisnienie
        type: REAL
        byte_offset: 4
```

Uwaga: przykład pokazuje sposób rozmieszczenia zmiennych i konfiguracji
list. Dostępność poszczególnych opcji oraz sposób mapowania encji
zależą od aktualnej implementacji komponentu.
Uwagi
- Każda lista NetVar ma własny identyfikator.
- Można używać kilku list z tym samym adresem IP i portem UDP.
- BOOL zajmuje jeden bajt, a nie pojedynczy bit.
- byte_offset oznacza pozycję w payload.
- bit_offset nie jest używany.
- Nie należy przekraczać limitu 1472 bajtów dla całego datagramu UDP.
- Maksymalny payload wynosi 1452 bajty przy założeniu 20-bajtowego
  nagłówka NetVar.
  
  