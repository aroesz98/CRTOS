/*
 * SpiDriver.cpp - CRTOS SPI Driver Module Implementation
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
 * SPI driver module - dynamically loadable kernel module.
 * Uses HAL for hardware access to LPSPI controller.
 */

#include "SpiDriver.hpp"
#include "../common/KernelAPI.hpp"
#include <cstring>

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

SpiDriver* SpiDriver::_instance = nullptr;

const DriverInfo SpiDriver::_driverInfo = {
    .name = "spi",
    .description = "LPSPI Master Driver",
    .versionMajor = 1,
    .versionMinor = 0,
    .type = DriverType::CHAR_DEVICE
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

SpiDriver::SpiDriver(hal_spi_instance_t instance)
    : _spiInstance(instance)
{
    _instance = this;

    // Get default configuration
    hal_spi_get_default_config(&_config);

    // Clear statistics
    const auto* api = GetAPI();
    if (api && api->memset)
    {
        api->memset(&_stats, 0, sizeof(_stats));
    }
}

SpiDriver::~SpiDriver()
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

SpiDriver* SpiDriver::getInstance()
{
    return _instance;
}

const DriverInfo* SpiDriver::getInfo() const
{
    return &_driverInfo;
}

// ============================================================================
// Driver Lifecycle
// ============================================================================

DriverResult SpiDriver::init()
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

    // Clear statistics
    if (api->memset)
    {
        api->memset(&_stats, 0, sizeof(_stats));
    }

    _state = DriverState::INITIALIZED;
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::deinit()
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

    _state = DriverState::UNINITIALIZED;
    return DriverResult::SUCCESS;
}

// ============================================================================
// Open / Close
// ============================================================================

