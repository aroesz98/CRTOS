# UART Driver Module

## Overview

This is the UART driver module for CRTOS. It is compiled as a **dynamically loadable
kernel module** (.ko file) that can be loaded at runtime from SD card or other storage.

## Features

- Implements `DriverBase` interface
- Interrupt-driven RX with ISR → DPC → driver flow
- Ring buffers for RX and TX (256 bytes each)
- Configurable baudrate via ioctl
- Blocking and non-blocking read modes
- Uses HAL layer for hardware access (no direct register manipulation)
- **Position Independent Code (PIC)** for runtime relocation
- **Dynamically resolved symbols** from kernel exports

## Building

### Prerequisites

- CMake 3.20+
- Ninja build system
- ARM GCC toolchain (arm-none-eabi-gcc)

### Using CMake + Ninja

```bash
cd drivers_modules/uart

# Configure (using preset)
cmake --preset default

# Build
cmake --build build

# Install to SD card directory
cmake --install build
```

### Alternative: Manual Configuration

```bash
cd drivers_modules/uart

# Configure with Ninja generator
cmake -B build -G Ninja

# Build
cmake --build build

# Install
cmake --install build
```

### Build Presets

| Preset | Description |
|--------|-------------|
| `default` | Release build with optimizations |
| `debug` | Debug build with full symbols |

### Build All Drivers

From the `drivers_modules` directory:

```bash
cd drivers_modules

# Configure all drivers
cmake --preset default

# Build all
cmake --build build
```

This will create:
- `build/uart/uart.ko` - Relocatable kernel module

The module will be installed to `sdcard/drivers/` for loading from SD card.

## Loading the Module

### From Kernel Code (at boot)

```cpp
#include <CRTOS/ModuleLoader.hpp>
#include <CRTOS/Drivers/Drivers.hpp>

// Load module from memory (e.g., embedded in firmware)
extern const uint8_t uart_module_data[];
extern const size_t uart_module_size;

void loadUartDriver()
{
    CRTOS::ModuleLoader& loader = CRTOS::ModuleLoader::getInstance();
    
    // Initialize module loader (registers kernel symbols)
    loader.init();
    
    // Load module from memory buffer
    CRTOS::ModuleResult result = loader.loadModule(
        uart_module_data,
        uart_module_size,
        "uart"
    );
    
    if (result == CRTOS::ModuleResult::SUCCESS)
    {
        // Driver is now registered and ready to use
        auto uart = CRTOS::Drivers::getDriver("uart");
        uart->open();
        uart->write("Module loaded!\r\n", 16);
    }
}
```

### From File (SD Card)

```cpp
#include <CRTOS/ModuleLoader.hpp>
#include <CRTOS/HAL/FileSystem.hpp>

void loadDriverFromSD()
{
    CRTOS::ModuleLoader& loader = CRTOS::ModuleLoader::getInstance();
    
    // Read module file from SD card
    uint8_t* moduleData = nullptr;
    size_t moduleSize = 0;
    
    if (FileSystem::ReadFile("/drivers/uart.ko", &moduleData, &moduleSize))
    {
        // Load the module
        CRTOS::ModuleResult result = loader.loadModule(
            moduleData, 
            moduleSize, 
            "uart"
        );
        
        // Free file buffer (module data is copied)
        free(moduleData);
        
        if (result == CRTOS::ModuleResult::SUCCESS)
        {
            PRINTF("UART driver loaded successfully!\r\n");
        }
    }
}
```

### Unloading a Module

```cpp
void unloadDriver()
{
    CRTOS::ModuleLoader& loader = CRTOS::ModuleLoader::getInstance();
    loader.unloadModule("uart");
}
```

## IOCTL Commands

### Common Commands (0x0000 - 0x00FF)

| Command | Value | Description |
|---------|-------|-------------|
| GET_STATE | 0x0000 | Get driver state |
| GET_VERSION | 0x0001 | Get driver version |
| GET_NAME | 0x0002 | Get driver name |
| RESET | 0x0003 | Reset driver |
| FLUSH | 0x0004 | Flush buffers |
| SET_BLOCKING | 0x0005 | Set blocking mode |
| SET_NONBLOCKING | 0x0006 | Set non-blocking mode |
| GET_RX_PENDING | 0x0007 | Get bytes in RX buffer |
| GET_TX_PENDING | 0x0008 | Get bytes in TX buffer |

### UART-Specific Commands (0x0100 - 0x01FF)

| Command | Value | Description |
|---------|-------|-------------|
| SET_BAUDRATE | 0x0100 | Set baudrate (arg = uint32_t*) |
| GET_BAUDRATE | 0x0101 | Get baudrate |
| SET_PARITY | 0x0102 | Set parity (0=none, 1=odd, 2=even) |
| GET_PARITY | 0x0103 | Get parity |
| SET_STOP_BITS | 0x0104 | Set stop bits (1 or 2) |
| GET_STOP_BITS | 0x0105 | Get stop bits |
| SET_DATA_BITS | 0x0106 | Set data bits (7, 8, 9) |
| GET_DATA_BITS | 0x0107 | Get data bits |
| CLEAR_RX_BUFFER | 0x0108 | Clear RX buffer |
| CLEAR_TX_BUFFER | 0x0109 | Clear TX buffer |
| GET_RX_COUNT | 0x010A | Get bytes in RX buffer |
| GET_TX_COUNT | 0x010B | Get bytes in TX buffer |
| SET_RX_TIMEOUT | 0x010C | Set default RX timeout |

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                         Application                               │
├──────────────────────────────────────────────────────────────────┤
│                    Driver Interface (DriverBase)                  │
│                    open/close/read/write/ioctl                    │
├──────────────────────────────────────────────────────────────────┤
│                      UART Driver Module                           │
│  ┌─────────────┐  ┌─────────────┐  ┌──────────────────────────┐  │
│  │  RX Buffer  │  │  TX Buffer  │  │  Configuration/Stats     │  │
│  │ (RingBuffer)│  │ (RingBuffer)│  │                          │  │
│  └──────┬──────┘  └──────┬──────┘  └──────────────────────────┘  │
│         │                │                                        │
│  ┌──────┴──────┐  ┌──────┴──────┐                                │
│  │  DPC Handler│  │   Writer    │                                │
│  │ (Task Ctx)  │  │             │                                │
│  └──────┬──────┘  └──────┬──────┘                                │
├─────────┼────────────────┼───────────────────────────────────────┤
│         │       HAL Layer│                                        │
│  ┌──────┴──────┐  ┌──────┴──────┐                                │
│  │ ISR Callback│  │ hal_uart_*  │                                │
│  └──────┬──────┘  └──────┬──────┘                                │
├─────────┼────────────────┼───────────────────────────────────────┤
│         │      Hardware  │                                        │
│  ┌──────┴────────────────┴──────┐                                │
│  │         LPUART Peripheral     │                                │
│  └───────────────────────────────┘                                │
└──────────────────────────────────────────────────────────────────┘
```

## Files

```
drivers_modules/uart/
├── Makefile            # Build script
├── README.md           # This file
├── UartDriver.hpp      # Driver header
├── UartDriver.cpp      # Driver implementation
└── hal_uart.h          # Local HAL interface
```

## Dependencies

- CRTOS/Drivers/DriverBase.hpp
- CRTOS/Drivers/RingBuffer.hpp
- CRTOS/BinarySemaphore.hpp
- CRTOS/DPCWorker.hpp
- CRTOS/Task.hpp
- HAL_UART implementation (in CRTOS/HAL/)

## License

This source code is provided for hobbyist and private use only.
Any commercial or industrial use is prohibited.
