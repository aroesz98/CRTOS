/*
 * PinMuxDriver.cpp - CRTOS PinMux Driver Module Implementation
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
 * PinMux driver module - dynamically loadable kernel module.
 * Uses HAL for hardware access to IOMUXC controller.
 * Provides Linux Device Tree-like pin configuration through JSON files.
 */

#include "PinMuxDriver.hpp"
#include "../common/KernelAPI.hpp"

// Debug macro for PinMux driver - uses kernel API printf
#define PINMUX_DRV_DEBUG 0
#if PINMUX_DRV_DEBUG
#define PINMUX_DRV_LOG(...) do { auto* _api = GetAPI(); if (_api && _api->printf) _api->printf(__VA_ARGS__); } while(0)
#else
#define PINMUX_DRV_LOG(...)
#endif

// i.MX RT1052 IOMUXC base addresses
#define IOMUXC_BASE         0x401F8000U
#define IOMUXC_GPR_BASE     0x400AC000U

// Pin register offsets from IOMUXC_BASE
#define IOMUXC_SW_MUX_CTL_OFFSET    0x0014U
#define IOMUXC_SW_PAD_CTL_OFFSET    0x0204U

namespace CRTOS
{
namespace Drivers
{

// ============================================================================
// Kernel API Access
// ============================================================================

extern "C" const CRTOS::KernelAPI* crtos_get_kernel_api(void) __attribute__((weak));

// NOTE: Do NOT cache the API pointer in a static variable for dynamic modules!
// The .bss section may not be properly zeroed when the module is loaded,
// causing the cached pointer to contain garbage.
static inline const CRTOS::KernelAPI* GetAPI()
{
    if (crtos_get_kernel_api != nullptr)
    {
        return crtos_get_kernel_api();
    }
    return nullptr;
}

// ============================================================================
// Pin Database
// i.MX RT1052 Pin Definitions - register addresses for common pins
// ============================================================================

// Macro to define a pin entry with empty ALT function names (loaded from JSON at runtime)
// Fields: name, muxRegister (offset), padRegister (offset), gpioPort, gpioPin, currentAlt, inUse, claimedBy, altFunctions[8]
// muxRegister and padRegister store OFFSETS relative to IOMUXC_BASE, not absolute addresses
#define PIN_ENTRY(name, mux_offset, pad_offset, port, pin) \
    { name, mux_offset, pad_offset, port, pin, 0, false, "", {"", "", "", "", "", "", "", ""} }

// GPIO_AD_B0 pins (commonly used for LPSPI, UART, etc.)
// Offsets extracted from fsl_iomuxc.h - base is 0x401F8000
static const PinDatabaseEntry s_defaultPinDatabase[] = {
    // GPIO_AD_B0 bank - AD pins 0-15 (GPIO1_0-15)
    PIN_ENTRY("GPIO_AD_B0_00", 0x00BC, 0x02AC, HAL_GPIO_PORT_1, 0),
    PIN_ENTRY("GPIO_AD_B0_01", 0x00C0, 0x02B0, HAL_GPIO_PORT_1, 1),
    PIN_ENTRY("GPIO_AD_B0_02", 0x00C4, 0x02B4, HAL_GPIO_PORT_1, 2),
    PIN_ENTRY("GPIO_AD_B0_03", 0x00C8, 0x02B8, HAL_GPIO_PORT_1, 3),
    PIN_ENTRY("GPIO_AD_B0_04", 0x00CC, 0x02BC, HAL_GPIO_PORT_1, 4),
    PIN_ENTRY("GPIO_AD_B0_05", 0x00D0, 0x02C0, HAL_GPIO_PORT_1, 5),
    PIN_ENTRY("GPIO_AD_B0_06", 0x00D4, 0x02C4, HAL_GPIO_PORT_1, 6),
    PIN_ENTRY("GPIO_AD_B0_07", 0x00D8, 0x02C8, HAL_GPIO_PORT_1, 7),
    PIN_ENTRY("GPIO_AD_B0_08", 0x00DC, 0x02CC, HAL_GPIO_PORT_1, 8),
    PIN_ENTRY("GPIO_AD_B0_09", 0x00E0, 0x02D0, HAL_GPIO_PORT_1, 9),
    PIN_ENTRY("GPIO_AD_B0_10", 0x00E4, 0x02D4, HAL_GPIO_PORT_1, 10),
    PIN_ENTRY("GPIO_AD_B0_11", 0x00E8, 0x02D8, HAL_GPIO_PORT_1, 11),
    PIN_ENTRY("GPIO_AD_B0_12", 0x00EC, 0x02DC, HAL_GPIO_PORT_1, 12),
    PIN_ENTRY("GPIO_AD_B0_13", 0x00F0, 0x02E0, HAL_GPIO_PORT_1, 13),
    PIN_ENTRY("GPIO_AD_B0_14", 0x00F4, 0x02E4, HAL_GPIO_PORT_1, 14),
    PIN_ENTRY("GPIO_AD_B0_15", 0x00F8, 0x02E8, HAL_GPIO_PORT_1, 15),
    
    // GPIO_AD_B1 bank - AD pins 16-31 (GPIO1_16-31)
    PIN_ENTRY("GPIO_AD_B1_00", 0x00FC, 0x02EC, HAL_GPIO_PORT_1, 16),
    PIN_ENTRY("GPIO_AD_B1_01", 0x0100, 0x02F0, HAL_GPIO_PORT_1, 17),
    PIN_ENTRY("GPIO_AD_B1_02", 0x0104, 0x02F4, HAL_GPIO_PORT_1, 18),
    PIN_ENTRY("GPIO_AD_B1_03", 0x0108, 0x02F8, HAL_GPIO_PORT_1, 19),
    PIN_ENTRY("GPIO_AD_B1_04", 0x010C, 0x02FC, HAL_GPIO_PORT_1, 20),
    PIN_ENTRY("GPIO_AD_B1_05", 0x0110, 0x0300, HAL_GPIO_PORT_1, 21),
    PIN_ENTRY("GPIO_AD_B1_06", 0x0114, 0x0304, HAL_GPIO_PORT_1, 22),
    PIN_ENTRY("GPIO_AD_B1_07", 0x0118, 0x0308, HAL_GPIO_PORT_1, 23),
    PIN_ENTRY("GPIO_AD_B1_08", 0x011C, 0x030C, HAL_GPIO_PORT_1, 24),
    PIN_ENTRY("GPIO_AD_B1_09", 0x0120, 0x0310, HAL_GPIO_PORT_1, 25),
    PIN_ENTRY("GPIO_AD_B1_10", 0x0124, 0x0314, HAL_GPIO_PORT_1, 26),
    PIN_ENTRY("GPIO_AD_B1_11", 0x0128, 0x0318, HAL_GPIO_PORT_1, 27),
    PIN_ENTRY("GPIO_AD_B1_12", 0x012C, 0x031C, HAL_GPIO_PORT_1, 28),
    PIN_ENTRY("GPIO_AD_B1_13", 0x0130, 0x0320, HAL_GPIO_PORT_1, 29),
    PIN_ENTRY("GPIO_AD_B1_14", 0x0134, 0x0324, HAL_GPIO_PORT_1, 30),
    PIN_ENTRY("GPIO_AD_B1_15", 0x0138, 0x0328, HAL_GPIO_PORT_1, 31),
    
    // GPIO_B0 bank (LCD/flexSPI) - GPIO2_0-15
    PIN_ENTRY("GPIO_B0_00", 0x013C, 0x032C, HAL_GPIO_PORT_2, 0),
    PIN_ENTRY("GPIO_B0_01", 0x0140, 0x0330, HAL_GPIO_PORT_2, 1),
    PIN_ENTRY("GPIO_B0_02", 0x0144, 0x0334, HAL_GPIO_PORT_2, 2),
    PIN_ENTRY("GPIO_B0_03", 0x0148, 0x0338, HAL_GPIO_PORT_2, 3),
    PIN_ENTRY("GPIO_B0_04", 0x014C, 0x033C, HAL_GPIO_PORT_2, 4),
    PIN_ENTRY("GPIO_B0_05", 0x0150, 0x0340, HAL_GPIO_PORT_2, 5),
    PIN_ENTRY("GPIO_B0_06", 0x0154, 0x0344, HAL_GPIO_PORT_2, 6),
    PIN_ENTRY("GPIO_B0_07", 0x0158, 0x0348, HAL_GPIO_PORT_2, 7),
    PIN_ENTRY("GPIO_B0_08", 0x015C, 0x034C, HAL_GPIO_PORT_2, 8),
    PIN_ENTRY("GPIO_B0_09", 0x0160, 0x0350, HAL_GPIO_PORT_2, 9),
    PIN_ENTRY("GPIO_B0_10", 0x0164, 0x0354, HAL_GPIO_PORT_2, 10),
    PIN_ENTRY("GPIO_B0_11", 0x0168, 0x0358, HAL_GPIO_PORT_2, 11),
    PIN_ENTRY("GPIO_B0_12", 0x016C, 0x035C, HAL_GPIO_PORT_2, 12),
    PIN_ENTRY("GPIO_B0_13", 0x0170, 0x0360, HAL_GPIO_PORT_2, 13),
    PIN_ENTRY("GPIO_B0_14", 0x0174, 0x0364, HAL_GPIO_PORT_2, 14),
    PIN_ENTRY("GPIO_B0_15", 0x0178, 0x0368, HAL_GPIO_PORT_2, 15),
    
    // GPIO_B1 bank - GPIO2_16-31
    PIN_ENTRY("GPIO_B1_00", 0x017C, 0x036C, HAL_GPIO_PORT_2, 16),
    PIN_ENTRY("GPIO_B1_01", 0x0180, 0x0370, HAL_GPIO_PORT_2, 17),
    PIN_ENTRY("GPIO_B1_02", 0x0184, 0x0374, HAL_GPIO_PORT_2, 18),
    PIN_ENTRY("GPIO_B1_03", 0x0188, 0x0378, HAL_GPIO_PORT_2, 19),
    PIN_ENTRY("GPIO_B1_04", 0x018C, 0x037C, HAL_GPIO_PORT_2, 20),
    PIN_ENTRY("GPIO_B1_05", 0x0190, 0x0380, HAL_GPIO_PORT_2, 21),
    PIN_ENTRY("GPIO_B1_06", 0x0194, 0x0384, HAL_GPIO_PORT_2, 22),
    PIN_ENTRY("GPIO_B1_07", 0x0198, 0x0388, HAL_GPIO_PORT_2, 23),
    PIN_ENTRY("GPIO_B1_08", 0x019C, 0x038C, HAL_GPIO_PORT_2, 24),
    PIN_ENTRY("GPIO_B1_09", 0x01A0, 0x0390, HAL_GPIO_PORT_2, 25),
    PIN_ENTRY("GPIO_B1_10", 0x01A4, 0x0394, HAL_GPIO_PORT_2, 26),
    PIN_ENTRY("GPIO_B1_11", 0x01A8, 0x0398, HAL_GPIO_PORT_2, 27),
    PIN_ENTRY("GPIO_B1_12", 0x01AC, 0x039C, HAL_GPIO_PORT_2, 28),
    PIN_ENTRY("GPIO_B1_13", 0x01B0, 0x03A0, HAL_GPIO_PORT_2, 29),
    PIN_ENTRY("GPIO_B1_14", 0x01B4, 0x03A4, HAL_GPIO_PORT_2, 30),
    PIN_ENTRY("GPIO_B1_15", 0x01B8, 0x03A8, HAL_GPIO_PORT_2, 31),
    
    // GPIO_SD_B0 bank (SD card) - GPIO3_12-17
    PIN_ENTRY("GPIO_SD_B0_00", 0x01BC, 0x03AC, HAL_GPIO_PORT_3, 12),
    PIN_ENTRY("GPIO_SD_B0_01", 0x01C0, 0x03B0, HAL_GPIO_PORT_3, 13),
    PIN_ENTRY("GPIO_SD_B0_02", 0x01C4, 0x03B4, HAL_GPIO_PORT_3, 14),
    PIN_ENTRY("GPIO_SD_B0_03", 0x01C8, 0x03B8, HAL_GPIO_PORT_3, 15),
    PIN_ENTRY("GPIO_SD_B0_04", 0x01CC, 0x03BC, HAL_GPIO_PORT_3, 16),
    PIN_ENTRY("GPIO_SD_B0_05", 0x01D0, 0x03C0, HAL_GPIO_PORT_3, 17),
    
    // GPIO_SD_B1 bank - GPIO3_0-11
    PIN_ENTRY("GPIO_SD_B1_00", 0x01D4, 0x03C4, HAL_GPIO_PORT_3, 0),
    PIN_ENTRY("GPIO_SD_B1_01", 0x01D8, 0x03C8, HAL_GPIO_PORT_3, 1),
    PIN_ENTRY("GPIO_SD_B1_02", 0x01DC, 0x03CC, HAL_GPIO_PORT_3, 2),
    PIN_ENTRY("GPIO_SD_B1_03", 0x01E0, 0x03D0, HAL_GPIO_PORT_3, 3),
    PIN_ENTRY("GPIO_SD_B1_04", 0x01E4, 0x03D4, HAL_GPIO_PORT_3, 4),
    PIN_ENTRY("GPIO_SD_B1_05", 0x01E8, 0x03D8, HAL_GPIO_PORT_3, 5),
    PIN_ENTRY("GPIO_SD_B1_06", 0x01EC, 0x03DC, HAL_GPIO_PORT_3, 6),
    PIN_ENTRY("GPIO_SD_B1_07", 0x01F0, 0x03E0, HAL_GPIO_PORT_3, 7),
    PIN_ENTRY("GPIO_SD_B1_08", 0x01F4, 0x03E4, HAL_GPIO_PORT_3, 8),
    PIN_ENTRY("GPIO_SD_B1_09", 0x01F8, 0x03E8, HAL_GPIO_PORT_3, 9),
    PIN_ENTRY("GPIO_SD_B1_10", 0x01FC, 0x03EC, HAL_GPIO_PORT_3, 10),
    PIN_ENTRY("GPIO_SD_B1_11", 0x0200, 0x03F0, HAL_GPIO_PORT_3, 11),

    // GPIO_EMC bank - EMC pins (memory controller) - GPIO4
    // Note: GPIO_EMC starts at offset 0x0014, PAD at 0x0204
    PIN_ENTRY("GPIO_EMC_00", 0x0014, 0x0204, HAL_GPIO_PORT_4, 0),
    PIN_ENTRY("GPIO_EMC_01", 0x0018, 0x0208, HAL_GPIO_PORT_4, 1),
    PIN_ENTRY("GPIO_EMC_02", 0x001C, 0x020C, HAL_GPIO_PORT_4, 2),
    PIN_ENTRY("GPIO_EMC_03", 0x0020, 0x0210, HAL_GPIO_PORT_4, 3),
    PIN_ENTRY("GPIO_EMC_04", 0x0024, 0x0214, HAL_GPIO_PORT_4, 4),
    PIN_ENTRY("GPIO_EMC_05", 0x0028, 0x0218, HAL_GPIO_PORT_4, 5),
    PIN_ENTRY("GPIO_EMC_06", 0x002C, 0x021C, HAL_GPIO_PORT_4, 6),
    PIN_ENTRY("GPIO_EMC_07", 0x0030, 0x0220, HAL_GPIO_PORT_4, 7),
    PIN_ENTRY("GPIO_EMC_08", 0x0034, 0x0224, HAL_GPIO_PORT_4, 8),
    PIN_ENTRY("GPIO_EMC_09", 0x0038, 0x0228, HAL_GPIO_PORT_4, 9),
    PIN_ENTRY("GPIO_EMC_10", 0x003C, 0x022C, HAL_GPIO_PORT_4, 10),
    PIN_ENTRY("GPIO_EMC_11", 0x0040, 0x0230, HAL_GPIO_PORT_4, 11),
    PIN_ENTRY("GPIO_EMC_12", 0x0044, 0x0234, HAL_GPIO_PORT_4, 12),
    PIN_ENTRY("GPIO_EMC_13", 0x0048, 0x0238, HAL_GPIO_PORT_4, 13),
    PIN_ENTRY("GPIO_EMC_14", 0x004C, 0x023C, HAL_GPIO_PORT_4, 14),
    PIN_ENTRY("GPIO_EMC_15", 0x0050, 0x0240, HAL_GPIO_PORT_4, 15),
    PIN_ENTRY("GPIO_EMC_16", 0x0054, 0x0244, HAL_GPIO_PORT_4, 16),
    PIN_ENTRY("GPIO_EMC_17", 0x0058, 0x0248, HAL_GPIO_PORT_4, 17),
    PIN_ENTRY("GPIO_EMC_18", 0x005C, 0x024C, HAL_GPIO_PORT_4, 18),
    PIN_ENTRY("GPIO_EMC_19", 0x0060, 0x0250, HAL_GPIO_PORT_4, 19),
    PIN_ENTRY("GPIO_EMC_20", 0x0064, 0x0254, HAL_GPIO_PORT_4, 20),
    PIN_ENTRY("GPIO_EMC_21", 0x0068, 0x0258, HAL_GPIO_PORT_4, 21),
    PIN_ENTRY("GPIO_EMC_22", 0x006C, 0x025C, HAL_GPIO_PORT_4, 22),
    PIN_ENTRY("GPIO_EMC_23", 0x0070, 0x0260, HAL_GPIO_PORT_4, 23),
    PIN_ENTRY("GPIO_EMC_24", 0x0074, 0x0264, HAL_GPIO_PORT_4, 24),
    PIN_ENTRY("GPIO_EMC_25", 0x0078, 0x0268, HAL_GPIO_PORT_4, 25),
    PIN_ENTRY("GPIO_EMC_26", 0x007C, 0x026C, HAL_GPIO_PORT_4, 26),
    PIN_ENTRY("GPIO_EMC_27", 0x0080, 0x0270, HAL_GPIO_PORT_4, 27),
    PIN_ENTRY("GPIO_EMC_28", 0x0084, 0x0274, HAL_GPIO_PORT_4, 28),
    PIN_ENTRY("GPIO_EMC_29", 0x0088, 0x0278, HAL_GPIO_PORT_4, 29),
    PIN_ENTRY("GPIO_EMC_30", 0x008C, 0x027C, HAL_GPIO_PORT_4, 30),
    PIN_ENTRY("GPIO_EMC_31", 0x0090, 0x0280, HAL_GPIO_PORT_4, 31),
    PIN_ENTRY("GPIO_EMC_32", 0x0094, 0x0284, HAL_GPIO_PORT_3, 18),
    PIN_ENTRY("GPIO_EMC_33", 0x0098, 0x0288, HAL_GPIO_PORT_3, 19),
    PIN_ENTRY("GPIO_EMC_34", 0x009C, 0x028C, HAL_GPIO_PORT_3, 20),
    PIN_ENTRY("GPIO_EMC_35", 0x00A0, 0x0290, HAL_GPIO_PORT_3, 21),
    PIN_ENTRY("GPIO_EMC_36", 0x00A4, 0x0294, HAL_GPIO_PORT_3, 22),
    PIN_ENTRY("GPIO_EMC_37", 0x00A8, 0x0298, HAL_GPIO_PORT_3, 23),
    PIN_ENTRY("GPIO_EMC_38", 0x00AC, 0x029C, HAL_GPIO_PORT_3, 24),
    PIN_ENTRY("GPIO_EMC_39", 0x00B0, 0x02A0, HAL_GPIO_PORT_3, 25),
    PIN_ENTRY("GPIO_EMC_40", 0x00B4, 0x02A4, HAL_GPIO_PORT_3, 26),
    PIN_ENTRY("GPIO_EMC_41", 0x00B8, 0x02A8, HAL_GPIO_PORT_3, 27),
};

static const size_t s_defaultPinCount = sizeof(s_defaultPinDatabase) / sizeof(s_defaultPinDatabase[0]);

// ============================================================================
// ALT Function Names Configuration (loaded from JSON or defaults)
// ============================================================================

// NOTE: ALT function names are now stored per-pin in PinDatabaseEntry::altFunctions
// This allows precise function names from JSON for each individual pin

// Configuration file path
static const char* const CONFIG_FILE_PATH = "/config/pinmux.json";

// ============================================================================
// Static Member Initialization
// ============================================================================

PinMuxDriver* PinMuxDriver::_instance = nullptr;

const DriverInfo PinMuxDriver::_driverInfo = {
    .name = "pinmux",
    .description = "Pin Multiplexer Driver (Device Tree-like)",
    .versionMajor = 1,
    .versionMinor = 0,
    .type = DriverType::CHAR_DEVICE
};

// ============================================================================
// String comparison (no libc dependency)
// ============================================================================

static int stringCompare(const char* s1, const char* s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

// Get ALT function name for a pin from its database entry
static const char* getAltFunctionName(const PinDatabaseEntry& pin, uint8_t altMode)
{
    if (altMode > 7) return nullptr;
    
    const char* funcName = pin.altFunctions[altMode];
    if (funcName[0] != '\0' && funcName[0] != '-')
    {
        return funcName;
    }
    
    return nullptr;
}

// Check if the ALT function is a GPIO function (pin is free)
static bool isGpioFunction(const char* funcName)
{
    if (funcName == nullptr || funcName[0] == '\0') return true;
    
    // GPIO1, GPIO2, GPIO3, GPIO4, GPIO5 are all GPIO modes
    if (funcName[0] == 'G' && funcName[1] == 'P' && funcName[2] == 'I' && funcName[3] == 'O')
    {
        return true;
    }
    return false;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

PinMuxDriver::PinMuxDriver()
    : _pinDatabase(nullptr)
    , _numPins(0)
{
    _instance = this;
    
    const auto* api = GetAPI();
    if (api && api->memset)
    {
        api->memset(&_stats, 0, sizeof(_stats));
        api->memset(_configPath, 0, sizeof(_configPath));
    }
}

PinMuxDriver::~PinMuxDriver()
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

PinMuxDriver* PinMuxDriver::getInstance()
{
    return _instance;
}

const DriverInfo* PinMuxDriver::getInfo() const
{
    return &_driverInfo;
}

// ============================================================================
// Driver Lifecycle
// ============================================================================

DriverResult PinMuxDriver::init()
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

    // Initialize HAL
    if (!hal_pinmux_init())
    {
        return DriverResult::ERROR_GENERIC;
    }

    // Initialize pin database
    initPinDatabase();

    _state = DriverState::INITIALIZED;
    return DriverResult::SUCCESS;
}

DriverResult PinMuxDriver::deinit()
{
    if (_state == DriverState::UNINITIALIZED)
    {
        return DriverResult::SUCCESS;
    }

    // Free pin database if dynamically allocated
    if (_pinDatabase != nullptr)
    {
        const auto* api = GetAPI();
        if (api && api->mem_free)
        {
            api->mem_free(_pinDatabase);
        }
        _pinDatabase = nullptr;
    }
    _numPins = 0;

    _state = DriverState::UNINITIALIZED;
    return DriverResult::SUCCESS;
}

// ============================================================================
// Open / Close
// ============================================================================

DriverResult PinMuxDriver::open()
{
    if (_state == DriverState::UNINITIALIZED)
    {
        return DriverResult::ERROR_NOT_INIT;
    }

    if (_state == DriverState::OPENED)
    {
        return DriverResult::SUCCESS;
    }

    _state = DriverState::OPENED;
    return DriverResult::SUCCESS;
}

DriverResult PinMuxDriver::close()
{
    if (_state != DriverState::OPENED)
    {
        return DriverResult::SUCCESS;
    }

    _state = DriverState::INITIALIZED;
    return DriverResult::SUCCESS;
}

// ============================================================================
// Read / Write (Not used for PinMux)
// ============================================================================

int32_t PinMuxDriver::read(void* /*buffer*/, size_t /*size*/, uint32_t /*timeoutMs*/)
{
    return static_cast<int32_t>(DriverResult::ERROR_NOT_SUPPORTED);
}

int32_t PinMuxDriver::write(const void* /*buffer*/, size_t /*size*/, uint32_t /*timeoutMs*/)
{
    return static_cast<int32_t>(DriverResult::ERROR_NOT_SUPPORTED);
}

// ============================================================================
// IOCTL
// ============================================================================

DriverResult PinMuxDriver::ioctl(uint32_t cmd, void* arg)
{
    PinMuxIoctl ioctlCmd = static_cast<PinMuxIoctl>(cmd);
    
    switch (ioctlCmd)
    {
        case PinMuxIoctl::LOAD_CONFIG:
            return handleLoadConfig(arg);
        case PinMuxIoctl::RELOAD_CONFIG:
            return handleReloadConfig();
        case PinMuxIoctl::GET_CONFIG_PATH:
            return handleGetConfigPath(arg);
        case PinMuxIoctl::SET_PIN_MUX:
            return handleSetPinMux(arg);
        case PinMuxIoctl::GET_PIN_MUX:
            return handleGetPinMux(arg);
        case PinMuxIoctl::SET_PIN_CONFIG:
            return handleSetPinConfig(arg);
        case PinMuxIoctl::GET_PIN_CONFIG:
            return handleGetPinConfig(arg);
        case PinMuxIoctl::APPLY_PERIPHERAL:
            return handleApplyPeripheral(arg);
        case PinMuxIoctl::RELEASE_PERIPHERAL:
            return handleReleasePeripheral(arg);
        case PinMuxIoctl::GET_PERIPHERAL_PINS:
            return handleGetPeripheralPins(arg);
        case PinMuxIoctl::GET_PIN_INFO:
            return handleGetPinInfo(arg);
        case PinMuxIoctl::GET_PIN_ALT_FUNCS:
            return handleGetPinAltFuncs(arg);
        case PinMuxIoctl::IS_PIN_AVAILABLE:
            return handleIsPinAvailable(arg);
        case PinMuxIoctl::SET_GPIO_MODE:
            return handleSetGpioMode(arg);
        case PinMuxIoctl::GET_STATS:
            return handleGetStats(arg);
        case PinMuxIoctl::GET_PIN_COUNT:
            return handleGetPinCount(arg);
        case PinMuxIoctl::GET_PIN_BY_INDEX:
            return handleGetPinByIndex(arg);
        default:
            return DriverResult::ERROR_INVALID;
    }
}

// ============================================================================
// Internal Methods
// ============================================================================

// Simple JSON parsing helpers
static const char* skipWhitespace(const char* p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static const char* findChar(const char* p, char c)
{
    while (*p && *p != c) p++;
    return p;
}

static bool parseJsonString(const char*& p, char* out, size_t maxLen)
{
    p = skipWhitespace(p);
    if (*p != '"') return false;
    p++;  // skip opening quote
    
    size_t i = 0;
    while (*p && *p != '"' && i < maxLen - 1)
    {
        out[i++] = *p++;
    }
    out[i] = '\0';
    
    if (*p == '"') p++;  // skip closing quote
    return true;
}

static const char* findKey(const char* json, const char* key)
{
    // Simple key search in JSON - not a full parser but works for our format
    size_t keyLen = 0;
    while (key[keyLen]) keyLen++;
    
    const char* p = json;
    while (*p)
    {
        // Find quote
        p = findChar(p, '"');
        if (!*p) return nullptr;
        p++;  // skip quote
        
        // Check if this is our key
        bool match = true;
        for (size_t i = 0; i < keyLen; i++)
        {
            if (p[i] != key[i]) { match = false; break; }
        }
        if (match && p[keyLen] == '"')
        {
            // Found the key, skip to value
            p = findChar(p + keyLen + 1, ':');
            if (*p) p++;  // skip colon
            return skipWhitespace(p);
        }
        
        // Skip to next quote
        p = findChar(p, '"');
        if (*p) p++;
    }
    return nullptr;
}

// Load ALT function names from JSON configuration file
void PinMuxDriver::loadAltFunctionsFromJson()
{
    const auto* api = GetAPI();
    if (api == nullptr || api->file_open == nullptr || _pinDatabase == nullptr)
    {
        return;
    }
    
    // Try to open config file
    void* file = api->file_open(CONFIG_FILE_PATH, 0x01);  // FA_READ
    if (file == nullptr)
    {
        return;  // File not found, use defaults
    }
    
    // Get file size
    int fileSize = api->file_size(file);
    if (fileSize <= 0 || fileSize > 65536)  // Limit to 64KB
    {
        api->file_close(file);
        return;
    }
    
    // Allocate buffer for file content
    char* jsonBuffer = static_cast<char*>(api->mem_alloc(fileSize + 1));
    if (jsonBuffer == nullptr)
    {
        api->file_close(file);
        return;
    }
    
    // Read file content
    uint32_t bytesRead = 0;
    if (api->file_read(file, jsonBuffer, fileSize, &bytesRead) != 0 || bytesRead == 0)
    {
        api->mem_free(jsonBuffer);
        api->file_close(file);
        return;
    }
    jsonBuffer[bytesRead] = '\0';
    api->file_close(file);
    
    // Find "pins" object
    const char* pinsStart = findKey(jsonBuffer, "pins");
    if (pinsStart == nullptr || *pinsStart != '{')
    {
        api->mem_free(jsonBuffer);
        return;
    }
    
    // For each pin in our database, try to find it in JSON and load ALT functions
    for (size_t i = 0; i < _numPins; i++)
    {
        PinDatabaseEntry& pin = _pinDatabase[i];
        
        // Find pin entry in JSON
        const char* pinEntry = findKey(pinsStart, pin.name);
        if (pinEntry == nullptr || *pinEntry != '{')
        {
            continue;  // Pin not found in JSON, keep empty ALT names
        }
        
        // Find "alt" array
        const char* altArray = findKey(pinEntry, "alt");
        if (altArray == nullptr || *altArray != '[')
        {
            continue;
        }
        
        // Parse ALT function names array
        altArray++;  // skip '['
        for (int altIdx = 0; altIdx < 8; altIdx++)
        {
            altArray = skipWhitespace(altArray);
            if (*altArray == ']') break;  // End of array
            
            // Parse string
            char funcName[PINMUX_ALT_FUNC_NAME_LEN];
            if (parseJsonString(altArray, funcName, sizeof(funcName)))
            {
                // Copy to pin entry
                for (size_t j = 0; j < PINMUX_ALT_FUNC_NAME_LEN - 1 && funcName[j]; j++)
                {
                    pin.altFunctions[altIdx][j] = funcName[j];
                    pin.altFunctions[altIdx][j + 1] = '\0';
                }
            }
            
            // Skip comma if present
            altArray = skipWhitespace(altArray);
            if (*altArray == ',') altArray++;
        }
    }
    
    api->mem_free(jsonBuffer);
    _stats.configLoads++;
}

void PinMuxDriver::initPinDatabase()
{
    const auto* api = GetAPI();
    if (api == nullptr || api->mem_alloc == nullptr)
    {
        return;
    }

    // Allocate pin database
    size_t dbSize = s_defaultPinCount * sizeof(PinDatabaseEntry);
    _pinDatabase = static_cast<PinDatabaseEntry*>(api->mem_alloc(dbSize));
    
    if (_pinDatabase != nullptr && api->memcpy != nullptr)
    {
        // Copy default database
        api->memcpy(_pinDatabase, s_defaultPinDatabase, dbSize);
        _numPins = s_defaultPinCount;
        
        // Load ALT function names from JSON config file
        loadAltFunctionsFromJson();
        
        // Synchronize with actual hardware state
        syncWithHardware();
    }
}

void PinMuxDriver::syncWithHardware()
{
    if (_pinDatabase == nullptr || _numPins == 0)
    {
        return;
    }

    // Read actual MUX mode from hardware for each pin
    for (size_t i = 0; i < _numPins; i++)
    {
        PinDatabaseEntry& pin = _pinDatabase[i];
        
        // Calculate absolute MUX register address
        // muxRegister stores offset relative to IOMUXC base (0x401F8000)
        uint32_t muxRegAddr = IOMUXC_BASE + pin.muxRegister;
        
        // Read current MUX mode from register using HAL function
        uint8_t currentMux = hal_pinmux_get_mux_mode(muxRegAddr);
        
        // Update the database entry with actual hardware state
        pin.currentAlt = currentMux;
        
        // Get function name for this ALT mode from pin's ALT function table
        const char* funcName = getAltFunctionName(pin, currentMux);
        
        // Check if this is a GPIO function (pin is free)
        if (isGpioFunction(funcName))
        {
            pin.inUse = false;
            pin.claimedBy[0] = '\0';
        }
        else
        {
            pin.inUse = true;
            // Copy function name to claimedBy
            size_t j = 0;
            while (funcName[j] != '\0' && j < PINMUX_MAX_PERIPHERAL_NAME - 1)
            {
                pin.claimedBy[j] = funcName[j];
                j++;
            }
            pin.claimedBy[j] = '\0';
        }
    }
}

PinDatabaseEntry* PinMuxDriver::findPinByName(const char* name)
{
    if (name == nullptr || _pinDatabase == nullptr)
    {
        return nullptr;
    }

    for (size_t i = 0; i < _numPins; i++)
    {
        if (stringCompare(_pinDatabase[i].name, name) == 0)
        {
            return &_pinDatabase[i];
        }
    }

    return nullptr;
}

bool PinMuxDriver::setMuxByName(const char* name, uint8_t altFunc, bool sion)
{
    PINMUX_DRV_LOG("[PinMux] setMuxByName: '%s' alt=%d sion=%d\r\n", name, altFunc, sion);
    
    PinDatabaseEntry* pin = findPinByName(name);
    if (pin == nullptr)
    {
        PINMUX_DRV_LOG("[PinMux] ERROR: pin not found\r\n");
        return false;
    }

    // Call HAL to set mux - muxRegister is offset, add IOMUXC_BASE for absolute address
    uint32_t muxAddr = IOMUXC_BASE + pin->muxRegister;
    PINMUX_DRV_LOG("[PinMux] muxAddr=0x%08lX, calling hal_pinmux_set_mux_raw\r\n", muxAddr);
    hal_pinmux_set_mux_raw(muxAddr, altFunc, 0, 0, sion);
    
#if PINMUX_DRV_DEBUG
    // Read back and verify
    volatile uint32_t* muxReg = (volatile uint32_t*)muxAddr;
    PINMUX_DRV_LOG("[PinMux] Readback MUX @ 0x%08lX = 0x%08lX\r\n", muxAddr, *muxReg);
#endif
    
    _stats.muxChanges++;
    return true;
}

bool PinMuxDriver::setPadConfigByName(const char* name, const PinPadConfig& config)
{
    PinDatabaseEntry* pin = findPinByName(name);
    if (pin == nullptr)
    {
        return false;
    }

    // Convert to HAL pad config
    hal_pin_pad_config_t halConfig;
    halConfig.drive = static_cast<hal_drive_strength_t>(config.drive);
    halConfig.speed = static_cast<hal_pin_speed_t>(config.speed);
    halConfig.pull = static_cast<hal_pull_config_t>(config.pull);
    halConfig.openDrain = config.openDrain;
    halConfig.hysteresis = config.hysteresis;
    halConfig.pullKeeperEnable = config.pullKeeperEnable;
    halConfig.pullKeeperSelect = config.pullKeeper;

    // padRegister is offset, add IOMUXC_BASE for absolute address
    uint32_t padAddr = IOMUXC_BASE + pin->padRegister;
    hal_pinmux_set_pad_config(padAddr, &halConfig);
    return true;
}

bool PinMuxDriver::claimPin(const char* pinName, const char* peripheralName)
{
    PinDatabaseEntry* pin = findPinByName(pinName);
    if (pin == nullptr)
    {
        return false;
    }

    if (pin->inUse)
    {
        return false;  // Already claimed
    }

    pin->inUse = true;
    safeCopyString(pin->claimedBy, peripheralName, PINMUX_MAX_PERIPHERAL_NAME);
    _stats.claimedPins++;
    
    return true;
}

bool PinMuxDriver::releasePin(const char* pinName)
{
    PinDatabaseEntry* pin = findPinByName(pinName);
    if (pin == nullptr)
    {
        return false;
    }

    if (!pin->inUse)
    {
        return true;  // Already free
    }

    pin->inUse = false;
    pin->claimedBy[0] = '\0';
    if (_stats.claimedPins > 0)
    {
        _stats.claimedPins--;
    }
    
    return true;
}

bool PinMuxDriver::isPinAvailable(const char* pinName)
{
    PinDatabaseEntry* pin = findPinByName(pinName);
    if (pin == nullptr)
    {
        return false;
    }
    return !pin->inUse;
}

void PinMuxDriver::safeCopyString(char* dest, const char* src, size_t maxLen)
{
    if (dest == nullptr || src == nullptr || maxLen == 0)
    {
        return;
    }
    
    size_t i = 0;
    while (i < maxLen - 1 && src[i] != '\0')
    {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

// ============================================================================
// IOCTL Handlers
// ============================================================================

DriverResult PinMuxDriver::handleLoadConfig(void* arg)
{
    if (arg == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    const char* path = static_cast<const char*>(arg);
    safeCopyString(_configPath, path, PINMUX_MAX_CONFIG_PATH);
    
    // TODO: Parse JSON config file
    // For now, just record the path
    _stats.configLoads++;
    
    return DriverResult::SUCCESS;
}

DriverResult PinMuxDriver::handleReloadConfig()
{
    if (_configPath[0] == '\0')
    {
        return DriverResult::ERROR_INVALID;
    }
    
    return handleLoadConfig(_configPath);
}

DriverResult PinMuxDriver::handleGetConfigPath(void* arg)
{
    if (arg == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    safeCopyString(static_cast<char*>(arg), _configPath, PINMUX_MAX_CONFIG_PATH);
    return DriverResult::SUCCESS;
}

DriverResult PinMuxDriver::handleSetPinMux(void* arg)
{
    if (arg == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    PinMuxConfig* config = static_cast<PinMuxConfig*>(arg);
    
    if (!setMuxByName(config->pinName, config->altFunction, config->sion))
    {
        return DriverResult::ERROR_INVALID;
    }
    
    _stats.configuredPins++;
    return DriverResult::SUCCESS;
}

DriverResult PinMuxDriver::handleGetPinMux(void* arg)
{
    if (arg == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    PinMuxConfig* config = static_cast<PinMuxConfig*>(arg);
    PinDatabaseEntry* pin = findPinByName(config->pinName);
    
    if (pin == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    // muxRegister is offset, add IOMUXC_BASE for absolute address
    uint32_t muxAddr = IOMUXC_BASE + pin->muxRegister;
    config->altFunction = hal_pinmux_get_mux_mode(muxAddr);
    return DriverResult::SUCCESS;
}

DriverResult PinMuxDriver::handleSetPinConfig(void* arg)
{
    if (arg == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    PinPadConfig* config = static_cast<PinPadConfig*>(arg);
    
    if (!setPadConfigByName(config->pinName, *config))
    {
        return DriverResult::ERROR_INVALID;
    }
    
    return DriverResult::SUCCESS;
}

DriverResult PinMuxDriver::handleGetPinConfig(void* arg)
{
    if (arg == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    PinPadConfig* config = static_cast<PinPadConfig*>(arg);
    PinDatabaseEntry* pin = findPinByName(config->pinName);
    
    if (pin == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    // padRegister is offset, add IOMUXC_BASE for absolute address
    uint32_t padAddr = IOMUXC_BASE + pin->padRegister;
    uint32_t padValue = hal_pinmux_get_pad_config(padAddr);
    
    // Decode pad value
    config->drive = static_cast<DriveStrength>((padValue >> 3) & 0x07);
    config->speed = static_cast<PinSpeed>((padValue >> 6) & 0x03);
    config->openDrain = (padValue & (1 << 11)) != 0;
    config->pullKeeperEnable = (padValue & (1 << 12)) != 0;
    config->pullKeeper = (padValue & (1 << 13)) != 0;
    config->pull = static_cast<PullConfig>((padValue >> 14) & 0x03);
    config->hysteresis = (padValue & (1 << 16)) != 0;
    
    return DriverResult::SUCCESS;
}

DriverResult PinMuxDriver::handleApplyPeripheral(void* arg)
{
    if (arg == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    // const char* peripheralName = static_cast<const char*>(arg);
    // TODO: Look up peripheral in config file and apply all its pins
    
    return DriverResult::ERROR_NOT_SUPPORTED;
}

DriverResult PinMuxDriver::handleReleasePeripheral(void* arg)
{
    if (arg == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    const char* peripheralName = static_cast<const char*>(arg);
    
    // Release all pins claimed by this peripheral
    for (size_t i = 0; i < _numPins; i++)
    {
        if (_pinDatabase[i].inUse && 
            stringCompare(_pinDatabase[i].claimedBy, peripheralName) == 0)
        {
            releasePin(_pinDatabase[i].name);
        }
    }
    
    return DriverResult::SUCCESS;
}

DriverResult PinMuxDriver::handleGetPeripheralPins(void* arg)
{
    if (arg == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    PeripheralPins* pins = static_cast<PeripheralPins*>(arg);
    pins->numPins = 0;
    
    // Find all pins claimed by this peripheral
    for (size_t i = 0; i < _numPins && pins->numPins < PINMUX_MAX_PINS_PER_PERIPHERAL; i++)
    {
        if (_pinDatabase[i].inUse && 
            stringCompare(_pinDatabase[i].claimedBy, pins->peripheralName) == 0)
        {
            safeCopyString(pins->pins[pins->numPins].pinName, 
                          _pinDatabase[i].name, PINMUX_MAX_PIN_NAME);
            pins->numPins++;
        }
    }
    
    return DriverResult::SUCCESS;
}

DriverResult PinMuxDriver::handleGetPinInfo(void* arg)
{
    if (arg == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    PinInfo* info = static_cast<PinInfo*>(arg);
    PinDatabaseEntry* pin = findPinByName(info->pinName);
    
    if (pin == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    info->gpioPort = static_cast<GpioPort>(pin->gpioPort);
    info->gpioPin = pin->gpioPin;
    // muxRegister is offset, add IOMUXC_BASE for absolute address
    uint32_t muxAddr = IOMUXC_BASE + pin->muxRegister;
    info->currentAlt = hal_pinmux_get_mux_mode(muxAddr);
    info->inUse = pin->inUse;
    safeCopyString(info->claimedBy, pin->claimedBy, PINMUX_MAX_PERIPHERAL_NAME);
    
    return DriverResult::SUCCESS;
}

DriverResult PinMuxDriver::handleGetPinAltFuncs(void* /*arg*/)
{
    // TODO: Implement alt function database
    return DriverResult::ERROR_NOT_SUPPORTED;
}

DriverResult PinMuxDriver::handleIsPinAvailable(void* arg)
{
    if (arg == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    const char* pinName = static_cast<const char*>(arg);
    return isPinAvailable(pinName) ? DriverResult::SUCCESS : DriverResult::ERROR_BUSY;
}

DriverResult PinMuxDriver::handleSetGpioMode(void* arg)
{
    if (arg == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    GpioModeConfig* config = static_cast<GpioModeConfig*>(arg);
    PinDatabaseEntry* pin = findPinByName(config->pinName);
    
    if (pin == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    // Set pin to GPIO mode (ALT5 is usually GPIO)
    // muxRegister is offset, add IOMUXC_BASE for absolute address
    uint32_t muxAddr = IOMUXC_BASE + pin->muxRegister;
    hal_pinmux_set_mux_raw(muxAddr, 5, 0, 0, false);
    
    // Configure GPIO direction
    hal_pinmux_set_gpio_direction(pin->gpioPort, pin->gpioPin, 
                                   config->output, config->initialValue);
    
    _stats.configuredPins++;
    _stats.muxChanges++;
    
    return DriverResult::SUCCESS;
}

DriverResult PinMuxDriver::handleGetStats(void* arg)
{
    if (arg == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    const auto* api = GetAPI();
    if (api && api->memcpy)
    {
        api->memcpy(arg, &_stats, sizeof(PinMuxStats));
    }
    
    return DriverResult::SUCCESS;
}

DriverResult PinMuxDriver::handleGetPinCount(void* arg)
{
    if (arg == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    *static_cast<uint32_t*>(arg) = static_cast<uint32_t>(_numPins);
    return DriverResult::SUCCESS;
}

DriverResult PinMuxDriver::handleGetPinByIndex(void* arg)
{
    if (arg == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    IndexedPinQuery* query = static_cast<IndexedPinQuery*>(arg);
    
    if (query->index >= _numPins || _pinDatabase == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    const PinDatabaseEntry& pin = _pinDatabase[query->index];
    
    // Copy pin name
    safeCopyString(query->pinName, pin.name, PINMUX_MAX_PIN_NAME);
    
    // Set GPIO info
    query->gpioPort = static_cast<GpioPort>(pin.gpioPort);
    query->gpioPin = pin.gpioPin;
    
    // Use cached current mux mode (synchronized from hardware during init)
    query->currentAlt = pin.currentAlt;
    
    // Usage info
    query->inUse = pin.inUse;
    safeCopyString(query->claimedBy, pin.claimedBy, PINMUX_MAX_PERIPHERAL_NAME);
    
    return DriverResult::SUCCESS;
}

// ============================================================================
// Module Entry Points
// ============================================================================

extern "C" DriverBase* driver_module_init()
{
    return new PinMuxDriver();
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
