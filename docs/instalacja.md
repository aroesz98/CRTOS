# Instalacja

## Czego potrzebujesz

**Sprzęt**

- Płytka **NXP MIMXRT1050-EVKB** z ekranem dotykowym 4,3" (480×272): **RK043FN66HS-CTG**
  (dotyk GT911, obecnie dołączany do płytki) albo starszym **RK043FN02H-CT** (dotyk FT5406).
  Oba działają bez zmian w konfiguracji. Działa też panel 800×480 z tym samym 40-pinowym
  złączem (np. z modułu Waveshare 16249); zobacz [Ekran i dotyk](sterowniki.md#ekran-i-dotyk).
- Kabel **micro-USB** do gniazda **J28**. Przez to jedno gniazdo idzie zasilanie, sonda
  debugująca (DAPLink) i port szeregowy.
- Karta **microSD** sformatowana jako FAT32. Wystarczy 1 GB; system zajmuje około 7 MB.
- Opcjonalnie drugi kabel **micro-USB** do gniazda **J9** (USB OTG1). Płytka jest wtedy dla
  komputera portem szeregowym z powłoką systemu ([Konsola przez USB](debugowanie.md#konsola-przez-usb-j9)).
  Po przełączeniu J9 w tryb host podłączysz tam przez przejściówkę OTG **klawiaturę albo mysz
  USB** ([sterowniki](sterowniki.md#usb-j9-urządzenie-albo-host)). Bez klawiatury pisze się
  na klawiaturze ekranowej.
- Opcjonalnie **kabel Ethernet** do tego samego routera co komputer. Pliki wgrywają się wtedy
  w kilka sekund zamiast kilku minut, a przeglądarka ma dostęp do internetu.

**Komputer**: Windows 10/11, Linux albo macOS.

| Narzędzie | Do czego |
|---|---|
| Python 3.8+ | polecenie `crtos` |
| Git | pobranie źródeł; build pobiera nim też kod z zewnątrz (`third_party/`) |
| CMake 3.20+ i Ninja | budowanie |
| Arm GNU Toolchain (`arm-none-eabi-gcc`, wersja 14) | kompilator |
| pakiety Pythona: pyOCD, pyserial, Pillow, pyelftools | sonda, port szeregowy, zrzuty ekranu i okno `crtos desktop`, analiza awarii |

## Najprościej: `crtos setup`

Zainstaluj Pythona i Gita, pobierz źródła i uruchom `crtos setup`:

```
git clone https://github.com/aroesz98/CRTOS.git
cd CRTOS
.\crtos setup
```

(na Linuksie i macOS: `./crtos setup`)

`crtos setup`:

1. instaluje pakiety Pythona z `tools/requirements.txt`,
2. instaluje CMake i Ninja przez `pip`, jeśli ich nie ma,
3. na Windows instaluje Arm GNU Toolchain przez `winget` (system może poprosić o zgodę
   administratora); na Linuksie i macOS pokazuje, jak go zainstalować,
4. pobiera kod z zewnątrz, którego nie ma w repozytorium: lwIP, TinyUSB, pdpmake i sterowniki
   NXP SDK, newlib i biblioteki GCC do `third_party/` (pobiera ok. 40 MB; z `--netsurf` także
   przeglądarkę, około 110 MB).
   Pobiera wskazane commity z `third_party/sources.txt` i nakłada zmiany CRTOS z `patches/`.
   Samo `crtos build` też to robi, gdy czegoś brakuje,
5. na końcu uruchamia `crtos doctor`, który pokazuje, czy czegoś brakuje.

## Ręcznie

**Windows** (winget jest wbudowany w Windows 10/11):

```
winget install -e --id Python.Python.3.12
winget install -e --id Git.Git
winget install -e --id Kitware.CMake
winget install -e --id Ninja-build.Ninja
winget install -e --id Arm.GnuArmEmbeddedToolchain
```

Po instalacji otwórz nowe okno terminala (żeby zobaczyło nowe ścieżki), a potem:

```
py -m pip install -r tools\requirements.txt
```

**Debian / Ubuntu**:

```
sudo apt install python3 python3-venv git cmake ninja-build
python3 -m venv ~/.venv/crtos && . ~/.venv/crtos/bin/activate
pip install -r tools/requirements.txt
```

Kompilator pobierz ze strony
[Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
(wersja *AArch32 bare-metal target (arm-none-eabi)*). Rozpakuj go np. do
`/opt/arm-gnu-toolchain-14`, a `crtos` sam go tam znajdzie. Pakiet `gcc-arm-none-eabi`
z dystrybucji też powinien działać, ale testowany jest kompilator w wersji 14.

Dostęp do sondy bez uprawnień roota wymaga reguł udev z projektu
[pyOCD](https://github.com/pyocd/pyOCD/tree/main/udev). Dostęp do portu szeregowego
wymaga członkostwa w grupie `dialout` (`sudo usermod -aG dialout $USER`, potem wyloguj się
i zaloguj ponownie).

**macOS** (Homebrew):

```
brew install python git cmake ninja
brew install --cask gcc-arm-embedded
python3 -m pip install -r tools/requirements.txt
```

Jeśli kompilator jest w nietypowym miejscu, wskaż jego katalog `bin` zmienną
`CRTOS_GCC_BIN`.

## Sprawdzenie: `crtos doctor`

```
.\crtos doctor
```

```
Tools:
 ok  Python 3.12.10         C:\...\python.exe
 ok  CMake 4.4.3            C:\Program Files\CMake\bin\cmake.EXE
 ok  Ninja 1.13.2           ...
 ok  Arm GCC 14.3.1         C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.3 rel1\bin
 ok  Git 2.55.0             C:\Program Files\Git\cmd\git.EXE
 ok  pyocd 0.45.1           debug probe
 ok  pyserial 3.5           serial console
 ok  pillow 12.2.0          screenshots
Board:
 ok  debug probe            0227000047784e45002a9003d9170018e561000097969900
 ok  serial port            COM28
 ok  CRTOS on the board     SWD console at 200000d4
 ok  network deploy         192.168.100.11
```

- `ok`: jest i działa.
- `!!`: brakuje czegoś potrzebnego do budowania (`crtos setup` to naprawi).
- `--`: opcjonalne albo jeszcze nieustawione, np. wgrywanie przez sieć przed pierwszym
  `crtos deploy`.

## Płytka

1. Przełączniki i zworki zostaw w ustawieniu fabrycznym. Płytka startuje z pamięci HyperFlash
   (przełącznik SW7: OFF, ON, ON, OFF).
2. Włóż kartę microSD (FAT32) do gniazda na płytce. Na karcie mogą być inne pliki: CRTOS
   używa tylko katalogu `/crtos`.
3. Podłącz kabel USB do gniazda **J28**. Komputer zobaczy sondę DAPLink (także jako dysk
   USB, którego nie trzeba używać) i port szeregowy. Na Windows 10/11 sterowniki instalują
   się same.
4. Opcjonalnie podłącz kabel Ethernet. Płytka pobiera adres z DHCP; `crtos find` go pokazuje.

Dalej: [Budowanie i uruchamianie](budowanie.md).

## Toolchain CRTOS (opcjonalnie)

Do budowania systemu i programów przez `crtos build` toolchain nie jest potrzebny. Przydaje się
w dwóch sytuacjach.

### Na komputerze: `arm-crtos-gcc`

Kompilator programów CRTOS bez CMake, np. `arm-crtos-gcc -O2 hello.c -o hello.app`. To nakładka
na Arm GNU Toolchain, więc wymaga go (instaluje go `crtos setup`). Potrzebny jest też
kompilator C dla komputera, tylko do zbudowania samej nakładki: `gcc`, `clang` albo `cl`.
`crtos` szuka go w `PATH`, w `C:\mingw64\bin` i w MSYS2. Na Windows najprościej:

```
winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT
```

Potem w katalogu CRTOS:

```
.\crtos toolchain              -> build\toolchain\arm-crtos
```

Polecenie buduje biblioteki i nagłówki toolchainu, także biblioteki programów wykonywanych
w miejscu (`lib/xip`; za pierwszym razem ok. minuty), a potem `arm-crtos-gcc`, `arm-crtos-g++`
i `crtos-app`. Na koniec dodaj katalog `bin` do `PATH`. W PowerShell, na stałe dla użytkownika:

```
[Environment]::SetEnvironmentVariable("PATH", "$PWD\build\toolchain\arm-crtos\bin;" + [Environment]::GetEnvironmentVariable("PATH", "User"), "User")
```

Otwórz nowe okno terminala i sprawdź: `arm-crtos-gcc -dumpversion` (14.3.1). Szczegóły:
[Toolchain](toolchain.md#instalacja). SDK (`crtos sdk`) zawiera ten sam toolchain.

### Na płytce: `gcc` w powłoce płytki

GCC i binutils, które działają na samej płytce: `/flash0` i `/sd/crtos/usr`. Buduje się je
w Linuksie, a na Windows w WSL. Za pierwszym razem trwa to ok. godziny i zajmuje ok. 10 GB
w systemie Linux:

```
wsl --install -d Ubuntu-22.04
wsl -d Ubuntu-22.04 -- sudo apt install build-essential texinfo bison flex python3 curl xz-utils
.\crtos toolchain native        -> build\toolchain\native
.\crtos toolchain install       -> płytka, przez sieć
```

`crtos toolchain install` wysyła 43 MB na `/flash0` i 33 MB na kartę. Potrzebuje płytki
w sieci i 16,5 MB ciągłego miejsca na `/flash0` (`flashfs info`). Zapis flasha na chwilę
zatrzymuje płytkę. Na Linuksie WSL nie jest potrzebny: wystarczą te same pakiety. Szczegóły:
[Toolchain → Kompilator na płytce](toolchain.md#kompilator-na-płytce).

## Gdy coś nie działa

| Objaw | Co zrobić |
|---|---|
| `debug probe: no debug probe found` | Sprawdź kabel (gniazdo J28, kabel z liniami danych, nie tylko do ładowania). Zamknij sesję debugowania w MCUXpresso IDE: sondę może używać tylko jeden program naraz. |
| `several debug probes` | Podłączone są dwie sondy. Wybierz sondę płytki: `set CRTOS_PROBE=<id>` (PowerShell: `$env:CRTOS_PROBE="<id>"`). Identyfikatory pokazuje komunikat błędu. |
| `serial port ... is a terminal program using it?` | Port szeregowy jest zajęty przez inny program (PuTTY, Tera Term, monitor w IDE). Zamknij go. |
| `no C compiler for this computer` (`crtos toolchain`) | Zainstaluj kompilator C, np. `winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT`, albo wskaż go zmienną `CC`. |
| `no WSL` (`crtos toolchain native`) | `wsl --install -d Ubuntu-22.04`, potem pakiety z [Toolchain na płytce](#na-płytce-gcc-w-powłoce-płytki). Inną dystrybucję wskazuje zmienna `CRTOS_WSL`. |
| `arm-none-eabi-gcc not found` | Uruchom `crtos setup` albo ustaw `CRTOS_GCC_BIN` na katalog `bin` kompilatora. |
| Błędy budowania typu `No such file or directory` przy bardzo długiej ścieżce (Windows) | Kompilator nie obsługuje ścieżek dłuższych niż 260 znaków. Trzymaj projekt w krótkiej ścieżce, np. `C:\dev\CRTOS`. |
| `third_party: the sources are not complete` przy budowaniu | Pierwsze budowanie pobiera kod z zewnątrz przez gita: sprawdź połączenie z internetem i czy `git` jest w `PATH`. Stan każdego katalogu pokaże `python tools/thirdparty.py --status`. Katalog z własnymi zmianami zapisz przez `--save` ([patches/README.md](../patches/README.md)) albo usuń, a build pobierze go od nowa. |
| Pierwszy `crtos deploy` trwa kilka minut | Tak ma być przez USB (~45 KB/s). Z kablem sieciowym kolejne wgrania trwają sekundy. Możesz też skopiować pliki czytnikiem kart: `crtos sdcard E:`. |
| Po `crtos flash` płytka nie startuje | Sprawdź przełącznik SW7 (start z HyperFlash). Uruchom `crtos log --reset`, żeby zobaczyć komunikaty startowe. |
| Ekran jest czarny, a `crtos kmon` działa | Na karcie brakuje plików systemu: `crtos deploy`, potem `crtos reboot`. Stan pokaże `crtos kmon dmesg`. |
| System nie startuje po zmianie sterownika | Trzymaj przycisk **SW8** podczas startu (tryb awaryjny: samo jądro i monitor, bez sterowników i usług), wgraj poprawione pliki i zrestartuj płytkę. |
