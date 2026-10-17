# Łatki CRTOS

Kodu z zewnątrz nie ma w repozytorium. `third_party/sources.txt` wymienia repozytoria i commity,
a ten katalog przechowuje zmiany, które CRTOS do nich wprowadza. Każda zmiana w kodzie jest
oznaczona komentarzem `CRTOS`.

| Łatka | Do czego | Kto nakłada |
|---|---|---|
| `<katalog>.patch` | `third_party/<katalog>` (np. `lwip.patch`, `netsurf-libs/curl.patch`) | `tools/thirdparty.py` |
| `arm-gnu-toolchain/<katalog>/*.patch` | źródła Arm GNU Toolchain 14.3.Rel1 (`gcc/`), z których powstaje kompilator na płytkę | `toolchain/native/build.sh fetch` |

## Jak to działa

`tools/thirdparty.py` uruchamia się przy każdej konfiguracji CMake (`crtos build`):

- brakujące źródła grupy `base` (lwIP, TinyUSB, pdpmake, sterowniki NXP SDK, newlib i biblioteki
  GCC) pobiera jako płytkie checkouty gita wskazanych commitów (za pierwszym razem ok. 40 MB, na
  dysku ok. 120 MB; potrzebne są git i sieć);
- katalog, którego commit w `sources.txt` albo łatka się zmieniły, doprowadza do nowego stanu:
  zdejmuje starą łatkę (jej kopia leży w `.git/crtos.patch` checkoutu), przełącza commit i nakłada
  nową;
- katalogu z własnymi zmianami (poza łatką) nie rusza, tylko to zgłasza;
- gdy wszystko się zgadza, nic nie wypisuje (trwa to ok. 0,3 s).

Pliki checkoutów mają końce linii takie jak w repozytorium (LF): każdy checkout ma
`core.autocrlf=false` i `core.eol=lf`, niezależnie od ustawień gita na komputerze. Łatki są
diffem tej postaci. Bez tego Windows zapisałby z CRLF pliki oznaczone w repozytorium jako
tekst (lwIP: `*.c text`), a `git apply` nie widzi atrybutów częściowego checkoutu, więc łatka
by się nie nałożyła. Starszy checkout bez tych ustawień, do którego łatka nie pasuje, narzędzie
raz zapisuje od nowa.

NetSurf z bibliotekami (grupa `netsurf`, około 110 MB) pobiera się na życzenie:
`crtos setup --netsurf` albo `python tools/thirdparty.py netsurf`.

## Zmiana kodu z zewnątrz

1. Zmień pliki w `third_party/<katalog>` i oznacz zmiany komentarzem `CRTOS`.
2. `python tools/thirdparty.py --save <katalog>` zapisuje `git diff` checkoutu względem commitu do
   `patches/<katalog>.patch`. Nowe pliki trzeba najpierw dodać przez `git add -N`.
3. `python tools/thirdparty.py --status` pokazuje stan każdego katalogu (commit, łatka, własne
   zmiany).

Nowa wersja biblioteki: zmień commit w `sources.txt`. Jeśli łatka się nie nakłada, narzędzie to
zgłosi. Wtedy nanieś zmiany ręcznie na nowej wersji i zapisz je przez `--save`.

Łatki są zapisane bajt w bajt (`.gitattributes`: `*.patch -text`). Pliki lwIP od NXP mają końce
linii CRLF, więc `lwip.patch` też je ma.

## Co zmieniają

| Łatka | Zmiany |
|---|---|
| `lwip.patch` | `sockets.c`: hak `LWIP_HOOK_CRTOS_SOCKET_EVENT`. Jądro dostaje tylko zdarzenia gniazd, które mogą obudzić czekające wywołanie (dane albo miejsce po ich braku, błąd); każde zdarzenie kosztowało ok. 10% procesora. `tcp.c`: rzutowanie w komunikacie debugowania na `tcpwnd_size_t` (32 bity przy `LWIP_WND_SCALE`). |
| `pdpmake.patch` | `rules.c`: domyślny `CC` to `gcc`, kompilator na płytce. `main.c`: `get_shell()` zwraca `$SHELL` albo `/sd/crtos/bin/sh.app` (newlib nie ma `confstr`). `make.c`: `timespec_le()` – równe czasy liczą się jako aktualne (FAT zapisuje czas co 2 s). |
| `netsurf.patch` | Front-end framebuffer na oknie CRTOS: zdarzenia, schowek, harmonogram. Duktape: osobne środowisko na każdą stronę, skrypty przygotowujące (polyfill), limit czasu skryptu, `Date.now()`. Brakujące części DOM i BOM: `Location`, `Navigator`, rozmiary okna, `submit()`, `href`. Obrazy JPEG, PNG i WebP, konfiguracja (`docs/netsurf.md`). |
| `netsurf-libs/curl.patch` | `vtls/mbedtls.c`: plik zaufanych certyfikatów CA jest parsowany raz i współdzielony przez połączenia. |
| `netsurf-libs/libdom.patch` | `core/node.c`: faza przechwytywania obejmuje przodków celu, a faza bąbelkowania działa tylko dla zdarzeń, które bąbelkują. |
| `netsurf-libs/nsgenbind.patch` | Metody i atrybuty wiązań JavaScript są zapisywalne i konfigurowalne jak w Web IDL; dłuższy bufor nazwy pliku tymczasowego. |
| `arm-gnu-toolchain/gcc/*.patch` | Kompilator na płytce: host `arm*-*-eabi*`, nazwy `.app`, katalog plików tymczasowych, opcje z pliku `specs`, strony odśmiecacza z `memalign` (`docs/specyfikacja/komponenty/T02-toolchain.md`). |
