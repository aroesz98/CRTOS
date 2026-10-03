# Sterowniki i drzewo urządzeń

Sterowniki CRTOS to **moduły jądra** (`.ko`) ładowane z karty SD. To, jakie urządzenia są
na płytce i jak są podłączone, opisuje **drzewo urządzeń** (device tree), tak samo jak
w Linuksie.

## Drzewo urządzeń

- `dts/imxrt1052.dtsi`: układ i.MX RT1052 (kontrolery, adresy, przerwania, zegary),
- `dts/evkbimxrt1050.dts`: płytka EVKB (piny, ekran, dotyk, przyciski, Ethernet, ...),
- `dts/include/`: stałe (`dt-bindings`, funkcje pinów).

`crtos build` kompiluje je narzędziem `tools/dtc.py` do `/crtos/boot/board.dtb`.
Kompilator obsługuje `#include`/`#define` (przez preprocesor C), etykiety, `&etykieta`,
komórki `< >`, ciągi znaków, tablice bajtów, `/delete-node/` i `/delete-property/`.

Przykład węzła, obsługiwanego przez przykładowy moduł `examples/drivers/hello`:

```dts
/ {
	hello-test {
		compatible = "crtos,hello";
		label = "device tree test node";
	};
};
```

Po starcie `crtos kmon dt` pokazuje wczytane drzewo, `crtos kmon devices` urządzenia i ich
sterowniki, a `crtos kmon lsmod` załadowane moduły.

## Jak jądro dobiera sterownik

1. Każdy węzeł drzewa z właściwością `compatible` staje się urządzeniem.
2. Jeśli żaden załadowany sterownik go nie obsługuje, jądro szuka jego `compatible`
   w `/crtos/drivers/modules.alias` i ładuje wskazany moduł.
3. Sterownik rejestruje się z tabelą obsługiwanych `compatible`, a jądro wywołuje jego
   funkcję `probe` dla każdego pasującego urządzenia.

Plik `modules.alias` powstaje przy budowaniu z parametrów `ALIASES` w
`drivers/CMakeLists.txt`.

## Nowy sterownik

1. Utwórz plik, np. `drivers/misc/mojczujnik.c`:

```c
#include <crtos/device.h>
#include <crtos/module.h>
#include <crtos/of.h>
#include <crtos/printk.h>

static int czujnik_probe(struct device *dev)
{
    uint32_t okres = 1000;
    of_property_read_u32(dev->of_node, "okres-ms", &okres);   /* właściwość z drzewa */
    dev_info(dev, "czujnik gotowy, okres %u ms\n", (unsigned)okres);
    return 0;               /* < 0: błąd (kod errno ze znakiem minus) */
}

static void czujnik_remove(struct device *dev)
{
    dev_info(dev, "czujnik usunięty\n");
}

static const struct of_device_id czujnik_ids[] = {
    { "moja,czujnik", NULL },
    { NULL, NULL },
};

static struct driver czujnik_driver = {
    .name = "mojczujnik",
    .of_match_table = czujnik_ids,
    .probe = czujnik_probe,
    .remove = czujnik_remove,
};

static int czujnik_init(void)
{
    return driver_register(&czujnik_driver);
}

static void czujnik_exit(void)
{
    driver_unregister(&czujnik_driver);
}

MODULE("mojczujnik", "przykładowy czujnik", czujnik_init, czujnik_exit);
```

2. Dopisz go do `drivers/CMakeLists.txt`:

```cmake
crtos_module(mojczujnik SOURCES misc/mojczujnik.c ALIASES moja,czujnik)
```

   Jeśli sterownik używa sterownika z NXP SDK, dodaj np. `SDK_DRIVERS fsl_lpspi.c`.
   Pliki są w repozytoriach MCUXpresso SDK pobranych do `third_party/nxp-sdk` (`core/drivers/<blok>/`
   i `devices-rt/RT1050/MIMXRT1052/drivers/`), a `CRTOS_SDK_DRIVER_DIRS` w `cmake/crtos.cmake`
   wymienia katalogi wersji bloków tego układu (wszystkie są na ścieżce nagłówków modułów).

3. Dodaj węzeł do `dts/evkbimxrt1050.dts`:

```dts
	czujnik {
		compatible = "moja,czujnik";
		okres-ms = <500>;
	};
```

4. Zbuduj, wgraj i zrestartuj:

```
.\crtos build
.\crtos deploy
.\crtos reboot
```

