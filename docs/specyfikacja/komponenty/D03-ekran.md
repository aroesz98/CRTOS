# D03 Sterowniki ekranu i akceleratora 2D

## 1. Identyfikacja

| | |
|---|---|
| Identyfikator | D03 |
| Warstwa | L1 (moduły `.ko`) |
| Moduły | `lcdif-imxrt.ko` (`fsl,imxrt1050-lcdif`), `pxp-imxrt.ko` (`fsl,imxrt1050-pxp`); STM32H7S7: `ltdc-stm32.ko` (`st,stm32-ltdc`), `gpu2d-stm32.ko` (`st,stm32-gpu2d`) |
| Pliki | `drivers/display/lcdif-imxrt.c`, `drivers/display/pxp-imxrt.c`; SDK: `fsl_elcdif.c`, `fsl_pxp.c`; `drivers/display/ltdc-stm32.c`, `drivers/display/gpu2d-stm32.c` (rejestry, bez HAL i bez NemaGFX) |
| Interfejsy realizowane | `fb_ops`, `gpu2d_ops` (S03) |

## 2. Odpowiedzialność

- **lcdif-imxrt**: kontroler eLCDIF z panelem RGB na 40-pinowym złączu (J8). Obsługuje
  kilka paneli, po jednym trybie na panel w węźle `display-timings`:
  - 480×272: RK043FN66HS, RK043FN02H;
  - 800×480: np. panel 4,3" IPS z modułu Waveshare 16249.

  Do tego:
  - start w trybie domyślnym (`native-mode`) i zmiana trybu na prośbę frameworka (S03,
    `set_size`), gdy kontroler dotyku zgłosi rozmiar swojego panelu;
  - dwa bufory RGB565 w pamięci bez cache, przełączane na początku ramki;
  - przerwanie końca ramki (`fb_vsync`);
  - zegar pikseli z PLL wideo;
  - wygaszanie (podświetlenie).
- **pxp-imxrt**: PXP jako akcelerator 2D:
  - wypełnienie prostokąta (kolor tła PXP);
  - kopia z konwersją formatu (RGB565, XRGB8888, ARGB8888);
  - mieszanie ARGB z powierzchnią docelową;
  - obcinanie do powierzchni i utrzymanie spójności pamięci podręcznej.
- **ltdc-stm32** (STM32H7S78-DK): kontroler LTDC z panelem RGB płytki RK050HR18 (5",
  800×480, 24-bitowy). Ten sam opis panelu w DT i to samo zachowanie co `lcdif-imxrt`:
  - start w trybie `native-mode`, zmiana trybu (`set_size`) tylko przed startem `gfxd`;
  - dwa bufory RGB565 w pamięci bez cache (PSRAM), pokazywane warstwą 1 nad czarnym tłem,
    przełączane w pionowym wygaszaniu;
  - przerwanie linii 0 jako początek ramki (`fb_vsync`);
  - zegar pikseli z PLL3 R (D01);
  - wygaszanie (podświetlenie); liczenie niedopełnień FIFO i błędów magistrali.
