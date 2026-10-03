# Polecenie `crtos`

`crtos` to jedno polecenie do budowania systemu i pracy z płytką (`tools/crtos.py`).
Uruchamiasz je z katalogu repozytorium:

- Windows (PowerShell, cmd): `.\crtos ...`
- Linux, macOS, Git Bash: `./crtos ...`

`crtos --help` pokazuje listę poleceń, a `crtos POLECENIE --help` opcje jednego polecenia.

## Przygotowanie

| Polecenie | Działanie |
|---|---|
| `crtos doctor` | sprawdza narzędzia, sondę, port szeregowy, czy CRTOS działa na płytce i czy odpowiada przez sieć |
| `crtos setup` | instaluje brakujące narzędzia i pakiety Pythona, pobiera kod z zewnątrz do `third_party/` (`tools/thirdparty.py`, robi to też `crtos build`) |
| `crtos setup --netsurf` | dodatkowo pobiera źródła przeglądarki NetSurf (grupa `netsurf` w `third_party/sources.txt`) i jej narzędzia |

## Budowanie

| Polecenie | Działanie |
|---|---|
| `crtos build` | buduje wszystko do `build/` |
| `crtos build kernel` | tylko jądro |
| `crtos build NAZWA...` | wybrane programy lub sterowniki (np. `crtos build paint sh`) |
| `crtos build --base` | sam system bazowy: jądro, sterowniki, `system/` (bez `apps/`, `tests/`, `examples/`) |
| `crtos build --rtos [KATALOG]` | obraz samego RTOS z aplikacją z KATALOGU (domyślnie `examples/rtos/blinky`) w `build/rtos` ([RTOS](rtos.md)) |
| `crtos build -v` | pokazuje polecenia kompilatora |
| `crtos build -j 4` | liczba równoległych zadań |
| `crtos build --fresh` | konfiguruje CMake od nowa |
| `crtos clean` | usuwa wyniki budowania |
| `crtos clean --all` | usuwa cały katalog `build/` |
| `crtos new NAZWA` | nowy program z oknem w `apps/NAZWA` (i wpis w menu) |
| `crtos new NAZWA --console` | nowy program konsolowy w `apps/NAZWA` (instalowany w `/crtos/bin`) |
| `crtos new NAZWA --service` | nowa usługa w `apps/NAZWA` (instalowana w `/crtos/sbin`) |
| `crtos new NAZWA --title "Tytuł"` | nazwa programu w menu **Apps** |
| `crtos sdk [KATALOG]` | tworzy SDK w `build/sdk` (i kopię w KATALOGU) |
| `crtos toolchain` | `arm-crtos-gcc`, `arm-crtos-g++` i `crtos-app` w `build/toolchain/arm-crtos` ([Toolchain](toolchain.md)) |
| `crtos toolchain native [kroki]` | GCC i binutils działające na płytce, budowane w WSL/Linuksie do `build/toolchain/native` (kroki: `fetch`, `cross`, `native`, `install`; zmienna `CRTOS_WSL` wybiera dystrybucję WSL) |
| `crtos src [--check] [--full] [--dry-run]` | źródła programów drzewa z plikami `Makefile` do `/sd/crtos/src`, żeby budować je na płytce (`tools/boardsrc.py`; `--check`: najpierw kompilacja każdego pliku tutaj tak jak na płytce) |
| `crtos toolchain install [--full] [--dry-run]` | kompilator na płytkę przez sieć: `/flash0` i `/sd/crtos/usr`, tylko zmienione pliki |
| `crtos kbuild` | buduje jądro projektem MCUXpresso IDE (do `Release/`) |

## Wgrywanie

