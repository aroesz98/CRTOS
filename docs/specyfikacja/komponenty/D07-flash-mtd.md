# D07 Pamięć flash (MTD) i aktualizacja jądra

## 1. Identyfikacja

| | |
|---|---|
| Identyfikator | D07 |
| Warstwa | L1 (moduł `.ko`) |
| Moduł | `flexspi-mtd.ko` (`fsl,imxrt1050-flexspi`); STM32H7S7: `xspi-mtd.ko` (`st,stm32-xspi`) |
| Pliki | `drivers/mtd/flexspi-mtd.c`; SDK `fsl_flexspi.c`; sekwencje LUT z przykładu NXP `flexspi/hyper_flash` (BSD-3-Clause); `drivers/mtd/xspi-mtd.c` (rejestry XSPI2, bez HAL), nagłówek obrazu jądra `kernel/platform/stm32h7s78dk/board/image.h` |
| Interfejs | `kernel/include/crtos/mtd.h` (`/dev/mtdN`: `MEMGETINFO`, `MEMERASE`, `MTD_IOC_KERNEL_UPDATE`; dla modułów: `mtd_part_*`) |

## 2. Odpowiedzialność

- HyperFlash S26KS512S (64 MB) na FlexSPI – ta sama pamięć, z której działa jądro (XIP).
- Partycje z DT jako pliki `/dev/mtd0` (`kernel`, 2 MB, tylko odczyt) i `/dev/mtd1`
  (`flash0`, 62 MB, system plików `/flash0`, D10): odczyt przez okno pamięci, kasowanie
  bloków 256 KB, programowanie (stron 512 B, dowolna długość i wyrównanie).
- Dostęp dla innych modułów (`mtd_part_*`, eksportowane): partycja po węźle DT, geometria
  i okno pamięci, zajęcie (`claim`), programowanie, kasowanie.
- Kasowanie i programowanie z kodu w RAM przy wyłączonych przerwaniach i pamięciach
  podręcznych (żadna instrukcja nie może być w tym czasie pobierana z flash).
- **Aktualizacja jądra** (`MTD_IOC_KERNEL_UPDATE`): sprawdzenie obrazu, zapis, weryfikacja
  i restart – bez powrotu do starego jądra.
- **xspi-mtd** (STM32H7S78-DK): to samo dla NOR Macronix MX66UW1G45G (128 MB) na XSPI2,
  zmapowanej przez program startowy od 0x70000000 (XIP): ten sam interfejs, partycje
  `kernel` (2 MB, tylko odczyt) i `flash0` (126 MB), ten sam blok kasowania 256 KB (cztery
  bloki 64 KB układu), strona programowania 256 B. Na czas operacji XSPI2 wychodzi z trybu
  mapowania pamięci, komendy idą w trybie pośrednim (octal DTR, jak ustawił układ program
  startowy), a potem wraca tryb mapowania z ustawieniami programu startowego. Aktualizacja
  sprawdza nagłówek obrazu, który sprawdza też program startowy (K18).

## 3. Wymagania

