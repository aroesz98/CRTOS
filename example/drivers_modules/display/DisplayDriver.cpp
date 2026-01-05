/*
 * DisplayDriver.cpp - CRTOS Display Driver Module Implementation
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
 * Display driver module - dynamically loadable kernel module.
 * Uses HAL for hardware access and DPC for VSYNC handling.
 */

#include "DisplayDriver.hpp"
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

DisplayDriver* DisplayDriver::_instance = nullptr;

const DriverInfo DisplayDriver::_driverInfo = {
    .name = "display",
    .description = "eLCDIF Display Driver",
    .versionMajor = 1,
    .versionMinor = 0,
    .type = DriverType::DISPLAY
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

DisplayDriver::DisplayDriver()
{
    _instance = this;

    // Default configuration
    hal_display_get_default_config(&_config);

    // Clear statistics
    const auto* api = GetAPI();
    if (api && api->memset)
    {
        api->memset(&_stats, 0, sizeof(_stats));
    }
}

DisplayDriver::~DisplayDriver()
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

DisplayDriver* DisplayDriver::getInstance()
{
    return _instance;
}

const DriverInfo* DisplayDriver::getInfo() const
{
    return &_driverInfo;
}

// ============================================================================
// Driver Lifecycle
// ============================================================================

DriverResult DisplayDriver::init()
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

DriverResult DisplayDriver::deinit()
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

DriverResult DisplayDriver::open()
{
    if (_state == DriverState::UNINITIALIZED)
    {
        return DriverResult::ERROR_NOT_INIT;
    }

    if (_state == DriverState::OPENED)
    {
        _openCount++;
        return DriverResult::SUCCESS;
    }

    const auto* api = GetAPI();
    if (api == nullptr)
    {
        return DriverResult::ERROR_GENERIC;
    }

    // Check if display is already initialized by kernel
    // (Display is typically initialized during system startup)
    if (!hal_display_is_initialized())
    {
        // Initialize display with current config
        if (!hal_display_init(&_config))
        {
            return DriverResult::ERROR_GENERIC;
        }
    }

    // NOTE: We do NOT register our own DPC handler for VSYNC here!
    // The HAL (GlobalDisplay) already handles VSYNC interrupts and provides
    // synchronization via hal_display_wait_vsync(). Registering a second
    // handler would cause conflicts - our module callback would be invoked
    // from kernel context without proper GOT setup, causing crashes.
    //
    // VSYNC waiting is done via hal_display_wait_vsync() which uses the
    // HAL's internal semaphore managed by GlobalDisplay.

    // Enable display output
    hal_display_enable();

    _openCount = 1;
    _state = DriverState::OPENED;

    return DriverResult::SUCCESS;
}

DriverResult DisplayDriver::close()
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

    // NOTE: We don't need to unregister DPC handlers here since we don't
    // register any in open(). The HAL manages VSYNC interrupts internally.
    
    // Note: We don't deinitialize the display hardware here
    // as it may be shared with kernel GUI components

    _openCount = 0;
    _state = DriverState::INITIALIZED;

    return DriverResult::SUCCESS;
}

// ============================================================================
// Read / Write
// ============================================================================

int32_t DisplayDriver::read(void* buffer, size_t size, uint32_t timeoutMs)
{
    (void)timeoutMs;

    if (_state != DriverState::OPENED)
    {
        return static_cast<int32_t>(DriverResult::ERROR_NOT_OPEN);
    }

    if (buffer == nullptr || size == 0)
    {
        return static_cast<int32_t>(DriverResult::ERROR_INVALID);
    }

    // Read from framebuffer
    uint32_t* fb = hal_display_get_framebuffer();
    if (fb == nullptr)
    {
        return static_cast<int32_t>(DriverResult::ERROR_GENERIC);
    }

    uint32_t fbSize = hal_display_get_width() * hal_display_get_height() * 
                      hal_display_get_bytes_per_pixel();
    size_t bytesToRead = (size < fbSize) ? size : fbSize;

    const auto* api = GetAPI();
    if (api && api->memcpy)
    {
        api->memcpy(buffer, fb, bytesToRead);
    }

    return static_cast<int32_t>(bytesToRead);
}

int32_t DisplayDriver::write(const void* buffer, size_t size, uint32_t timeoutMs)
{
    (void)timeoutMs;

    if (_state != DriverState::OPENED)
    {
        return static_cast<int32_t>(DriverResult::ERROR_NOT_OPEN);
    }

    if (buffer == nullptr || size == 0)
    {
        return static_cast<int32_t>(DriverResult::ERROR_INVALID);
    }

    // Write to back buffer (or framebuffer if single buffered)
    uint32_t* bb = hal_display_get_backbuffer();
    if (bb == nullptr)
    {
        bb = hal_display_get_framebuffer();
    }
    
    if (bb == nullptr)
    {
        return static_cast<int32_t>(DriverResult::ERROR_GENERIC);
    }

    uint32_t fbSize = hal_display_get_width() * hal_display_get_height() * 
                      hal_display_get_bytes_per_pixel();
    size_t bytesToWrite = (size < fbSize) ? size : fbSize;

    const auto* api = GetAPI();
    if (api && api->memcpy)
    {
        api->memcpy(bb, buffer, bytesToWrite);
    }

    _stats.frameCount++;

    return static_cast<int32_t>(bytesToWrite);
}