| Polecenie | Działanie |
|---|---|
| `crtos flash` | zapisuje jądro (`build/kernel/crtos.bin`) w pamięci flash i restartuje płytkę |
| `crtos flash PLIK` | zapisuje wskazany obraz (`.bin` albo `.axf`) |
| `crtos flash --net` | to samo przez sieć, bez sondy: jądro wgrywa działający system ([Aktualizacja jądra bez sondy](sterowniki.md#aktualizacja-jądra-bez-sondy)) |
| `crtos flash --net --host ADRES` | płytka o podanym adresie IP |
| `crtos deploy` | wysyła zmienione pliki systemu na kartę SD płytki i na `/flash0` (z `build/flash0`; sieć, a gdy jej nie ma, sonda USB) |
| `crtos deploy -n` | pokazuje, co zostałoby wysłane |
| `crtos deploy --full` | wysyła wszystkie pliki |
| `crtos deploy --net` / `--swd` / `--serial` | tylko przez sieć / sondę / port szeregowy |
| `crtos deploy --host ADRES` | płytka o podanym adresie IP |
| `crtos sdcard E:` | kopiuje pliki na kartę w czytniku komputera (tylko do `E:\crtos`) |
| `crtos put PLIK ŚCIEŻKA` | wysyła jeden plik, np. `crtos put a.cfg /sd/crtos/etc/network.cfg` |

## Praca z płytką

| Polecenie | Działanie |
|---|---|
| `crtos run NAZWA [ARG...]` | uruchamia program na płytce, pokazuje jego wyjście i zwraca jego kod wyjścia; Ctrl-C go kończy |
| `crtos run --time 10 NAZWA` | kończy program po 10 s (opcje przed nazwą programu) |
| `crtos run /ram/test.app` | program spod dowolnej ścieżki |
| `crtos serial` | terminal na porcie szeregowym, czyli powłoka `sh` płytki. Ctrl-X kończy. Ctrl-] przełącza na monitor jądra, a jego polecenie `exit` wraca do powłoki. |
| `crtos kmon` | monitor jądra przez sondę, interaktywnie (`help` pokazuje polecenia, Ctrl-C dwa razy kończy) |
| `crtos kmon POLECENIE...` | wykonuje polecenia monitora, np. `crtos kmon ps mem "dmesg 2000"` |
| `crtos kmon --serial ...` | monitor jądra przez port szeregowy zamiast sondy |
| `crtos log` | pokazuje wyjście portu szeregowego (Ctrl-C kończy) |
| `crtos log --reset --time 20` | restartuje płytkę i przez 20 s pokazuje komunikaty startowe |
| `crtos shot [PLIK.png]` | zrzut ekranu płytki (domyślnie `screenshot-DATA-GODZINA.png`) |
| `crtos reboot` | restart płytki (przez sondę, a bez niej przez sieć) |
| `crtos find` | płytki z CRTOS w sieci lokalnej |
| `crtos crash` | ostatni raport awarii z płytki, z adresami zamienionymi na linie kodu ([Debugowanie](debugowanie.md)) |
| `crtos bench` | pomiar wydajności: wywołania systemowe, przełączanie, IPC, pamięć, karta SD, start, grafika ([Wydajność](wydajnosc.md)) |
| `crtos bench --save NAZWA` / `--compare NAZWA` | zapisuje wyniki / porównuje z zapisanymi (`build/bench/`) |

## Pliki i zdalny pulpit przez sieć

| Polecenie | Działanie |
|---|---|
| `crtos scp PLIK board:/sd/crtos/home/` | wysyła plik na płytkę (do katalogu albo pod podaną nazwą) |
| `crtos scp -r KATALOG board:/sd/crtos/` | wysyła katalog z zawartością |
| `crtos scp board:/sd/crtos/var/log.txt .` | pobiera plik z płytki |
| `crtos scp -r board:/sd/crtos/src/apps/paint kopia` | pobiera katalog |
| `crtos desktop` | ekran płytki w oknie komputera: mysz, kółko, klawiatura, schowek w obie strony (VNC); okno zmaksymalizowane, obraz jak największy, F11 – pełny ekran |
| `crtos desktop --fullscreen` | od razu na pełnym ekranie (F11 wraca do okna) |
| `crtos desktop --zoom 3` | okno o stałej wielkości: 3 razy ekran płytki |
| `crtos desktop --password` | adres i hasło dla innej przeglądarki VNC |
| `crtos desktop --shot PLIK.png` | jeden obraz ekranu przez sieć (bez sondy) |
| `crtos netbench` | przepustowość sieci: TCP i UDP do płytki i z płytki, po 5 s (`--time S`, `--only tcp-rx,udp-tx`, `--rate MBIT` – tempo UDP do płytki), z obciążeniem procesora w trakcie |
| `crtos desktop --stats` | jak `crtos desktop`, a co sekundę w konsoli: aktualizacje/s, narysowane klatki/s, czas rysowania klatki, KB/s |
| `crtos wallpaper zdjecie.jpg` | obraz z komputera jako tapeta płytki (niżej) |

**`crtos netbench`** uruchamia na płytce program `nettest` (przez sondę, jak `crtos kmon`)
i łączy się z nim przez sieć na porcie 5001. Wynik w Mbit/s mierzy strona, która odbiera.
Pod każdym testem są wątki płytki, które zajęły procesor, a na końcu liczniki `eth0`
(odebrane, odrzucone i uszkodzone ramki). Na łączu 100 Mbit/s TCP osiąga 90–95 Mbit/s,
a UDP ok. 95 Mbit/s. Przy połączeniu komputera przez Wi-Fi wynik zależy też od radia.

**`crtos scp`** działa jak `scp`: ścieżka na płytce ma przedrostek `board:` (albo sam `:`
lub adres IP, np. `192.168.100.11:/ram/x`); ścieżka bez `/` na początku jest liczona od
`/sd/crtos`. Wiele źródeł, katalog albo ścieżka zakończona `/` oznacza kopiowanie do katalogu.
Nazwy mogą zawierać spacje (w cudzysłowie). Pliki przesyła `deployd` z kontrolą CRC, z tym
samym tokenem co `crtos deploy`:
- zapisać można tylko w `/sd/crtos/...`, `/flash0/...` i `/ram/...` (reszta karty należy do
  użytkownika: gry, stare pliki);
- czytać można całe `/sd`, `/flash0` i `/ram`.

**`crtos desktop`** łączy się z usługą `vncd` płytki (port 5900, protokół VNC/RFB). Za
pierwszym razem tworzy losowe hasło w `~/.crtos/vnc.passwd` i przez `deployd` zapisuje je na
płytce w `/sd/crtos/etc/vnc.passwd`; bez tego pliku płytka nie wpuszcza nikogo. Okno potrzebuje
Pythona z Tk i pakietu Pillow (instaluje go `crtos setup`). Bez Pillow obraz rośnie tylko
o całkowitą wielokrotność, np. na ekranie 1920×1080 trzykrotnie. Działa też każda inna
przeglądarka VNC (TightVNC, TigerVNC, RealVNC): adres i hasło pokazuje
`crtos desktop --password`. W oknie:
- okno otwiera się zmaksymalizowane, a obraz wypełnia je z zachowaniem proporcji ekranu
  płytki (czarne pasy po bokach), także po zmianie wielkości okna; F11 przełącza pełny ekran.
  Obraz skalowany jest ostro: piksele płytki zostają wyraźnymi kwadratami, bez rozmycia;
- lewy przycisk myszy działa jak palec, kółko przewija okno pod kursorem; kliknięcie w czarny
  pas nie trafia do płytki;
- klawisze idą do płytki (także Ctrl+C/Ctrl+V w terminalu i klawisz Windows, który otwiera
  menu programów);
- tekst skopiowany na komputerze można wkleić na płytce, a skopiowany na płytce trafia do
  schowka komputera (przy powrocie do okna).

Płynność: `vncd` wysyła tylko zmienione kafelki ekranu, skompresowane (kodowanie Zlib)
i w formacie ekranu płytki (RGB565). Zmiany interfejsu (okna, menu, przesuwanie, animacje)
dochodzą z częstotliwością odświeżania ekranu, ok. 60 klatek na sekundę. Obraz zmieniający
się na całym ekranie (gra 3D, film) to ok. 10–25 klatek na sekundę, a gra na płytce zwalnia
w tym czasie, bo `vncd` zajmuje procesor na porównanie, kompresję i wysłanie każdej klatki.
Inne przeglądarki VNC też dostają Zlib, jeśli go zgłoszą; najmniej pracy dla płytki to
format 16-bitowy RGB565.

**`crtos wallpaper OBRAZ`** robi z dowolnego obrazu (JPEG, PNG... – czyta go Pillow) tapetę
płytki: zmniejsza go tak, żeby pokrywał ekran 800×480 z zachowaniem proporcji (`--size
SZERxWYS` dla innego ekranu; mniejszego obrazu nie powiększa), zapisuje jako PPM (płytka nie
czyta JPEG ani PNG), wysyła przez `deployd` do `/sd/crtos/share/wallpapers/NAZWA.ppm` i ustawia
przez sondę poleceniem `appearance`. Opcje:
- `--name NAZWA` – nazwa pliku na płytce (domyślnie nazwa obrazu);
- `--fit fill|fit|stretch|center|tile` – dopasowanie do ekranu (domyślnie `fill`:
  wypełnia ekran, nadmiar przycięty);
- `--no-set` – tylko wysyła; tapetę wybiera się potem w Settings (strona Wallpaper).

Pozostałe ustawienia wyglądu (skala, czcionka, kolory, przezroczystość, rogi) zmienia
program Settings albo polecenie `appearance` w powłoce płytki ([Architektura](architektura.md#wygląd-interfejsu)).

## Starsze formy

Działają nadal dla zgodności ze starszymi skryptami:
`crtos swd kmon|deploy|put|shot ...` (tylko przez sondę) i
`crtos net setup|deploy|put|reboot|find` (tylko przez sieć; `net setup` instaluje token przez
sondę).

## Ustawienia

Wszystko jest wykrywane automatycznie. Zmienne środowiska przydają się w nietypowych
sytuacjach:

| Zmienna | Znaczenie |
|---|---|
| `CRTOS_PROBE` | identyfikator sondy, gdy podłączonych jest kilka (pokazuje go `crtos doctor`) |
| `CRTOS_PORT` | port szeregowy, np. `COM5` albo `/dev/ttyACM0` |
| `CRTOS_HOST` | adres IP płytki, gdy nie odpowiada na rozgłoszenia UDP |
| `CRTOS_GCC_BIN` | katalog `bin` kompilatora `arm-none-eabi-gcc` |
| `CRTOS_BUILD` | inny katalog budowania niż `build/` |
| `CRTOS_BAUD` | prędkość portu szeregowego (domyślnie 1000000) |

Pliki `crtos` poza repozytorium:

- `~/.crtos/deploy.token`: token wgrywania przez sieć (jego kopia jest na karcie w
  `/crtos/etc/deploy.token`),
- `~/.crtos/board.host`: ostatni adres płytki,
- `~/.crtos/vnc.passwd`: hasło zdalnego pulpitu (jego kopia jest na karcie w
  `/crtos/etc/vnc.passwd`),
- `build/deployed.json`: co zostało wysłane na płytkę (sumy CRC).
