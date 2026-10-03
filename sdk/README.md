# CRTOS SDK

Zestaw do pisania programów dla CRTOS **poza** drzewem źródeł systemu: we własnym katalogu,
we własnym repozytorium. Nie potrzebujesz źródeł jądra ani sterowników.

SDK tworzy polecenie `crtos sdk` w drzewie źródeł CRTOS (wynik trafia do `build/sdk`).
Skopiuj ten katalog, gdzie chcesz.

## Co jest w środku

| Katalog / plik | Zawartość |
|---|---|
| `crtos`, `crtos.cmd` | polecenie `crtos` (Linux/macOS / Windows) |
| `bin/` | toolchain: `arm-crtos-gcc`, `arm-crtos-g++` i pozostałe `arm-crtos-*`, `crtos-app` |
| `include/` | nagłówki: `crtos.h`, `crtos/*.h` (interfejs jądra), `pthread.h`, gniazda sieciowe, `gfx.h` (okna i rysowanie), `tftlib/` |
| `lib/` | `crtos.specs` (reguły budowania programu), obiekty startowe `crtos-*.o`, biblioteki `libcrtos.a`, `libgfx.a`, `libtftlib.a` |
| `cmake/` | obsługa CMake: `crtos-sdk.cmake`, `crtos-app.cmake`, plik toolchaina |
| `templates/` | szablony nowych programów |
| `tools/` | skrypty polecenia `crtos` |
| `src/` | źródła `arm-crtos-gcc` i `crtos-app` (`crtos toolchain` buduje je, gdy SDK przyszło bez `bin/`) |

Katalog SDK jest zarazem katalogiem toolchainu CRTOS. Program zbudujesz także bez CMake:
`bin/arm-crtos-gcc -O2 hello.c -o hello.app` (zob. `docs/toolchain.md` w drzewie źródeł
CRTOS).

## Czego potrzebujesz

Pythona 3.8+, CMake 3.20+, Ninja i Arm GNU Toolchain (`arm-none-eabi-gcc`).
Brakujące rzeczy zainstaluje:

```
crtos setup
```

`crtos doctor` pokazuje, co jest zainstalowane i czy płytka odpowiada.

Na płytce musi działać CRTOS, najlepiej zbudowany z tych samych źródeł co SDK.

## Pierwszy program

Wpisz w dowolnym katalogu roboczym (na Windows: `ścieżka\do\sdk\crtos`, albo dodaj katalog
SDK do zmiennej PATH):

```
crtos new hello        # program z oknem (--console: konsolowy, --service: usługa)
cd hello
crtos build            # -> build/sdcard/crtos/apps/hello.app
crtos deploy           # wysyła program na kartę SD płytki (przez sieć albo sondę)
crtos run hello        # uruchamia go i pokazuje, co wypisuje (Ctrl-C kończy)
```

## CMakeLists.txt programu

`crtos new` tworzy taki plik:

```cmake
cmake_minimum_required(VERSION 3.20)
if(NOT CRTOS_SDK)
  set(CRTOS_SDK $ENV{CRTOS_SDK})           # "crtos build" podaje -DCRTOS_SDK
endif()
set(CMAKE_TOOLCHAIN_FILE ${CRTOS_SDK}/cmake/toolchain-arm-none-eabi.cmake)
project(hello C CXX ASM)
include(${CRTOS_SDK}/cmake/crtos-sdk.cmake)
set(CRTOS_DEFAULT_DEST apps)

crtos_app(hello SOURCES hello.c STACK 16384 HEAP 65536 LIBS gfx ICON icon.png)
```

Parametry `crtos_app`:

| Parametr | Znaczenie |
|---|---|
| `SOURCES` | pliki źródłowe (C, C++, asembler) |
| `DEST` | katalog na karcie: `apps` (programy z oknem), `bin` (polecenia), `sbin` (usługi); `flash0:<katalog>` – na dysk `/flash0` |
| `XIP` | program wykonywany w miejscu: kod niezależny od położenia, z `/flash0` działa wprost z flasha (biblioteki XIP z `lib/xip` buduje `crtos build`); nie łączy się z `FAST` ([Toolchain](../docs/toolchain.md#programy-wykonywane-w-miejscu-xip)) |
| `STACK`, `HEAP` | stos głównego wątku i sterta w bajtach (domyślnie 16 KB i 64 KB) |
| `LIBS` | `gfx` (okna i rysowanie), `tftlib` (rysowanie w stylu Adafruit/TFT_eSPI) albo własne biblioteki statyczne |
| `FAST`, `FAST_KB` | plik z listą najgorętszych funkcji (symbol w wierszu) i limit w KB (domyślnie 48): jądro umieszcza je w ITCM, jeśli obszar szybkiego kodu jest wolny ([Wydajność](../docs/wydajnosc.md#szybki-kod-programu-w-itcm)) |
| `INCLUDES`, `DEFINES` | dodatkowe katalogi nagłówków i definicje |
| `ICON` | ikona programu: kwadratowy PNG (zwykle 64 × 64, z przezroczystością) → `/crtos/share/icons/<nazwa>.pam` na karcie; menedżer okien pokazuje ją w menu **Apps**, na pasku zadań i w pasku tytułu (`crtos new` daje `icon.png` do podmiany) |

Bez polecenia `crtos` zbudujesz program samym CMake:

```
cmake -S . -B build -G Ninja -DCRTOS_SDK=<katalog SDK>
cmake --build build
```

Pełna dokumentacja (API, uruchamianie, debugowanie) jest w katalogu `docs/` drzewa źródeł
CRTOS.
