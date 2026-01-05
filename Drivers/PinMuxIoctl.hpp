/*
 * PinMuxIoctl.hpp - PinMux Driver IOCTL Definitions
 * Author: Arkadiusz Szlanta
 * Date: 04 Jan 2026
 *
 * Description:
 * Shared IOCTL command definitions and structures for PinMux driver.
 * This header can be included by both the driver module and applications.
 * 
 * The PinMux system is similar to Linux Device Tree - it allows runtime
 * configuration of pin multiplexing through JSON configuration files.
 */

#ifndef PINMUX_IOCTL_HPP
#define PINMUX_IOCTL_HPP

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// IOCTL base for PinMux
#ifndef IOCTL_BASE_PINMUX
#define IOCTL_BASE_PINMUX 0x0600
#endif

namespace CRTOS
{
namespace Drivers
{

// Maximum lengths
constexpr size_t PINMUX_MAX_PIN_NAME = 32;
constexpr size_t PINMUX_MAX_FUNCTION_NAME = 32;
constexpr size_t PINMUX_MAX_PERIPHERAL_NAME = 16;
constexpr size_t PINMUX_MAX_CONFIG_PATH = 64;
constexpr size_t PINMUX_MAX_PINS_PER_PERIPHERAL = 8;
constexpr size_t PINMUX_MAX_ALT_FUNCTIONS = 8;

// PinMux-specific IOCTL commands
enum class PinMuxIoctl : uint32_t
{
    // Configuration loading
    LOAD_CONFIG        = IOCTL_BASE_PINMUX + 0,   ///< Load JSON config file (arg: const char* path)
    RELOAD_CONFIG      = IOCTL_BASE_PINMUX + 1,   ///< Reload current config
    GET_CONFIG_PATH    = IOCTL_BASE_PINMUX + 2,   ///< Get current config path (arg: char* buffer)
    
    // Pin configuration
    SET_PIN_MUX        = IOCTL_BASE_PINMUX + 10,  ///< Set single pin mux (arg: PinMuxConfig*)
    GET_PIN_MUX        = IOCTL_BASE_PINMUX + 11,  ///< Get single pin mux (arg: PinMuxConfig*)
    SET_PIN_CONFIG     = IOCTL_BASE_PINMUX + 12,  ///< Set pin pad config (arg: PinPadConfig*)
    GET_PIN_CONFIG     = IOCTL_BASE_PINMUX + 13,  ///< Get pin pad config (arg: PinPadConfig*)
    
    // Peripheral configuration
    APPLY_PERIPHERAL   = IOCTL_BASE_PINMUX + 20,  ///< Apply config for peripheral (arg: const char* name)
    RELEASE_PERIPHERAL = IOCTL_BASE_PINMUX + 21,  ///< Release pins for peripheral (arg: const char* name)
    GET_PERIPHERAL_PINS= IOCTL_BASE_PINMUX + 22,  ///< Get pins for peripheral (arg: PeripheralPins*)
    
    // Pin information
    GET_PIN_INFO       = IOCTL_BASE_PINMUX + 30,  ///< Get pin info (arg: PinInfo*)
    GET_PIN_ALT_FUNCS  = IOCTL_BASE_PINMUX + 31,  ///< Get alt functions (arg: PinAltFunctions*)
    IS_PIN_AVAILABLE   = IOCTL_BASE_PINMUX + 32,  ///< Check if pin is free (arg: const char* name)
    
    // GPIO mode
    SET_GPIO_MODE      = IOCTL_BASE_PINMUX + 40,  ///< Set pin as GPIO (arg: GpioModeConfig*)
    
