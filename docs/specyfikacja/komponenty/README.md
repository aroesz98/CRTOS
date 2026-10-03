# Komponenty CRTOS

Projekt szczegółowy każdego komponentu (ISO 26262-6, rozdz. 8: projekt jednostek
programowych). Każdy dokument ma te same rozdziały: identyfikacja, odpowiedzialność,
wymagania (`REQ-<ID>-NN`), interfejs udostępniany i wymagany, struktura statyczna (diagram
klas), zachowanie dynamiczne (diagramy sekwencji, stanów lub aktywności), implementacja,
obsługa błędów, konfiguracja, weryfikacja, ograniczenia – zob.
[konwencje](../README.md#konwencje).

Diagramy są plikami PlantUML w [`../diagramy/<ID>/`](../diagramy); obraz SVG obok źródła
tworzy `python tools/diagrams.py`.

## L0: jądro – część zależna od architektury

| ID | Komponent | Wymagania | Diagramy |
|---|---|---|---|
| K01 | [Przełączanie kontekstu i wejście do jądra](K01-kontekst-i-svc.md) | 6 | 4 |
| K02 | [Przerwania](K02-przerwania.md) | 5 | 4 |
| K03 | [Ochrona pamięci (MPU)](K03-mpu.md) | 8 | 3 |
| K04 | [Wyjątki procesora](K04-wyjatki.md) | 6 | 3 |

## L0: jądro – usługi

| ID | Komponent | Wymagania | Diagramy |
|---|---|---|---|
| K05 | [Scheduler, czas i praca odroczona](K05-scheduler.md) | 8 | 5 |
| K06 | [Synchronizacja](K06-synchronizacja.md) | 7 | 4 |
| K07 | [Pamięć jądra](K07-pamiec-jadra.md) | 9 | 2 |
| K08 | [Procesy, uchwyty i uprawnienia](K08-procesy.md) | 9 | 5 |
| K09 | [Wywołania systemowe](K09-wywolania-systemowe.md) | 7 | 3 |
| K10 | [IPC: porty i komunikaty](K10-ipc.md) | 7 | 3 |
| K11 | [Pamięć współdzielona](K11-pamiec-wspoldzielona.md) | 9 | 3 |
| K12 | [Potoki i poll](K12-potoki-i-poll.md) | 5 | 3 |
| K13 | [System plików (VFS)](K13-vfs.md) | 9 | 3 |
| K14 | [Karta SD i FAT](K14-karta-sd-fat.md) | 8 | 4 |
| K15 | [Konsola, log jądra i terminal](K15-konsola-log.md) | 6 | 4 |
| K16 | [Loader: moduły i programy](K16-loader.md) | 14 | 5 |
| K17 | [Drzewo urządzeń i model sterowników](K17-drzewo-urzadzen.md) | 7 | 3 |
| K18 | [Start systemu](K18-start.md) | 7 | 3 |
| K19 | [Monitor jądra i autotesty](K19-kmon.md) | 6 | 3 |
| K20 | [Pamięć emulowana](K20-pamiec-emulowana.md) | 8 | 2 |
| K21 | [API RTOS: kolejki, timery, aplikacja](K21-api-rtos.md) | 6 | 2 |

## L0: frameworki podsystemów

| ID | Komponent | Wymagania | Diagramy |
|---|---|---|---|
| S01 | [Zegary, piny i GPIO](S01-zegary-piny-gpio.md) | 5 | 3 |
| S02 | [Magistrale I2C i SPI](S02-i2c-spi.md) | 5 | 3 |
| S03 | [Ekran i akcelerator 2D](S03-fb-gpu2d.md) | 7 | 2 |
| S04 | [Urządzenia wejścia](S04-wejscie.md) | 6 | 2 |
| S05 | [Sieć i gniazda](S05-siec.md) | 8 | 3 |

## L1: sterowniki (moduły `.ko`)

| ID | Komponent | Wymagania | Diagramy |
|---|---|---|---|
| D01 | [Sterowniki platformy: piny, zegary, GPIO](D01-platforma.md) | 6 | 2 |
| D02 | [Sterowniki magistral i portów szeregowych](D02-magistrale.md) | 6 | 4 |
| D03 | [Sterowniki ekranu i akceleratora 2D](D03-ekran.md) | 7 | 4 |
| D04 | [Sterowniki dotyku, przycisków i pada](D04-wejscie.md) | 13 | 4 |
| D05 | [Ethernet i stos TCP/IP](D05-siec.md) | 9 | 3 |
| D06 | [USB](D06-usb.md) | 8 | 4 |
| D07 | [Pamięć flash (MTD) i aktualizacja jądra](D07-flash-mtd.md) | 8 | 3 |
| D08 | [Generator liczb losowych i zegar RTC](D08-trng-rtc.md) | 5 | 3 |
| D09 | [Dźwięk: SAI i kodek WM8960](D09-dzwiek.md) | 6 | 3 |
| D10 | [System plików flash: /flash0](D10-flashfs.md) | 9 | 3 |

## L2: usługi

| ID | Komponent | Wymagania | Diagramy |
|---|---|---|---|
| U01 | [init](U01-init.md) | 4 | 2 |
| U02 | [devmgr](U02-devmgr.md) | 3 | 2 |
| U03 | [gfxd: serwer grafiki](U03-gfxd.md) | 14 | 5 |
| U04 | [inputd i osk](U04-inputd-osk.md) | 9 | 4 |
| U05 | [netmgr](U05-netmgr.md) | 3 | 2 |
| U06 | [httpd i deployd](U06-httpd-deployd.md) | 7 | 3 |
| U07 | [getty](U07-getty.md) | 4 | 2 |
| U08 | [vncd: zdalny pulpit](U08-vncd.md) | 10 | 3 |

## L3: aplikacje i programy

| ID | Komponent | Wymagania | Diagramy |
|---|---|---|---|
| A01 | [wm: menedżer okien](A01-wm.md) | 11 | 5 |
| A02 | [Aplikacje z oknem](A02-aplikacje.md) | 17 | 6 |
| A03 | [Programy konsolowe](A03-programy-konsolowe.md) | 13 | 6 |

## Biblioteki programów

| ID | Komponent | Wymagania | Diagramy |
|---|---|---|---|
| L01 | [libcrtos: biblioteka programów](L01-libcrtos.md) | 14 | 4 |
| L02 | [libgfx i TFTLIB](L02-libgfx.md) | 13 | 4 |

## Narzędzia (komputer)

| ID | Komponent | Wymagania | Diagramy |
|---|---|---|---|
| T01 | [Narzędzia i budowanie](T01-narzedzia.md) | 17 | 3 |
| T02 | [Toolchain CRTOS](T02-toolchain.md) | 15 | 5 |

Razem: 51 komponentów, 419 wymagań, 173 diagramy (każdy komponent ma co najmniej jeden
diagram klas i jeden diagram sekwencji). Powiązanie wymagań z weryfikacją:
[04 Weryfikacja](../04-weryfikacja.md#4-śledzenie-wymagań).
