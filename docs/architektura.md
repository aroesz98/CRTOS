# Architektura

## Warstwy

```
 warstwa 3: programy          wm | term | files | sysmon | settings | paint | NetSurf | ...
 (bez uprawnień, osobne       sh | ping | ifconfig | nc | apptest | ...
  obszary pamięci)
 ---------------------------  porty IPC, pamięć współdzielona  ----------------------------
 warstwa 2: usługi            init | devmgr | gfxd (grafika) | inputd | osk | netmgr | httpd | deployd
                              | vncd (zdalny pulpit) | getty
 ===========================  wywołania systemowe (SVC), uchwyty  ===========================
 warstwa 1: sterowniki .ko    pinctrl, clk, gpio, lpi2c, lpspi, lpuart, lcdif, pxp, ft5406/gt911,
 (z karty SD, wg drzewa       gpio-keys, enet + stos TCP/IP lwIP, rtc, trng, usb-imxrt (TinyUSB),
  urządzeń)                   flexspi-mtd (HyperFlash)
                              frameworki: irq, gpio, i2c, spi, fb, gpu2d, input, net, tty, block
 warstwa 0: jądro (flash)     część OS (kernel/os): procesy, uchwyty, IPC, loader ELF, drzewo
                              urządzeń, VFS (/sd, /ram, /dev); wbudowane: karta SD i FAT
                              rdzeń RTOS (kernel/rtos): scheduler O(1), synchronizacja, kolejki,
                              timery, przerwania, MPU, sterty, konsola, printk, kmon
```

- **Jądro** (`kernel/`) jest w pamięci flash płytki. Zawiera tylko to, co jest potrzebne do
  startu z karty SD. Wszystkie inne sterowniki ładuje z karty. Ma dwie części: **rdzeń RTOS**
  (`kernel/rtos`: zadania, synchronizacja, kolejki, timery, przerwania, pamięć, konsola,
  monitor) i na nim **część systemu operacyjnego** (`kernel/os`: procesy, pliki, moduły,
  start z karty). Sam rdzeń z jedną aplikacją buduje się też jako zwykły RTOS
  ([RTOS bez systemu](rtos.md)).
- **Sterowniki** (`drivers/`) to moduły jądra (`.ko`). Działają w trybie uprzywilejowanym
  i widzą symbole, które jądro eksportuje. Jądro ładuje je według drzewa urządzeń. Opis
  w rozdziale [Sterowniki i drzewo urządzeń](sterowniki.md).
- **Usługi** (`system/services/`) i **programy** (`system/commands/`, `system/apps/`, dodatki
  w `apps/`) to procesy bez uprawnień.
  Każdy ma własny obszar pamięci chroniony przez MPU. Błąd programu kończy tylko ten program;
  usługi `init` uruchamia ponownie.

## Start systemu

1. Po resecie procesor startuje z HyperFlash: zegary, pamięć SDRAM (DCD), MPU.
2. Jądro uruchamia konsolę startową (UART, 1 Mbit/s), sterty, scheduler i monitor jądra
   (`kmon`).
3. Wbudowany sterownik karty SD i system plików FAT montują kartę jako `/sd`.
4. Jądro wczytuje drzewo urządzeń `/sd/crtos/boot/board.dtb` i tworzy urządzenia.
5. Dla urządzeń bez wbudowanego sterownika ładuje moduły według
   `/sd/crtos/drivers/modules.alias` (linie `<compatible> <moduł>`).
6. Uruchamia `/sd/crtos/sbin/init.app`, a `init` uruchamia wszystko z `/sd/crtos/etc/init.cfg`:
   usługi, menedżer okien, programy startowe i powłokę na konsoli szeregowej.

**Tryb awaryjny**: przycisk **SW8** wciśnięty podczas startu pomija kroki 5 i 6. Działa
wtedy samo jądro z monitorem, więc można naprawić pliki na karcie (`crtos deploy`,
`crtos put`) po złym sterowniku albo konfiguracji.

## Pamięć

