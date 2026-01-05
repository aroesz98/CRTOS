# CRTOS System Call Interface

## Opis

Implementacja interfejsu System Call dla CRTOS, zapewniająca separację przestrzeni user/kernel.

## Struktura

```
CRTOS/
├── Syscall/
│   ├── SystemCall.hpp          - Definicje syscalls i interfejsów
│   ├── SVC_Handler.cpp         - Handler wyjątku SVC (assembly + C++)
│   ├── Syscall_Dispatcher.cpp  - Dispatcher syscalli
│   ├── Syscall_IO.cpp          - Implementacja I/O syscalls
│   ├── Syscall_Process.cpp     - Implementacja process management syscalls
│   └── Syscall_Stubs.cpp       - Stub'y dla niezaimplementowanych syscalls
│
├── Userspace/
│   └── crtos_userspace.h       - API dla aplikacji user-space
│
└── HAL/                         - (przygotowane na przyszłość)
```

## Jak to działa?

### 1. User Space → Kernel

```
User App:  write(1, "Hello", 5)
    ↓
User API:  syscall3(23, 1, "Hello", 5)  [inline assembly: SVC #23]
    ↓
Hardware:  CPU wywołuje wyjątek SVC
    ↓
Kernel:    SVC_Handler (assembly) → SVC_Handler_Main (C++)
    ↓
Kernel:    SVC_Dispatch(23, arg0, arg1, arg2, arg3)
    ↓
Kernel:    sys_write(fd, buffer, count)
    ↓
Kernel:    DbgConsole_Printf(...) lub device->write()
    ↓
Hardware:  Powrót z wyjątku (wynik w r0)
    ↓
User App:  Otrzymuje wynik
```

### 2. Dostępne System Calls

#### Process Management
- `exit(status)` - zakończ proces
- `getpid()` - pobierz PID
- `yield()` - oddaj CPU
- `sleep(ticks)` - śpij przez N ticków

#### I/O Operations
- `open(path, flags)` - otwórz urządzenie/plik
- `close(fd)` - zamknij deskryptor
- `read(fd, buffer, count)` - czytaj
- `write(fd, buffer, count)` - pisz
- `ioctl(fd, cmd, arg)` - kontrola urządzenia

#### Memory Management
- `malloc(size)` - alokuj pamięć
- `free(ptr)` - zwolnij pamięć
- `sbrk(increment)` - rozszerz heap (TODO)

#### Time
- `get_tick()` - pobierz system tick
- `get_time()` - pobierz czas (TODO)

#### Debug
- `debug_print(msg, len)` - wypisz debug (używa sys_write)
- `get_system_info(info)` - informacje o systemie
- `get_process_info(info)` - informacje o procesie (TODO)

#### IRQ/Events (TODO - będzie w następnej fazie)
- `wait_irq(event, timeout)` - czekaj na przerwanie
- `register_irq(irq_num)` - zarejestruj się na IRQ
- `unregister_irq(irq_num)` - wyrejestruj z IRQ
- `poll_irq(event)` - sprawdź pending events

## Użycie w Modułach

### Przykład 1: Prosta aplikacja

```c
#include <crtos_userspace.h>

void module_main(void* args)
{
    // Wypisz wiadomość
    puts("Hello from user space!");
    
    // Pobierz PID
    pid_t pid = getpid();
    
    // Alokuj pamięć
    void* buffer = malloc(256);
    if (buffer) {
        // Użyj bufora
        free(buffer);
    }
    
    // Śpij
    sleep(1000);
    
    // Zakończ
    exit(0);
}
```

### Przykład 2: I/O z urządzeniem

```c
#include <crtos_userspace.h>

void module_main(void* args)
{
    // Otwórz UART
    int fd = open("/dev/uart0", O_RDWR);
    if (fd < 0) {
        puts("ERROR: Cannot open device");
        exit(1);
    }
    
    // Pisz do UART
    const char* msg = "Hello UART!\n";
    write(fd, msg, 12);
    
    // Czytaj z UART (gdy będzie zaimplementowane)
    char buffer[64];
    int bytes = read(fd, buffer, sizeof(buffer));
    
    // Zamknij
    close(fd);
    
    exit(0);
}
```

### Przykład 3: Obsługa przerwań (TODO - przyszła implementacja)

