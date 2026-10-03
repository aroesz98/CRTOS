# DualSense Edge → ESP32 po Bluetooth (Zephyr)

Aplikacja Zephyr, która łączy się z padem DualSense / DualSense Edge przez
Bluetooth i wypisuje na konsoli każdy wciśnięty (i puszczony) przycisk.

## Sprzęt: klasyczny ESP32, nie ESP32-S3

DualSense i DualSense Edge używają wyłącznie **Bluetooth Classic (BR/EDR)** —
HID na kanałach L2CAP. **ESP32-S3 ma radio tylko BLE**, więc fizycznie nie jest
w stanie zobaczyć tego pada (pad w ogóle nie rozgłasza się po BLE). Dlatego
projekt celuje w oryginalny ESP32 (moduł **ESP32-WROOM-32**), który ma radio
dual-mode:

| Płytka | Target Zephyra |
| --- | --- |
| ESP32-DevKitC / klony z WROOM-32 | `esp32_devkitc/esp32/procpu` |

## Budowanie i wgrywanie

Wymagany workspace Zephyra z obsługą BT Classic na ESP32 (sprawdzone z 4.4.99)
oraz pobrane bloby Espressifa (`west blobs fetch hal_espressif`).

```powershell
$env:ZEPHYR_BASE = "C:\Users\Guitarek\zephyrproject\zephyr"
cd C:\Users\Guitarek\Desktop\esp32-s3-bt-mod

west build -p always -b esp32_devkitc/esp32/procpu
west flash                    # ewentualnie: west flash --esp-device COM5
west espressif monitor        # ewentualnie: west espressif monitor -p COM5
```

## Użycie

1. **Pierwsze parowanie:** przy wyłączonym padzie przytrzymaj **CREATE + PS**
   (ok. 3–5 s), aż pasek świetlny zacznie szybko migać. ESP32 cały czas
   skanuje, znajdzie pada, sparuje się (SSP „Just Works”, bez PIN-u) i otworzy
   kanały HID.
2. **Kolejne połączenia:** wystarczy nacisnąć **PS** — pad sam łączy się
   z zapamiętanym hostem. Klucz parowania jest trzymany we flashu (NVS),
   więc przetrwa restart ESP32.
3. Obsługiwany jest jeden pad naraz; sparowanie nowego zastępuje poprzedniego.

Oczekiwane wyjście na konsoli:

```
[00:00:01.420,000] <inf> main: Hold CREATE + PS on the controller until the light bar blinks to pair,
[00:00:01.420,000] <inf> main: or press PS on an already paired controller
[00:00:07.113,000] <inf> main: Found controller XX:XX:XX:XX:XX:XX, connecting
[00:00:07.902,000] <inf> main: Connected to XX:XX:XX:XX:XX:XX (outgoing)
[00:00:08.731,000] <inf> main: Link secured (level 2)
[00:00:08.790,000] <inf> hid_host: HID control channel connected
[00:00:08.842,000] <inf> hid_host: HID interrupt channel connected
[00:00:08.842,000] <inf> main: Controller ready, waiting for button presses
[00:00:08.901,000] <inf> dualsense: Full input report active
[DualSense] CROSS pressed
[DualSense] CROSS released
[DualSense] D-PAD UP pressed
[DualSense] BACK LEFT pressed
[DualSense] L2 pressed
[DualSense] L2 = 87
[DualSense] L2 = 255
[DualSense] LEFT STICK x = -64, y = 101
[DualSense] LEFT STICK x = 0, y = 0
```

Rozpoznawane przyciski: krzyżak (4 kierunki), SQUARE, CROSS, CIRCLE, TRIANGLE,
L1, R1, L2, R2, L3, R3, CREATE, OPTIONS, PS, TOUCHPAD (klik), MUTE oraz
specyficzne dla wersji Edge: FN LEFT, FN RIGHT, BACK LEFT, BACK RIGHT.

Wartości analogowe:

- **L2 / R2** — siła nacisku 0 (puszczony) … 255 (wciśnięty do końca),
  niezależnie od zdarzeń `L2 pressed/released`.
- **LEFT STICK / RIGHT STICK** — wychylenie od środka, ok. ±127 na oś;
  w prawo i w górę są dodatnie, `x = 0, y = 0` to gałka w spoczynku.

Żeby konsola nadążała, wartości analogowe są filtrowane (stałe w
`src/dualsense.c`): martwa strefa gałek `DS_STICK_DEADZONE`, minimalna zmiana
`DS_ANALOG_MIN_DELTA` i najwyżej jedna linia na 50 ms na kontrolkę
(`DS_ANALOG_PRINT_INTERVAL_MS`). Powrót do spoczynku jest drukowany zawsze.

## Wyjście dla emulatora PSX (UART2)

Poza logiem na konsoli stan pada jest wysyłany binarnie na **UART2**, żeby
odebrał go emulator PSX na i.MX RT1050 (`source/gamepad.c` w projekcie
`psxe_embedded`). Konsola na UART0 zostaje nietknięta.

