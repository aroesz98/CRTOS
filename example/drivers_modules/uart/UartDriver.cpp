/*
 * UartDriver.cpp - CRTOS UART Driver Module Implementation
 * Author: Arkadiusz Szlanta
 * Date: 04 Jan 2026
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * UART driver module - dynamically loadable kernel module.
 * Uses HAL for hardware access and DPC for interrupt handling.
 */

#include "UartDriver.hpp"
#include "../common/KernelAPI.hpp"
#include <cstring>

// Debug macro for UART driver - uses kernel API printf
// WARNING: Do NOT enable in handleIrq() - it runs in ISR context!
#define UART_DRV_DEBUG 0
#if UART_DRV_DEBUG
#define UART_DRV_LOG(...) do { auto* _api = GetAPI(); if (_api && _api->printf) _api->printf(__VA_ARGS__); } while(0)
#else
#define UART_DRV_LOG(...)
#endif

namespace CRTOS
{
namespace Drivers
{

// ============================================================================
// Kernel API Access
// ============================================================================

// Weak symbol - resolved by ModuleLoader
extern "C" const CRTOS::KernelAPI* crtos_get_kernel_api(void) __attribute__((weak));

// NOTE: Do NOT cache the API pointer in a static variable for dynamic modules!
// The .bss section may not be properly zeroed when the module is loaded.
static inline const CRTOS::KernelAPI* GetAPI()
{
    if (crtos_get_kernel_api != nullptr)
    {
        return crtos_get_kernel_api();
    }
    return nullptr;
}

// ============================================================================
// Static Member Initialization
// ============================================================================

UartDriver* UartDriver::_instance = nullptr;

const DriverInfo UartDriver::_driverInfo = {
    .name = "uart",
    .description = "UART Serial Driver (multi-instance)",
    .versionMajor = 2,
    .versionMinor = 0,
    .type = DriverType::CHAR_DEVICE
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

UartDriver::UartDriver()
    : _rxDataAvailable(nullptr)
    , _uartInstance(HAL_UART_3)
    , _defaultRxTimeout(1000)
    , _irqNumber(0)
{
    _instance = this;

    // Default configuration
    hal_uart_get_default_config(&_config);

    // Clear statistics
    const auto* api = GetAPI();
    if (api && api->memset)
    {
        api->memset(&_stats, 0, sizeof(_stats));
    }
}

UartDriver::~UartDriver()
{
    if (_state != DriverState::UNINITIALIZED)
    {
        deinit();
    }
    
    if (_instance == this)
    {
        _instance = nullptr;
    }
}

UartDriver* UartDriver::getInstance()
{
    return _instance;
}

const DriverInfo* UartDriver::getInfo() const
{
    return &_driverInfo;
}

// ============================================================================
// Driver Lifecycle
// ============================================================================

DriverResult UartDriver::init()
{
    if (_state != DriverState::UNINITIALIZED)
    {
        return DriverResult::SUCCESS;
    }

    const auto* api = GetAPI();
    if (api == nullptr)
    {
        return DriverResult::ERROR_GENERIC;
    }

    // Create semaphore for blocking reads
    if (api->semaphore_create)
    {
        _rxDataAvailable = static_cast<BinarySemaphore*>(api->semaphore_create());
    }

    // Reset buffers
    _rxBuffer.reset();
    _txBuffer.reset();

    // Get IRQ number for selected instance
    _irqNumber = hal_uart_get_irq_number(_uartInstance);

    // Clear statistics
    if (api->memset)
    {
        api->memset(&_stats, 0, sizeof(_stats));
    }

    _state = DriverState::INITIALIZED;
    return DriverResult::SUCCESS;
}

DriverResult UartDriver::deinit()
{
    if (_state == DriverState::UNINITIALIZED)
    {
        return DriverResult::SUCCESS;
    }

    // Close first if open
    if (_state == DriverState::OPENED)
    {
        close();
    }

    const auto* api = GetAPI();

    // Free semaphore
    if (_rxDataAvailable != nullptr && api && api->semaphore_delete)
    {
        api->semaphore_delete(_rxDataAvailable);
        _rxDataAvailable = nullptr;
    }

    _state = DriverState::UNINITIALIZED;
    return DriverResult::SUCCESS;
}

// ============================================================================
// Open / Close
// ============================================================================

DriverResult UartDriver::open()
{
    UART_DRV_LOG("[UartDrv] open() called, state=%d, instance=%d\r\n", (int)_state, (int)_uartInstance);
    
    if (_state == DriverState::UNINITIALIZED)
    {
        UART_DRV_LOG("[UartDrv] ERROR: not initialized\r\n");
        return DriverResult::ERROR_NOT_INIT;
    }

    if (_state == DriverState::OPENED)
    {
        _openCount++;
        UART_DRV_LOG("[UartDrv] Already opened, count=%d\r\n", _openCount);
        return DriverResult::SUCCESS;
    }

    const auto* api = GetAPI();
    if (api == nullptr)
    {
        UART_DRV_LOG("[UartDrv] ERROR: no API\r\n");
        return DriverResult::ERROR_GENERIC;
    }

    // Initialize HAL
    UART_DRV_LOG("[UartDrv] Calling hal_uart_init_instance(%d)\r\n", (int)_uartInstance);
    if (!hal_uart_init_instance(_uartInstance, &_config))
    {
        UART_DRV_LOG("[UartDrv] ERROR: hal_uart_init_instance failed\r\n");
        return DriverResult::ERROR_GENERIC;
    }
    UART_DRV_LOG("[UartDrv] HAL init OK\r\n");

    // Register with DPC dispatcher
    if (api->dpc_register_irq)
    {
        int regResult = api->dpc_register_irq(_irqNumber);
        if (api->printf) api->printf("[UartDrv] dpc_register_irq(%lu) = %d\r\n", _irqNumber, regResult);
    }

    if (api->dpc_register_handler && _rxDataAvailable)
    {
        // Pass GOT base so DPC dispatcher can set r9 before calling our callback
        int regResult = api->dpc_register_handler(_irqNumber, _rxDataAvailable, 
                                  staticDpcCallback, this, getGotBase());
        if (api->printf) api->printf("[UartDrv] dpc_register_handler(%lu) = %d, gotBase=0x%08lX\r\n", 
                                      _irqNumber, regResult, getGotBase());
    }

    // Enable NVIC
    if (api->nvic_set_priority)
    {
        api->nvic_set_priority(_irqNumber, 5);
    }
    if (api->nvic_enable_irq)
    {
        api->nvic_enable_irq(_irqNumber);
        if (api->printf) api->printf("[UartDrv] NVIC enabled for IRQ %lu\r\n", _irqNumber);
    }

    // Enable RX interrupt at peripheral level
    hal_uart_enable_rx_irq(_uartInstance);
    if (api->printf) api->printf("[UartDrv] RX IRQ enabled for instance %d\r\n", (int)_uartInstance);

    _openCount = 1;
    _state = DriverState::OPENED;
    
    if (api->printf) api->printf("[UartDrv] open() OK, state=OPENED\r\n");

    return DriverResult::SUCCESS;
}

DriverResult UartDriver::close()
{
    if (_state != DriverState::OPENED)
    {
        return DriverResult::ERROR_NOT_OPEN;
    }

    if (_openCount > 1)
    {
        _openCount--;
        return DriverResult::SUCCESS;
    }

    const auto* api = GetAPI();

    // Disable RX interrupt
    hal_uart_disable_rx_irq(_uartInstance);

    // Disable NVIC
    if (api && api->nvic_disable_irq)
    {
        api->nvic_disable_irq(_irqNumber);
    }

    // Unregister from DPC
    if (api)
    {
        if (api->dpc_unregister_handler && _rxDataAvailable)
        {
            api->dpc_unregister_handler(_irqNumber, _rxDataAvailable);
        }
        if (api->dpc_unregister_irq)
        {
            api->dpc_unregister_irq(_irqNumber);
        }
    }

    // Deinitialize HAL
    hal_uart_deinit_instance(_uartInstance);

    _openCount = 0;
    _state = DriverState::INITIALIZED;

    return DriverResult::SUCCESS;
}

// ============================================================================
// Read / Write
// ============================================================================

int32_t UartDriver::read(void* buffer, size_t size, uint32_t timeoutMs)
{
    if (_state != DriverState::OPENED)
    {
        return static_cast<int32_t>(DriverResult::ERROR_NOT_OPEN);
    }

    if (buffer == nullptr || size == 0)
    {
        return static_cast<int32_t>(DriverResult::ERROR_INVALID);
    }

    const auto* api = GetAPI();
    uint8_t* data = static_cast<uint8_t*>(buffer);
    size_t bytesRead = 0;
    uint32_t timeout = (timeoutMs > 0) ? timeoutMs : _defaultRxTimeout;

    // Non-blocking mode
    if (!_isBlocking && timeoutMs == 0)
    {
        bytesRead = _rxBuffer.read(data, size);
        _stats.rxBytes += bytesRead;
        return static_cast<int32_t>(bytesRead);
    }

    // Blocking mode with timeout
    uint32_t startTime = 0;
    if (api && api->get_tick_count)
    {
        startTime = api->get_tick_count();
    }
    
    while (bytesRead < size)
    {
        size_t readNow = _rxBuffer.read(data + bytesRead, size - bytesRead);
        bytesRead += readNow;

        if (bytesRead >= size)
        {
            break;
        }

        // Check timeout
        if (timeout > 0 && api && api->get_tick_count)
        {
            uint32_t elapsed = api->get_tick_count() - startTime;
            if (elapsed >= timeout)
            {
                break;
            }

            uint32_t remainingTimeout = timeout - elapsed;
            if (_rxDataAvailable && api->semaphore_wait)
            {
                api->semaphore_wait(_rxDataAvailable, remainingTimeout);
            }
            else if (api->task_delay)
            {
                api->task_delay(1);
            }
        }
        else if (api && api->task_yield)
        {
            api->task_yield();
        }
    }

    _stats.rxBytes += bytesRead;
    return static_cast<int32_t>(bytesRead);
}

int32_t UartDriver::write(const void* buffer, size_t size, uint32_t timeoutMs)
{
    (void)timeoutMs;

    UART_DRV_LOG("[UartDrv] write() called: size=%zu, state=%d, inst=%d\r\n", 
                 size, (int)_state, (int)_uartInstance);

    if (_state != DriverState::OPENED)
    {
        UART_DRV_LOG("[UartDrv] write() ERROR: not opened\r\n");
        return static_cast<int32_t>(DriverResult::ERROR_NOT_OPEN);
    }

    if (buffer == nullptr || size == 0)
    {
        UART_DRV_LOG("[UartDrv] write() ERROR: invalid buffer\r\n");
        return static_cast<int32_t>(DriverResult::ERROR_INVALID);
    }

    const uint8_t* data = static_cast<const uint8_t*>(buffer);

    // Write directly using HAL (blocking)
    UART_DRV_LOG("[UartDrv] calling hal_uart_write_instance(%d, %p, %zu)\r\n", 
                 (int)_uartInstance, data, size);
    hal_uart_write_instance(_uartInstance, data, size);

    _stats.txBytes += size;
    UART_DRV_LOG("[UartDrv] write() OK, txBytes=%lu\r\n", (unsigned long)_stats.txBytes);
    return static_cast<int32_t>(size);
}

// ============================================================================
// IOCTL
// ============================================================================

DriverResult UartDriver::ioctl(uint32_t cmd, void* arg)
{
    if (_state == DriverState::UNINITIALIZED)
    {
        return DriverResult::ERROR_NOT_INIT;
    }

    // Handle common IOCTLs
    switch (static_cast<CommonIoctl>(cmd))
    {
        case CommonIoctl::GET_VERSION:
            if (arg)
            {
                *static_cast<uint32_t*>(arg) = (_driverInfo.versionMajor << 16) | _driverInfo.versionMinor;
            }
            return DriverResult::SUCCESS;

        case CommonIoctl::GET_STATE:
            if (arg)
            {
                *static_cast<DriverState*>(arg) = _state;
            }
            return DriverResult::SUCCESS;

        case CommonIoctl::SET_BLOCKING:
            _isBlocking = true;
            return DriverResult::SUCCESS;

        case CommonIoctl::SET_NONBLOCKING:
            _isBlocking = false;
            return DriverResult::SUCCESS;

        default:
            break;
    }

    // Handle UART-specific IOCTLs
    switch (static_cast<UartIoctl>(cmd))
    {
        case UartIoctl::SET_BAUDRATE:
            if (arg)
            {
                _config.baudrate = *static_cast<uint32_t*>(arg);
                if (_state == DriverState::OPENED)
                {
                    hal_uart_deinit_instance(_uartInstance);
                    hal_uart_init_instance(_uartInstance, &_config);
                    hal_uart_enable_rx_irq(_uartInstance);
                }
            }
            return DriverResult::SUCCESS;

        case UartIoctl::GET_BAUDRATE:
            if (arg)
            {
                *static_cast<uint32_t*>(arg) = _config.baudrate;
            }
            return DriverResult::SUCCESS;

        case UartIoctl::SET_DATA_BITS:
            if (arg)
            {
                _config.dataBits = *static_cast<uint8_t*>(arg);
            }
            return DriverResult::SUCCESS;

        case UartIoctl::SET_PARITY:
            if (arg)
            {
                _config.parity = *static_cast<uint8_t*>(arg);
            }
            return DriverResult::SUCCESS;

        case UartIoctl::SET_STOP_BITS:
            if (arg)
            {
                _config.stopBits = *static_cast<uint8_t*>(arg);
            }
            return DriverResult::SUCCESS;

        case UartIoctl::GET_RX_COUNT:
            if (arg)
            {
                *static_cast<size_t*>(arg) = _rxBuffer.count();
            }
            return DriverResult::SUCCESS;

        case UartIoctl::CLEAR_RX_BUFFER:
            _rxBuffer.reset();
            return DriverResult::SUCCESS;

        case UartIoctl::CLEAR_TX_BUFFER:
            _txBuffer.reset();
            return DriverResult::SUCCESS;

        case UartIoctl::SET_RX_TIMEOUT:
            if (arg)
            {
                _defaultRxTimeout = *static_cast<uint32_t*>(arg);
            }
            return DriverResult::SUCCESS;

        case UartIoctl::SET_INSTANCE:
            UART_DRV_LOG("[UartDrv] SET_INSTANCE: arg=%p, state=%d\r\n", arg, (int)_state);
            if (arg && _state != DriverState::OPENED)
            {
                hal_uart_instance_t inst = *static_cast<hal_uart_instance_t*>(arg);
                UART_DRV_LOG("[UartDrv] SET_INSTANCE: requested inst=%d\r\n", (int)inst);
                if (inst >= HAL_UART_1 && inst <= HAL_UART_8)
                {
                    _uartInstance = inst;
                    _irqNumber = hal_uart_get_irq_number(_uartInstance);
                    UART_DRV_LOG("[UartDrv] SET_INSTANCE OK: _uartInstance=%d, IRQ=%d\r\n", 
                                 (int)_uartInstance, _irqNumber);
                    return DriverResult::SUCCESS;
                }
                UART_DRV_LOG("[UartDrv] SET_INSTANCE: invalid range\r\n");
                return DriverResult::ERROR_INVALID;
            }
            UART_DRV_LOG("[UartDrv] SET_INSTANCE: no arg or already opened\r\n");
            return DriverResult::ERROR_INVALID;

        case UartIoctl::GET_INSTANCE:
            if (arg)
            {
                *static_cast<hal_uart_instance_t*>(arg) = _uartInstance;
            }
            return DriverResult::SUCCESS;

        case UartIoctl::WAIT_TX_COMPLETE:
            hal_uart_flush_tx_instance(_uartInstance);
            return DriverResult::SUCCESS;

        default:
            return DriverResult::ERROR_NOT_SUPPORTED;
    }
}

// ============================================================================
// DPC Callback
// ============================================================================

void UartDriver::staticDpcCallback(void* context)
{
    auto* driver = static_cast<UartDriver*>(context);
    if (driver)
    {
        driver->handleIrq();
    }
}

void UartDriver::handleIrq()
{
    uint32_t status = hal_uart_get_status(_uartInstance);

    // Clear errors FIRST - overrun can block further reception
    if (status & (HAL_UART_STATUS_RX_OVERRUN | HAL_UART_STATUS_FRAMING_ERR | 
                  HAL_UART_STATUS_PARITY_ERR | HAL_UART_STATUS_NOISE_ERR))
    {
        _stats.rxErrors++;
        hal_uart_clear_errors(_uartInstance);
        // Re-read status after clearing errors
        status = hal_uart_get_status(_uartInstance);
    }

    // Read all available data
    while (status & HAL_UART_STATUS_RX_READY)
    {
        uint8_t byte;
        if (hal_uart_read_byte_instance(_uartInstance, &byte))
        {
            if (!_rxBuffer.push(byte))
            {
                _stats.rxOverflows++;
            }
            _stats.rxBytes++;
        }
        status = hal_uart_get_status(_uartInstance);
    }
}

} // namespace Drivers
} // namespace CRTOS

// ============================================================================
// Module Entry/Exit Points
// ============================================================================

static CRTOS::Drivers::UartDriver* s_moduleInstance = nullptr;

extern "C" {

CRTOS::Drivers::DriverBase* driver_module_init(void)
{
    if (s_moduleInstance == nullptr)
    {
        s_moduleInstance = new CRTOS::Drivers::UartDriver();
        if (s_moduleInstance != nullptr)
        {
            s_moduleInstance->init();
        }
    }
    return s_moduleInstance;
}

void driver_module_exit(CRTOS::Drivers::DriverBase* driver)
{
    if (driver == s_moduleInstance && s_moduleInstance != nullptr)
    {
        s_moduleInstance->deinit();
        delete s_moduleInstance;
        s_moduleInstance = nullptr;
    }
}

// Module metadata
const char __module_name[] __attribute__((used, section(".modinfo"))) = "uart";
const char __module_version[] __attribute__((used, section(".modinfo"))) = "2.0";
const char __module_description[] __attribute__((used, section(".modinfo"))) = "UART Driver v2";

} // extern "C"