Moduł można też ładować i usuwać bez restartu: `crtos kmon "insmod mojczujnik"` i
`crtos kmon "rmmod mojczujnik"`. W powłoce płytki służą do tego `insmod` i `rmmod`.
`rmmod` odmawia (błąd -16, zajęty), dopóki jakiś proces ma otwarty plik urządzenia modułu
albo inny moduł korzysta z jego symboli.

Jeśli nowy sterownik psuje start systemu, uruchom płytkę z wciśniętym **SW8** (tryb
awaryjny bez modułów), wgraj poprawkę i zrestartuj.

## Co jest dostępne w module

Moduł działa w trybie uprzywilejowanym, w przestrzeni jądra. Może używać tylko tych funkcji
jądra, które są eksportowane: lista `KSYM(...)` w `kernel/os/ksyms.cpp`. Może też używać
symboli innych modułów (`EXPORT_SYMBOL(funkcja)` w module, który je daje). Budowanie to
sprawdza (modcheck).

| Nagłówek | Zawartość |
|---|---|
| `crtos/module.h` | `MODULE`, `MODULE_DEPENDS` (moduły ładowane wcześniej), `EXPORT_SYMBOL` |
| `crtos/device.h` | sterowniki, urządzenia, `devm_kzalloc` (pamięć zwalniana przy odłączeniu), `dev_info`/`dev_err` |
| `crtos/of.h` | odczyt drzewa: `of_property_read_u32/_string/_bool/...` |
| `crtos/irq.h` | przerwania: `irq_request` |
| `crtos/gpio.h` | GPIO: `gpiod_get`, `gpiod_set_value`, `gpiod_to_irq` |
| `crtos/i2c.h` | magistrala I2C: `i2c_transfer` |
| `crtos/spi.h` | magistrala SPI: `spi_device_get`, `spi_sync`, `spi_write_then_read` |
| `crtos/uaccess.h` | wskaźniki od programów, np. wewnątrz struktury `ioctl`: `uaccess_ok`, `copy_from_user`, `copy_to_user`; uprawnienia procesu: `capable(CAP_...)` |
| `crtos/clk.h`, `crtos/pinctrl.h` | zegary i piny |
| `crtos/fb.h`, `crtos/gpu2d.h`, `crtos/input.h`, `crtos/net.h`, `crtos/rtc.h` | frameworki: ekran, akcelerator 2D, wejście, sieć, zegar |
| `crtos/keys.h` | kody klawiszy (wartości Linuksa). Sterownik wejścia deklaruje klawisze, które potrafi wysłać: `input_set_key()`. Urządzenie z klawiszami liter jest traktowane jako klawiatura, a klawiatura ekranowa wtedy się chowa |
| `crtos/vfs.h` | pliki urządzeń w `/dev` (`file_ops`: `read`, `write`, `ioctl`, `poll`) |
| `crtos/sched.h`, `crtos/sync.h` | wątki jądra, uśpienie, muteksy, semafory, zdarzenia |
| `crtos/mm.h` | `kmalloc`/`kfree` (pule: szybka, DMA, duża) |
| `crtos/printk.h` | `printk` do logu jądra |

Dobre przykłady:

| Moduł | Pokazuje |
|---|---|
| `examples/drivers/hello` | minimalny moduł, eksport symbolu, drugi moduł zależny (`hello_dep`) |
| `drivers/input/gpio-keys.c` | GPIO, przerwania, zdarzenia wejścia |
| `drivers/input/ft5406.c` | urządzenie na I2C (dotyk) |
| `drivers/misc/trng-imxrt.c` | sterownik NXP SDK i plik w `/dev` (`/dev/random`) |
| `drivers/display/lcdif-imxrt.c` | ekran: framework `fb` |
| `drivers/net/enet-imxrt.c` | karta sieciowa: framework `net` |
| `drivers/spi/lpspi-imxrt.c` | kontroler magistrali: framework `spi` |
| `drivers/tty/lpuart-imxrt.c` | plik w `/dev` obsługiwany przerwaniami: bufory pierścieniowe, `poll`, `ioctl` |
| `drivers/mtd/flexspi-mtd.c` | kod działający z RAM przy wyłączonych przerwaniach (zapis pamięci flash, z której działa jądro) |

## Moduły w repozytorium

