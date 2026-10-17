# Debugowanie

## Gdzie szukać komunikatów

| Źródło | Jak zobaczyć |
|---|---|
| wyjście programu uruchomionego przez `crtos run` | w terminalu komputera |
| wyjście usług i programów z menu | konsola szeregowa: `crtos log` albo `crtos serial` |
| log jądra (sterowniki, start, błędy) | `crtos kmon dmesg` (ostatnie 4 KB; `"dmesg 16000"`: więcej) |
| komunikaty startu | `crtos log --reset --time 20` (restart i 20 s komunikatów) |

Błędy w logu zaczynają się od `E:`, ostrzeżenia od `W:`.

## Monitor jądra (kmon)

Monitor jądra działa zawsze, nawet gdy nie wystartowała żadna usługa. Są dwie drogi dostępu:

- **przez sondę** (nie zajmuje portu szeregowego): `crtos kmon` interaktywnie albo
  `crtos kmon ps mem`, aby wykonać polecenia i zakończyć,
- **przez port szeregowy**: w `crtos serial` naciśnij Ctrl-]. Polecenie `exit` oddaje
  konsolę powłoce.

| Polecenie | Pokazuje / robi |
|---|---|
| `help` | listę poleceń |
| `ps` | wątki: priorytet, stan, obciążenie CPU, zużycie stosu |
| `procs` | procesy: pamięć, uchwyty, uprawnienia |
| `mem` | pule pamięci jądra i test ich spójności |
| `vmem` | pamięć emulowana: plik wymiany, pamięć podręczna stron, regiony procesów, liczniki dostępów |
| `mpu` | regiony MPU |
| `irq` | zarejestrowane przerwania i ich liczniki |
| `net` | interfejsy sieciowe i gniazda |
| `uptime` | czas od startu, bezczynność CPU, spóźnione tyknięcia zegara (przerwania zablokowane dłużej niż 1 ms) |
| `dmesg [bajty]` | koniec logu jądra |
| `run [-w] program [arg]` | uruchamia program (`-w`: czeka na koniec) |
| `kill <id wątku>` / `kill -p <pid>` | kończy wątek albo proces |
| `lsmod`, `insmod`, `rmmod` | moduły jądra |
| `dt [węzeł]`, `devices`, `drivers` | drzewo urządzeń, urządzenia, sterowniki |
| `ls`, `cat`, `hexdump`, `rm`, `mkdir`, `mv`, `df` | pliki na karcie |
| `sd`, `sdbench` | stan i szybkość karty SD |
| `evtest`, `fbtest` | test wejścia (dotyk, przyciski) i ekranu |
| `fb [WxH]` | tryb ekranu (rozmiar, odświeżanie); z rozmiarem zmiana na inny tryb panelu, gdy nikt nie używa ekranu ([Ekran i dotyk](sterowniki.md#ekran-i-dotyk)) |
| `test all` | testy jądra (13 grup) |
| `reboot`, `panic` | restart; celowy błąd krytyczny |

## Awaria programu

Gdy program odwoła się poza swoją pamięć, przepełni stos albo wykona błędną instrukcję,
jądro kończy tylko ten program i zapisuje raport w logu:

```
*** MemManage: memory access violation at 00000000
    task 23 'apptest' (process 12 'apptest', arena 80400000)
    cfsr=00000082 hfsr=00000000 exc_return=fffffffd
    pc=80402494 lr=8040233b sp=8045fce0 psr=61000000
    ...
    -> process terminated
```

Wpisz wtedy:

```
.\crtos crash
```

`crtos crash` pobiera log z płytki, znajduje ostatni raport i zamienia adresy `pc`
(miejsce błędu) oraz `lr` (miejsce wywołania) na funkcje i linie kodu:

```
80402494: main+0x220 child_main .../tests/apptest/apptest.c:598
8040233b: main+0xc7  child_main .../tests/apptest/apptest.c:597
```

Pod spodem działa `tools/appsym.py`: bierze niestripowaną kopię programu
(`build/.../<nazwa>.debug.app`) i adres areny z raportu. Program z szybkim kodem w ITCM
(`crtos_app FAST`, [Wydajność](wydajnosc.md#szybki-kod-programu-w-itcm)) ma w raporcie
jeszcze adres tego bloku (`arena 80800000, fast 0000c000`), który `crtos crash` przekazuje
jako `--fast`. Program wykonywany w miejscu (XIP, [Toolchain](toolchain.md)) ma w raporcie
adres tekstu i tablicy GOT (`text 60780000, got 80500010`); adres kodu to wtedy adres tekstu
plus przesunięcie w pliku, co `crtos crash` też liczy. Dla awarii w jądrze `crtos crash` używa
`build/kernel/crtos.axf`. Adresy trzeba tłumaczyć tą samą wersją programu, która działała na
płytce. Programy kompilatora na płytce mają kopie z symbolami w `build/toolchain/native/debug`
(`python tools/appsym.py build/toolchain/native/debug/cc1.debug.app TEKST PC LR`).

Najczęstsze przyczyny:

| Raport | Przyczyna |
|---|---|
| `memory access violation at 00000000` (albo mały adres) | dereferencja `NULL` |
| `-> stack overflow (user stack)` | za mały `STACK` w `CMakeLists.txt` albo zbyt głęboka rekurencja; duże tablice przenieś na stertę |
| adres w pamięci jądra (`2000xxxx`, `6xxxxxxx`) | zły wskaźnik; program nie ma dostępu do pamięci jądra |
| `UsageFault: undefined instruction` albo `invalid state` | uszkodzony wskaźnik na funkcję albo nadpisany stos |
| `UsageFault: division by zero` | dzielenie całkowite przez zero |

Program z `libcrtosheap` (np. NetSurf, kompilator na płytce), który zwolni blok drugi raz,
kończy się przez `abort()` (kod 134) bez raportu z rejestrami, ale z komunikatem sterty:

```
heap: bad pointer 0x80fef9b8 freed or reallocated, called from 0x8042a1c5 (arena 0x80400000)
```

`called from` to adres powrotu z `free` albo `realloc`, czyli linia tuż za wywołaniem:

```
python tools/appsym.py build/apps/netsurf/netsurf.debug.app 80400000 8042a1c5
```

W programie XIP w miejsce areny podaje się adres kodu z logu (`code in place at 60280000`).

## Błąd krytyczny jądra (panic)

Błąd w jądrze albo w sterowniku zatrzymuje system: raport idzie na konsolę szeregową,
a płytka restartuje się po 10 s. Raport zostaje w pamięci, której start nie czyści,
i pojawia się w logu po restarcie:

```
*** the previous run ended with a panic: ...
```

`crtos crash` pokazuje go tak samo jak awarię programu. Awarię w sterowniku zlokalizujesz
przez `tools/appsym.py build/drivers/<moduł>.debug.ko <adres modułu z lsmod> <pc>`.

## Debuger (krok po kroku)

- **MCUXpresso IDE**: zaimportuj projekt, zbuduj konfigurację Release i uruchom
  *crtos LinkServer Release*. Działają pułapki, podgląd zmiennych i rejestrów jądra.
  Zobacz [Budowanie](budowanie.md#mcuxpresso-ide-opcjonalnie).
- **pyOCD + GDB**: `pyocd gdbserver -t mimxrt1050_hyperflash`, a potem
  `arm-none-eabi-gdb build/kernel/crtos.axf` i `target remote :3333`.

Programy (`.app`) ładują się pod adres areny znany dopiero w trakcie działania, więc ich
symboli debuger nie zna. Do programów wystarczają zwykle `printf` i `crtos crash`.

## Konsola przez USB (J9)

Drugi kabel micro-USB między gniazdem **J9** a komputerem daje drugą powłokę, niezależną od
konsoli na J28. Na Windows płytka pojawia się jako „Urządzenie szeregowe USB (COMx)”, numer
portu pokaże Menedżer urządzeń. Otwórz port dowolnym programem terminala, prędkość nie ma
znaczenia:

```
python -m serial.tools.miniterm COM6          # Windows (numer z Menedżera urządzeń)
python -m serial.tools.miniterm /dev/ttyACM0  # Linux
```

Działa też PuTTY (Serial) i Tera Term. Powłoka startuje, gdy program otworzy port, i kończy
się, gdy go zamknie. Działają edycja linii, Backspace i Ctrl-C. Log jądra pokazuje przebieg
(`crtos kmon dmesg`): `usb: cable plugged in`, `usb: connected to a computer (high speed)`,
`usb: a terminal opened /dev/ttyACM0`.

Gdy port się nie pojawia, sprawdź, czy kabel ma linie danych (nie tylko do ładowania) i czy
log pokazuje `usb: cable plugged in`. Jeśli J9 jest w trybie host, zobacz
[USB (J9): urządzenie albo host](sterowniki.md#usb-j9-urządzenie-albo-host).

## Karta SD wyjęta w czasie pracy

Jeśli karta zostanie wyjęta (albo straci kontakt), odczyty kończą się błędami
(`E: sd: command failed ...`, `card initialisation failed`), a programów nie da się
uruchamiać. Po włożeniu karty wystarczy:

```
crtos kmon "sd reinit"
```

(wynik `sd: reinit 0`), bez restartu płytki. Samo włożenie karty nie jest wykrywane.

## Inne narzędzia

| Narzędzie | Do czego |
|---|---|
| `crtos shot` | zrzut ekranu przez sondę, nawet gdy grafika się zawiesiła |
| `crtos run gfxtap tap X Y` | symulowane dotknięcie ekranu (testy, zdalna obsługa); `gfxtap hold X0 Y0 X1 Y1 [MS]`: dwa palce naraz; mysz: `mclick X Y [N]`, `mdrag X0 Y0 X1 Y1`, `wheel X Y N [h]`, `hover X Y [X1 Y1 [kroki]]` (ruch bez przycisku: podpowiedzi paska zadań); klawisze: `key 29+46` (Ctrl-C: kody Linuksa, naciskane po kolei), `type tekst` (słowa osobno, `\n` to Enter) |
| `crtos run gfxinfo` | statystyki serwera grafiki, test liczby klatek |
| `crtos run evtest` | surowe zdarzenia dotyku i przycisków |
| `crtos run apptest` | test API programów (1762 sprawdzenia) |
| `crtos netbench` | przepustowość sieci w obie strony (TCP, UDP), obciążenie procesora w trakcie i liczniki `eth0` |
| `crtos run spi ...`, `uart ...`, `mtd ...` | magistrala SPI, port szeregowy J22, pamięć flash ([Sterowniki](sterowniki.md#spi-j24)) |
| `tools/rescue.py` | odzyskanie dostępu do procesora, gdy firmware blokuje debuger |
| tryb awaryjny (przycisk **SW8** podczas startu) | samo jądro i monitor, bez sterowników i usług |

**Profil bez zatrzymywania procesora.** Rejestr DWT_PCSR (`0xE000101C`) czytany przez sondę
(`swdcon.open_session()`, `target.read32`) podaje bieżący PC rdzenia. Rdzeń przy tym nie
staje, więc tak można profilować także sieć i dźwięk, które zatrzymanie by zaburzyło.
Daje to ok. 500 próbek na sekundę. Adresy zamienia się na funkcje jak w raporcie awarii:
jądro z `crtos.axf`, moduł od adresu z `lsmod` (sekcje: kod, dane, bss), program z
`*.debug.app`.