DriverResult SpiDriver::open()
{
    if (_state == DriverState::UNINITIALIZED)
    {
        return DriverResult::ERROR_NOT_INIT;
    }

    if (_state == DriverState::OPENED)
    {
        return DriverResult::SUCCESS;
    }

    // Initialize HAL
    if (!hal_spi_init(_spiInstance, &_config))
    {
        return DriverResult::ERROR_GENERIC;
    }

    _state = DriverState::OPENED;
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::close()
{
    if (_state != DriverState::OPENED)
    {
        return DriverResult::SUCCESS;
    }

    // Abort any ongoing transfer
    hal_spi_abort_transfer(_spiInstance);

    // Deinitialize HAL
    hal_spi_deinit(_spiInstance);

    _state = DriverState::INITIALIZED;
    return DriverResult::SUCCESS;
}

// ============================================================================
// Read / Write
// ============================================================================

int32_t SpiDriver::read(void* buffer, size_t size, uint32_t timeoutMs)
{
    if (_state != DriverState::OPENED)
    {
        return static_cast<int32_t>(DriverResult::ERROR_NOT_OPEN);
    }

    if (buffer == nullptr || size == 0)
    {
        return static_cast<int32_t>(DriverResult::ERROR_INVALID);
    }

    // Perform read (TX is dummy bytes, RX is actual data)
    if (!hal_spi_read(_spiInstance, static_cast<uint8_t*>(buffer), size))
    {
        _stats.errorCount++;
        return static_cast<int32_t>(DriverResult::ERROR_GENERIC);
    }

    // Wait for completion if non-blocking mode
    if (_config.transferMode != HAL_SPI_TRANSFER_BLOCKING)
    {
        if (!hal_spi_wait_complete(_spiInstance, timeoutMs))
        {
            _stats.errorCount++;
            return static_cast<int32_t>(DriverResult::ERROR_TIMEOUT);
        }
    }

    _stats.rxBytes += size;
    _stats.transferCount++;
    return static_cast<int32_t>(size);
}

int32_t SpiDriver::write(const void* buffer, size_t size, uint32_t timeoutMs)
{
    if (_state != DriverState::OPENED)
    {
        return static_cast<int32_t>(DriverResult::ERROR_NOT_OPEN);
    }

    if (buffer == nullptr || size == 0)
    {
        return static_cast<int32_t>(DriverResult::ERROR_INVALID);
    }

    // Perform write (TX only, RX is discarded)
    if (!hal_spi_write(_spiInstance, static_cast<const uint8_t*>(buffer), size))
    {
        _stats.errorCount++;
        return static_cast<int32_t>(DriverResult::ERROR_GENERIC);
    }

    // Wait for completion if non-blocking mode
    if (_config.transferMode != HAL_SPI_TRANSFER_BLOCKING)
    {
        if (!hal_spi_wait_complete(_spiInstance, timeoutMs))
        {
            _stats.errorCount++;
            return static_cast<int32_t>(DriverResult::ERROR_TIMEOUT);
        }
    }

    _stats.txBytes += size;
    _stats.transferCount++;
    return static_cast<int32_t>(size);
}

// ============================================================================
// IOCTL
// ============================================================================

DriverResult SpiDriver::ioctl(uint32_t cmd, void* arg)
{
    if (_state == DriverState::UNINITIALIZED)
    {
        return DriverResult::ERROR_NOT_INIT;
    }

    SpiIoctl ioctlCmd = static_cast<SpiIoctl>(cmd);

    switch (ioctlCmd)
    {
        case SpiIoctl::SET_BAUDRATE:
            return handleSetBaudrate(arg);
        case SpiIoctl::GET_BAUDRATE:
            return handleGetBaudrate(arg);
        case SpiIoctl::SET_MODE:
            return handleSetMode(arg);
        case SpiIoctl::GET_MODE:
            return handleGetMode(arg);
        case SpiIoctl::SET_PCS:
            return handleSetPcs(arg);
        case SpiIoctl::GET_PCS:
            return handleGetPcs(arg);
        case SpiIoctl::SET_TRANSFER_MODE:
            return handleSetTransferMode(arg);
        case SpiIoctl::GET_TRANSFER_MODE:
            return handleGetTransferMode(arg);
        case SpiIoctl::SET_BITS_PER_FRAME:
            return handleSetBitsPerFrame(arg);
        case SpiIoctl::GET_BITS_PER_FRAME:
            return handleGetBitsPerFrame(arg);
        case SpiIoctl::SET_PCS_CONTINUOUS:
            return handleSetPcsContinuous(arg);
        case SpiIoctl::GET_STATUS:
            return handleGetStatus(arg);
        case SpiIoctl::ABORT_TRANSFER:
            return handleAbortTransfer();
        case SpiIoctl::WAIT_COMPLETE:
            return handleWaitComplete(arg);
        case SpiIoctl::TRANSFER:
            return handleTransfer(arg);
        case SpiIoctl::GET_CLOCK_FREQ:
            return handleGetClockFreq(arg);
        case SpiIoctl::SET_DELAYS:
            return handleSetDelays(arg);
        case SpiIoctl::GET_DELAYS:
            return handleGetDelays(arg);
        default:
            return DriverResult::ERROR_NOT_SUPPORTED;
    }
}

// ============================================================================
// IOCTL Handlers
// ============================================================================

DriverResult SpiDriver::handleSetBaudrate(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    
    uint32_t* baudRate = static_cast<uint32_t*>(arg);
    uint32_t actual = hal_spi_set_baudrate(_spiInstance, *baudRate);
    _config.baudRate = actual;
    *baudRate = actual;  // Return actual achieved baud rate
    
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::handleGetBaudrate(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    
    *static_cast<uint32_t*>(arg) = _config.baudRate;
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::handleSetMode(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    
    uint8_t mode = *static_cast<uint8_t*>(arg);
    if (mode > 3) return DriverResult::ERROR_INVALID;
    
    hal_spi_mode_t halMode = static_cast<hal_spi_mode_t>(mode);
    if (!hal_spi_set_mode(_spiInstance, halMode))
    {
        return DriverResult::ERROR_GENERIC;
    }
    
    _config.mode = halMode;
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::handleGetMode(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    
    *static_cast<uint8_t*>(arg) = static_cast<uint8_t>(_config.mode);
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::handleSetPcs(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    
    uint8_t pcs = *static_cast<uint8_t*>(arg);
    if (pcs > 3) return DriverResult::ERROR_INVALID;
    
    hal_spi_pcs_t halPcs = static_cast<hal_spi_pcs_t>(pcs);
    if (!hal_spi_set_pcs(_spiInstance, halPcs))
    {
        return DriverResult::ERROR_GENERIC;
    }
    
    _config.pcs = halPcs;
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::handleGetPcs(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    
    *static_cast<uint8_t*>(arg) = static_cast<uint8_t>(_config.pcs);
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::handleSetTransferMode(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    
    SpiTransferMode mode = *static_cast<SpiTransferMode*>(arg);
    hal_spi_transfer_mode_t halMode;
    
    switch (mode)
    {
        case SpiTransferMode::Blocking:
            halMode = HAL_SPI_TRANSFER_BLOCKING;
            break;
        case SpiTransferMode::Interrupt:
            halMode = HAL_SPI_TRANSFER_INTERRUPT;
            break;
        case SpiTransferMode::DMA:
            halMode = HAL_SPI_TRANSFER_DMA;
            break;
        default:
            return DriverResult::ERROR_INVALID;
    }
    
    if (!hal_spi_set_transfer_mode(_spiInstance, halMode))
    {
        return DriverResult::ERROR_GENERIC;
    }
    
    _config.transferMode = halMode;
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::handleGetTransferMode(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    
    SpiTransferMode mode;
    switch (_config.transferMode)
    {
        case HAL_SPI_TRANSFER_BLOCKING:
            mode = SpiTransferMode::Blocking;
            break;
        case HAL_SPI_TRANSFER_INTERRUPT:
            mode = SpiTransferMode::Interrupt;
            break;
        case HAL_SPI_TRANSFER_DMA:
            mode = SpiTransferMode::DMA;
            break;
        default:
            mode = SpiTransferMode::Blocking;
            break;
    }
    
    *static_cast<SpiTransferMode*>(arg) = mode;
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::handleSetBitsPerFrame(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    
    uint8_t bits = *static_cast<uint8_t*>(arg);
    if (bits < 8 || bits > 32) return DriverResult::ERROR_INVALID;
    
    _config.bitsPerFrame = bits;
    // Note: This requires reinit to take effect
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::handleGetBitsPerFrame(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    
    *static_cast<uint8_t*>(arg) = _config.bitsPerFrame;
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::handleSetPcsContinuous(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    
    _config.pcsContinuous = *static_cast<bool*>(arg);
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::handleGetStatus(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    
    hal_spi_status_t halStatus = hal_spi_get_status(_spiInstance);
    SpiStatus status;
    
    switch (halStatus)
    {
        case HAL_SPI_STATUS_IDLE:
            status = SpiStatus::Idle;
            break;
        case HAL_SPI_STATUS_BUSY:
            status = SpiStatus::Busy;
            break;
        case HAL_SPI_STATUS_COMPLETE:
            status = SpiStatus::Complete;
            break;
        case HAL_SPI_STATUS_ERROR:
            status = SpiStatus::Error;
            break;
        default:
            status = SpiStatus::Idle;
            break;
    }
    
    *static_cast<SpiStatus*>(arg) = status;
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::handleAbortTransfer()
{
    hal_spi_abort_transfer(_spiInstance);
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::handleWaitComplete(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    
    SpiWaitArg* waitArg = static_cast<SpiWaitArg*>(arg);
    waitArg->success = hal_spi_wait_complete(_spiInstance, waitArg->timeoutMs);
    
    return waitArg->success ? DriverResult::SUCCESS : DriverResult::ERROR_TIMEOUT;
}

DriverResult SpiDriver::handleTransfer(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    if (_state != DriverState::OPENED) return DriverResult::ERROR_NOT_OPEN;
    
    SpiTransferArg* xferArg = static_cast<SpiTransferArg*>(arg);
    
    hal_spi_transfer_t transfer;
    transfer.txData = xferArg->txData;
    transfer.rxData = xferArg->rxData;
    transfer.dataSize = xferArg->dataSize;
    
    bool success;
    if (xferArg->blocking)
    {
        success = hal_spi_transfer_blocking(_spiInstance, &transfer);
    }
    else
    {
        success = hal_spi_transfer_nonblocking(_spiInstance, &transfer, nullptr, nullptr);
        if (success && xferArg->timeoutMs > 0)
        {
            success = hal_spi_wait_complete(_spiInstance, xferArg->timeoutMs);
        }
    }
    
    if (success)
    {
        _stats.transferCount++;
        if (xferArg->txData) _stats.txBytes += xferArg->dataSize;
        if (xferArg->rxData) _stats.rxBytes += xferArg->dataSize;
        return DriverResult::SUCCESS;
    }
    
    _stats.errorCount++;
    return DriverResult::ERROR_GENERIC;
}

DriverResult SpiDriver::handleGetClockFreq(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    
    *static_cast<uint32_t*>(arg) = hal_spi_get_clock_freq(_spiInstance);
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::handleSetDelays(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    
    SpiDelays* delays = static_cast<SpiDelays*>(arg);
    _config.pcsToSckDelayNs = delays->pcsToSckNs;
    _config.sckToPcsDelayNs = delays->sckToPcsNs;
    _config.betweenTransferDelayNs = delays->betweenTransferNs;
    // Note: This requires reinit to take effect
    
    return DriverResult::SUCCESS;
}

DriverResult SpiDriver::handleGetDelays(void* arg)
{
    if (arg == nullptr) return DriverResult::ERROR_INVALID;
    
    SpiDelays* delays = static_cast<SpiDelays*>(arg);
    delays->pcsToSckNs = _config.pcsToSckDelayNs;
    delays->sckToPcsNs = _config.sckToPcsDelayNs;
    delays->betweenTransferNs = _config.betweenTransferDelayNs;
    
    return DriverResult::SUCCESS;
}

// ============================================================================
// Module Entry Points
// ============================================================================

extern "C" DriverBase* driver_module_init()
{
    // Create driver instance (uses SPI1 by default)
    return new SpiDriver(HAL_SPI_INSTANCE_1);
}

extern "C" void driver_module_exit(DriverBase* driver)
{
    if (driver)
    {
        delete driver;
    }
}

} // namespace Drivers
} // namespace CRTOS