// ============================================================================
// IOCTL
// ============================================================================

DriverResult DisplayDriver::ioctl(uint32_t cmd, void* arg)
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

    // Handle Display-specific IOCTLs
    switch (static_cast<DisplayIoctl>(cmd))
    {
        case DisplayIoctl::GET_WIDTH:
            if (arg)
            {
                *static_cast<uint16_t*>(arg) = hal_display_get_width();
            }
            return DriverResult::SUCCESS;

        case DisplayIoctl::GET_HEIGHT:
            if (arg)
            {
                *static_cast<uint16_t*>(arg) = hal_display_get_height();
            }
            return DriverResult::SUCCESS;

        case DisplayIoctl::GET_FORMAT:
            if (arg)
            {
                *static_cast<hal_display_format_t*>(arg) = hal_display_get_format();
            }
            return DriverResult::SUCCESS;

        case DisplayIoctl::GET_BYTES_PER_PIXEL:
            if (arg)
            {
                *static_cast<uint32_t*>(arg) = hal_display_get_bytes_per_pixel();
            }
            return DriverResult::SUCCESS;

        case DisplayIoctl::GET_FRAMEBUFFER:
            if (arg)
            {
                *static_cast<uint32_t**>(arg) = hal_display_get_framebuffer();
            }
            return DriverResult::SUCCESS;

        case DisplayIoctl::GET_BACKBUFFER:
            if (arg)
            {
                *static_cast<uint32_t**>(arg) = hal_display_get_backbuffer();
            }
            return DriverResult::SUCCESS;

        case DisplayIoctl::SWAP_BUFFERS:
            if (hal_display_swap_buffers())
            {
                _stats.swapCount++;
                return DriverResult::SUCCESS;
            }
            return DriverResult::ERROR_GENERIC;

        case DisplayIoctl::FLUSH_CACHE:
            hal_display_flush_cache();
            return DriverResult::SUCCESS;

        case DisplayIoctl::WAIT_VSYNC:
            if (arg)
            {
                auto* vsArg = static_cast<DisplayVSyncArg*>(arg);
                vsArg->success = hal_display_wait_vsync(vsArg->timeout_ms);
                if (vsArg->success)
                {
                    _stats.vsyncCount++;
                }
            }
            else
            {
                hal_display_wait_vsync(0);
                _stats.vsyncCount++;
            }
            return DriverResult::SUCCESS;

        case DisplayIoctl::CLEAR:
            if (arg)
            {
                auto* clearArg = static_cast<DisplayClearArg*>(arg);
                hal_display_clear(clearArg->color);
            }
            else
            {
                hal_display_clear(0);  // Clear to black
            }
            return DriverResult::SUCCESS;

        case DisplayIoctl::FILL_RECT:
            if (arg)
            {
                auto* fillArg = static_cast<DisplayFillRectArg*>(arg);
                hal_display_rect_t halRect = {
                    .x = fillArg->rect.x,
                    .y = fillArg->rect.y,
                    .width = fillArg->rect.width,
                    .height = fillArg->rect.height
                };
                hal_display_fill_rect(&halRect, fillArg->color);
            }
            return DriverResult::SUCCESS;

        case DisplayIoctl::ENABLE:
            hal_display_enable();
            return DriverResult::SUCCESS;

        case DisplayIoctl::DISABLE:
            hal_display_disable();
            return DriverResult::SUCCESS;

        case DisplayIoctl::GET_STATUS:
            if (arg)
            {
                *static_cast<uint32_t*>(arg) = hal_display_get_status();
            }
            return DriverResult::SUCCESS;

        default:
            return DriverResult::ERROR_NOT_SUPPORTED;
    }
}

} // namespace Drivers
} // namespace CRTOS

// ============================================================================
// Module Entry/Exit Points
// ============================================================================

static CRTOS::Drivers::DisplayDriver* s_moduleInstance = nullptr;

extern "C" {

CRTOS::Drivers::DriverBase* driver_module_init(void)
{
    if (s_moduleInstance == nullptr)
    {
        s_moduleInstance = new CRTOS::Drivers::DisplayDriver();
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
const char __module_name[] __attribute__((used, section(".modinfo"))) = "display";
const char __module_version[] __attribute__((used, section(".modinfo"))) = "1.0";
const char __module_description[] __attribute__((used, section(".modinfo"))) = "eLCDIF Display Driver";

} // extern "C"