Połączenie (piny UART2 z plików płytki, wolne na WROOM-32):

| ESP32 DevKitC | i.MX RT1050-EVKB |
| --- | --- |
| GPIO17 (UART2 TX) | GPIO_AD_B1_07 = LPUART3_RXD (złącze Arduino, D0) |
| GPIO16 (UART2 RX) | GPIO_AD_B1_06 = LPUART3_TXD (złącze Arduino, D1) - na razie nieużywany |
| GND | GND |

Prędkość 460800 8N1 (`app.overlay`). Ramka ma 13 bajtów:

```
a5 5a  buttons[0..2]  lx ly rx ry  l2 r2  seq  xor(bajty 2..11)
```

`buttons` to dokładnie bitmapa `enum ds_button` z `src/dualsense.c` (bit 0 =
D-PAD UP, ... bit 16 = PS), gałki i spusty tak, jak przychodzą w raporcie
(0x80 = środek gałki). Mapowanie na przyciski PlayStation robi emulator, ESP32
nie wie nic o protokole pada PSX.

Zmiana stanu przycisku wychodzi natychmiast; ruch gałek jest ograniczony do
jednej ramki na 8 ms, a niezmieniony stan powtarzany co 200 ms, żeby odbiornik
widział, że łącze żyje. Rozłączenie pada wysyła ramkę „wszystko puszczone",
więc emulator nie zostaje z wciśniętym przyciskiem.

Po stronie emulatora: przycisk **PS** przełącza tryb analogowy emulowanego pada,
a lewa gałka działa równolegle jako krzyżak.

## Jak to działa

Zephyr ma stos BT Classic (L2CAP, SSP, SDP), ale nie ma profilu HID host,
więc jest on zrobiony tutaj bezpośrednio na L2CAP:

| Plik | Rola |
| --- | --- |
| `src/main.c` | inquiry (szukanie urządzeń klasy „gamepad”), zestawianie połączenia ACL, obsługa reconnectu, ponawianie skanowania po rozłączeniu |
| `src/hid_host.c` | HIDP na L2CAP: kanał control (PSM 0x11) i interrupt (PSM 0x13), w obu kierunkach (host otwiera kanały przy parowaniu, pad przy reconnekcie) |
| `src/dualsense.c` | parser raportów wejściowych i wypisywanie zmian stanu przycisków |

Po połączeniu pad wysyła po Bluetooth uproszczony raport `0x01` (bez MUTE
i przycisków Edge). Odczyt raportu feature `0x05` (kalibracja) przełącza go na
pełny raport `0x31` — aplikacja robi to automatycznie i obsługuje oba formaty.

Wyszukiwanie (inquiry) zajmuje radio i utrudnia nasłuch połączeń
przychodzących, dlatego działa w seriach: ok. 4 s inquiry, potem 5 s przerwy,
w której ESP32 tylko nasłuchuje (szybki, przeplatany page scan). Parametry są
w `src/main.c` (`INQUIRY_LENGTH`, `INQUIRY_PAUSE`). Po zestawieniu połączenia
nasłuch jest wyłączany, żeby nie zabierać czasu radiowego padowi.

## Status

Sprawdzone na sprzęcie (ESP32-WROOM-32 rev. 1 + DualSense Edge): parowanie,
pełny raport `0x31`, wszystkie standardowe przyciski oraz FN LEFT / FN RIGHT.
Układ raportów pochodzi ze sterownika Linuksa `hid-playstation`, a bity
przycisków Edge ze sterownika PS5 w SDL.

## Rozwiązywanie problemów

- **Pad nie jest znajdowany** — upewnij się, że pasek szybko miga (tryb
  parowania); samo naciśnięcie PS na niesparowanym padzie nie wystarczy.
- **`Security failed`** — wprowadź pada ponownie w tryb parowania; stary bond
  po stronie ESP32 jest wtedy kasowany automatycznie.
- **Pad nie wraca po naciśnięciu PS** — sprawdź, czy w logu pojawia się
  `Connection request from ...`. Jeśli nie, żądanie pada nie dociera do ESP32:
  wydłuż `INQUIRY_PAUSE` albo skróć `INQUIRY_LENGTH` w `src/main.c`.
- **Pad łączy się z PS5/PC zamiast z ESP32 po naciśnięciu PS** — pad pamięta
  tylko ostatniego hosta; sparuj go ponownie z ESP32.
- **Logi pojawiają się z opóźnieniem** — domyślnie wątek logów Zephyra budzi
  się raz na sekundę albo co 10 wiadomości; `prj.conf` ustawia
  `CONFIG_LOG_PROCESS_TRIGGER_THRESHOLD=1`, żeby każde zdarzenie wychodziło
  od razu. Po zmianie `prj.conf` zbuduj z `-p always`.
- **Więcej logów ze stosu BT** — dodaj do `prj.conf` np.
  `CONFIG_BT_L2CAP_LOG_LEVEL_DBG=y` lub `CONFIG_BT_CONN_LOG_LEVEL_DBG=y`.
