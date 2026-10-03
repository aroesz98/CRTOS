# Toolchain CRTOS

Toolchain CRTOS to kompilator C i C++, który z pliku źródłowego robi od razu program dla
CRTOS (`.app`). Na komputerze to `arm-crtos-gcc` i `arm-crtos-g++`: nakładka na
Arm GNU Toolchain 14.3 (`arm-none-eabi-gcc`).

```
arm-crtos-gcc -O2 hello.c -o hello.app
```

Ten sam zestaw reguł (`crtos.specs`) stosują budowanie drzewa CRTOS (`crtos build`), SDK,
`arm-crtos-gcc` i kompilator na samej płytce (`gcc` w powłoce płytki, zob. niżej). Program
zbudowany dowolną z tych dróg jest taki sam.

## Instalacja

W drzewie źródeł CRTOS:

```
.\crtos toolchain          -> build\toolchain\arm-crtos
```

Polecenie buduje biblioteki i nagłówki, także biblioteki C i C++ programów wykonywanych
w miejscu (`lib/xip`, za pierwszym razem ok. minuty). Potem kompiluje `arm-crtos-gcc` i
`crtos-app` kompilatorem C komputera, a na koniec dopisuje do `lib/gcc-bin.txt`, gdzie leży
Arm GNU Toolchain. Kompilator C komputera `crtos` szuka w zmiennej `CC`, w `PATH`,
w `C:\mingw64\bin` i w MSYS2. Na Windows wystarczy
`winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT`, na Linuksie `gcc`.