| ID | Wymaganie | Weryfikacja |
|---|---|---|
| REQ-D07-01 | Partycja `kernel` i każda partycja zaczynająca się w pierwszym bloku (nagłówek startowy) są tylko do odczytu (`-EROFS`), niezależnie od DT. | `mtd info`, przegląd kodu |
| REQ-D07-02 | W czasie kasowania/programowania wszystkie przerwania są zamaskowane, I-cache i D-cache wyłączone, a kod działa z RAM (OCRAM; STM32: AXI SRAM albo PSRAM). | przegląd kodu, `mtd test` |
| REQ-D07-03 | Programowanie odbywa się przy zegarze FlexSPI ¼ (32 MHz, limit HyperFlash dla danych programowania); zegar i DLL wracają dokładnie do wartości z Boot ROM. | `mtd test` (0 różnic), start po aktualizacji |
| REQ-D07-04 | Aktualizacja jądra wymaga `CAP_SYS` i jest odrzucana (`-EINVAL`, flash nietknięta), jeśli obraz nie ma nagłówka FCFB i IVT albo suma CRC-32 się nie zgadza. | `crtos flash --net` ze złym plikiem (27.09.2026: `ERR Invalid argument`) |
| REQ-D07-05 | Zapisane jądro jest porównywane z obrazem; przy niezgodności zapis jest powtarzany (3 próby); po sukcesie następuje restart. | `crtos flash --net` (27.09.2026: nowe jądro wystartowało) |
| REQ-D07-06 | Kasowanie przyjmuje tylko całe bloki w granicach partycji (`-EINVAL`). | przegląd kodu |
| REQ-D07-07 | Partycja zajęta przez moduł (`mtd_part_claim`) odrzuca zapis i kasowanie przez `/dev/mtdN` (`-EBUSY`); `mtd_part_program`/`erase` działają tylko w granicach partycji, nie na partycji tylko do odczytu. | `mtd write mtd1` przy zamontowanym `/flash0` → `EBUSY` |
| REQ-D07-08 | Po każdym programowaniu i kasowaniu bufory AHB FlexSPI są resetowane, a pamięci podręczne unieważnione: odczyt przez okno (także kod wykonywany z flash) widzi nowe dane. | `flashtest.sh` (odczyt po zapisie, `flashfs check`), programy XIP (K16) |
| REQ-D07-09 | `xspi-mtd`: na czas kasowania/programowania region MPU pamięci NOR jest niewykonywalny (XN: rdzeń nie pobiera z niej nawet spekulatywnie). Po każdej operacji, także nieudanej, XSPI2 wraca do trybu mapowania pamięci z rejestrami zapisanymi przy `probe` (ustawienia programu startowego), a pamięci podręczne są unieważniane przy ponownym włączeniu: odczyt przez okno (także kod XIP) widzi nowe dane. | przegląd kodu; programy XIP zapisane na `/flash0` STM32H7S78-DK (NES, `nes -b`, [Sterowniki](../../sterowniki.md#dysk-flash0)) |
| REQ-D07-10 | `xspi-mtd` przyjmuje do aktualizacji tylko obraz, który przyjąłby program startowy: nagłówek pod 0x400 z magią `CRKN`, właściwym rozmiarem nagłówka, rozmiarem obrazu równym długości (najwyżej 2 MB) i zgodnym CRC-32 (liczonym z polem CRC = 0), oraz zgodną sumą CRC-32 przesłanego pliku. Inaczej `-EINVAL`, flash nietknięta. | przegląd kodu |

`xspi-mtd` spełnia też REQ-D07-01, 02 i 05–07 (ten sam kod partycji, sekcji cichej i trzech
prób zapisu), sprawdzone tylko przeglądem kodu. REQ-D07-03 i 08 opisują FlexSPI (ich
odpowiednik to REQ-D07-09), REQ-D07-04 – obraz i.MX RT (odpowiednik: REQ-D07-10).

## 4. Interfejs udostępniany

| Operacja | Opis | Wynik |
|---|---|---|
| `open`/`close` `/dev/mtdN` | pozycja na otwarcie (`CAP_DEV`) | |
| `read(buf, len)` | kopia z okna 0x60000000 + przesunięcie partycji (pod mutexem) | bajty, 0 na końcu |
| `write(buf, len)` | programowanie od pozycji: bity 1 → 0 (miejsce musi być skasowane) | bajty, `-EROFS`, `-ENOSPC`, `-EIO`, `-ETIMEDOUT` |
| `lseek`, `fstat` | pozycja, rozmiar partycji | |
| `MEMGETINFO` | `struct mtd_info_user`: typ NOR, flagi zapisu, rozmiar, blok 256 KB, `writesize` 1 | 0 |
| `MEMERASE {start, length}` | kasowanie bloków (wielokrotności 256 KB) | 0, `-EINVAL`, `-EROFS`, `-EIO` |
| `MTD_IOC_KERNEL_UPDATE {image, len, crc32}` (na `/dev/mtd0`) | nowe jądro i restart; wraca tylko przy odmowie | `-EPERM`, `-EINVAL`, `-EFAULT`, `-ENOMEM` |
| `mtd_part_of_node(np)` | partycja o węźle DT `np` (dla modułów) | wskaźnik albo `NULL` |
| `mtd_part_geometry(p, g)` | rozmiar, blok kasowania, adres okna | 0 |
| `mtd_part_claim(p, owner)`, `mtd_part_release(p)` | zajęcie partycji: `/dev/mtdN` nie zapisze | 0, `-EBUSY`, `-EROFS` |
| `mtd_part_program(p, off, buf, len)` | programowanie (bity 1 → 0) | 0, `-EINVAL`, `-EROFS`, `-EIO` |
| `mtd_part_erase(p, off, len)` | kasowanie całych bloków | 0, `-EINVAL`, `-EROFS`, `-EIO` |

Parametry DT: węzeł `&flexspi` z dzieckiem `flash@0` i `partitions` (`label`, `reg`,
`read-only`).

`xspi-mtd` daje ten sam interfejs z oknem od 0x70000000 (`read`, `mtd_part_geometry`).
Parametry DT: `&xspi2` (`reg` 0: rejestry, 1: okno 128 MB) z dzieckiem `flash@0`
(`compatible = "mxicy,mx66uw1g45g"`, `size`) i `partitions`; bez partycji cała pamięć jest
jedną partycją `flash`.

## 5. Interfejsy wymagane

K08 (`capable`, `uaccess_ok`), K13 (`devfs_register`), K07 (kopia obrazu w SDRAM), K05
(`task_sleep_ms` przed aktualizacją), K06 (mutex), SDK (`fsl_flexspi`, `fsl_clock`, CMSIS
cache), `crc32` jądra. Użytkownicy: `mtd` (A03), `deployd` (U06) → `crtos flash --net`
(T01). `xspi-mtd`: te same K08, K13, K07 (`KM_LARGE`: kopia obrazu w PSRAM), K05, K06;
CMSIS (pamięci podręczne, MPU, `NVIC_SystemReset`), UART4 konsoli odpytywaniem w czasie
aktualizacji, `board/image.h`.

## 6. Struktura statyczna

![D07: struktura statyczna (diagram klas)](../diagramy/D07/struktura-statyczna.svg)

*Źródło: [D07/struktura-statyczna.puml](../diagramy/D07/struktura-statyczna.puml)*

## 7. Zachowanie dynamiczne

### 7.1 Zapis strony

![D07: zapis strony (diagram sekwencji)](../diagramy/D07/zapis-strony.svg)

*Źródło: [D07/zapis-strony.puml](../diagramy/D07/zapis-strony.puml)*

### 7.2 Aktualizacja jądra

![D07: aktualizacja jądra (diagram sekwencji)](../diagramy/D07/aktualizacja-jadra.svg)

*Źródło: [D07/aktualizacja-jadra.puml](../diagramy/D07/aktualizacja-jadra.puml)*

## 8. Implementacja

- Moduł leży w OCRAM (kod `KM_EXEC`), więc działa bez flash. Sekwencje LUT FlexSPI 2..11
  (odczyt statusu, write enable, kasowanie sektora, programowanie strony) są instalowane
  w `probe` w sekcji „cichej”; sekwencja 0 (odczyt XIP z Boot ROM) nie jest zmieniana.
- Zegar FlexSPI: korzeń 261,8 MHz (PFD0 PLL3), `PODF` 0 → 130 MHz DDR; przy programowaniu
  `PODF` 3 → 32 MHz; `FLEXSPI_UpdateDllValue` odtwarza DLL (0x79).
- Po kasowaniu i programowaniu `FLEXSPI_SoftwareReset` czyści bufory AHB (stare dane
  z okna XIP).
- `kernel_update` nie wraca: stary kod jądra (XIP) nie może się już wykonać, bo flash jest
  kasowana.
- Czasy: strona 512 B ok. 0,8 ms, 256 KB zapisu ok. 0,4 s, kasowanie bloku z danymi ok.
  0,8 s (pusty ok. 11 ms); aktualizacja 240 KB ok. 2 s + restart.
- **xspi-mtd** (STM32H7S78-DK):
  - moduł leży w AXI SRAM albo PSRAM; `memcpy`/`memset` w czasie aktualizacji to funkcje
    jądra z ITCM. `probe` zapamiętuje ustawienia trybu mapowania z programu startowego
    (`CR`, `CCR`, `TCR`, `IR`, `WCCR`, `WTCR`, `WIR`) i odmawia, gdy XSPI2 nie jest w tym
    trybie;
  - sekcja cicha: `PRIMASK`, wyłączenie I-cache i D-cache (z czyszczeniem), XN na regionie
    MPU od 0x70000000; wyjście w odwrotnej kolejności (włączenie pamięci podręcznych je
    unieważnia);
  - wyjście z trybu mapowania: `ABORT` (odczyt zmapowany trzyma wybór układu i czyta
    z wyprzedzeniem), czekanie na koniec i bezczynność kontrolera, tryb pośredni. Komendy
    octal DTR: WREN (z czekaniem na WEL), RDSR (5 cykli przerwy, 2 bajty), PP (strona 256 B
    od parzystego adresu, parzysta długość, 0xFF obok zapisywanych bajtów), BE64K;
    gotowość: bit WIP, najwyżej 5 s; każdy krok kontrolera najwyżej 10 ms;
  - `kernel_update`: sekcja cicha na stałe, do 3 prób (kasowanie, programowanie,
    porównanie), komunikaty na UART4 odpytywaniem, `NVIC_SystemReset`;
  - czasy: strona 256 B ok. 0,15 ms, kasowanie bloku 256 KB do ok. 1 s.

## 9. Obsługa błędów i mechanizmy bezpieczeństwa

| Sytuacja | Reakcja |
|---|---|
| zapis/kasowanie partycji tylko do odczytu | `-EROFS` |
| błąd programowania/kasowania (bity statusu 0x3200) | `-EIO` |
| brak gotowości w 5 s | `-ETIMEDOUT` |
| obraz jądra zły, brak `CAP_SYS` | odmowa bez zmian we flash |
| weryfikacja nieudana 3 razy | komunikat na UART co kilka sekund; naprawa sondą (`crtos flash`) |
| odcięcie zasilania w trakcie aktualizacji | płytka nie startuje; naprawa sondą (tryb awaryjny SW8 nie pomaga) |
| `xspi-mtd`: XSPI2 nie w trybie mapowania przy `probe` | `-ENODEV` („the boot loader sets it up”) |
| `xspi-mtd`: kontroler albo układ nie odpowiada | `-ETIMEDOUT` (krok 10 ms, gotowość 5 s); tryb mapowania i tak wraca (REQ-D07-09) |
| `xspi-mtd`: brak WEL po WREN | `-EIO` |
| `xspi-mtd`: obraz bez nagłówka `CRKN` albo z inną sumą | `-EINVAL`, flash nietknięta (REQ-D07-10) |

## 10. Konfiguracja

`SECTOR` (256 KB), `PAGE` (512 B), `MAX_PARTS` (4), `BUSY_TIMEOUT_S` (5); partycje w
`dts/evkbimxrt1050.dts`. `xspi-mtd.c`: `SECTOR` (256 KB), `BLOCK` (64 KB), `PAGE` (256 B),
`MAX_PARTS` (4), `BUSY_TIMEOUT_S` (5); partycje w `dts/stm32h7s78dk.dts`; nagłówek obrazu
(`CRTOS_IMAGE_HDR_OFF` 0x400, `CRTOS_IMAGE_MAX` 2 MB) w `board/image.h`, wypełnia go
`tools/imagehdr.py` (T01).

## 11. Weryfikacja

- `crtos run mtd info`, `mtd test mtd1 0x100000` (kasowanie, wzór, porównanie,
  przywrócenie danych; 27.09.2026: 0 różnic).
- `crtos flash --net` z poprawnym i z uszkodzonym obrazem (27.09.2026).
- STM32H7S78-DK: programy XIP (NES, voxel) zapisane na `/flash0` i uruchamiane z niego
  ([Sterowniki](../../sterowniki.md#dysk-flash0)); `mtd test` i `crtos flash --net` – te same
  polecenia, wyników z tej płytki dokumentacja nie zawiera.

## 12. Ograniczenia i znane problemy

- W czasie kasowania/programowania **cały system stoi** (do ok. 0,8 s): sieć i UART mogą
  gubić dane, tyknięcia są nadrabiane (K05).
- Brak obrazu zapasowego jądra (A/B): nieudana aktualizacja wymaga sondy.
- Surowy dostęp do `/dev/mtd1` jest zablokowany, gdy działa `flashfs` (D10). Bez modułu
  `flashfs` partycja jest zwykłym obszarem na dane.
- STM32: system stoi do ok. 1 s na każdy kasowany blok 256 KB. `xspi-mtd` nie czyta bitów
  błędu programowania i kasowania układu: błąd widać dopiero przy porównaniu (aktualizacja
  jądra) albo sumie CRC-32 pliku (`flashfs`, D10).