| Pamięć | Rozmiar | Do czego |
|---|---|---|
| HyperFlash | 64 MB | jądro w pierwszych 2 MB (wykonywane wprost z flash, XIP); reszta to system plików `/flash0` na programy i dane ([Dysk /flash0](sterowniki.md#dysk-flash0)) |
| ITCM | 128 KB | gorący kod jądra (ok. 49 KB: scheduler, wywołania systemowe, pliki, IPC, przerwania, `memcpy`...), mała szybka sterta i obszar szybkiego kodu jednego programu (ok. 72 KB, [Wydajność](wydajnosc.md#szybki-kod-programu-w-itcm)) |
| DTCM | 128 KB | dane i stosy jądra |
| OCRAM | 256 KB | kod i dane sterowników `.ko`, bufory DMA |
| SDRAM | 30 MB | obszary pamięci procesów, duże bufory |
| SDRAM bez pamięci podręcznej | 2 MB | bufory ekranu i DMA |
| pamięć emulowana (od 0x90000000) | 64 MB | plik wymiany `/sd/crtos/var/swap` na karcie, dla programów, którym nie wystarcza SDRAM ([API](api.md#pamięć-emulowana-plik-wymiany)) |

`crtos kmon mem` pokazuje zajętość każdej puli, `crtos kmon vmem` stan pamięci emulowanej.

Procesor nie ma MMU, więc pamięć emulowana nie jest pamięcią wirtualną. Pod jej adresami
nie ma żadnej pamięci, a region 0 MPU zabrania tam dostępu. Każdy dostęp programu kończy
się wyjątkiem MemManage. Jądro dekoduje wtedy instrukcję i wykonuje ją za program na
stronie 4 KB w pamięci podręcznej stron (1 MB w SDRAM), potem przesuwa PC. Gdy strony nie
ma w pamięci podręcznej, jądro kończy wyjątek i wczytuje ją z karty w wątku programu, jak
w wywołaniu systemowym, a potem instrukcja wykonuje się jeszcze raz. Dostęp kosztuje ok.
1 µs, wczytanie strony ok. 0,5 ms.

## Ochrona pamięci (MPU)

| Regiony | Zawartość |
|---|---|
| 0 | tło 4 GB: brak dostępu |
| 1–7 | pamięci jądra i peryferia: dostęp tylko w trybie uprzywilejowanym |
| 8 | obszar (arena) procesu, którego wątek działa: kod, dane, stos, sterta |
| 9–11 | okna pamięci współdzielonej tego procesu; jedno z nich może zajmować jego szybki kod w ITCM (tylko odczyt i wykonanie) |
| 12 | strażnik NULL (0x0–0xFF): brak dostępu nawet dla jądra |
| 13 | strażnik na dnie stosu przerwań (256 B) |
| 14 | strażnik na dnie stosu działającego wątku: stosu użytkownika w wątku programu, stosu jądra w wątku jądra |
| 15 | strażnik na dnie stosu jądra wątku programu (używanego w wywołaniach systemowych) |

Regiony 8–11, 14 i 15 przełączają się przy każdej zmianie wątku. Wywołanie systemowe ich
nie zmienia, bo oba strażniki wątku programu działają przez cały czas. Odwołanie poza
własną arenę, dereferencja NULL i przepełnienie stosu kończą tylko winny proces, a jądro
wypisuje raport ([Debugowanie](debugowanie.md)).

## Scheduler i procesy

- Scheduler ma priorytety i wybiera wątek w czasie stałym, O(1): kolejka FIFO dla każdego
  priorytetu i mapa bitowa niepustych kolejek. Wątki o tym samym priorytecie dzielą czas
  procesora po równo (kwant czasu). Uśpione wątki czekają w kolejce posortowanej według
  czasu obudzenia.
- Muteksy jądra dziedziczą priorytet. Bezczynny procesor śpi (`WFI`).
- Wywołanie systemowe (`SVC`) przełącza wątek w tryb uprzywilejowany na jego stosie jądra,
  więc jądro może w nim blokować i być wywłaszczane. Tylko `getpid`, `gettid` i `time_us`
  obsługuje od razu obsługa `SVC`. Koszty: [Wydajność](wydajnosc.md).
- Proces to zestaw wątków, arena, tablica uchwytów (pliki, porty, pamięć współdzielona,
  gniazda) i uprawnienia (`caps`). Zakończenie procesu zwalnia wszystko, co miał.

## Programy i moduły: format plików

`.app` i `.ko` to pliki ELF typu relokowalnego (`ld -r`). Loader jądra umieszcza je
w pamięci i poprawia adresy (relokacje ARM: `ABS32`, `REL32`, `THM_CALL`, `THM_JUMP24`,
`THM_MOVW/MOVT`, ...).

- `.app` zawiera całą bibliotekę C, którą używa (linkowaną statycznie). Nie może mieć
  nierozwiązanych symboli. Sekcja `.crtos_app` podaje rozmiar stosu i sterty.
- `.ko` rozwiązuje symbole z tablicy eksportów jądra i innych modułów. Sekcja
  `.crtos_module` podaje nazwę, opis, zależności oraz funkcje `init` i `exit`.

Budowanie sprawdza oba warunki (`tools/modcheck.py`).

## Karta SD

```
/crtos/
  boot/board.dtb          drzewo urządzeń
  drivers/*.ko            sterowniki
  drivers/modules.alias   który moduł obsługuje który "compatible"
  sbin/*.app              usługi (init, devmgr, gfxd, inputd, netmgr, httpd, deployd, vncd)
  bin/*.app               programy konsolowe
  apps/*.app              programy z oknem
  etc/init.cfg            co uruchamia init
  etc/launcher.cfg        menu Apps
  etc/network.cfg         sieć: DHCP albo stały adres, DNS, NTP
  etc/timezone            strefa czasowa (ustawia ją program Settings)
  etc/ui.cfg              wygląd: skala, czcionka, kolory, przezroczystość, rogi, tapeta
                          (Settings albo polecenie appearance)
  etc/deploy.token        token wgrywania przez sieć
  etc/ssl/cacert.pem      certyfikaty dla HTTPS
  share/icons/*.pam       ikony programów (PAM z przezroczystością)
  share/fonts/*.fnt       czcionki interfejsu w rozmiarach skal (tools/fonts.py)
  share/wallpapers/       tapety użytkownika, PPM albo PAM (crtos wallpaper)
  share/netsurf/          zasoby przeglądarki
  var/netsurf/            ciasteczka przeglądarki
  tmp/                    pliki tymczasowe
```

CRTOS nie zmienia niczego poza katalogiem `/crtos`.

## Usługi

| Usługa | Rola |
|---|---|
| `init` | uruchamia usługi i programy z `init.cfg`, uruchamia ponownie te z `respawn` |
| `devmgr` | rozsyła informacje o urządzeniach (pojawienie się i zniknięcie) do usług, np. `inputd`; ładowanie sterowników dla urządzeń podłączonych po starcie nie jest jeszcze zaimplementowane |
| `gfxd` | serwer grafiki: okna w pamięci współdzielonej, składanie ekranu akceleratorem PXP, kierowanie dotyku, kółka myszy i klawiszy do okien (klawisz Windows do menedżera okien), schowek wspólny dla programów, kopia ekranu dla zdalnego pulpitu, tapeta pulpitu i ogłaszanie zmian wyglądu |
| `inputd` | czyta `/dev/event*` (dotyk, przyciski, klawiatury i myszy USB z kółkiem, pad z gałkami) i przekazuje zdarzenia do `gfxd`; rysuje kursor myszy |
| `osk` | klawiatura ekranowa, gdy nie ma podłączonej klawiatury: pokazuje ją przycisk na pasku zadań, a klawisze trafiają do aktywnego okna jak z prawdziwej klawiatury |
| `netmgr` | konfiguracja sieci z `network.cfg`: DHCP, DNS, zegar z NTP |
| `httpd` | mały serwer WWW: `/status`, `/api/status`, pliki z `/sd/crtos/www` |
| `deployd` | odbiera pliki z komputera (`crtos deploy`, `crtos scp`) i nowe jądro (`crtos flash --net`, zapis do flash przez `/dev/mtd0`), wysyła pliki do komputera (`crtos scp board:...`); chroniony tokenem, pisze pliki tylko w `/sd/crtos`, `/flash0` i `/ram`. Narzędzie deweloperskie: w produkcie usunąć z `init.cfg` |
| `vncd` | zdalny pulpit: serwer VNC (port 5900, hasło w `/sd/crtos/etc/vnc.passwd`) – obraz ekranu, mysz, klawiatura i schowek dla `crtos desktop` albo innej przeglądarki VNC |
| `getty` | powłoka na porcie szeregowym USB (`/dev/ttyACM0`): startuje, gdy program terminala na komputerze otworzy port, i kończy się, gdy go zamknie |
| `wm` | menedżer okien: ramki (z ikoną programu), pasek zadań (okna jako ikony, podpowiedź z tytułem po najechaniu myszą), menu **Apps** z ikonami; w skali interfejsu, półprzezroczyste i z zaokrąglonymi rogami według ustawień wyglądu. Można go zakończyć i uruchomić ponownie bez zamykania programów |

## Wygląd interfejsu

Jak w Windows, wygląd całego systemu ustawia się w jednym miejscu: w programie Settings
(strony **Appearance** i **Wallpaper**) albo poleceniem `appearance` w powłoce. Ustawienia
leżą w `/sd/crtos/etc/ui.cfg`:

| Ustawienie | Wartości |
|---|---|
| `scale` | skala interfejsu: 100, 125, 150, 175 albo 200% |
| `font` | rodzina czcionek: `dejavu` (DejaVu Sans), `noto`, `liberation`, `dejavu-serif` |
| `accent` | kolor akcentu (RRGGBB): aktywne okno, zaznaczenia, przyciski |
| `transparency`, `opacity` | półprzezroczysty pasek zadań, menu i podpowiedzi; krycie 30–100% |
| `rounded` | zaokrąglone rogi przycisków, menu, podpowiedzi i pasków tytułu |
| `wallpaper`, `fit` | tapeta: wbudowana (Aurora, Dusk, Ocean, Forest, Graphite, CRTOS), `color:RRGGBB` albo obraz z `share/wallpapers`; dopasowanie `fill`, `fit`, `stretch`, `center`, `tile` |

Zmiana działa od razu, bez restartu: program zapisuje plik i powiadamia `gfxd`, który rysuje
tapetę od nowa i wysyła wszystkim programom zdarzenie `GFX_EV_SETTINGS`. Biblioteka `libgfx`
każdego programu wczytuje wtedy ustawienia (motyw `ui_theme`: czcionki skali, kolory, rogi,
krycie), a program układa okno od nowa. Menedżer okien tworzy nowy pasek zadań i ramki.
Za skalą idą programy, które liczą wymiary przez `ui_px` (Settings, Files, System monitor,
Terminal, klawiatura ekranowa, `wm`, szablon `crtos new`); gry, NetSurf i dema mają stałe
rozmiary. Przezroczystość składa akcelerator PXP (okna ARGB). Dolne rogi okien programów
zostają proste.