```c
#include <crtos_userspace.h>

void module_main(void* args)
{
    // Zarejestruj się na GPIO IRQ
    register_irq(GPIO_IRQ_NUM);
    
    puts("Waiting for button press...");
    
    while (1) {
        struct irq_event event;
        
        // Czekaj na przerwanie (blokująco)
        if (wait_irq(&event, TIMEOUT_INFINITE) == 0) {
            puts("Button pressed!");
            
            // Przetwórz event
            // event.irq_number, event.timestamp, event.data
        }
    }
    
    unregister_irq(GPIO_IRQ_NUM);
    exit(0);
}
```

## Kompilacja Modułów

### Dla GCC ARM

```bash
arm-none-eabi-gcc \
    -mcpu=cortex-m7 \
    -mthumb \
    -O2 \
    -I../CRTOS/Userspace \
    -fPIC \
    -fno-builtin \
    -nostdlib \
    -c syscall_test_module.c -o syscall_test_module.o

arm-none-eabi-ld \
    -T module.ld \
    syscall_test_module.o -o syscall_test.elf

arm-none-eabi-objcopy \
    -O binary \
    syscall_test.elf syscall_test.bin
```

### Include w Assembly

```assembly
.section .rodata
.global syscall_test_module_bin
.global syscall_test_module_bin_end
.global syscall_test_module_bin_size

syscall_test_module_bin:
    .incbin "syscall_test.bin"
syscall_test_module_bin_end:

.section .data
syscall_test_module_bin_size:
    .word syscall_test_module_bin_end - syscall_test_module_bin
```

## Ładowanie Modułu

```cpp
// W main.cpp
extern "C" {
    extern uint8_t syscall_test_module_bin[];
    extern uint32_t syscall_test_module_bin_size;
}

CRTOS::Task::TaskHandle hTestModule;
CRTOS::Task::LPC55S69_Features::CreateTaskForBinModule(
    syscall_test_module_bin,
    nullptr,  // args
    10,       // priority
    &hTestModule
);
```

## Status Implementacji

### ✅ Zaimplementowane
- [x] System call interface (numery, struktury)
- [x] SVC handler (assembly + C++)
- [x] Syscall dispatcher
- [x] Podstawowe syscalls:
  - [x] `exit`, `getpid`, `yield`, `sleep`
  - [x] `open`, `close`, `write` (podstawowa implementacja)
  - [x] `malloc`, `free`
  - [x] `get_tick`, `get_system_info`
- [x] User-space API (crtos_userspace.h)
- [x] Test module

### ⏳ TODO (Następne fazy)
- [ ] Device Driver Framework
- [ ] IRQ Event Queue per process
- [ ] Process Control Block (PCB)
- [ ] MPU memory protection
- [ ] File descriptor table per process
- [ ] Implementacja `read()` z urządzeń
- [ ] Implementacja `ioctl()`
- [ ] IPC (Inter-Process Communication)
- [ ] Shared memory
- [ ] User-space synchronization (mutex, semaphore)

## Testowanie

1. Dodaj pliki z `CRTOS/Syscall/` do build system
2. Skompiluj `syscall_test_module.c` jako moduł BIN
3. Załaduj moduł przez `CreateTaskForBinModule()`
4. Obserwuj output - powinny pojawić się komunikaty `[SYSCALL TEST]`

## Bezpieczeństwo

### Obecne (MVP):
- ✅ System call interface separuje user/kernel
- ✅ Walidacja parametrów (null pointers)
- ✅ Kody błędów kompatybilne z POSIX

### Przyszłe (TODO):
- ⏳ MPU sprawdza czy wskaźniki są w user-space
- ⏳ Per-process file descriptor table
- ⏳ Resource limits
- ⏳ Privilege levels

## Integracja z Obecnym Kodem

System call interface jest **wstecznie kompatybilny**:
- Stary kod może nadal używać `CRTOS::Task::` bezpośrednio
- Nowy kod w modułach używa syscalls
- Stopniowa migracja w miarę dodawania warstw (HAL, OS Services)

## Następne Kroki

1. **HAL Layer** - Hardware Abstraction
2. **Device Driver Framework** - Uniform device access
3. **Process Management** - Prawdziwe procesy z PCB
4. **IRQ Event Queue** - Replacement dla DPC dla user-space
5. **MPU Protection** - Memory isolation

---

**Autor**: Arkadiusz Szlanta  
**Data**: 27 Dec 2025  
**Wersja**: 1.0 - Initial syscall implementation
