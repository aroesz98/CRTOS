# Budowanie i uruchamianie

## Co się buduje

CRTOS składa się z dwóch części:

1. **Jądro**: plik `build/kernel/crtos.bin`, zapisywany w pamięci flash płytki
   (`crtos flash`). Zmienia się rzadko, tylko po zmianach w `kernel/`.
2. **Pliki na kartę SD**: katalog `build/sdcard/crtos/`, wysyłany na kartę jako `/crtos`
   (`crtos deploy`). Są w nim sterowniki, usługi, programy i konfiguracja:

Źródła w drzewie:

| Katalog | Co zawiera | Budowane |
|---|---|---|
| `kernel/` | jądro: `rtos/` (rdzeń RTOS), `os/` (procesy, pliki, moduły, start z karty), `subsys/`, BSP płytki | zawsze |
| `drivers/` | sterowniki – moduły jądra | zawsze |
| `system/` | system bazowy: biblioteki programów (`lib/libcrtos`, `lib/libgfx`, `lib/tftlib`), usługi (`services/`), polecenia (`commands/`), programy z oknem (`apps/`: wm, term, files, settings, sysmon) | zawsze |
| `apps/` | programy dodatkowe: paint, dema, NetSurf, gry; nowe programy z `crtos new` | domyślnie (bez nich: `crtos build --base`) |
| `tests/` | testy i pomiary na płytce | domyślnie |
| `examples/` | przykłady: programy, moduł sterownika, aplikacja RTOS | domyślnie (aplikacja RTOS tylko przez `--rtos`) |
| `rootfs/`, `dts/` | pliki stałe karty, drzewo urządzeń | zawsze |

| Katalog na karcie | Zawartość | Źródła |
|---|---|---|
| `/crtos/boot/board.dtb` | drzewo urządzeń płytki | `dts/` |
| `/crtos/drivers/*.ko` | sterowniki (moduły jądra) i `modules.alias` | `drivers/` |
| `/crtos/sbin/*.app` | usługi systemowe | `system/services/` |
| `/crtos/bin/*.app` | programy konsolowe | `system/commands/`, `tests/`, `examples/hello` |
| `/crtos/apps/*.app` | programy z oknem | `system/apps/`, `apps/`, `examples/hello_world` |
| `/crtos/etc/` | konfiguracja: `init.cfg`, `launcher.cfg`, `network.cfg`, certyfikaty | `rootfs/etc/` |
| `/crtos/share/icons/*.pam` | ikony programów (menu, pasek zadań, paski tytułu) | `icon.png` programów (`crtos_app(... ICON)`, `tools/icon.py`) |
| `/crtos/share/netsurf/` | zasoby przeglądarki | `third_party/netsurf` (po `crtos setup --netsurf`) |

## `crtos build`

```
.\crtos build              wszystko
.\crtos build kernel       samo jądro
.\crtos build paint        jeden program (nazwa jego katalogu)
.\crtos build --base       sam system bazowy (bez apps/, tests/, examples/)
.\crtos build --rtos       obraz samego RTOS z aplikacją examples/rtos/blinky (inna: --rtos KATALOG)
.\crtos build -v           z pełnymi poleceniami kompilatora
.\crtos build --fresh      konfiguracja CMake od nowa (np. po zmianie kompilatora)
.\crtos clean              usuwa wyniki budowania (--all: cały katalog build/)
```

Przy pierwszym budowaniu `crtos build` konfiguruje CMake. Później buduje tylko to, co się
zmieniło. Nowe katalogi programów (np. z `crtos new`) CMake wykrywa sam.

Kodu z zewnątrz (lwIP, TinyUSB, pdpmake, sterowniki NXP SDK, newlib i biblioteki GCC) nie ma
w repozytorium. Każda konfiguracja CMake uruchamia `tools/thirdparty.py`:

- brakujące źródła pobiera do `third_party/` (git; za pierwszym razem pobiera ok. 40 MB,
  na dysku zajmują ok. 120 MB);
- katalog, którego commit w `third_party/sources.txt` albo łatka w `patches/` się zmieniły,
  przełącza na nową wersję;
- gdy wszystko się zgadza, nic nie robi (ok. 0,3 s).

Szczegóły i zmiana kodu z zewnątrz: [patches/README.md](../patches/README.md).

