# API dla programów

Program CRTOS to zwykły program w C albo C++ z funkcją `main`. Działa jako proces we
własnym obszarze pamięci (arenie), chronionym przez MPU, i nie widzi symboli jądra. Z jądrem
rozmawia wyłącznie przez wywołania systemowe, które biblioteka `libcrtos` opakowuje
w funkcje C i POSIX.

| Nagłówek | Zawartość |
|---|---|
| `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<math.h>`, `<time.h>`, ... | biblioteka C (newlib-nano) |
| `<unistd.h>`, `<fcntl.h>`, `<sys/stat.h>`, `<dirent.h>` | pliki i katalogi (POSIX) |
| `<crtos.h>` | procesy, wątki, blokady, IPC, pamięć współdzielona, informacje o systemie |
| `<pthread.h>` | wątki POSIX (podzbiór) |
| `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<netdb.h>` | sieć |
| `<poll.h>` | czekanie na wiele uchwytów naraz |
| `"gfx.h"`, `"gfx_ui.h"` (`LIBS gfx`) | okna, rysowanie, przyciski, listy, klawiatura ekranowa |

Zasady wspólne:

- Funkcje zwracają `-1` i ustawiają `errno`, jeśli opis nie mówi inaczej.
- Limity czasu podaje się w milisekundach. `CRTOS_FOREVER` oznacza czekanie bez limitu.
- Pełne opisy funkcji są w komentarzach nagłówków: `system/lib/libcrtos/include/`,
  `kernel/include/crtos/`, `system/lib/libgfx/include/`.

## Stos i sterta programu

Jądro tworzy arenę programu przed jego startem, więc musi znać rozmiar stosu głównego wątku
i sterty. Oba są w nagłówku programu (sekcja `.crtos_app`).

W CMake nagłówek ustawiają parametry `crtos_app(... STACK n HEAP n)`. W programie budowanym
samym kompilatorem (`arm-crtos-gcc`) ustawia go makro z `crtos.h`, zapisane raz, w jednym
pliku:

```c
CRTOS_APP(32 * 1024, 1024 * 1024);     /* stos 32 KB, sterta 1 MB */
```

Bez nagłówka program dostaje 16 KB stosu i 64 KB sterty. Gotowy program zmienia
`crtos-app set prog.app --stack 32K --heap 1M`.

## Pliki i katalogi

| Ścieżka | Co to |
|---|---|
| `/sd/` | karta SD (FAT32); pliki systemu w `/sd/crtos/` |
| `/ram/` | dysk w pamięci RAM (znika po restarcie) |
| `/dev/` | urządzenia |

Działają standardowe funkcje C i POSIX: `fopen`/`fread`/`fprintf`/..., `open`/`read`/
`write`/`lseek`/`close`, `stat`/`fstat`, `mkdir`/`rmdir`/`unlink`/`rename`,
`opendir`/`readdir`/`closedir`, `getcwd`/`chdir`, `dup`/`dup2`, `fcntl`, `pipe`, `fsync`.

Urządzenia:

| Plik | Urządzenie |
|---|---|
| `/dev/console` | konsola szeregowa (terminal) |
| `/dev/null`, `/dev/zero` | jak w Uniksie |
| `/dev/random`, `/dev/urandom` | sprzętowy generator liczb losowych |
| `/dev/audio` | wyjście dźwięku: słuchawki J12, głośniki ([Dźwięk](#dźwięk-devaudio)) |
| `/dev/fb0` | ekran (bufor ramki). Zwykle używa go tylko serwer grafiki `gfxd` |
| `/dev/gpu2d` | akcelerator 2D (PXP) |
| `/dev/event0`, `/dev/event1` | zdarzenia wejścia: ekran dotykowy, przyciski (`evtest` je pokazuje) |
| `/dev/uevent` | powiadomienia o urządzeniach (dla `devmgr`) |
| `/dev/ttyACM0` | port szeregowy przez USB (gniazdo J9) |
| `/dev/ttyS3` | port szeregowy na złączu J22 ([Porty szeregowe](sterowniki.md#porty-szeregowe-j22)) |
| `/dev/spidev3.0` | magistrala SPI na złączu J24, jak `spidev` w Linuksie ([SPI](sterowniki.md#spi-j24)) |
| `/dev/mtd0`, `/dev/mtd1` | pamięć flash: jądro (tylko odczyt) i partycja systemu plików `/flash0` ([Pamięć flash](sterowniki.md#pamięć-flash-hyperflash)) |
| `/dev/flashfs0` | system plików `/flash0`: informacje, kontrola, formatowanie (`crtos/flashfs.h`, polecenie `flashfs`) |

Urządzenia poza konsolą, `null`, `zero`, `random` i `audio` wymagają uprawnienia `dev`.

### Dźwięk: `/dev/audio`

Próbki 16-bit ze znakiem (little endian), kanały na przemian. Pisze jeden program naraz
(następne otwarcie do zapisu: `EBUSY`); otwarcie tylko do odczytu daje stan i głośność.
Stałe i struktury są w `crtos/audio.h`.

```c
#include <crtos/audio.h>

int fd = open(AUDIO_DEVICE, O_WRONLY);
ioctl(fd, AUDIO_IOC_SET_RATE, 44100);    /* 8000..48000 Hz */
ioctl(fd, AUDIO_IOC_SET_CHANNELS, 1);    /* mono: na obie strony */
write(fd, samples, n * sizeof(int16_t)); /* czeka na miejsce (O_NONBLOCK: nie) */
ioctl(fd, AUDIO_IOC_DRAIN, 0);           /* czeka na odtworzenie */
close(fd);
```

| `ioctl` | Działanie |
|---|---|
| `AUDIO_IOC_SET_RATE`, `GET_RATE` | częstotliwość (zmiana odrzuca kolejkę) |
| `AUDIO_IOC_SET_CHANNELS` | 1 albo 2 |
| `AUDIO_IOC_GET_QUEUED`, `GET_SPACE` | ramki czekające na odtworzenie / miejsce w buforze (4096 ramek) |
| `AUDIO_IOC_SET_VOLUME`, `GET_VOLUME` | 0..100 |
| `AUDIO_IOC_DRAIN`, `FLUSH` | czekanie na koniec / odrzucenie kolejki |
| `AUDIO_IOC_GET_STATS` | `struct audio_stats`: odtworzone ramki, przerwy w danych, błędy FIFO |

Gdy bufor się opróżni, płytka gra ciszę. Program, który gra ciągle (np. gra), trzyma
w kolejce kilkadziesiąt milisekund (`AUDIO_IOC_GET_QUEUED`) i pisze bez czekania
(`O_NONBLOCK`).

## Czas

| Funkcja | Zwraca |
|---|---|
| `crtos_time_us()` | mikrosekundy od startu płytki (monotoniczny) |
| `crtos_wall_us()` | mikrosekundy od 1970 roku (zegar ścienny, ustawiany z NTP) |
| `time()`, `gettimeofday()`, `clock_gettime()` | jak w POSIX (`CLOCK_MONOTONIC`, `CLOCK_REALTIME`) |
| `crtos_sleep_ms(ms)`, `sleep()`, `usleep()`, `nanosleep()` | czekanie |

## Procesy

```c
char *argv[] = { "ping", "8.8.8.8", NULL };
int pid = crtos_spawnv("/sd/crtos/bin/ping.app", argv);   /* ten sam stdin/stdout */
int status;
crtos_wait(pid, &status, CRTOS_FOREVER);                  /* status = kod wyjścia */
```

- `crtos_spawn(&sp)` to pełna wersja: własne `stdio`, zmienne środowiska, uprawnienia
  (`caps`) i priorytet.
- `crtos_kill(pid, kod)` kończy proces, `getpid()` zwraca własny numer procesu.
- `fork()` i `exec()` nie istnieją. Nowy program zawsze startuje przez `crtos_spawn`.

Uprawnienia (`caps`) dostaje proces przy starcie (z `init.cfg`, `launcher.cfg` albo od
rodzica) i nigdy nie może mieć więcej niż jego rodzic:

| Stała | Pozwala |
|---|---|
| `CAP_SPAWN` | uruchamiać procesy |
| `CAP_KILL` | kończyć procesy, których nie uruchomił |
| `CAP_MODULE` | ładować i usuwać sterowniki |
| `CAP_SYS` | restart, ustawianie zegara, priorytety powyżej zwykłych |
| `CAP_DEV` | otwierać urządzenia w `/dev` |

### Funkcje POSIX

Przenoszone programy (kompilatory, `make`) mogą używać zwykłych funkcji POSIX. libcrtos
realizuje je na `crtos_spawn` i `crtos_wait`:

| Funkcja | W CRTOS |
|---|---|
| `posix_spawn`, `posix_spawnp` | akcje plików (`adddup2`, `addopen`, `addclose`, `addchdir`) dotyczą deskryptorów 0–2 dziecka, bo tylko te dziecko dostaje; `posix_spawnp` szuka w `$PATH` także `nazwa.app` |
| `waitpid`, `wait` | status jak w POSIX: `WIFEXITED`/`WEXITSTATUS`; program zakończony przez jądro to `WIFSIGNALED` (`SIGSEGV` – błąd pamięci, `SIGINT` – Ctrl-C); `WNOHANG` |
| `system`, `popen`, `pclose` | przez `sh -c` (`$SHELL`, domyślnie `/sd/crtos/bin/sh.app`) |
| `tmpfile`, `tmpnam`, `mkstemp` | w `$TMPDIR` (domyślnie `/ram`); plik `tmpfile` znika przy wyjściu z programu |
| `realpath`, `getcwd(NULL, 0)` | ścieżki bez dowiązań symbolicznych (ich nie ma) |
| `sysconf`, `getpagesize`, `getrusage`, `getppid` | `_SC_PAGESIZE` 4096, `_SC_PHYS_PAGES`, czas procesora procesu |
| `pathconf`, `fpathconf` | limity systemów plików CRTOS: `_PC_PATH_MAX` 256, `_PC_NAME_MAX` 255 |
| `utime`, `utimes`, `utimensat` | tylko „teraz” (`NULL`, `UTIME_NOW`); FAT i ramfs nie przyjmują innych czasów |
| `glob`, `fnmatch`, `basename`, `dirname` | wzorce `*` `?` `[…]` |
| `sigaction`, `signal`, `raise` | tylko w obrębie procesu; nikt z zewnątrz nie wysyła sygnałów |
| `chmod`, `umask`, `chown`, `readlink`, `lstat` | bez skutku: FAT i ramfs nie mają trybów, właścicieli ani dowiązań (`lstat` działa jak `stat`) |
| `O_BINARY`, `O_TEXT` (`fcntl.h`) | równe 0: pliki nie mają trybu tekstowego |

Nie ma `fork`, `exec*` ani `mmap`. Zamiast nich służy `posix_spawn`, a pamięć przydziela się
przez `malloc` albo `crtos_shm_create`.

Pliki mają czas ostatniego zapisu (`st_mtime`, UTC, z dokładnością do 2 s na karcie). Jądro
bierze go z zegara ściennego. `statvfs()` (albo `crtos_statfs()`) podaje rozmiar i wolne
miejsce systemu plików.

Pliki na `/flash0` zapisuje się raz, od początku: `open` z `O_CREAT` i `O_TRUNC`, potem
`write`. Dopisywanie (`O_APPEND`, `>>`) i przesuwanie pozycji przy zapisie nie działają
(`EINVAL`, `ESPIPE`).

## Wątki i blokady

Wątki POSIX (podzbiór): `pthread_create`/`join`/`detach`/`self`/`equal`/`exit`, atrybut
rozmiaru stosu, muteksy (zawsze rekurencyjne), zmienne warunkowe (`pthread_cond_*`, także
`timedwait`) i `pthread_once`.

```c
#include <pthread.h>

static void *praca(void *arg)
{
    printf("wątek: %s\n", (const char *)arg);
    return NULL;
}

pthread_t t;
pthread_create(&t, NULL, praca, "cześć");
pthread_join(t, NULL);
```

Własne API CRTOS robi to samo prościej:

- `crtos_thread_start(fn, arg, stack, prio)` i `crtos_thread_join(t)`; domyślny stos
  wątku to 8 KB,
- `crtos_mutex_t m = CRTOS_MUTEX_INIT;` z `crtos_mutex_lock`/`unlock`/`trylock`,
- `crtos_futex_wait`/`crtos_futex_wake` do budowania własnych mechanizmów synchronizacji.

Stosy wątków są w arenie procesu, więc liczą się do jego pamięci (`HEAP`).

## IPC: porty i komunikaty

Procesy rozmawiają przez **porty**. Serwer tworzy port o nazwie, a klient łączy się z nim
po nazwie. Komunikat może nieść **uchwyt**, np. pamięci współdzielonej albo innego portu.

Serwer:

```c
int port = crtos_port_create("zegar");
for (;;) {
    char req[64];
    struct crtos_msginfo info;
    int n = crtos_msg_recv(port, req, sizeof(req), &info, CRTOS_FOREVER);
    if (n < 0)
        continue;
    if (info.token) {                          /* nadawca czeka na odpowiedź */
        uint64_t now = crtos_time_us();
        crtos_msg_reply(info.token, &now, sizeof(now), -1);
    }
}
```

Klient:

```c
int port = crtos_port_connect("zegar", 1000);   /* czeka do 1 s, aż port powstanie */
uint64_t t;
int n = crtos_msg_call(port, "time?", 6, &t, sizeof(t), 1000);   /* pytanie i odpowiedź */
```

- `crtos_msg_send` wysyła komunikat bez czekania na odpowiedź.
- `crtos_msg_call2` pozwala przekazać uchwyt w pytaniu i odebrać uchwyt w odpowiedzi.
- Tak rozmawiają usługi systemowe, np. `gfxd` (`system/lib/libgfx/include/gfx_proto.h`) i `devmgr`
  (`system/lib/libcrtos/include/devmgr_proto.h`).

## Pamięć współdzielona

```c
int h = crtos_shm_create(64 * 1024, 0);    /* SHM_NOCACHE: bez pamięci podręcznej (dla DMA) */
void *p = crtos_shm_map(h);
/* ... przekaż uchwyt h innemu procesowi w komunikacie; on też woła crtos_shm_map() ... */
crtos_shm_unmap(p);
close(h);
```

Proces ma trzy okna pamięci współdzielonej (regiony MPU). Obiekt ma do 32 MB, a jego
rozmiar jest zaokrąglany w górę do podregionu MPU. Obiekt, którego nie da się opisać jednym
regionem (ponad 16 MB albo brak miejsca na taki), jądro składa z kilku regionów (do
trzech) i zajmuje on wtedy kilka okien. `crtos_shm_map` zwraca `NULL` z `ENOSPC`, gdy
proces nie ma ich tyle wolnych.

### Pamięć emulowana (plik wymiany)

Gdy SDRAM nie wystarcza, program może wziąć pamięć z pliku wymiany na karcie
(`/sd/crtos/var/swap`, 64 MB). Procesor nie ma MMU, więc to nie jest pamięć wirtualna:
leży pod adresami od `CRTOS_VMEM_BASE` (0x90000000), gdzie nie ma żadnej pamięci, a każdy
dostęp tam kończy się wyjątkiem, w którym jądro wykonuje instrukcję programu za niego
(odczyt albo zapis w pamięci podręcznej stron 4 KB, w razie potrzeby wczytanie strony
z karty).

```c
char *big = crtos_vmem_map(32u << 20);     /* NULL: brak pliku albo miejsca w nim */
big[1000000] = 1;                           /* zwykły dostęp, ok. 1 us */
struct crtos_vmeminfo vi;
crtos_vmem_info(&vi);                       /* rozmiar, wolne, moje, pamięć podręczna, liczniki */
crtos_vmem_unmap(big);                      /* albo przy wyjściu */
```

- **Szybkość**: ok. 1 us na dostęp do strony w pamięci podręcznej (1 MB z SDRAM, brana przy
  pierwszym `crtos_vmem_map`), ok. 0,5 ms na stronę czytaną z karty. Nadaje się na dane
  używane rzadko albo po kolei, nie na gorące pętle.
- **Instrukcje**: wszystkie `LDR`/`STR` (także `LDRD`/`STRD`, `LDM`/`STM`, `LDREX`/`STREX`),
  `VLDR`/`VSTR`/`VLDM`/`VSTM`. Kod nie może tu leżeć, a stos wątku też nie.
- **Wywołania systemowe**: jądro samo kopiuje dane dla wywołań ze ścieżką albo buforem
  (`open`, `read`, `write`, `stat`, `readdir`...). IPC, `poll`, `ioctl`, futeksy i stosy
  wątków muszą być w zwykłej pamięci.
- Strony nigdy niezapisane są zerami. Pamięć procesu wraca do systemu przy wyjściu.
- `CRTOS_IN_VMEM(p)` mówi, czy wskaźnik leży w pamięci emulowanej. `kmon vmem` pokazuje
  stan pliku, pamięci podręcznej i regionów.

### Sterta ponad arenę: libcrtosheap

Program, któremu nie wystarcza sterta z nagłówka, linkuje `libcrtosheap` (`-lcrtosheap`,
w drzewie `crtos_app(... LIBS crtosheap)`). Zastępuje ona całą rodzinę `malloc`
alokatorem TLSF, który mniej fragmentuje pamięć. Gdy sterta areny się skończy, bierze
obiekt pamięci współdzielonej z prawie całej wolnej pamięci systemu i dokłada go do sterty.
Gdy zabraknie i tego, dokłada pamięć emulowaną (wolniejszą pulę, używaną dopiero wtedy, gdy
szybka nie ma miejsca). Tak działa kompilator na płytce.

| Zmienna środowiska | Znaczenie |
|---|---|
| `CRTOS_HEAP_WINDOWS` | ile okien może wziąć (0–3, domyślnie 3); program z oknami `libgfx` potrzebuje ich na powierzchnie |
| `CRTOS_HEAP_WINDOW_KB` | największe okno w KB (domyślnie tyle, ile system ma wolnej pamięci, najwyżej 30 MB) |
| `CRTOS_HEAP_RESERVE` | ile KB wolnej pamięci zostawia systemowi (domyślnie 1024) |
| `CRTOS_HEAP_SWAP` | ile MB pamięci emulowanej może wziąć (domyślnie cały wolny plik wymiany, 0: wcale) |
| `CRTOS_HEAP_STATS` | raport sterty (szczyt, pule, dostępy emulowane) na `stderr` przy wyjściu i przy braku pamięci |

Program może mieć własne wartości domyślne `CRTOS_HEAP_WINDOWS`, `CRTOS_HEAP_WINDOW_KB`
i `CRTOS_HEAP_SWAP` (zmienna środowiska nadal ma pierwszeństwo). Definiuje je raz, poza
funkcjami:

```c
#include <crtos.h>

const int crtos_heap_windows = 1;      /* jedno okno, dwa zostają dla libgfx */
const int crtos_heap_window_kb = 8192; /* najwyżej 8 MB: zwykle jeden region MPU */
const int crtos_heap_swap = 0;         /* bez pamięci emulowanej */
```

Tak robi przeglądarka NetSurf. Używa TLSF, bo lista wolnych bloków newlib przy tysiącach
drobnych obiektów skryptów zajmowała większość czasu. Gdy strona z dużymi skryptami zapełni
stertę areny, NetSurf bierze jedno okno do 8 MB. Jądro zwykle mieści je w jednym regionie
MPU, więc pozostałe okna zostają na powierzchnię okna i schowek.

Pamięci nie oddaje przed końcem programu. `__crtos_heap_check()` sprawdza spójność sterty
(test: `heaptest`). Program, który zwalnia blok drugi raz albo wskaźnik spoza sterty, kończy
się przez `abort()` (kod 134) z komunikatem:

```
heap: bad pointer 0x802c3ab8 freed or reallocated, called from 0x60280fd5 (arena 0x802c0000)
```

`called from` to adres powrotu z `free`/`realloc`, czyli miejsce tuż za wywołaniem. Linię
kodu pokazuje `python tools/appsym.py build/.../X.debug.app ARENA ADRES`. W programie XIP
w miejsce areny podaje się adres kodu z `dmesg` (`code in place at ...`).

### Kod generowany w programie (JIT)

Arena programu pozwala na wykonywanie kodu, więc program może sam tworzyć instrukcje
(np. kompilator JIT emulatora). Po zapisaniu kodu, a przed jego wykonaniem, trzeba wywołać
`crtos_cache_sync(adres, długość)` (najwyżej 1 MB naraz). Funkcja zapisuje obszar z D-cache
do pamięci i unieważnia go w I-cache. Adres jest liczbą parzystą (Thumb), a skok do kodu
ustawia najmłodszy bit adresu na 1.

## `poll`

`poll()` czeka na wiele uchwytów naraz: pliki, urządzenia, potoki, gniazda sieciowe i porty
IPC. Przykład: program z oknem, który czyta też z gniazda, czeka na
`gfx_event_handle(g)` i na gniazdo jednocześnie. Dostępny jest też `select()`.

## Sieć

Gniazda BSD dla IPv4: TCP i UDP (`socket`, `connect`, `bind`, `listen`, `accept`, `send`,
`recv`, `sendto`, `recvfrom`, `setsockopt`, `getsockopt`, `shutdown`, `close`), nazwy
(`getaddrinfo`, `gethostbyname`) i konwersje adresów (`inet_pton`, `inet_ntop`, ...).

```c
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM }, *ai;
if (getaddrinfo("example.com", "80", &hints, &ai) == 0) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(s, ai->ai_addr, ai->ai_addrlen) == 0) {
        const char req[] = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
        send(s, req, sizeof(req) - 1, 0);
        char buf[512];
        int n = recv(s, buf, sizeof(buf) - 1, 0);
        ...
    }
    close(s);
    freeaddrinfo(ai);
}
```

Więcej przykładów: `system/commands/nc/nc.c` (klient i serwer TCP), `system/commands/ping/ping.c`.

## Informacje o systemie

- `crtos_sys_info(&si)`: czas od startu, obciążenie CPU, pamięć, liczba procesów i wątków,
- `crtos_proc_info(i, &pi)` i `crtos_task_info(i, &ti)`: kolejne procesy i wątki (tak działa
  `ps`),
- `uname()`, `gethostname()`,
- `crtos_reboot()`: restart płytki (uprawnienie `sys`).

## Grafika: `gfx.h`

Ekranem zarządza usługa **gfxd** (serwer grafiki). Program dostaje okno z własnym buforem
pikseli, rysuje w nim, a potem mówi serwerowi, co się zmieniło. Serwer składa okna na ekranie
akceleratorem 2D. Menedżer okien (`wm`) dodaje ramki, pasek zadań i menu.

```c
struct gfx *g = gfx_open();
struct gfx_win *w = gfx_win_create(g, -1, -1, 200, 120, 0, "demo");
gfx_fill(&w->s, 0, 0, 200, 120, GFX_RGB(0, 64, 128));
gfx_text(&w->s, &gfx_sans9, 10, 30, "hello", GFX_WHITE);
gfx_present(w);
```

**Okna**

| Funkcja | Działanie |
|---|---|
| `gfx_open()` / `gfx_close(g)` | połączenie z serwerem |
| `gfx_win_create(g, x, y, w, h, flagi, tytuł)` | nowe okno (`x < 0`: miejsce wybierze menedżer okien) |
| `gfx_present(w)` | pokaż całe okno po zmianach |
| `gfx_damage(w, x, y, w, h)` | pokaż zmieniony prostokąt (szybsze niż całe okno) |
| `gfx_wait_frame(w, timeout)` | czekaj, aż zmiany będą na ekranie (płynne animacje) |
| `gfx_win_resize(w, w, h)` | nowy rozmiar (odpowiedź na `GFX_EV_CONFIGURE`) |
| `gfx_win_move`, `gfx_win_raise`, `gfx_win_show`, `gfx_win_set_title`, `gfx_win_destroy` | przesuwanie, wysuwanie na wierzch, ukrywanie, tytuł, usuwanie |
| `gfx_screen_width(g)`, `gfx_screen_height(g)` | rozmiar ekranu (480×272 albo 800×480, zależnie od panelu) |

Flagi okna:

| Flaga | Znaczenie |
|---|---|
| `GFX_WIN_RESIZABLE` | użytkownik może zmieniać rozmiar (program dostaje `GFX_EV_CONFIGURE`) |
| `GFX_WIN_ALPHA` | piksele ARGB8888 z przezroczystością (domyślnie RGB565) |
| `GFX_WIN_XRGB` | piksele XRGB8888 (32 bity, bez przezroczystości; tego wymaga TFTLIB) |
| `GFX_WIN_NOFRAME` | bez ramki menedżera okien |
| `GFX_WIN_TOPMOST` | zawsze na wierzchu |
| `GFX_WIN_HIDDEN` | utwórz ukryte |
| `GFX_WIN_NOFOCUS` | dotknięcie nie zabiera fokusu klawiatury |
| `GFX_WIN_MULTITOUCH` | okno dostaje wszystkie palce, nie tylko pierwszy (np. przyciski gry trzymane naraz) |
| `GFX_WIN_HOVER` | okno dostaje ruch myszy bez przycisku (`GFX_PTR_HOVER`, `GFX_PTR_LEAVE`): podpowiedzi, podświetlenie pod kursorem |

**Zdarzenia**: `gfx_next_event(g, &ev, timeout)` zwraca 0, gdy przyszło zdarzenie.

| `ev.h.type` | Znaczenie |
|---|---|
| `GFX_EV_POINTER` | dotyk: `ev.kind` = `GFX_PTR_DOWN` / `GFX_PTR_MOVE` / `GFX_PTR_UP`, `ev.x`, `ev.y` względem okna, `ev.code` = numer palca (niżej); `ev.flags & GFX_PTR_MOUSE`: to mysz (lewy przycisk), a nie palec. Okno z `GFX_WIN_HOVER` dostaje też `GFX_PTR_HOVER` (mysz bez przycisku nad oknem, `ev.x`, `ev.y`) i `GFX_PTR_LEAVE` (kursor zszedł z okna) |
| `GFX_EV_WHEEL` | kółko myszy nad oknem: `ev.value` = liczba ząbków (dodatnia w górę), `ev.code` = `GFX_WHEEL_VERTICAL` albo `GFX_WHEEL_HORIZONTAL` (dodatnia w prawo), `ev.x`, `ev.y` = kursor |
| `GFX_EV_KEY` | klawisz: `ev.code` (kod Linuksa, `crtos/keys.h`), `ev.value` (1 wciśnięty, 0 puszczony, 2 powtórzenie) |
| `GFX_EV_CLOSE` | prośba o zamknięcie (krzyżyk) |
| `GFX_EV_CONFIGURE` | nowy rozmiar `ev.w` × `ev.hgt` |
| `GFX_EV_FOCUS` | okno dostało (`value` 1) albo straciło (0) fokus |
| `GFX_EV_FRAME` | ostatnie zmiany są już na ekranie |
| `GFX_EV_STICK` | gałka pada: `ev.code` = `GFX_STICK_LEFT` / `GFX_STICK_RIGHT` / `GFX_STICK_TRIGGERS`, `ev.x`, `ev.y` (niżej) |
| `GFX_EV_CLIP` | w schowku jest nowy tekst (po `gfx_clip_watch`) |
| `GFX_EV_SETTINGS` | użytkownik zmienił wygląd (skalę, czcionkę, kolory); `ui_theme` już za nim idzie, okno trzeba ułożyć i narysować od nowa (niżej: „Wygląd”) |

Kółko trafia do okna pod kursorem (nie do okna z fokusem). Listę `ui_list` przewija
`ui_list_wheel(&lista, ev.value)` (3 wiersze na ząbek). Rozróżnienie myszy i palca pozwala
np. zaznaczać tekst myszą, a przewijać palcem (tak robi `term`).

Ruch myszy bez przycisku (`GFX_PTR_HOVER`) przychodzi tylko do okien, które o niego
poprosiły (`GFX_WIN_HOVER`), więc programy, które traktują każde zdarzenie wskaźnika jak
dotknięcie, działają jak dotąd. `ui_list_pointer` z `GFX_PTR_HOVER` zaznacza wiersz pod
kursorem (tak działa menu programów). Ekran dotykowy nie ma najechania: podpowiedź warto
pokazać także po przytrzymaniu palca (jak pasek zadań `wm`).

**Wielodotyk**: każdy palec na ekranie ma od naciśnięcia do podniesienia numer
(`ev.code`, 0…`GFX_FINGERS − 1`); pierwszy palec na pustym ekranie ma 0. Palec zostaje przy
oknie, na którym go położono. Okno bez `GFX_WIN_MULTITOUCH` dostaje tylko palec 0, więc
program, który nie zna wielodotyku, działa jak dotąd. Okno z tą flagą dostaje też
pozostałe palce i rozróżnia je po `ev.code`:

```c
static uint16_t held[GFX_FINGERS];      /* przyciski pod każdym palcem */

case GFX_EV_POINTER:
    if (ev.code < GFX_FINGERS)
        held[ev.code] = ev.kind == GFX_PTR_UP ? 0 : button_at(ev.x, ev.y);
    break;
```

**Rysowanie** działa na dowolnej powierzchni `struct gfx_surface` (np. `&w->s`). Kolory
podaje się jako `0xAARRGGBB`: makra `GFX_RGB(r, g, b)` i `GFX_ARGB(a, r, g, b)`.

| Funkcja | Rysuje |
|---|---|
| `gfx_fill`, `gfx_rect`, `gfx_round_rect` | prostokąty (wypełnione, obrys, zaokrąglone) |
| `gfx_round_rect_aa(s, x, y, w, h, r, kolor, linia, rogi)` | zaokrąglony prostokąt z wygładzonymi rogami: `linia` 0 – wypełniony, inaczej obrys tej grubości; `rogi` – które zaokrąglić (`GFX_CORNERS_ALL`, `GFX_CORNERS_TOP`, `GFX_CORNER_TL \| GFX_CORNER_BR`...) |
| `gfx_blend_pixel(s, x, y, kolor, pokrycie)` | piksel zmieszany z tłem (`pokrycie` 0–255; na ARGB8888 miesza też przezroczystość) |
| `gfx_pixel`, `gfx_hline`, `gfx_vline`, `gfx_line` | punkty i linie |
| `gfx_circle`, `gfx_fill_circle` | koła |
| `gfx_text(s, font, x, y, tekst, kolor)` | tekst; `y` to linia bazowa. Zwraca x za tekstem |
| `gfx_text_width(font, tekst)`, `gfx_font_ascent(font)` | wymiary tekstu |

Czcionki: `gfx_sans9`, `gfx_sans_bold9`, `gfx_sans12`, `gfx_sans_bold12`, `gfx_mono9`,
`gfx_tiny`, `gfx_dejavu11`, `gfx_dejavu_bold11` i `gfx_dejavu_mono10` (ostre na małym
ekranie). Program, który ma iść za ustawieniami wyglądu, bierze czcionki z `ui_theme`
(niżej). `gfx_font_load(ścieżka)` wczytuje czcionkę z pliku `.fnt` (tworzy go
`tools/fontconv.py`); zły plik daje `NULL`.

**Obrazy i ikony.** `struct gfx_image` to obraz w pamięci (`w`, `h`, piksele `pix`
w formacie 0xAARRGGBB), zwalniany przez `free()`:

| Funkcja | Działanie |
|---|---|
| `gfx_image_load(ścieżka)` | plik netpbm: PAM (`P7`, 8 bitów, szarość, szarość z alfą, RGB, RGBA) albo PPM (`P6`); `NULL` przy błędzie (`errno`) |
| `gfx_image_scale(img, w, h)` | kopia w nowym rozmiarze; każdy piksel to średnia pokrytego obszaru (gładkie zmniejszanie) |
| `gfx_image_draw(s, x, y, img)` | rysuje obraz z przezroczystością (na RGB565, XRGB8888 i ARGB8888, przycięty) |
| `gfx_icon_load(nazwa, rozmiar)` | ikona programu `/sd/crtos/share/icons/nazwa.pam` przeskalowana do rozmiar × rozmiar; `NULL`, gdy jej nie ma |

```c
struct gfx_image *ikona = gfx_icon_load("paint", 20);   /* albo GFX_ICON_DEFAULT */
gfx_image_draw(&w->s, 8, 4, ikona);
free(ikona);
```

Ikony programów tworzy budowanie: `crtos_app(... ICON icon.png)` zamienia kwadratowy PNG
(zwykle 64 × 64) na `share/icons/<program>.pam` (`tools/icon.py`, zob.
[Budowanie](budowanie.md)). Nazwa ikony to nazwa programu, jaką pokazuje `ps` (plik bez
`.app`). Menedżer okien pokazuje ją w menu programów, na pasku zadań i w pasku tytułu
okien programu; program bez ikony dostaje `application`.

**Klawisze.** Klawisze przychodzą do okna, które ma fokus: z klawiatury albo z systemowej
klawiatury ekranowej (usługa `osk`, przycisk na pasku zadań). Dla programu wyglądają tak
samo: zwykłe zdarzenia `GFX_EV_KEY`, a Shift i Ctrl są osobnymi klawiszami. Na tekst
zamienia je `ui_key_char()` z `gfx_ui.h`:

```c
static struct ui_keys keys;             /* pamięta Shift, Ctrl i Caps Lock */
...
if (ev.h.type == GFX_EV_KEY) {
    int c = ui_key_char(&keys, &ev);    /* znak, '\b', '\n', '\t', 27, UI_KEY_UP.. albo -1 */
    if (c >= 0)
        wpisz(c);
}
```

Program może też sam pokazać albo schować klawiaturę ekranową (`osk_proto.h`: port `"osk"`,
`OSK_SHOW`, `OSK_HIDE`, `OSK_TOGGLE`).

Klawisze Windows (`KEY_LEFTMETA`, `KEY_RIGHTMETA`) nie trafiają do programów: dostaje je
menedżer okien (otwierają menu programów). Puszczenie klawisza trafia zawsze tam, gdzie jego
naciśnięcie, także gdy fokus zmienił się w międzyczasie. Menedżer okien może na czas swojego
menu wziąć wszystkie klawisze (`gfx_wm_keys(g, true)`).

**Schowek.** Jeden tekst dla wszystkich programów (do 64 KB) przechowuje `gfxd`:

```c
gfx_clip_set(g, tekst, strlen(tekst));      /* 0 albo -1 (errno) */
static char buf[4096];
int n = gfx_clip_get(g, buf, sizeof(buf));  /* długość całego tekstu, w buf najwyżej size-1 */
gfx_clip_watch(g);                          /* odtąd GFX_EV_CLIP przy każdym nowym tekście */
```

Tekst idzie kawałkami (komunikat mieści 512 bajtów), a czytający sprawdza, czy w trakcie nie
pojawił się nowy. Schowka używają `term` (Ctrl+C/Ctrl+V) i zdalny pulpit (`vncd`), który
wymienia go ze schowkiem komputera.

**Pad.** Przyciski pada (DualSense) przychodzą jako `GFX_EV_KEY` z kodami padów Linuksa
(`BTN_SOUTH` krzyżyk, `BTN_EAST` kółko, `BTN_NORTH` trójkąt, `BTN_WEST` kwadrat, `BTN_TL` /
`BTN_TR` L1 / R1, `BTN_TL2` / `BTN_TR2` L2 / R2, `BTN_SELECT`, `BTN_START`, `BTN_DPAD_*`;
pełna lista w [sterownikach](sterowniki.md)). Gałki przychodzą jako `GFX_EV_STICK`:

| `ev.code` | `ev.x` | `ev.y` |
|---|---|---|
| `GFX_STICK_LEFT` | lewa gałka: −32767 (w lewo) … 32767 (w prawo) | −32767 (w górę) … 32767 (w dół) |
| `GFX_STICK_RIGHT` | prawa gałka, jak lewa | jak lewa |
| `GFX_STICK_TRIGGERS` | L2: 0 (puszczony) … 32767 | R2: 0 … 32767 |

W spoczynku gałka ma 0, ale gałki mają luz: warto przyjąć strefę martwą (np. 15% zakresu).
Pozycja gałki przychodzi najwyżej 50 razy na sekundę, zawsze także ostatnia. Zdarzenia
dostaje tylko okno z fokusem; przy utracie fokusu (`GFX_EV_FOCUS` z `value` 0) program
powinien uznać gałki za puszczone. Lewa gałka wychylona mocno „wciska” też krzyżak
(`BTN_DPAD_*`, dla menu). Program, który używa gałki analogowo, rozpozna takie wciśnięcie
po tym, że pozycja gałki przyszła tuż przed nim.

```c
if (ev.h.type == GFX_EV_STICK && ev.code == GFX_STICK_LEFT) {
    float x = ev.x / 32767.0f, y = ev.y / 32767.0f;    /* -1..1 */
    ...
}
```

**Kontrolki** (`gfx_ui.h`): kontrolka rysuje się w powierzchni i przyjmuje zdarzenia dotyku
w tych samych współrzędnych. Układ okna i rysowanie zostają po stronie programu.

- `ui_theme` to kolory i czcionki motywu (`bg`, `text`, `accent`, `button`, `font`,
  `bold`, ...),
- `ui_button(s, &rect, tekst, wciśnięty)` rysuje przycisk, a `ui_inside(&rect, x, y)`
  sprawdza trafienie,
- `struct ui_list` to przewijana lista (przeciąganie, wybór, kółko: `ui_list_wheel`,
  zaznaczanie myszą przy `GFX_WIN_HOVER`), zobacz `system/apps/files`,
- `struct ui_keyboard` to klawiatura ekranowa, zobacz `system/apps/term`,
- `ui_text_center`, `ui_text_fit` wyśrodkowują tekst albo przycinają go do szerokości
  z „...”.

- `ui_choice(s, &rect, tekst, wybrany, wciśnięty)` to przycisk wyboru (wybrany ma kolor
  akcentu), `ui_panel(s, &rect, kolor)` – tło grupy kontrolek z rogami motywu,
- `ui_switch(s, &rect, włączony, dostępny)` to przełącznik (szerokość: `ui_switch_width()`),
  `ui_slider(s, &rect, wartość, dostępny)` – suwak (`wartość` 0–1000; z pozycji palca
  `ui_slider_value(&rect, x)`); zobacz `system/apps/settings`.

**Wygląd.** Użytkownik ustawia go w Settings (strony Wygląd i Tapeta) albo poleceniem
`appearance`; ustawienia leżą w `/sd/crtos/etc/ui.cfg` (`UI_CFG`, wiersze `klucz = wartość`).
`gfx_open()` wczytuje je i ustawia według nich `ui_theme`:

| Pole `ui_theme` | Co daje ustawienie |
|---|---|
| `font`, `bold`, `heading`, `mono` | czcionki wybranej rodziny w rozmiarze skali (tekst, pogrubiony, nagłówki, stała szerokość) |
| `accent`, `button_pressed` | kolor akcentu |
| `radius` | promień rogów w pikselach (0: rogi proste) |
| `alpha` | krycie powierzchni półprzezroczystych (255: przezroczystość wyłączona) |
| `scale` | skala interfejsu w procentach (100, 125, 150, 175, 200) |

Wymiary dobrane dla 100% przelicza `ui_px(px)` (`ui_px(26)` przy 150% to 39). Program,
który podaje wymiary przez `ui_px` i rysuje czcionkami motywu, wygląda jak reszta systemu
w każdej skali. Gdy użytkownik zmieni wygląd, przychodzi `GFX_EV_SETTINGS`: `ui_theme`
ma już nowe wartości, a program układa okno od nowa (przy innej skali także nowy rozmiar):

```c
case GFX_EV_SETTINGS:                   /* ui_theme i ui_px już według nowych ustawień */
    if (gfx_win_resize(w, ui_px(240), ui_px(150)) == 0)
        layout(w->s.w, w->s.h);
    draw(w);
    break;
```

Do zmiany ustawień: `struct ui_settings` (`scale`, `font`, `accent`, `transparency`,
`opacity`, `rounded`, `wallpaper`, `fit`), `ui_settings_load`, `ui_settings_set(s, klucz,
wartość)` (jak w pliku), `ui_settings_save` i `gfx_settings_changed(g)` (ogłoszenie zmiany:
`gfxd` rysuje tapetę od nowa i wysyła wszystkim programom `GFX_EV_SETTINGS`). Rodziny czcionek
z `/sd/crtos/share/fonts/families.txt` podaje `ui_font_families`, a czcionkę rodziny w bieżącej
skali – `ui_family_font(id)`.

Tapety: `gfx_wallpaper_draw(s, opis, dopasowanie)` rysuje tapetę w dowolnym rozmiarze
powierzchni RGB565 (tak robią `gfxd` i miniatury w Settings). `opis` to nazwa wbudowanej
(`gfx_wallpaper_builtin(i)`: Aurora, Dusk, Ocean, Forest, Graphite, CRTOS), `color:RRGGBB`
albo plik PPM/PAM (zwykle w `GFX_WALLPAPER_DIR`, czyli `/sd/crtos/share/wallpapers`);
dopasowanie obrazu: `GFX_FIT_FILL`, `FIT`, `STRETCH`, `CENTER`, `TILE`.

**Zdalne sterowanie** (dla usług takich jak `vncd`):

| Funkcja | Działanie |
|---|---|
| `gfx_send_input(g, kind, code, x, y, value)` | wejście jak z urządzenia (`GFX_PTR_*`, także `GFX_PTR_HOVER`: mysz bez przycisku; `GFX_KEY`, `GFX_WHEEL`) |
| `gfx_screen_watch(g, shm, stride)` | kopia ekranu (RGB565) w podanej pamięci współdzielonej; wymaga `CAP_SYS`, jeden klient naraz; `gfxd` kopiuje do niej zmiany akceleratorem i wysyła `GFX_EV_SCREEN`. Pamięć może być z pamięcią podręczną (`crtos_shm_create(n, 0)`, szybsze czytanie): akcelerator unieważnia ją przed kopią i po niej |
| `gfx_screen_take(g, prostokąty)` | gdzie ekran zmienił się od ostatniego razu (do 16 prostokątów) |

## TFTLIB

`LIBS tftlib` daje bibliotekę rysowania ze starego firmware'u CRTOS, w stylu Adafruit GFX
i TFT_eSPI (C++: `drawLine`, `fillRoundRect`, czcionki GFXFF, `print`). Rysuje na
powierzchniach XRGB8888: okno tworzy się z flagą `GFX_WIN_XRGB`. Przykład: `apps/tftdemo`.
