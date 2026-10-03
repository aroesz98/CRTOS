# RTOS bez systemu operacyjnego

Jądro CRTOS ma dwie części. **Rdzeń** (`kernel/rtos`) to kompletny RTOS: zadania
z priorytetami, muteksy, semafory, flagi zdarzeń, kolejki komunikatów, timery programowe,
przerwania, sterta, konsola i monitor jądra. **Część systemu operacyjnego** (`kernel/os`)
dokłada procesy, pliki, sterowniki ładowane z karty i start z karty SD.

Sam rdzeń można zbudować z jedną aplikacją wkompilowaną w obraz flash i używać płytki jak ze
zwykłym RTOS-em (np. FreeRTOS): bez karty SD, bez modułów `.ko`, bez procesów i sieci.

## Pierwsza aplikacja

Przykład `examples/rtos/blinky` miga diodą USER LED, mierzy regularność timera i wypisuje
raport co sekundę:

```
.\crtos build --rtos                     buduje build/rtos/kernel/crtos.bin (ok. 80 KB)
.\crtos flash --rtos                     wgrywa go sondą (system na płytce zostaje zastąpiony)
.\crtos kmon dmesg                       komunikaty aplikacji
.\crtos kmon ps                          jej zadania
```

Powrót do systemu: `.\crtos flash` (sondą; obraz RTOS nie ma sieci). Karta SD i dysk `/flash0`
nie są przy tym zmieniane.

Własna aplikacja to katalog z plikiem `CMakeLists.txt`:

```cmake
crtos_rtos_app(SOURCES main.c [inne.c ...] [SDK_DRIVERS fsl_lpi2c.c ...])
```

i funkcją `app_main()`:

```c
#include <crtos/rtos.h>
#include "fsl_gpio.h"

static void worker(void *arg)
{
    for (;;) {
        printk("tick\n");
        task_sleep_ms(1000);
    }
}

void app_main(void)
{
    kthread_create("worker", worker, NULL, PRIO_NORMAL, 2048);
}
```

Budowanie: `.\crtos build --rtos ścieżka\do\katalogu`. Jądro wywołuje `app_main()` w zadaniu
`app` (priorytet 10, stos 8 KB), gdy scheduler już działa. Funkcja może się skończyć – zadania,
które uruchomiła, działają dalej.

## API (`crtos/rtos.h`)

| Co | Funkcje | Nagłówek |
|---|---|---|
| zadania | `kthread_create(nazwa, fn, arg, priorytet, stos)`, `task_sleep_ms`, `task_yield`, `task_exit`, `task_current`, `task_set_priority` | `crtos/sched.h` |
| czas | `tick_get()` (ms), `time_us()` | `crtos/sched.h` |
| muteksy | `mutex_init`, `mutex_lock(m, timeout)`, `mutex_unlock` (dziedziczenie priorytetu); `MUTEX_INIT(m)` | `crtos/sync.h` |
| semafory | `sem_init(s, start, max)`, `sem_take(s, timeout)`, `sem_give` (także z przerwania) | `crtos/sync.h` |
| flagi zdarzeń | `event_init`, `event_set`, `event_clear`, `event_wait(e, bity, EVENT_ANY/ALL/CLEAR, timeout)` | `crtos/sync.h` |
| kolejki | `queue_init(q, bufor, rozmiar, liczba)`, `queue_create`, `queue_send(q, element, timeout)`, `queue_recv`, `queue_count`, `queue_delete` | `crtos/queue.h` |
| timery | `timer_init(t, fn, arg)`, `timer_start(t, za_ms, okres_ms)`, `timer_stop`, `timer_active` | `crtos/timer.h` |
| przerwania | `irq_request(numer, obsługa, ctx, priorytet, nazwa)`, `irq_enable`, `irq_disable`, `irq_free`; `irq_lock`/`irq_unlock` | `crtos/irq.h`, `crtos/arch.h` |
| pamięć | `kmalloc(rozmiar, KM_ANY)`, `kzalloc`, `kfree` | `crtos/mm.h` |
| tekst | `printk` (konsola UART i `kmon dmesg`) | `crtos/printk.h` |

Czas oczekiwania podaje się w milisekundach; `WAIT_FOREVER` czeka bez końca, `NO_WAIT` wcale.
Funkcje czekające zwracają 0 albo `-ETIMEDOUT` (i `-EINTR`, gdy zadanie jest kończone).

- **Kolejka** kopiuje elementy o stałym rozmiarze. Z obsługi przerwania można wysyłać
  i odbierać tylko bez czekania (każdy czas działa tam jak `NO_WAIT`). Element jest kopiowany
  przy wyłączonych przerwaniach – większe dane lepiej przekazywać wskaźnikiem.
- **Timer** wywołuje swoją funkcję w wątku `ktimer` (priorytet 21), nie w przerwaniu: może
  wysłać do kolejki albo zablokować mutex, ale powinien być krótki, bo pozostałe timery czekają.
  Okresowy, który się spóźnił o okres, pomija zaległe wywołania.
- **Sprzęt** obsługują sterowniki NXP SDK: GPIO, LPUART, zegary i pamięci podręczne są już w
  jądrze (`kernel/platform/evkbimxrt1050/drivers`), pozostałe (`fsl_lpi2c.c`, `fsl_lpspi.c`,
  `fsl_gpt.c`, ...) dołącza `SDK_DRIVERS` ze sterowników NXP SDK (`third_party/nxp-sdk`,
  katalogi w `CRTOS_SDK_DRIVER_DIRS` w `cmake/crtos.cmake`).

## Czego nie ma i na co uważać

- Aplikacja działa w trybie uprzywilejowanym, bez ochrony pamięci od jądra (jak sterownik).
  Strażnicy stosów MPU działają: przepełnienie stosu albo błąd w zadaniu kończy to zadanie
  (raport w `dmesg`); błąd w obsłudze przerwania zatrzymuje system (`panic`, restart po 10 s).
- Konsola UART należy do monitora jądra: aplikacja pisze przez `printk`, nie czyta konsoli.
  Monitor działa też przez sondę (`crtos kmon`): `ps`, `mem`, `irq`, `uptime`, `dmesg`,
  `test`, `kill`, `reboot`.
- Nie ma plików, karty SD, sieci, USB ani sterowników `.ko` – to wszystko należy do systemu
  operacyjnego.
- `crtos crash` pokazuje linie kodu obrazu systemu; adres z raportu awarii obrazu RTOS
  tłumaczy `arm-none-eabi-addr2line -e build/rtos/kernel/crtos.axf ADRES`.
- Testy rdzenia w obrazie RTOS: `.\crtos kmon "test all"` (10 testów: scheduler, muteksy,
  semafory, przerwania, sterta, kolejki, timery, FPU, ograniczanie błędów).