- **gpu2d-stm32**: NeoChrom GPU2D razem z DMA2D (Chrom-ART) jako akcelerator 2D. Wypełnienie,
  kopię z konwersją formatu i mieszanie robi DMA2D, skalowanie (także dwuliniowe) i prymitywy
  z teksturą (S03, `GPU2D_IOC_PRIMS`) GPU. Listy poleceń GPU pisze sterownik; szczegóły:
  [S03, rozdz. 8.1](S03-fb-gpu2d.md#81-neochrom-gpu2d-stm32-stm32h7s7).

## 3. Wymagania

| ID | Wymaganie | Weryfikacja |
|---|---|---|
| REQ-D03-01 | Bufor ramki pokazywany na ekranie zmienia się tylko na granicy ramek; sterownik zgłasza bufor faktycznie pobrany przez kontroler (`CUR_BUF`). | obserwacja (brak rozrywania), `gfxinfo` |
| REQ-D03-02 | Każde zakończenie ramki wywołuje `fb_vsync` (licznik ramek, `poll`). | `gfxinfo` (58 fps) |
| REQ-D03-03 | Operacja PXP kończy się w 200 ms albo PXP jest resetowany i zwracany jest `-ETIMEDOUT`. | przegląd kodu |
| REQ-D03-04 | Obszar docelowy operacji PXP (wypełnienie, kopia, mieszanie) w pamięci z cache jest czyszczony i unieważniany przed operacją i unieważniany ponownie po niej (brak nieaktualnych danych dla procesora, także linii wczytanych w czasie operacji). | przegląd kodu, `kmon gpu2dtest`, kopia ekranu `vncd` w pamięci z cache = `crtos shot` (02.10.2026) |
| REQ-D03-05 | Przed startem kontrolera linia `LCD_RST` (`enable-gpios` panelu: reset kontrolera dotyku; DISP panelu podciąga na płytce R67) jest przez 5 ms niska, potem wysoka; podświetlenie włącza się po starcie. | start płytki |
| REQ-D03-06 | Ekran startuje w trybie `native-mode` z `display-timings` panelu (bez tej właściwości: w pierwszym). `set_size` zmienia tryb tylko na tryb opisany w DT o tym rozmiarze (inaczej `-ENOENT`). Gdy na bufory nowego trybu brak pamięci, wraca stary tryb z buforami i zwracane jest `-ENOMEM`. | `kmon fb` (02.10.2026): 10 zmian 480×272 ↔ 800×480 z pomiarem `fbtest` (58,7 i 59,1 Hz), `fb 1024x600`: brak trybu, pula `ncache` po zmianach bez ubytku |
| REQ-D03-07 | Zmiana trybu nie czeka na sprzęt bez końca: koniec ramki przy zatrzymaniu najwyżej 50 ms, potwierdzenie programowego resetu najwyżej 5 ms (potem ostrzeżenie i dalej). PLL wideo zmienia się przy zamkniętej bramce zegara pikseli, a kontroler startuje po resecie. | przegląd kodu; na płytce przed poprawką trzecia zmiana z rzędu zawiesiła wątek kmon w `ELCDIF_Reset`, a bez resetu kontroler w 4 z 5 zmian nie generował ramek; po poprawce 10 zmian bez błędu |
| REQ-D03-08 | `ltdc-stm32`: nowy bufor obowiązuje od przeładowania rejestrów warstwy w pionowym wygaszaniu (`SRCR.VBR`), a jako pokazany jest zgłaszany dopiero w przerwaniu linii 0, gdy kontroler skasował żądanie przeładowania. Niedopełnienie FIFO i błąd magistrali są liczone, każde najwyżej raz na ramkę (ich przerwanie wraca w następnej ramce), więc za wolna pamięć bufora nie zalewa procesora przerwaniami. | przegląd kodu |
| REQ-D03-09 | `gpu2d-stm32` odrzuca (`-EINVAL`) operację i prymitywy, których powierzchnia albo tablica tekstur nie leży w całości w pamięci osiągalnej dla GPU i DMA2D (AXI SRAM, PSRAM, NOR flash; nie TCM). Źródła w pamięci z cache są czyszczone przed paczką, a cele czyszczone i unieważniane przed nią i po niej (przy ponad 64 KB cała pamięć podręczna danych). | przegląd kodu |
| REQ-D03-10 | `gpu2d-stm32`: lista poleceń, której GPU nie skończy w 200 ms, kończy wywołanie błędem `-ETIMEDOUT`, a GPU jest resetowany (RCC) i ustawiany od nowa. Operacja DMA2D, która nie skończy się w tym czasie (krótka, odpytywana: 50 ms) albo zgłosi błąd, jest przerywana (`ABORT`) z `-ETIMEDOUT` albo `-EIO`. | przegląd kodu |

REQ-D03-02 i REQ-D03-06 (bez pomiarów z płytki) dotyczą też `ltdc-stm32`; REQ-D03-01, 03–05
i 07 opisują eLCDIF i PXP, a ich odpowiedniki na STM32 to REQ-D03-08–10 (panel RK050HR18 nie
ma linii resetu, D04).

## 4. Interfejs udostępniany

| Moduł | Rejestracja | Operacje |
|---|---|---|
| `lcdif-imxrt` | `fb_register` → `/dev/fb0` | `show(i)`: `ELCDIF_SetNextBufferAddr`; `wait_vsync(timeout)`; `blank(on)`: podświetlenie; `set_size(w, h, info)`: zmiana trybu panelu (S03, `fb_suggest_size`) |
| `pxp-imxrt` | `gpu2d_register` | `fill(dst, r, argb)`, `blit(dst, dx, dy, src, sr, flags)` (`GPU2D_BLEND`: mieszanie alfa) |
| `ltdc-stm32` | `fb_register` → `/dev/fb0` | jak `lcdif-imxrt`; `show(i)`: adres warstwy 1 (`CFBAR`) i przeładowanie w wygaszaniu (`SRCR.VBR`) |
| `gpu2d-stm32` | `gpu2d_register` | `fill`, `blit`, `batch` (paczka `gfxd` jako jedna lista poleceń), `stats` (nazwa `DMA2D+NeoChrom`, `GPU2D_CAP_SCALE`, `GPU2D_CAP_PRIMS`), `prims` (S03) |

## 5. Interfejsy wymagane

- S03: `fb_register`, `fb_vsync`, `gpu2d_register`.
- S01: zegary `axi`, `pix-gate` (wyłączany na czas zmiany PLL), `pix`; GPIO panelu.
- D01: zegar pikseli z PLL wideo.
- K02: przerwania LCDIF 5, PXP 7.
- K06: flagi zdarzeń, semafor.
- K07: `KM_NOCACHE` na bufory.
- SDK: `fsl_elcdif`, `fsl_pxp`, `DCACHE_*`.
- STM32: zegary `bus` i `pix` (PLL3 R, D01), `gpu`, `dma2d`; przerwania LTDC 96 i 97
  (priorytet 5), GPU 149 i 150 oraz DMA2D 98 (priorytet 7); K07 `KM_NOCACHE` (bufory ekranu,
  pierścień poleceń GPU), `KM_DMA` (listy poleceń); CMSIS (`SCB_*DCache*`).

## 6. Struktura statyczna

![D03: struktura statyczna (diagram klas)](../diagramy/D03/struktura-statyczna.svg)

*Źródło: [D03/struktura-statyczna.puml](../diagramy/D03/struktura-statyczna.puml)*

## 7. Zachowanie dynamiczne

### 7.1 Przełączenie bufora

![D03: przełączenie bufora (diagram sekwencji)](../diagramy/D03/przelaczenie-bufora.svg)

*Źródło: [D03/przelaczenie-bufora.puml](../diagramy/D03/przelaczenie-bufora.puml)*

### 7.2 Kopia z mieszaniem (PXP)

![D03: kopia z mieszaniem (PXP) (diagram sekwencji)](../diagramy/D03/kopia-z-mieszaniem.svg)

*Źródło: [D03/kopia-z-mieszaniem.puml](../diagramy/D03/kopia-z-mieszaniem.puml)*

### 7.3 Wybór trybu panelu

![D03: wybór trybu panelu (diagram sekwencji)](../diagramy/D03/zmiana-trybu.svg)

*Źródło: [D03/zmiana-trybu.puml](../diagramy/D03/zmiana-trybu.puml)*

## 8. Implementacja

- Tryby to węzły w `display-timings` panelu: `hactive`, `vactive`, `clock-frequency`,
  przedsionki, synchronizacje i polaryzacje jak w Linuksie. `find_mode(0, 0)` daje tryb
  domyślny (`native-mode`), `find_mode(w, h)` tryb o tym rozmiarze.
- Bufory: 2 × szerokość × wysokość × 2 B w `ncache` (wyrównanie 64 B), zerowane:
  - 480×272: 2 × 255 KB;
  - 800×480: 2 × 750 KB (z 2 MB puli bez cache zostaje ok. 475 KB).

  Zwalniane akcją `devm` przy odłączeniu i przy zmianie trybu: stare przed alokacją
  nowych, bo obu naraz pula nie mieści.
- Zegar pikseli z DT ustawiany przez `clk_set_rate(pix)`:
  - 480×272: 9,3 MHz, 58,7 Hz;
  - 800×480: 27 MHz (24 MHz × 45 / 5 / 8 dokładnie), 59,2 Hz.

  Częstotliwość odświeżania = zegar / (htotal × vtotal).
- Start przy `probe`: `ELCDIF_RgbModeInit` z SDK. Blok jest jeszcze w resecie po
  włączeniu zasilania, który SDK tylko zwalnia.
- Zmiana trybu (`lcdif_set_size`):
  1. `stop`: wyłączenie `DOTCLK_MODE` i czekanie na koniec ramki, najwyżej 50 ms (funkcja
     SDK czekała bez końca).
  2. Nowe bufory.
  3. `clk_disable(pix-gate)`, ustawienie PLL i dzielników, `clk_enable`.
  4. `soft_reset`: SFTRST, czekanie na CLKGATE najwyżej 5 ms.
  5. `rgb_mode_regs`: rejestry trybu RGB z `ELCDIF_RgbModeInit`, bez resetu z SDK.
  6. Start i nowe `fb_info`.

  Reset z SDK (`ELCDIF_Reset`) czeka bez końca na CLKGATE: trzecia zmiana z rzędu zawiesiła
  w nim wątek kmon. Bez resetu, przy zegarze zmienianym pod pracującą bramką, kontroler
  często nie ruszał.
- PXP:
  - wypełnienie: powierzchnia procesu przesunięta poza obszar wyjściowy, więc PXP zapisuje
    kolor tła;
  - kopia: źródło jako powierzchnia procesu;
  - mieszanie: cel czytany jako powierzchnia procesu, źródło jako powierzchnia alfa, wynik
    w miejscu celu.
- Obszar docelowy w OCRAM i SDRAM z cache jest czyszczony i unieważniany
  (`DCACHE_CleanInvalidateByRange`) przed operacją PXP – zapisy procesora trafiają do pamięci
  i nie nadpiszą potem wyniku – i unieważniany ponownie po niej: program mógł w tym czasie
  wczytać linie tego obszaru (zdalny pulpit czyta swoją kopię ekranu, gdy `gfxd` kopiuje do
  niej następną klatkę), a bez tego widziałby stare piksele. Dzięki temu kopia ekranu dla
  `vncd` (U03, U08) może być w pamięci z cache.
- **ltdc-stm32** (STM32H7S78-DK):
  - tryb: czasy (`SSCR`, `BPCR`, `AWCR`, `TWCR`) i polaryzacje z `display-timings` jak
    w Linuksie; warstwa 1 RGB565 na cały ekran, stała alfa 255 nad czarnym tłem;
  - bufory: 2 × 750 KB (800×480) w `ncache` (ostatnie 2 MB PSRAM), zerowane;
  - zegar pikseli: 25 MHz z DT, PLL3 R daje 24,576 MHz, odświeżanie 59,9 Hz;
  - start: DISP (`enable-gpios`) po uruchomieniu kontrolera, podświetlenie 40 ms później
    (pierwsza ramka czerni);
  - zmiana trybu: wyłączenie kontrolera i 25 ms na dokończenie ramki (bez czekania na
    sprzęt), zwolnienie starych buforów, nowe, start; bez pamięci – stary tryb z buforami
    i `-ENOMEM` (REQ-D03-06).
- **gpu2d-stm32**: GPU (osobny bus master) widzi AXI SRAM, PSRAM i NOR, nie TCM.
  - Pierścień poleceń 1 KB bez cache, dwie listy po 8 KB w pamięci `KM_DMA` (z cache,
    czyszczone przed oddaniem GPU).
  - Paczka `gfxd` to jedna lista: rejestrów o tej samej wartości lista nie powtarza.
    Pamięć podręczna odczytu GPU (ICACHE) nie widzi zapisów, więc jest opróżniana przed każdą
    listą, a operacja, która czyta to, co zapisała wcześniejsza z tej samej listy, zaczyna
    nową.
  - Wypełnienia, kopie i mieszanie idą do DMA2D (z PSRAM czyta całe linie, kilka razy szybciej
    niż jednostka tekstur GPU): do 4096 pikseli czekanie odpytywaniem (ok. 20 µs), dłuższe na
    przerwanie.
  - Prymitywy, kafelki w AXI SRAM1 i listy na zmianę: [S03, rozdz.
    8.1](S03-fb-gpu2d.md#81-neochrom-gpu2d-stm32-stm32h7s7).

## 9. Obsługa błędów i mechanizmy bezpieczeństwa

| Sytuacja | Reakcja |
|---|---|
| brak parametrów panelu | `E: no panel timings`, `-EINVAL` |
| brak pamięci na bufory | `-ENOMEM`, zwolnienie |
| zmiana na tryb, którego panel nie ma | `-ENOENT`, tryb bez zmian |
| brak pamięci na bufory nowego trybu | stary tryb z nowymi buforami, `-ENOMEM` |
| kontroler nie kończy ramki przy zatrzymaniu | po 50 ms ostrzeżenie `W:`, `RUN` kasowany |
| kontroler nie potwierdza resetu | po 5 ms ostrzeżenie `W:`, programowanie dalej |
| PXP nie kończy operacji | reset PXP, `-ETIMEDOUT` |
| nieobsługiwany format | `-EINVAL` |
| prostokąt poza powierzchnią | obcięcie (pusty: 0 bez uruchamiania PXP) |
| `ltdc-stm32`: niedopełnienie FIFO, błąd magistrali | licznik, najwyżej raz na ramkę; wpis w logu przy usunięciu modułu |
| `gpu2d-stm32`: powierzchnia poza pamięcią GPU (np. TCM) | `-EINVAL` (REQ-D03-09) |
| `gpu2d-stm32`: lista poleceń nieskończona w 200 ms | reset GPU (RCC), `-ETIMEDOUT`, pierwsze 5 w logu (REQ-D03-10) |
| `gpu2d-stm32`: DMA2D bez końca albo z błędem | `ABORT`, `-ETIMEDOUT` / `-EIO`, pierwsze 5 w logu |
| `gpu2d-stm32`: układ to nie NeoChrom albo bez blendera ROP | `-ENODEV` |
| `gpu2d-stm32`: GPU nie wykonuje pierścienia przy starcie | błąd `probe`, `/dev/gpu2d` na procesorze (S03) |

## 10. Konfiguracja

- `dts/evkbimxrt1050.dts`:
  - węzeł `panel`: `enable-gpios` (`LCD_RST`), `backlight-gpios` i `display-timings`
    z trybami `timing-480x272` i `timing-800x480`;
  - `native-mode` (dziś 800×480, bo jego dotyk sterownik rozpoznaje dopiero po rozmiarze,
    a panel RK043 przełącza się sam);
  - węzły `&lcdif` (phandle `display`) i `&pxp`.
- `NBUF` (2).
- Nowy panel: kolejny węzeł w `display-timings` z czasami z noty panelu; jego dotyk wybierze
  go sam, jeśli zgłosi swój rozmiar (D04).
- STM32: `dts/stm32h7s78dk.dts` – węzeł `panel` (`rocktech,rk050hr18`: `enable-gpios` PE15 =
  DISP, `backlight-gpios` PG15, jeden tryb `timing-800x480`), `&ltdc` (`display = <&panel>`,
  piny R0–R7, G0–G7, B0–B7, DE, CLK, HSYNC, VSYNC), `&gpu2d`; w węźle GPU
  `crtos,no-gpu-cache` i `crtos,no-dma2d` (S03).

## 11. Weryfikacja

- `crtos kmon fbtest`, `gpu2dtest`; `crtos run gfxinfo -b` (czas składania, liczba klatek);
  `crtos shot`.
- `crtos kmon fb [WxH]` (K19), przy zatrzymanym `gfxd`. 02.10.2026, panel 800×480 z modułu
  Waveshare 16249:
  - 10 zmian 480×272 ↔ 800×480, każda z pomiarem `fbtest` (58,7 i 59,1 Hz);
  - przy działającym `gfxd` odmowa;
  - tryb 1024×600 odrzucony.
- `crtos bench` przy 800×480 wobec 480×272 (02.10.2026):
  - `memcpy`/`memset` 256 KB w SDRAM o 12–13% wolniejsze (odczyt ekranu zajmuje ok. 45 MB/s
    zamiast 16 MB/s);
  - składanie klatki w `gfxd` 290–359 µs zamiast 252–281 µs;
  - reszta bez zmian.
- Codzienne działanie interfejsu graficznego (U03, A01, A02); VNC (U08) przy 800×480.
- STM32H7S78-DK: `gpuprims` (prymitywy NeoChrom) i voxel na GPU (S03, 04.10.2026); dla
  `ltdc-stm32` te same polecenia (`kmon fbtest`, `gfxinfo -b`), pomiarów ta dokumentacja nie
  zawiera.

## 12. Ograniczenia i znane problemy

- Tylko RGB565 (16 linii danych: panele 24-bitowe dostają starsze bity kolorów).
- Panelu nie da się rozpoznać elektrycznie. Panel bez dotyku albo z dotykiem, który nie
  zgłasza rozmiaru (FT5406), dostaje tryb `native-mode`; inny panel wymaga wtedy zmiany DT.
- Tryb zmienia się tylko przed startem `gfxd` (S03); na działającym pulpicie nie.
- Przy 800×480 bufory ekranu zajmują 1,5 MB z 2 MB pamięci bez cache, a odczyt ekranu
  ok. 45 MB/s przepustowości SDRAM (pomiar w sekcji 11).
- Ruch PXP i LCDIF na magistrali powoduje przepełnienia FIFO odbiornika Ethernet przy pełnej
  prędkości sieci (D05).
- Pad `LCD_RST` (GPIO_AD_B0_02) jest też pinem J24 pin 2: nie może służyć jako MISO SPI
  (D02, [Sterowniki](../../sterowniki.md#spi-j24)).
- STM32: DT opisuje jeden tryb (800×480); `set_size` na inny rozmiar daje `-ENOENT`. Panel
  24-bitowy dostaje RGB565, jak na EVKB.
- STM32: zegar pikseli (PLL3 R) dzieli PLL z dźwiękiem: zmiana rodziny częstotliwości dźwięku
  restartuje PLL3, ekran gubi ramkę, a zegar pikseli się zmienia (D01, REQ-D01-08).
- STM32: zmiana trybu czeka stałe 25 ms zamiast końca ramki; przy wolniejszym panelu
  (ramka > 25 ms) bufor może zostać zwolniony w trakcie ostatniej ramki.