    // Status
    GET_STATUS         = IOCTL_BASE_PINMUX + 50,  ///< Get driver status
    GET_STATS          = IOCTL_BASE_PINMUX + 51,  ///< Get statistics (arg: PinMuxStats*)
    GET_PIN_COUNT      = IOCTL_BASE_PINMUX + 52,  ///< Get number of pins in database (arg: uint32_t*)
    GET_PIN_BY_INDEX   = IOCTL_BASE_PINMUX + 53,  ///< Get pin info by index (arg: IndexedPinQuery*)
};

/**
 * @brief GPIO port enumeration
 * Note: Names prefixed with PORT_ to avoid conflict with NXP GPIO macros
 */
enum class GpioPort : uint8_t
{
    PORT_GPIO1 = 0,
    PORT_GPIO2 = 1,
    PORT_GPIO3 = 2,
    PORT_GPIO4 = 3,
    PORT_GPIO5 = 4,
    COUNT
};

/**
 * @brief Pin drive strength
 */
enum class DriveStrength : uint8_t
{
    Disabled     = 0,  ///< Output driver disabled
    R0_150Ohm    = 1,  ///< 150 Ohm @ 3.3V
    R0_2_75Ohm   = 2,  ///< 75 Ohm @ 3.3V
    R0_3_50Ohm   = 3,  ///< 50 Ohm @ 3.3V
    R0_4_37Ohm   = 4,  ///< 37 Ohm @ 3.3V
    R0_5_30Ohm   = 5,  ///< 30 Ohm @ 3.3V
    R0_6_25Ohm   = 6,  ///< 25 Ohm @ 3.3V
    R0_7_22Ohm   = 7,  ///< 22 Ohm @ 3.3V
};

/**
 * @brief Pin speed (slew rate)
 */
enum class PinSpeed : uint8_t
{
    Low_50MHz    = 0,
    Medium_100MHz= 1,
    Fast_150MHz  = 2,
    Max_200MHz   = 3,
};

/**
 * @brief Pull-up/pull-down configuration
 */
enum class PullConfig : uint8_t
{
    Disabled     = 0,  ///< No pull
    PullDown_100K= 1,  ///< 100K pull-down
    PullUp_47K   = 2,  ///< 47K pull-up
    PullUp_100K  = 3,  ///< 100K pull-up
    PullUp_22K   = 4,  ///< 22K pull-up
};

/**
 * @brief Pin mux configuration for a single pin
 */
struct PinMuxConfig
{
    char pinName[PINMUX_MAX_PIN_NAME];  ///< Pin name e.g., "GPIO_AD_B0_12"
    uint8_t altFunction;                 ///< ALT function (0-7)
    bool sion;                           ///< Software Input On (for loopback)
};

/**
 * @brief Pin pad electrical configuration
 */
struct PinPadConfig
{
    char pinName[PINMUX_MAX_PIN_NAME];  ///< Pin name
    DriveStrength drive;                 ///< Drive strength
    PinSpeed speed;                      ///< Speed/slew rate
    PullConfig pull;                     ///< Pull-up/down config
    bool openDrain;                      ///< Open drain mode
    bool hysteresis;                     ///< Hysteresis enable
    bool pullKeeper;                     ///< Pull/keeper select
    bool pullKeeperEnable;               ///< Pull/keeper enable
};

/**
 * @brief Alternative function info
 */
struct AltFunctionInfo
{
    uint8_t altNumber;                          ///< ALT0-ALT7
    char functionName[PINMUX_MAX_FUNCTION_NAME];///< e.g., "LPSPI1_SCK"
    char peripheralName[PINMUX_MAX_PERIPHERAL_NAME]; ///< e.g., "LPSPI1"
    char signalName[PINMUX_MAX_FUNCTION_NAME];  ///< e.g., "SCK"
};

/**
 * @brief Pin information
 */
struct PinInfo
{
    char pinName[PINMUX_MAX_PIN_NAME];  ///< Pin name
    GpioPort gpioPort;                   ///< GPIO port when in GPIO mode
    uint8_t gpioPin;                     ///< GPIO pin number
    uint8_t currentAlt;                  ///< Current ALT function
    bool inUse;                          ///< Pin is claimed by a peripheral
    char claimedBy[PINMUX_MAX_PERIPHERAL_NAME]; ///< Peripheral using this pin
};

/**
 * @brief Alternative functions for a pin
 */
struct PinAltFunctions
{
    char pinName[PINMUX_MAX_PIN_NAME];          ///< Pin name (input)
    uint8_t numFunctions;                        ///< Number of valid functions
    AltFunctionInfo functions[PINMUX_MAX_ALT_FUNCTIONS]; ///< Alt functions
};

/**
 * @brief Peripheral pin configuration
 */
struct PeripheralPinConfig
{
    char signalName[PINMUX_MAX_FUNCTION_NAME];  ///< e.g., "SCK", "MOSI", "MISO"
    char pinName[PINMUX_MAX_PIN_NAME];          ///< e.g., "GPIO_AD_B0_00"
    uint8_t altFunction;                         ///< ALT function for this mapping
};

/**
 * @brief Pins for a peripheral
 */
struct PeripheralPins
{
    char peripheralName[PINMUX_MAX_PERIPHERAL_NAME]; ///< e.g., "LPSPI1"
    uint8_t numPins;                             ///< Number of pins
    PeripheralPinConfig pins[PINMUX_MAX_PINS_PER_PERIPHERAL]; ///< Pin configs
};

/**
 * @brief GPIO mode configuration
 */
struct GpioModeConfig
{
    char pinName[PINMUX_MAX_PIN_NAME];  ///< Pin name
    bool output;                         ///< True = output, false = input
    bool initialValue;                   ///< Initial output value (if output)
};

/**
 * @brief PinMux statistics
 */
struct PinMuxStats
{
    uint32_t configuredPins;     ///< Number of pins configured
    uint32_t claimedPins;        ///< Number of pins claimed by peripherals
    uint32_t configLoads;        ///< Number of config file loads
    uint32_t muxChanges;         ///< Number of mux changes
};

/**
 * @brief Query pin by index
 */
struct IndexedPinQuery
{
    uint32_t index;                         ///< Input: pin index (0 to pin_count-1)
    char pinName[PINMUX_MAX_PIN_NAME];      ///< Output: pin name
    GpioPort gpioPort;                      ///< Output: GPIO port
    uint8_t gpioPin;                        ///< Output: GPIO pin number
    uint8_t currentAlt;                     ///< Output: current ALT function
    bool inUse;                             ///< Output: pin claimed?
    char claimedBy[PINMUX_MAX_PERIPHERAL_NAME]; ///< Output: peripheral name
};

} // namespace Drivers
} // namespace CRTOS

#endif // PINMUX_IOCTL_HPP