| Moduł | `compatible` | Urządzenie |
|---|---|---|
| `pinctrl-imxrt` | `fsl,imxrt1050-iomuxc` | piny (IOMUXC) |
| `clk-imxrt` | `fsl,imxrt1050-ccm` | zegary (CCM) |
| `gpio-imxrt` | `fsl,imxrt1050-gpio` | porty GPIO i ich przerwania |
| `lpi2c-imxrt` | `fsl,imxrt1050-lpi2c` | magistrala I2C |
| `lpspi-imxrt` | `fsl,imxrt1050-lpspi` | magistrala SPI (`/dev/spidev3.0`); zobacz niżej |
| `lpuart-imxrt` | `fsl,imxrt1050-lpuart` | porty szeregowe poza konsolą (`/dev/ttyS3`); zobacz niżej |
| `flexspi-mtd` | `fsl,imxrt1050-flexspi` | pamięć flash płytki (`/dev/mtd0`, `/dev/mtd1`) i aktualizacja jądra; zobacz niżej |
| `flashfs` | `crtos,flashfs` | system plików `/flash0` na partycji flash (`/dev/flashfs0`); zobacz niżej |
| `lcdif-imxrt` | `fsl,imxrt1050-lcdif` | kontroler ekranu (`/dev/fb0`), tryby paneli 480×272 i 800×480; zobacz niżej |
| `pxp-imxrt` | `fsl,imxrt1050-pxp` | akcelerator 2D (`/dev/gpu2d`) |
| `ft5406`, `gt911` | `focaltech,ft5406`, `goodix,gt911` | ekrany dotykowe (GT911 pod 0x5D albo 0x14 podaje ekranowi rozmiar panelu); zobacz niżej |
| `gpio-keys` | `gpio-keys` | przyciski (SW8) |
| `sai-imxrt` | `fsl,imxrt1050-sai`, `wlf,wm8960` | dźwięk: SAI1 i kodek WM8960 (`/dev/audio`); zobacz niżej |
| `esp32-pad` | `crtos,esp32-pad` | pad DualSense przez mostek Bluetooth na ESP32 (`/dev/eventN`); zobacz niżej |
| `enet-imxrt` | `fsl,imxrt1050-enet` | Ethernet z układem PHY KSZ8081 |
| `net-lwip` | `crtos,lwip` | stos TCP/IP (lwIP) i gniazda |
| `rtc-snvs` | `fsl,imxrt1050-snvs-rtc` | zegar czasu rzeczywistego |
| `usb-imxrt` | `fsl,imxrt1050-usb` | USB na stosie [TinyUSB](https://github.com/hathach/tinyusb) (`third_party/tinyusb`): port szeregowy dla komputera (`/dev/ttyACM0`, J9) i host dla klawiatur, myszy i hubów (J10); zobacz niżej |
| `trng-imxrt` | `fsl,imxrt1050-trng` | generator liczb losowych (`/dev/random`) |
| `hello`, `hello_dep` | `crtos,hello` | przykład |
| `touchpaint` | `crtos,touchpaint` | test ekranu i dotyku bez serwera grafiki |

Wbudowane w jądro (bez modułów): konsola startowa (LPUART1), kontroler kart SD (USDHC)
i system plików FAT.

## Ekran i dotyk

Ekran to panel RGB na 40-pinowym złączu na spodzie płytki (J8), sterowany przez `lcdif-imxrt`
(`/dev/fb0`). Obok, w tej samej linii, jest 6-pinowe złącze taśmy dotyku.

| Panel | Rozdzielczość | Dotyk | Adres I2C dotyku |
|---|---|---|---|
| RK043FN66HS-CTG (dołączany do EVKB) | 480×272 | GT911 | 0x5D |
| RK043FN02H-CT (starsze EVKB) | 480×272 | FT5406 | 0x38 |
| 4,3" IPS z modułu Waveshare 16249 (SH430HI50) | 800×480 | GT911 | 0x14 |

**Wybór trybu.** Węzeł `panel` w `dts/evkbimxrt1050.dts` ma w `display-timings` jeden tryb na
panel. Ekran startuje w trybie `native-mode` (800×480). Sterownik GT911 po odczycie swojej
konfiguracji podaje ekranowi rozmiar panelu (`fb_suggest_size`), a ekran przełącza się na ten
tryb, zanim wystartuje serwer grafiki. W logu widać to tak:

```
display-controller@402b8000: 800x480 RGB565, pixel clock 27000000 Hz, 59.172 Hz refresh
touchscreen@5d: GT911 touch, config v65, 480x272 -> 480x272, 5 points, interrupt
fb0: now 480x272 16 bpp, 2 buffer(s) at 81e00480
```

Panel bez dotyku albo z dotykiem FT5406 (który rozmiaru nie zna) dostaje `native-mode`.
`crtos kmon "fb"` pokazuje bieżący tryb. `fb 480x272` zmienia go tylko wtedy, gdy żaden
program nie używa ekranu (przy działającym pulpicie odpowiada „in use”).

**Nowy panel:** dopisz tryb w `display-timings` z czasami z noty panelu (`clock-frequency`,
`hactive`, `vactive`, przedsionki, synchronizacje, polaryzacje). Jeśli jego dotyk nie poda
rozmiaru, ustaw na ten tryb `native-mode`. Panel 24-bitowy dostaje kolory 16-bitowe (RGB565).

**Koszt 800×480:** bufory zajmują 1,5 MB z 2 MB pamięci bez cache, a odczyt ekranu ok. 45 MB/s
przepustowości SDRAM. Kopiowanie dużych bloków w programach jest przez to o ok. 12% wolniejsze
niż przy 480×272.

**Złącze dotyku** (B1 to koniec bliższy złącza ekranu):

| Styk | Sygnał | Pin procesora | To samo na listwach |
|---|---|---|---|
| B1 | VDD 3,3 V (`LCD_3V3`) | – | J25 pin 4 (3V3) |
| B2 | RESET (`LCD_RST`) | GPIO_AD_B0_02 | J24 pin 2 |
| B3 | INT | GPIO_AD_B0_11 | J22 pin 3 |
| B4 | SCL (LPI2C1) | GPIO_AD_B1_00 | J23 pin 6 |
| B5 | SDA (LPI2C1) | GPIO_AD_B1_01 | J23 pin 5 |
| B6 | GND | – | J25 pin 6 lub 7 |

Taśma dotyku modułu Waveshare 16249 ma 8 styków: 1 GND, 2 RST, 3 VDD, 4 GND, 5 INT, 6 SDA,
7 SCL, 8 GND (złącze H2 na schemacie modułu). Do gniazda płytki nie pasuje, więc łączy się ją
przez przejściówkę FPC i przewody. Styk 1 rozpoznasz miernikiem: 1, 4 i 8 są ze sobą zwarte.

GT911 wybiera adres stanem INT w chwili zwolnienia resetu (INT niski: 0x5D, wysoki: 0x14), a ten
zależy od podciągnięcia na panelu; dlatego drzewo urządzeń ma dwa węzły GT911. Dotyk dzieli
magistralę I2C z kodekiem dźwięku: źle podłączony albo niezasilony kontroler (np. zamienione
SCL i SDA) trzyma SCL i wtedy nie wykrywa się też kodek (`audio-codec@1a: no answer at 0x1a`).
Po poprawieniu połączeń zrestartuj płytkę.

## USB (J9, J10): urządzenie i host

Płytka ma dwa gniazda USB z osobnymi kontrolerami i oba działają naraz:

| Gniazdo | Kontroler | Węzeł DT | Rola domyślnie |
|---|---|---|---|
| **J9** | USB1 | `&usb1` | urządzenie: port szeregowy dla komputera |
| **J10** | USB2 | `&usb2` | host: klawiatury, myszy, huby |

Klawiaturę i mysz podłącza się do **J10** przez przejściówkę OTG (micro-USB → gniazdo A),
także przez hub. Płytka sama podaje 5 V na J10. Po podłączeniu `dmesg` pokazuje np.
`usb: device 1: keyboard as /dev/event3`; klawiatura chowa klawiaturę ekranową, a mysz
dostaje kursor na ekranie. Lewy przycisk działa jak dotknięcie (programy wiedzą, że to mysz:
w terminalu przeciąganie zaznacza tekst), kółko przewija okno pod kursorem (menu programów,
listy, terminal, NetSurf), a prawy i środkowy przycisk nic nie robią. Klawisz Windows otwiera
menu programów.

Protokół boot, którego używają klawiatury, nie ma kółka myszy. Mysz, której deskryptor raportu
opisuje kółko, sterownik przełącza więc na protokół raportów i czyta jej raporty według
deskryptora (identyfikatory raportów, położenie i rozmiar pól: przyciski, X/Y, kółko, poziome
kółko „AC Pan”); `dmesg` pokazuje wtedy `mouse as /dev/eventN (wheel: report protocol)`. Mysz
bez kółka w deskryptorze zostaje przy protokole boot.

Rolę każdego gniazda wybiera właściwość `dr_mode` jego węzła w `dts/evkbimxrt1050.dts`.
Jeden kontroler może być urządzeniem, drugi hostem; dwóch o tej samej roli sterownik nie
uruchomi (TinyUSB ma jeden stos każdej roli):

| `dr_mode` | Co robi płytka |
|---|---|
| `"peripheral"` (domyślnie) | Jest dla komputera portem szeregowym: Windows pokazuje „Urządzenie szeregowe USB (COMx)”, Linux `/dev/ttyACM0`, macOS `/dev/cu.usbmodem…`. Na płytce to `/dev/ttyACM0`, na którym usługa `getty` uruchamia powłokę ([Konsola przez USB](debugowanie.md#konsola-przez-usb-j9)). |
| `"host"` | Obsługuje klawiatury (HID, protokół boot), myszy (protokół boot albo raportów, z kółkiem) oraz huby podłączone przez przejściówkę OTG. Klawiatura chowa klawiaturę ekranową, mysz ma kursor. Na J9 wymaga zamiany ról obu gniazd (J10 wtedy `"peripheral"` albo `status = "disabled"`). |

Zmiana roli: popraw `dr_mode`, potem `crtos build`, `crtos deploy` i `crtos reboot`.

Płytka przedstawia się jako VID 0x1209, PID 0x0001. To numer testowy
[pid.codes](https://pid.codes) do użytku prywatnego, nie do produktów. Numer seryjny urządzenia
to unikalny identyfikator procesora.

## SPI (J24)

Magistrala SPI to kontroler LPSPI3. Programy używają jej przez plik `/dev/spidev3.0`
(magistrala 3, chip select 0), tak jak `spidev` w Linuksie.

| Sygnał | Pin |
|---|---|
| SCK (zegar) | J24 pin 10 |
| SDO, MOSI (dane z płytki) | J24 pin 9 |
| PCS0 (chip select, aktywny stanem niskim) | J24 pin 1 |
| masa | J24 pin 7 |

Magistrala tylko nadaje: odbioru (MISO, SDI) nie ma. Jedyny wolny pad wejścia LPSPI3,
GPIO_AD_B1_13 (złącze kamery J35 pin 3), to dane kodeka dźwięku. Do czytania z urządzenia
wyłącz dźwięk w drzewie urządzeń (`&sai1 { status = "disabled"; };`) i dopisz
`MXRT1050_IOMUXC_GPIO_AD_B1_13_LPSPI3_SDI 0x10b0` do grupy `pinctrl_lpspi3`.

Sygnały mają poziom 3,3 V. Zegar może mieć do 32 MHz, domyślnie 1 MHz (`spi-max-frequency`
w drzewie urządzeń).

Te piny mają na płytce także inne zadania:

- **J24 pin 2**, w przykładach NXP wejście MISO, to `LCD_RST`: reset kontrolera dotyku
  panelu. Nic do niego nie podłączaj.
- **J24 piny 9 i 10**: powyższa tabela idzie za etykietami pinów z SDK NXP. Schemat EVKB B1
  (SPF-30168, strona 12) podaje na nich SDA i SCL magistrali I2C1, tej samej co kodek dźwięku
  i dotyk (przez rezystory 0 Ω R276/R277). Przed podłączeniem urządzenia SPI sprawdź
  miernikiem, czy J24 pin 10 nie jest zwarty z J23 pin 6 (SCL).
- J24 pin 9 to także pin ID gniazda USB J9. Nie używaj SPI, gdy w J9 jest przejściówka OTG
  (tryb host), bo zwiera ten pin do masy. Poza komunikatami sterownik tego pinu nie steruje.
- J24 piny 10 i 1 to także sygnały przeciążenia zasilania USB.
- Typowe piny SPI złącza Arduino (J24 3–6) to na tej płytce linie karty SD.

Program:

```c
#include <crtos/spi.h>

int fd = open("/dev/spidev3.0", O_RDWR);
uint32_t mode = SPI_MODE_0, hz = 8000000;
ioctl(fd, SPI_IOC_WR_MODE32, &mode);
ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &hz);

uint8_t cmd = 0x9f, id[3];
struct spi_ioc_transfer t[2] = {
    { .tx_buf = (uintptr_t)&cmd, .len = 1 },    /* polecenie */
    { .rx_buf = (uintptr_t)id, .len = 3 },      /* odpowiedź, chip select nadal aktywny */
};
ioctl(fd, SPI_IOC_MESSAGE(2), t);               /* identyfikator pamięci SPI flash */
```

- Komunikat (`SPI_IOC_MESSAGE(n)`) ma najwyżej 16 KB. Chip select jest aktywny od pierwszej do
  ostatniej części, chyba że część ma `cs_change` (po niej chip select się zwalnia) albo
  `delay_usecs`.
- `read()` i `write()` to komunikaty z jednej części: tylko odbiór albo tylko nadawanie.
- Słowa mają 8 bitów. Pętli wewnątrz układu (`SPI_LOOP`) LPSPI nie potrafi.
- Ustawienia (`SPI_IOC_WR_...`) obowiązują do następnej zmiany, także po zamknięciu pliku.
- Otwarcie wymaga uprawnienia `dev`.

Program `spi` wysyła jeden komunikat i pokazuje odpowiedź:

```
spi 9f 00 00 00           4 bajty naraz w obie strony
spi -c -r 3 9f            polecenie 9f, potem odczyt 3 bajtów przy aktywnym chip select
spi -s 20000000 -m 3 ...  zegar 20 MHz, tryb 3
spi -l 55 aa              sprawdzenie: J24 pin 9 połączony przewodem z J35 pin 3
```

**Urządzenie z własnym sterownikiem.** W `dts/evkbimxrt1050.dts` zamień węzeł `spidev@0` na
węzeł urządzenia (płytka wyprowadza tylko chip select 0):

```dts
&lpspi3 {
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_lpspi3>;
	status = "okay";

	czujnik@0 {
		compatible = "moja,czujnik-spi";
		reg = <0>;                      /* chip select */
		spi-max-frequency = <4000000>;
		spi-cpol;                       /* razem z spi-cpha: tryb 3 */
		spi-cpha;
	};
};
```

Sterownik dostaje w `probe` urządzenie SPI:

```c
#include <crtos/spi.h>

static int czujnik_probe(struct device *dev)
{
    struct spi_device *spi = spi_device_get(dev);
    uint8_t cmd = 0x8f, id;                             /* odczyt rejestru 0x0f */
    int r = spi_write_then_read(spi, &cmd, 1, &id, 1);
    if (r < 0)
        return r;
    dev_info(dev, "czujnik %02x\n", id);
    return 0;
}
```

Komunikaty na jednej magistrali idą po kolei, także gdy korzystają z niej naraz sterowniki
i programy.

## Porty szeregowe (J22)

Konsola (LPUART1, gniazdo J28) jest wbudowana w jądro. Pozostałe porty LPUART obsługuje
moduł `lpuart-imxrt` jako `/dev/ttyS<n>`, gdzie `n` to numer z aliasu `serial<n>` w drzewie
urządzeń. Na płytce włączony jest jeden:

| Port | Nadawanie (TX) | Odbiór (RX) | Masa |
|---|---|---|---|
| `/dev/ttyS3` (LPUART3) | J22 pin 2 | J22 pin 1 | J24 pin 7 |

Sygnały mają poziom 3,3 V (nie RS-232). Prędkość ustawia `current-speed` w drzewie
(domyślnie 115200 bodów, 8N1). Na EVKB port ma 460800 bodów, bo słucha go sterownik pada
(zobacz niżej). Te piny to także linie synchronizacji złącza kamery J35.

Port przenosi surowe bajty: bez echa i edycji linii. `read()` czeka na co najmniej jeden bajt
(z `O_NONBLOCK` zwraca `EAGAIN`), `write()` czeka na miejsce w buforze, działa `poll()`.
Bufory mają po 2 KB w każdą stronę.

| `ioctl` (`crtos/tty.h`) | Działanie |
|---|---|
| `TTY_IOC_GET_SPEED`, `&baud` | odczyt prędkości |
| `TTY_IOC_SET_SPEED`, `baud` | nowa prędkość (zostaje do restartu) |
| `TTY_IOC_SET_LOOPBACK`, `0/1` | pętla wewnątrz układu: wysłane bajty wracają, piny nie są używane |

Program `uart` wysyła tekst i pokazuje, co wraca:

```
uart hello              wysyła "hello" (z CR LF) i pokazuje odpowiedź
uart -b 9600 -t 2000    tylko słucha przez 2 s, 9600 bodów
uart -l test            sprawdzenie bez przewodów (pętla wewnętrzna)
uart -x -t 1000         przez 1 s pokazuje odebrane bajty szesnastkowo
```

Gdy działa sterownik pada, port należy do niego i `uart` nie zobaczy danych (albo zabierze
sterownikowi część bajtów). Przed testami portu: `rmmod esp32-pad`.

## Pad DualSense (mostek ESP32)

Pad Sony DualSense albo DualSense Edge łączy się przez Bluetooth z modułem ESP32 z
oprogramowaniem z katalogu `esp32-s3-bt-mod/` (aplikacja Zephyr; opis i budowanie w jego
`README.md`). ESP32 wysyła stan pada przez UART do płytki:

| ESP32 | EVKB |
|---|---|
| GPIO17 (TX UART2) | J22 pin 1 (RX LPUART3, `/dev/ttyS3`) |
| GND | masa, np. J24 pin 7 |

Sterownik `esp32-pad` czyta port i tworzy urządzenie wejścia `DualSense (ESP32 link)`.
Przyciski dostają kody padów z Linuksa (`kernel/include/crtos/keys.h`): krzyżyk
`BTN_SOUTH`, kółko `BTN_EAST`, trójkąt `BTN_NORTH`, kwadrat `BTN_WEST`, L1/R1 `BTN_TL`/`BTN_TR`,
L2/R2 `BTN_TL2`/`BTN_TR2`, Create `BTN_SELECT`, Options `BTN_START`, PS `BTN_MODE`, krzyżak
`BTN_DPAD_*`, a lewa gałka wychylona do końca działa jak krzyżak. Gałki i spusty są też
osiami: lewa gałka `ABS_X`/`ABS_Y`, prawa `ABS_RX`/`ABS_RY` (−128…127, 0 w spoczynku,
w prawo i w dół dodatnio), L2/R2 `ABS_Z`/`ABS_RZ` (0…255). `inputd` przekazuje przyciski
oknu z fokusem tak jak klawisze klawiatury (`GFX_EV_KEY` z tymi kodami), a gałki jako
`GFX_EV_STICK` ([API](api.md)); gra `voxel` używa ich do chodzenia i rozglądania się.

W drzewie urządzeń:

```dts
&lpuart3 {
	current-speed = <460800>;
};

/ {
	gamepad {
		compatible = "crtos,esp32-pad";
		port = "/dev/ttyS3";		/* domyślnie */
		current-speed = <460800>;	/* domyślnie */
	};
};
```

Sprawdzenie: `dmesg` pokazuje `gamepad: listening on /dev/ttyS3 at 460800 baud`, a po
pierwszej poprawnej ramce z ESP32 `gamepad: pad link up`. Gdy ESP32 milknie na 700 ms
(odłączony, wyłączony), sterownik puszcza wszystkie przyciski i pisze `pad link quiet`.
Przyciski i gałki na żywo: `evtest /dev/event1` albo `crtos kmon "evtest event1 5"`
(numer urządzenia jest w `dmesg`). Pierwsze
parowanie: przy wyłączonym padzie przytrzymaj Create + PS, aż pasek świetlny zacznie
mrugać; potem pad łączy się z ESP32 samym przyciskiem PS.

## Dźwięk (WM8960)

Na płytce jest kodek audio WM8960: gniazdo słuchawek J12 i wyjścia głośnikowe. Moduł
`sai-imxrt` łączy go z procesorem: kodek ustawia przez I2C (LPI2C1, adres 0x1a), a próbki
wysyła przez SAI1 (I2S, 16 bitów, stereo). Programy piszą próbki do `/dev/audio` (bez
uprawnienia `dev`; [API](api.md#dźwięk-devaudio)).

| Sygnał | Pad |
|---|---|
| MCLK (12,288 / 11,2896 MHz) | GPIO_AD_B1_09 |
| BCLK, LRCLK | GPIO_AD_B1_14, GPIO_AD_B1_15 |
| dane do kodeka | GPIO_AD_B1_13 (to także jedyne wejście SPI, zobacz [SPI](#spi-j24)) |

Częstotliwości: 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100 i 48000 Hz. Zegar
MCLK daje PLL audio (moduł `clk-imxrt`, korzeń `IMXRT1050_CLK_SAI1_ROOT`).

W drzewie urządzeń:

```dts
&lpi2c1 {
	wm8960: audio-codec@1a {
		compatible = "wlf,wm8960";
		reg = <0x1a>;
	};
};

&sai1 {
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_sai1>;
	audio-codec = <&wm8960>;
	status = "okay";
};
```

Program `audio`:

```
audio                   częstotliwość, głośność, odtworzone ramki, przerwy, błędy FIFO
audio -v 60             głośność 0..100 (słuchawki i głośniki; zostaje do restartu)
audio tone 440 1000     ton 440 Hz przez 1 s
audio play /sd/a.wav    plik WAV: 16-bit PCM, mono albo stereo
```

`dmesg` po starcie: `audio-codec@1a: WM8960 at 0x1a` i `sai@40384000: /dev/audio`. Liczniki
przerw i błędów FIFO w `audio` pokazują, czy dźwięk szedł bez przerw.

## Pamięć flash (HyperFlash)

Pamięć flash płytki (64 MB) obsługuje moduł `flexspi-mtd`. Każda partycja z drzewa urządzeń
to osobny plik:

| Plik | Partycja | Rozmiar | |
|---|---|---|---|
| `/dev/mtd0` | `kernel` | 2 MB | jądro; tylko do odczytu |
| `/dev/mtd1` | `flash0` | 62 MB | system plików `/flash0` (surowy zapis zablokowany, gdy działa `flashfs`) |

Jak w Linuksie (`mtd-user`): `read()` i `write()` od bieżącej pozycji (`lseek`), `ioctl`
`MEMGETINFO` (rozmiar, blok kasowania) i `MEMERASE` (kasowanie całych bloków po 256 KB).
Zapis zmienia tylko bity 1 na 0, więc miejsce trzeba wcześniej skasować. Otwarcie wymaga
uprawnienia `dev`.

Jądro działa wprost z tej pamięci, a w czasie kasowania albo zapisu nie da się z niej
czytać. Dlatego **cały system stoi**: przerwania są wyłączone przez ok. 0,8 s na każdy
kasowany blok i ok. 0,8 ms na każde zapisywane 512 bajtów (256 KB zapisuje się w 0,4 s).
Ekran nadal wyświetla obraz, ale sieć i port szeregowy mogą w tym czasie gubić dane (TCP
wysyła je ponownie). Zegar systemowy nadrabia potem stracone tyknięcia.

Program `mtd`:

```
mtd info                        partycje
mtd dump mtd1 0x1000 64         zawartość w szesnastkowym
mtd erase mtd1 0 0x40000        kasuje pierwszy blok partycji
mtd write mtd1 0 /sd/dane.bin   plik do pamięci
mtd read mtd1 0 4096 /ram/a.bin pamięć do pliku
mtd test mtd1 0x100000          kasuje blok, zapisuje wzór, sprawdza i przywraca dane bloku
mtd kernel /sd/crtos/boot/crtos.bin   nowe jądro (zobacz niżej)
```

### Dysk /flash0

Moduł `flashfs` robi z partycji `flash0` system plików zamontowany w `/flash0`. Zmieści się
tam 61,5 MB programów i danych. Programy z `/flash0` mogą wykonywać kod wprost z flasha, bez
kopiowania do SDRAM (programy w formacie XIP, [Toolchain](toolchain.md)). Zwykłe programy
ładują się do RAM tak jak z karty, tylko szybciej.

```
flashfs info                  stan, wolne miejsce, pliki
flashfs ls /bin               pliki z ich miejscem we flash i sumą CRC-32
flashfs check                 sprawdza dane każdego pliku z sumą zapisaną razem z nim
flashfs format yes            nowy, pusty system plików (wszystkie pliki znikają)
df                            (w sh) miejsce na /sd, /flash0 i /ram
```

Pliki kopiuje się zwykłym `cp` albo `crtos deploy` z katalogu `build/flash0` (program z
`crtos_app(... DEST flash0:bin)` trafia do `/flash0/bin`, które jest na początku `PATH`).

Czego się spodziewać:
- **Każdy plik zajmuje całe bloki po 256 KB.** Mały plik też zabiera 256 KB.
- **Plik zapisuje się raz, od początku.** Nowa wersja zastępuje starą dopiero po zamknięciu.
  Dopisywanie (`>>`) nie działa.
- **Zapis jest powolny i zatrzymuje cały system.** Programowanie trwa ok. 600 KB/s, a
  skasowanie każdego używanego wcześniej bloku zatrzymuje system na ok. 0,5 s.
- **Przerwany zapis nie psuje dysku.** Reset albo brak zasilania w trakcie oznacza, że nowego
  pliku po prostu nie ma, a reszta zostaje.
- **Uruchomiony program jest bezpieczny.** Usunięcie albo podmiana pliku działającego
  programu zwalnia jego miejsce dopiero, gdy program się skończy.
- **Sonda nie niszczy dysku.** `crtos flash` kasuje tylko sektory jądra.

### Aktualizacja jądra bez sondy

`crtos flash --net` wysyła `build/kernel/crtos.bin` przez sieć do usługi `deployd` (plik
trafia na kartę jako `/sd/crtos/boot/crtos.bin`), a ta wgrywa jądro przez `/dev/mtd0`
i restartuje płytkę. Całość trwa kilka sekund. Na płytce to samo robi `mtd kernel PLIK`
(uprawnienie `sys`).

Jądro najpierw sprawdza obraz: nagłówek startowy procesora (FCFB i IVT) i sumę CRC-32
przesłanego pliku. Zły plik zostaje odrzucony, a pamięć flash pozostaje nietknięta. Potem
system zatrzymuje się na dobre: jądro kasuje, zapisuje i porównuje nowe jądro (do trzech
prób) z kodu w RAM i restartuje płytkę.

Jeśli w tym czasie zabraknie zasilania, płytka nie wystartuje (tryb awaryjny SW8 nie pomoże,
bo uszkodzone jest samo jądro). Wtedy `crtos flash` przez sondę USB (J28) wgrywa jądro od
nowa.