Z newlib i źródeł GCC pierwsze budowanie kompiluje też biblioteki C i C++ programów
wykonywanych w miejscu (`lib/xip`, ok. 3000 obiektów, około minuty;
[Toolchain](toolchain.md#przepis-bibliotek-xip)). Później kompiluje je tylko po zmianie
źródeł albo przepisu.

Podczas budowania uruchamiają się też sprawdzenia:

- **modcheck**: każdy symbol, którego używa sterownik, musi być eksportowany przez jądro
  (`kernel/os/ksyms.cpp`) albo przez inny moduł (`EXPORT_SYMBOL`).
- **sprawdzenie programów**: program nie może mieć nierozwiązanych symboli, bo nie widzi
  symboli jądra i rozmawia z nim wyłącznie przez wywołania systemowe.
- **ramki stosu jądra**: ostrzeżenie o funkcjach, których ramka stosu jest większa niż
  strażnik stosu MPU (256 B).

Program z `crtos_app(... DEST flash0:bin)` trafia do `build/flash0/bin`, a `crtos deploy`
wysyła go do `/flash0/bin` na płytce ([Dysk /flash0](sterowniki.md#dysk-flash0)). Z opcją
`XIP` (`crtos_app(... XIP DEST flash0:bin)`) jego kod wykonuje się stamtąd w miejscu, bez
kopiowania do pamięci ([Toolchain](toolchain.md#programy-wykonywane-w-miejscu-xip)).

Programy (`.app`) kompiluje i linkuje `arm-none-eabi-gcc` z regułami z pliku
`toolchain/crtos.specs`: te same reguły ma `arm-crtos-gcc` i SDK
([Toolchain](toolchain.md)).
- Biblioteki i obiekty startowe trafiają najpierw do `build/toolchain/arm-crtos/lib`.
- Każdy program linkuje się stamtąd.

Bez polecenia `crtos` zbudujesz wszystko samym CMake:

```
cmake -S . -B build -G Ninja
cmake --build build
```

Opcje CMake:

| Opcja | Znaczenie |
|---|---|
| `-DCRTOS_BUILD_NETSURF=OFF` | bez przeglądarki (szybsze budowanie) |
| `-DCRTOS_BUILD_APPS=OFF`, `-DCRTOS_BUILD_TESTS=OFF`, `-DCRTOS_BUILD_EXAMPLES=OFF` | bez programów dodatkowych, testów, przykładów (`crtos build --base`: wszystkie trzy) |
| `-DCRTOS_PROFILE=rtos -DCRTOS_RTOS_APP=<katalog>` | sam RTOS z aplikacją (`crtos build --rtos`, w `build/rtos`; [RTOS](rtos.md)) |
| `-DCRTOS_BUILD_KERNEL=OFF` | bez jądra (tylko pliki na kartę) |
| `-DCRTOS_GCC_BIN=<katalog>` | kompilator z konkretnego katalogu |

## Wgrywanie na płytkę

### Jądro: `crtos flash`

Zapisuje `build/kernel/crtos.bin` w pamięci HyperFlash przez sondę DAPLink (pyOCD) i
restartuje płytkę. Trwa około 10 sekund. Jeśli pyOCD zawiedzie, a zainstalowany jest NXP
LinkServer, `crtos` spróbuje nim.

`crtos flash --net` robi to samo przez sieć, bez sondy: obraz dostaje działający system
(usługa `deployd`), sprawdza go, zapisuje i restartuje płytkę. Trwa kilka sekund. Jeśli
płytka po nieudanym zapisie nie startuje (np. brak zasilania w trakcie), pomaga `crtos flash`
przez sondę. Szczegóły: [Aktualizacja jądra bez sondy](sterowniki.md#aktualizacja-jądra-bez-sondy).

`crtos flash --rtos` wgrywa obraz samego RTOS (`build/rtos/kernel/crtos.bin`) – tylko przez
sondę, bo obraz RTOS nie ma sieci. Do systemu wraca się przez `crtos flash`
([RTOS](rtos.md)).

### Pliki systemu: `crtos deploy`

```
.\crtos deploy             tylko zmienione pliki
.\crtos deploy -n          pokazuje, co zostałoby wysłane
.\crtos deploy --full      wszystkie pliki
.\crtos deploy --swd       tylko przez sondę (--net: tylko przez sieć)
```

`crtos deploy` pamięta, co wysłał (sumy CRC w `build/deployed.json`), więc wysyła tylko
zmiany. Wybiera najszybszą drogę:

1. **Sieć**: jeśli płytka odpowiada w sieci lokalnej (usługa `deployd`). Wgrywanie trwa
   sekundy (~1,3 MB/s).
2. **Sonda USB**: w przeciwnym razie. Wolniej, około 45 KB/s, więc cały system (7 MB,
   w tym 4,4 MB przeglądarki) wgrywa się około 2,5 minuty. Małe pliki idą najpierw.

Sieć wymaga **tokenu**, czyli hasła, które zna tylko Twój komputer i płytka. Pierwszy
`crtos deploy` przez sondę tworzy token (`~/.crtos/deploy.token`) i zapisuje go na karcie
(`/crtos/etc/deploy.token`). Od tej chwili, jeśli płytka ma kabel sieciowy, kolejne
wgrania idą przez sieć. `deployd` zapisuje pliki tylko w `/sd/crtos/`, na `/flash0` i w
`/ram` (ten sam token obsługuje `crtos scp` i instalację hasła `crtos desktop`).

Kiedy zmiana zaczyna działać:

- **programy** (`apps/`, `system/commands/`): przy następnym uruchomieniu programu,
- **sterowniki, usługi, konfiguracja** (`drivers/`, `system/services/`, `rootfs/`, `dts/`): po
  restarcie płytki (`crtos reboot`).

### Karta w czytniku: `crtos sdcard`

Jeśli masz czytnik kart, możesz skopiować pliki bezpośrednio:

```
.\crtos sdcard E:
```

Kopiowane są tylko zmienione pliki i tylko do katalogu `E:\crtos` (razem z tokenem sieci).
Nic poza tym katalogiem nie jest ruszane. `crtos` nie pozwoli pisać na dysk systemowy.

### Jeden plik: `crtos put`

```
.\crtos put moj.cfg /sd/crtos/etc/network.cfg
```

Przez sieć można pisać tylko w `/sd/crtos/`. Przez sondę można pisać także gdzie indziej,
np. do dysku w RAM `/ram/` (znika po restarcie).

## Uruchamianie programów

**Z komputera**:

```
.\crtos run hello raz dwa
```

Wyjście programu pojawia się w terminalu, Ctrl-C kończy program, a kod wyjścia programu
staje się kodem wyjścia `crtos`. Opcje `crtos run` podaje się **przed** nazwą programu:
`crtos run --time 10 gfxinfo`. Wszystko po nazwie trafia do programu.

**Na płytce**:

- programy z oknem: przycisk **Apps** na pasku zadań (lista z `rootfs/etc/launcher.cfg`),
- programy konsolowe: w terminalu (program *Terminal* na ekranie albo `crtos serial` na
  komputerze) wpisz nazwę programu, np. `ping 8.8.8.8`,
- przy starcie systemu: wpis w `rootfs/etc/init.cfg`:

```
service <nazwa> <respawn|once|wait> [caps=...] <program> [argumenty]
```

`respawn` uruchamia program ponownie, gdy się zakończy. `once` uruchamia go raz. `wait`
uruchamia go i czeka na jego koniec. `caps` to uprawnienia:

| Uprawnienie | Pozwala |
|---|---|
| `spawn` | uruchamiać procesy |
| `kill` | kończyć procesy innych |
| `module` | ładować i usuwać sterowniki |
| `sys` | restart, ustawianie zegara, wysokie priorytety |
| `dev` | otwierać urządzenia w `/dev` |

## Sieć

Domyślnie płytka pobiera adres z DHCP i ustawia zegar z `pool.ntp.org`. Zmienisz to
w `rootfs/etc/network.cfg`:

```
iface eth0 static 192.168.1.50/24 gw 192.168.1.1
dns 1.1.1.1 8.8.8.8
ntp pool.ntp.org
```

- `crtos find` pokazuje płytki w sieci lokalnej.
- `http://<adres płytki>/status` pokazuje stan systemu (usługa `httpd`), a
  `/api/status` to samo w JSON.
- Na płytce działają `ping`, `ifconfig` i `nc`.

## MCUXpresso IDE (opcjonalnie)

Katalog repozytorium jest też projektem MCUXpresso IDE o nazwie `crtos`. IDE przydaje się
głównie do debugowania jądra krok po kroku.

1. *File → Import → General → Existing Projects into Workspace*, wskaż katalog
   repozytorium.
2. Konfiguracja **Release** buduje samo jądro (wszystkie pliki z `kernel/`) do
   `Release/crtos.axf`. Sterowniki i programy buduje `crtos build`.
3. Debugowanie: konfiguracje *crtos LinkServer Release/Debug* (pliki `.launch`).

`crtos kbuild` buduje projekt IDE z wiersza poleceń, bez otwierania IDE. IDE używa własnego
kompilatora (GCC 14.2), a `crtos build` kompilatora z systemu (np. 14.3). Oba działają.

## VS Code

Otwórz katalog repozytorium. Po pierwszym `crtos build` podpowiedzi (IntelliSense) działają
dla całego kodu dzięki `build/compile_commands.json`. Ustawienia są w `.vscode/settings.json`.