Dodaj `build\toolchain\arm-crtos\bin` do zmiennej `PATH`
([Instalacja](instalacja.md#na-komputerze-arm-crtos-gcc) pokazuje polecenie PowerShell).

SDK (`crtos sdk`, zob. [SDK](sdk.md)) zawiera ten sam toolchain w katalogu `bin/`.

Potrzebne są:
- Arm GNU Toolchain 14.3.Rel1 (instaluje go `crtos setup`);
- kompilator C dla komputera, tylko do zbudowania `arm-crtos-gcc`.

`arm-crtos-gcc` szuka Arm GNU Toolchain w tej kolejności:
1. zmienna `CRTOS_GCC_BIN`;
2. plik `lib/gcc-bin.txt`;
3. `PATH`;
4. typowe katalogi instalacji.

## Pierwszy program

```c
/* hello.c */
#include <stdio.h>
#include <unistd.h>
#include <crtos.h>

CRTOS_APP(8192, 32768);        /* stos 8 KB, sterta 32 KB (bez tego: 16 KB i 64 KB) */

int main(int argc, char **argv)
{
    printf("hello, pid %d\n", (int)getpid());
    return 0;
}
```

```
arm-crtos-gcc -O2 -Wall hello.c -o hello.app
crtos put hello.app /sd/crtos/bin/hello.app
crtos run hello
```

W powłoce płytki (`sh`) program uruchamia się poleceniem `hello`. Programy z `/sd/crtos/bin`
i `/sd/crtos/apps` są w `PATH`.

## Przykłady

| Co | Polecenie |
|---|---|
| C++ | `arm-crtos-g++ -O2 gra.cpp -o gra.app` |
| kilka plików | `arm-crtos-gcc -O2 a.c b.c -o prog.app` |
| osobna kompilacja | `arm-crtos-gcc -c a.c` … `arm-crtos-gcc a.o b.o -o prog.app` |
| okno i rysowanie (libgfx) | `arm-crtos-gcc okno.c -lgfx -o okno.app` |
| TFTLIB | `arm-crtos-g++ demo.cpp -I<toolchain>/include/tftlib -ltftlib -o demo.app` |
| własna biblioteka | `arm-crtos-ar rcs libmoja.a x.o y.o`, potem `-L. -lmoja` |
| zmiana stosu lub sterty | `crtos-app set prog.app --stack 32K --heap 2M` |
| co jest w programie | `crtos-app info prog.app` |
| czy jądro go załaduje | `crtos-app check prog.app` |
| obiekt częściowy (bez programu) | `arm-crtos-gcc -r a.o b.o -o czesc.o` |

Pozostałe narzędzia mają nazwy `arm-crtos-as`, `arm-crtos-ld`, `arm-crtos-objdump`,
`arm-crtos-nm`, `arm-crtos-gdb` itd. Przekazują argumenty do narzędzi `arm-none-eabi-*` bez
zmian.

## Co dodaje arm-crtos-gcc

`arm-crtos-gcc` uruchamia `arm-none-eabi-gcc` z tymi samymi argumentami i dopisuje:

| Opcja | Po co |
|---|---|
| `-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard` | procesor płytki; każda tylko wtedy, gdy nie podasz własnej |
| `-specs=<toolchain>/lib/crtos.specs` | reguły programu CRTOS (niżej) |
| `-B<toolchain>/lib/ -L<toolchain>/lib` | obiekty startowe i biblioteki CRTOS |
| `-isystem <toolchain>/include` | `crtos.h`, `crtos/*.h`, `pthread.h`, gniazda, `gfx.h`… |

Reguły z `crtos.specs`:

- **Nagłówki i biblioteki newlib-nano** (przez `nano.specs` Arm): `newlib.h` pasuje do
  `libc_nano`, a `-lstdc++` staje się `-lstdc++_nano`.
- **Każda kompilacja** dostaje `-DCRTOS_USER=1 -D__CRTOS__=1`, osobne sekcje funkcji
  i zmiennych (`-ffunction-sections -fdata-sections`) i `-fno-common`.
- **C++**: domyślnie bez wyjątków, RTTI i blokad inicjalizacji zmiennych statycznych.
  Opcje `-fexceptions`, `-frtti` i `-fthreadsafe-statics` to zmieniają, ale wyjątki w CRTOS
  nie są sprawdzone.
- **Linkowanie**: program to plik relokowalny ELF (`ld -r`). Wszystko jest w nim
  wlinkowane, bo jądro rozwiązuje tylko jego relokacje i nie udostępnia programom żadnych
  symboli. Punkt wejścia to `_start`, a nieużywany kod usuwa `--gc-sections`.
- **Obiekty startowe**:
  - `crtos-crt0.o`: wejście i konstruktory;
  - `crtos-memops.o`: szybkie `memcpy` i `memset` jądra zamiast bajtowych z newlib-nano;
  - `crtos-cxxrt.o`: `operator new` i `delete` na `malloc`.
- **Biblioteki**: `libcrtos`, `libc_nano`, `libm`, `libgcc`.
- **Nagłówek programu** `.crtos_app`: stos i sterta, które jądro czyta przed startem.
  - Ustawia go `CRTOS_APP(stos, sterta)` w jednym pliku źródłowym albo `crtos-app set`.
  - Bez niego dochodzi domyślny z libcrtos: 16 KB stosu i 64 KB sterty.
- **Konstruktory z priorytetem** (`__attribute__((constructor(N)))`, `init_priority`) są
  łączone w jedną tablicę `.init_array` według priorytetu (`crtos-app.ld`). Uruchamiają się
  przed `main` w kolejności priorytetów.

## Programy wykonywane w miejscu (XIP)

Program zbudowany z `-mxip` może wykonywać kod wprost z flasha: z dysku `/flash0` jądro go
nie kopiuje, a w SDRAM trzyma tylko dane, stertę i stos. Tak działa kompilator na płytce
(`cc1` ma 15 MB kodu). Ten sam plik uruchomiony z karty jądro kopiuje do pamięci i działa
tak samo.

```
arm-crtos-gcc -mxip -O2 prog.c -o prog.app
crtos put prog.app /flash0/bin/prog.app
```

W drzewie: `crtos_app(prog SOURCES prog.c XIP DEST flash0:bin)`.

- **Format**: plik ELF `ET_DYN` (statyczny PIE) z dwoma segmentami, tekstem od adresu 0
  i danymi od `0x10000000` (`lib/crtos-xip.ld`).
- **Kod** jest niezależny od położenia: do zmiennych globalnych i wskaźników do funkcji
  dochodzi przez tablicę GOT w danych, której adres jądro trzyma w r9 każdego wątku
  (`-fPIC -msingle-pic-base -mpic-register=r9 -mno-pic-data-is-text-relative`). W kodzie
  nie ma relokacji, a jądro poprawia tylko wskaźniki w danych (`R_ARM_RELATIVE`).
- **Biblioteki** z `lib/xip/`: pełna newlib (z `%lld` w `printf`), libm, libstdc++, libsupc++,
  libgcc, libcrtos, libgfx, TFTLIB. Wszystkie buduje `crtos build` kompilatorem Arm z komputera,
  bez WSL: newlib i biblioteki GCC ze źródeł z `third_party/newlib` i `third_party/gcc`
  (commity źródeł Arm GNU Toolchain 14.3.Rel1). Każdy obiekt (ok. 3000, przy pierwszym
  budowaniu ok. minuty) powstaje z opcjami, które nadaje mu własne budowanie newlib i GCC –
  zapisanymi w `toolchain/xiplibs/recipe.txt` ([niżej](#przepis-bibliotek-xip)).
- **Koszt**: każdy dostęp do zmiennej globalnej to jedno dodatkowe `ldr` z GOT, a kod
  z HyperFlash dłużej czeka na chybienia pamięci podręcznej niż kod z SDRAM. Opłaca się
  przy dużych programach, które inaczej nie zmieściłyby się w pamięci.
- **Nie łączy się** z `FAST` (szybki kod w ITCM) ani ze zmiennymi `__thread`.

### Przepis bibliotek XIP

newlib i biblioteki GCC buduje się zwykle ich własnymi skryptami (`configure`, `make`), które
działają tylko w systemie POSIX. CRTOS trzyma więc w repozytorium wynik takiego budowania:

- `toolchain/xiplibs/recipe.txt`: dla każdego obiektu biblioteka, plik źródłowy i opcje
  kompilatora, a także pliki kopiowane przed kompilacją (np. drzewo nagłówków libstdc++);
- `toolchain/xiplibs/gen/`: kilka plików, które generuje `configure` (`newlib.h`,
  `c++config.h`, `config.h` libstdc++, `tm.h`, `auto-target.h`, pliki `.vis` libgcc).

`cmake/xiplibs.cmake` czyta przepis i kompiluje każdy obiekt jak zwykłą regułę Ninja.
Wewnętrzne nagłówki GCC, takie same jak w kompilacji, bierze z `plugin/include` kompilatora
Arm. Przepis jest dla GCC 14.3.1. Przy innej wersji kompilatora build pomija programy XIP
z ostrzeżeniem.

Przepis tworzy `tools/xiplibs_recipe.py` z budowania etapu A (`crtos toolchain native cross`,
Linux albo WSL). Potrzeba go tylko przy zmianie wersji kompilatora. Biblioteki zbudowane z
przepisu porównano obiekt po obiekcie z bibliotekami etapu A: 3050 z 3057 obiektów jest
identycznych. Sześć różni się tylko ścieżką pliku w komunikatach `assert`, a jeden
(`codecvt_members.o`) był w archiwum etapu A nieaktualny. Ścieżki `__FILE__` są względne wobec
repozytorium (`-fmacro-prefix-map`), więc biblioteki nie zależą od tego, gdzie leży repozytorium.
- **`crtos crash`** rozumie raport programu XIP (adres tekstu i GOT).

## Kompilator na płytce

GCC 14.3 (C i C++) i binutils 2.44 działają też na samej płytce, jako zwykłe programy CRTOS.
Kod `cc1` (15 MB) i `cc1plus` (16 MB) wykonuje się z flasha (programy XIP z `/flash0`), a
w SDRAM są tylko ich dane i sterta. Program zbudowany na płytce jest taki sam jak zbudowany
przez `arm-crtos-gcc` na komputerze: te same reguły, nagłówki i biblioteki.

### Budowa i instalacja

Kompilator na płytkę buduje się w Linuksie, na Windows w WSL (Ubuntu 22.04, ok. 10 GB
miejsca; inna dystrybucja: zmienna `CRTOS_WSL`):

```
wsl --install -d Ubuntu-22.04
wsl -d Ubuntu-22.04 -- sudo apt install build-essential texinfo bison flex python3 curl xz-utils
.\crtos toolchain native       -> build\toolchain\native (pierwszy raz ok. 1 h)
.\crtos toolchain install      -> płytka, przez sieć (kilka minut)
```

`crtos toolchain native` uruchamia `toolchain/native/build.sh`. Pracuje w katalogu
`~/crtos-toolchain` systemu Linux (`CRTOS_TC_WORK`), bo na `/mnt/c` budowanie jest kilka
razy wolniejsze. Kolejne kroki:

| Krok | Co robi | Czas (16 rdzeni) |
|---|---|---|
| `fetch` | źródła Arm GNU Toolchain 14.3.Rel1 (suma SHA-256) i poprawki z `patches/arm-gnu-toolchain/` | kilka minut |
| `cross` | etap A: GCC krzyżowy dla Linuksa z bibliotekami XIP | ok. 1 h |
| `native` | etap B: binutils i GCC, które działają na płytce | ok. 10 min |
| `install` | pliki płytki w `build/toolchain/native` | ok. 1 min |

Pojedyncze kroki: `crtos toolchain native install`. Kroki już zrobione są pomijane.

`crtos toolchain install` wysyła tylko zmienione pliki, jak `crtos deploy`, ale wyłącznie
przez sieć. Zapisuje 43 MB na `/flash0` i 33 MB do `/sd/crtos/usr`. Zapis flasha na chwilę
zatrzymuje płytkę (do ok. 0,5 s na każdy kasowany blok 256 KB). `cc1plus` potrzebuje
16,5 MB ciągłego miejsca na `/flash0` (`flashfs info`: „largest file”).

| Na płytce | Co |
|---|---|
| `/flash0/bin` | `gcc`, `g++`, `cpp`, `ar`, `ranlib`, `nm`, `objcopy`, `objdump`, `strip`, `size`, `readelf`, `addr2line`, `strings` |
| `/flash0/gcc/libexec/gcc/arm-none-eabi/14.3.1` | `cc1`, `cc1plus`, `collect2`, `as`, `ld` |
| `/sd/crtos/usr/lib/gcc/arm-none-eabi/14.3.1` | `specs` (reguły CRTOS), nagłówki GCC, `libgcc.a` |
| `/sd/crtos/usr/include` | nagłówki CRTOS, libstdc++ (`c++/14.3.1`) |
| `/sd/crtos/usr/arm-none-eabi` | newlib, biblioteki zwykłe (`lib/`) i XIP (`lib/xip/`) |

### Użycie

W powłoce płytki (`sh` w `term`, przez USB albo UART):

```
gcc -O2 -Wall hello.c -o hello.app
./hello.app
g++ -O2 figury.cpp -o figury.app
make                                        (Makefile z CC = gcc)
gcc -mxip -O2 duzy.c -o /ram/duzy.app       (program XIP)
cp /ram/duzy.app /flash0/bin/duzy.app
```

Kompilacja idzie w osobnych procesach: preprocesor, kompilacja, asembler i linker; każdy
oddaje pamięć przed następnym. Ich pliki pośrednie trafiają na kartę do `/sd/crtos/tmp`
(inny katalog: zmienna `GCC_TMPDIR`), a nie do `$TMPDIR`: RAM-dysk `/ram` zabierałby tę
samą SDRAM co sterta kompilatora. Wynik linkowania nie może iść wprost na `/flash0` (`ld`
pisze z przesunięciami: `Illegal seek`), więc buduj na karcie i kopiuj.

Czasy na płytce (29.09.2026, z uruchomieniem przez `crtos run`):

| Co | Czas |
|---|---|
| `hello.c` (40 linii) `-O0 -c` / `-O2 -c` / linkowanie | ok. 2,5 s / 6 s / 1 s |
| `sh.c` (1900 linii) `-O2 -Wall` z linkowaniem | ok. 60 s |
| `figury.cpp` (klasy, `std::vector`) `-O2` | ok. 20 s |
| `make` projektu z dwóch plików `.c` od zera | ok. 5 s |
| emulator NES (`make`, 21 plików, `-O3`, 30.09.2026) | ok. 7,5 min |
| emulator SNES (`make`, 29 plików C++, `-O2`, 30.09.2026; 7 z nich z pamięcią emulowaną) | ok. 70 min |
| `smp.cpp` (SNES, szczyt sterty 25 MB, z tego ok. 4,5 MB w pamięci emulowanej) | ok. 8,5 min |

Pamięć: `cc1` i `cc1plus` mają małą arenę (3 MB), a stertę biorą z pamięci współdzielonej
(`libcrtosheap`): jeden obiekt z prawie całej wolnej SDRAM, przy działającym pulpicie ok.
21 MB. Zostaje 1 MB dla reszty systemu, więc w trakcie kompilacji nowe okno na pulpicie
może się nie otworzyć; pamięć wraca po zakończeniu `cc1`. `sh.c` z `-O2` zajmuje ok. 5 MB,
`nes_fast.c` (NES) z `-O3` ok. 18 MB. Im więcej programów działa, tym mniej ma kompilator.

Gdy SDRAM się skończy, sterta kompilatora rośnie dalej w pamięci emulowanej: pliku wymiany
na karcie (`/sd/crtos/var/swap`, 64 MB, [API](api.md#pamięć-emulowana-plik-wymiany)).
Kompilacja się wtedy kończy, zamiast przerwać się z `cc1: out of memory`, ale każdy dostęp
do tej części sterty wykonuje jądro (ok. 1 µs), więc trwa kilka razy dłużej: `smp.cpp`
emulatora SNES (25 MB) wykonał 221 mln dostępów emulowanych, ok. 4 z 8,5 min. Kompilator
może zająć najwyżej tyle, ile ma SDRAM i plik wymiany razem. `export CRTOS_HEAP_SWAP=0`
wyłącza pamięć emulowaną (wtedy znów `out of memory`), a `export CRTOS_HEAP_STATS=1` pokazuje
raport pamięci kompilatora: pule, szczyt i liczbę dostępów emulowanych.

Różnice względem komputera:
- **C++ na płytce to domyślnie C++14** (`-std=gnu++14`). Nagłówki biblioteki C++ w wersji
  C++17 potrzebują o kilka MB więcej; `-std=gnu++17` działa, ale w większych plikach
  sięga po pamięć emulowaną i jest wolniejsze.
- **Program z wieloma nagłówkami biblioteki C++** (np. `<algorithm>`, `<map>`, `<memory>`,
  `<string>` i `<vector>` razem) potrzebuje ponad 20 MB i kompiluje się kilka razy dłużej
  (pamięć emulowana). Szybciej zbudujesz go na komputerze (`arm-crtos-g++`).
- Nie ma LTO, `gcov` ani `-mcpu=native`.

### Programy systemu do zbudowania na płytce

```
.\crtos src                 -> /sd/crtos/src (tylko zmienione pliki, przez sieć)
```

`crtos src` kopiuje na kartę źródła wszystkich programów drzewa (`apps/`, `system/commands/`,
`system/services/`) z plikiem `Makefile` w każdym katalogu, zrobionym z tego, jak buduje je
`crtos build` (`tools/boardsrc.py`: flagi każdego pliku z `compile_commands.json`, biblioteki,
stos i sterta). Na płytce:

```
cd /sd/crtos/src/apps/paint
make                    paint.app w tym katalogu
make install            zastępuje program systemu (/sd/crtos/apps/paint.app)
cd /sd/crtos/src; make  wszystkie (make apps, make commands, make services)
```

Różnice względem budowania na komputerze: bez `-g`, C++ z `-O2` tam, gdzie drzewo ma `-O3`,
bez przenoszenia gorącego kodu do ITCM (`FAST`). NetSurf się tu nie buduje. Czasy na
płytce: `paint` ok. 11 s, `make` (9 plików) ok. 105 s, `tftdemo` ok. 25 s, `voxel` (7 plików
C++) ok. 4,5 min, `nes` ok. 7,5 min, `snes` ok. 70 min (7 z 29 plików potrzebuje 19–25 MB
i korzysta z pamięci emulowanej). `snes.app` zbudowany na płytce daje te same sumy kontrolne
obrazu i dźwięku co zbudowany na komputerze; klatka trwa ok. 14% dłużej (`-O2` i bez `FAST`). Zmiany zrobione na płytce nie
wracają same do drzewa na komputerze.

## crtos-app

Działa na komputerze (`bin/crtos-app`) i na płytce (`/sd/crtos/bin/crtos-app.app`).

| Polecenie | Wynik |
|---|---|
| `crtos-app info PROGRAM...` | nagłówek (ABI, stos, sterta), kod, dane i bss, przybliżony rozmiar areny |
| `crtos-app set PROGRAM --stack N --heap N` | nowe rozmiary w nagłówku; `N` w bajtach albo z `K`/`M` |
| `crtos-app check [-q] PROGRAM...` | to, co odrzuciłby loader jądra |

`check` sprawdza zwykły program:
- czy nie ma symboli niezdefiniowanych i COMMON;
- czy są tylko obsługiwane typy relokacji;
- czy `_start` to funkcja Thumb;
- wyrównanie sekcji (najwyżej 4 KB);
- nagłówek programu i limity: plik do 16 MB, stos do 1 MB, sterta do 24 MB.

Program XIP: dwa segmenty z `crtos-xip.ld`, GOT w danych, tylko relokacje `R_ARM_RELATIVE`
w danych, wejście Thumb w tekście, nagłówek i limity. `info` pokazuje wtedy arenę dla kodu
w miejscu i dla kopii.

Kod wyjścia jest różny od zera, gdy coś jest nie tak.

## Katalog toolchainu

| Katalog | Zawartość |
|---|---|
| `bin/` | `arm-crtos-*`, `crtos-app` |
| `lib/` | `crtos.specs`, `crtos-app.ld`, `crtos-xip.ld`, `crtos-crt0.o`, `crtos-memops.o`, `crtos-cxxrt.o`, `libcrtos.a`, `libcrtosheap.a` (`malloc` ponad arenę, [API](api.md)), `libgfx.a`, `libtftlib.a`, `gcc-bin.txt` |
| `lib/xip/` | to samo niezależne od położenia (programy XIP) oraz newlib, libm, libgcc, libstdc++ i libsupc++ zbudowane z przepisu `toolchain/xiplibs` |
| `include/` | nagłówki libcrtos i libgfx, `crtos/` (interfejs jądra dla programów: wywołania systemowe, `ioctl` urządzeń), `tftlib/` |

Budowanie drzewa (`crtos build`) odświeża `lib/` i `include/`. Toolchain pasuje do systemu
zbudowanego z tych samych źródeł. Po zmianie numerów wywołań systemowych przebuduj
programy.

## Ograniczenia

- **Formatowanie printf**: `printf` z newlib-nano nie obsługuje `%lld` i `%llu`. Zmiennoprzecinkowe `%f`
  działa, bo każdy program linkuje się z `-u _printf_float`.
- **Brak `fork` i `exec`**: procesy uruchamia `crtos_spawn` (zob. [API](api.md)).
- **Limity programu**: plik do 16 MB, stos do 1 MB, sterta do 24 MB. Sterta nie rośnie ponad
  rozmiar z nagłówka (plus zaokrąglenie areny).
- **Ścieżki na Windows**: GCC nie znosi ścieżek dłuższych niż 260 znaków. Trzymaj projekty
  w krótkich katalogach.
