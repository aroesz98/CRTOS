# CRTOS System Call Testing Guide

## Przygotowane testy

### 1. **Test Kernel-side** (`source/syscall_test.cpp`)
Test wywołuje syscalls bezpośrednio z kernela (przed startem schedulera).

**Co testuje:**
- SYS_GET_TICK - pobieranie system tick
- SYS_WRITE - zapis do stdout  
- SYS_MALLOC/FREE - alokacja pamięci
- SYS_GET_SYSTEM_INFO - informacje o systemie
- Obsługa błędów (ENOSYS dla niezaimplementowanych)

**Użycie:**
Test jest automatycznie wywoływany w `main()` przed `Scheduler::Start()`.

### 2. **Test User-space** (`modules/src/syscall_test_module.c`)
Pełny moduł BIN który używa syscalls przez `crtos_userspace.h` API.

**Co testuje:**
- Wszystkie podstawowe syscalls
- Prawdziwa separacja user/kernel (moduł = user space)
- Wielokrotne alokacje/dealokacje
- Operacje I/O
- Długotrwałe działanie w pętli

## Kompilacja

### Krok 1: Zbuduj moduły

```bash
cd modules
mkdir -p build
cd build

# Konfiguracja CMake
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-none-eabi.cmake

# Kompilacja (utworzy .bin files)
make -j8
```

Utworzone pliki:
- `modules/build/syscall_test_module.bin`
- `modules/build/htop_module.bin`
- `modules/build/copy_module.bin`
- `modules/build/shm_module.bin`
- `modules/build/dpc_test_module.bin`

### Krok 2: Zbuduj główny projekt CRTOS

W MCUXpresso IDE:
1. Clean project
2. Build project
3. Powinien zlinkować wszystkie moduły .bin przez assembly wrappers

Lub przez terminal:
```bash
cd Debug
make clean
make all -j8
```

## Uruchomienie testów

### Flash i uruchom

1. Flash projekt na mikrokontroler
2. Otwórz terminal (115200 baud)
3. Obserwuj output

### Oczekiwany output

```
=== Testing Syscall Interface (Kernel-side) ===
========================================
Testing New Syscall Interface
========================================
✓ SYS_GET_TICK: 1234 ticks
✓ SYS_WRITE: 20 bytes written
Hello from syscall!
✓ SYS_MALLOC: allocated at 0x20001234
✓ SYS_FREE: freed memory
✓ SYS_GET_SYSTEM_INFO:
  Total memory: 131072 bytes
  Free memory:  44156 bytes
  Uptime:       1500 ticks
✓ Unimplemented syscall: -38 (expected -38 ENOSYS)

========================================
Syscall Interface Test PASSED!
========================================

[... scheduler starts ...]

--- Loading Syscall Test Module ---
Syscall Test Module: 0x60015000, size: XXXX bytes
SUCCESS: Syscall test module loaded!
Module will start testing syscalls...

[... moduł uruchamia się ...]

================================================================================
           CRTOS System Call Test Module - User Space Edition
================================================================================
This module tests syscall interface from TRUE user space (BIN module)
All operations go through SVC → Kernel → Hardware
================================================================================

[TEST 1] Getting Process ID...
  PID: 0x00015ABC ✓

[TEST 2] Getting system tick...
  Tick: 2345 ✓

[TEST 3] Testing sleep(500 ticks)...
  Slept for: 502 ticks ✓

[TEST 4] Testing write() to stdout...
  Hello from syscall write()!
  Wrote 30 bytes ✓

[TEST 5] Testing malloc/free...
  malloc(256) = OK, pattern verified ✓
  free() completed ✓

[TEST 6] Testing multiple malloc/free...
  Allocated 5 x 128 bytes ✓
  Freed all 5 allocations ✓

[TEST 7] Testing get_system_info()...
  Total memory: 131072 bytes
  Free memory:  42000 bytes
  Uptime:       3000 ticks ✓

[TEST 8] Testing yield()...
  Yielding... back
  Yielding... back
  Yielding... back
  Yield test ✓

[TEST 9] Testing open/close...
  open() returned error: -2 (expected, no device registered yet)

[TEST 10] Testing unimplemented syscall...
  wait_irq() returned: -38 (ENOSYS as expected) ✓

================================================================================
                     System Call Test Module - COMPLETED
================================================================================
Tests performed:
  ✓ Process ID retrieval
  ✓ System tick access
  ✓ Sleep/delay
  ✓ Console I/O (write)
  ✓ Memory allocation (malloc/free)
  ✓ System information query
  ✓ Process yielding
  ✓ File descriptor operations
  ✓ Error handling
================================================================================

Module entering monitoring loop...

[Syscall Test Module] Status update:
  Loop iteration: 1
  Current tick: 8000
  Free memory: 41500 bytes
  Module still running... ✓
```

## Weryfikacja

### Testy pozytywne ✓

1. **Kernel-side test** pokazuje "PASSED"
2. **User-space module** przechodzi wszystkie 10 testów
3. Moduł działa w pętli bez crashów
4. HTOP pokazuje moduł jako aktywny task

### Możliwe problemy

#### Problem: "open() returned error: -2"
**Status:** To jest OK! 
**Przyczyna:** Device driver framework jeszcze nie zaimplementowany.
**Expected:** `-2` (ENOENT - brak urządzenia)

#### Problem: Module nie ładuje się
**Sprawdź:**
1. Czy `modules/build/syscall_test_module.bin` istnieje?
2. Czy assembly wrapper jest w projekcie?
3. Czy rozmiar modułu < 1MB?

#### Problem: System crashuje
**Możliwe przyczyny:**
1. Stack overflow w module - zwiększ stack size
2. Błąd w MPU (gdy będzie zaimplementowany)
3. Null pointer dereference - sprawdź return values

## Następne kroki rozwoju

### Faza 1: Device Driver Framework ✅ Gotowy do implementacji
- Struktura `Device` i `DeviceOperations`
- `DeviceManager` registry
- UART driver jako przykład

### Faza 2: IRQ Event Queue
- Replace DPC dla user-space
- Per-process event queue
- `sys_wait_irq()` implementation

### Faza 3: Process Management
- Process Control Block (PCB)
- Per-process file descriptor table
- Resource tracking

### Faza 4: MPU Protection
- Memory isolation per process
- User/kernel separation w hardware
- Fault handlers

## Debugging

### Enable verbose output

W `syscall_test.cpp` dodaj więcej PRINTF:
```cpp
PRINTF("DEBUG: syscall number = %lu\n", syscall_num);
PRINTF("DEBUG: args = %lu, %lu, %lu, %lu\n", arg0, arg1, arg2, arg3);
```

W `Syscall_Dispatcher.cpp`:
```cpp
PRINTF("SVC_Dispatch: num=%lu\n", syscallNum);
```

### GDB breakpoints

```
break SVC_Handler_Main
break CRTOS::Syscall::SVC_Dispatch
break sys_write
break sys_malloc
```

---

**Autor**: Arkadiusz Szlanta  
**Data**: 27 Dec 2025  
**Status**: Syscall interface - MVP Complete ✅
