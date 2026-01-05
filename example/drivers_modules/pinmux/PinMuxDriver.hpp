/*
 * PinMuxDriver.hpp - CRTOS PinMux Driver Module
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
 * PinMux driver module implementing Linux Device Tree-like pin configuration.
 * Uses JSON configuration files for runtime-configurable pin multiplexing.
 *
 * Features:
 * - JSON-based pin configuration (like Linux Device Tree)
 * - Runtime pin reconfiguration
 * - Peripheral pin group management
 * - GPIO configuration support
 * - Pin conflict detection
 */

#ifndef PINMUX_DRIVER_HPP
#define PINMUX_DRIVER_HPP

#include <CRTOS/Drivers/DriverBase.hpp>
#include <CRTOS/Drivers/PinMuxIoctl.hpp>
#include "hal_pinmux.h"

namespace CRTOS
{
namespace Drivers
{

// Maximum pins in database
constexpr size_t PINMUX_MAX_DB_PINS = 256;

// Maximum length for ALT function name
constexpr size_t PINMUX_ALT_FUNC_NAME_LEN = 24;

/**
 * @brief Pin database entry
 * Maps pin names to register addresses
 */
struct PinDatabaseEntry
{
    char name[PINMUX_MAX_PIN_NAME];      ///< Pin name e.g., "GPIO_AD_B0_00"
    uint32_t muxRegister;                 ///< SW_MUX_CTL register offset (relative to IOMUXC base)
    uint32_t padRegister;                 ///< SW_PAD_CTL register offset (relative to IOMUXC base)
    hal_gpio_port_t gpioPort;             ///< GPIO port when in GPIO mode
    uint8_t gpioPin;                      ///< GPIO pin number
    uint8_t currentAlt;                   ///< Current ALT function (0-7), read from hardware
    bool inUse;                           ///< Pin is claimed by a peripheral
    char claimedBy[PINMUX_MAX_PERIPHERAL_NAME]; ///< Peripheral name
    char altFunctions[8][PINMUX_ALT_FUNC_NAME_LEN]; ///< ALT0-ALT7 function names from JSON
};

/**
 * @brief Input select register entry
 * For pins with daisy chain selection
 */
struct InputSelectEntry
{
    char signalName[PINMUX_MAX_FUNCTION_NAME]; ///< Signal name e.g., "LPSPI1_SCK"
    uint32_t selectRegister;                    ///< Select input register
    uint8_t daisyValue;                         ///< Daisy chain value
};

/**
 * @brief PinMux Driver Class
 *
 * Implements the DriverBase interface for pin multiplexing.
 * Uses HAL functions for hardware access.
 * Provides Linux Device Tree-like configuration through JSON files.
 */
class PinMuxDriver : public DriverBase
{
public:
    PinMuxDriver();
    virtual ~PinMuxDriver();

    // DriverBase interface implementation
    DriverResult init() override;
    DriverResult deinit() override;
    DriverResult open() override;
    DriverResult close() override;
    int32_t read(void* buffer, size_t size, uint32_t timeoutMs = 0) override;
    int32_t write(const void* buffer, size_t size, uint32_t timeoutMs = 0) override;
    DriverResult ioctl(uint32_t cmd, void* arg) override;
    const DriverInfo* getInfo() const override;

    /**
     * @brief Get singleton instance
     */
    static PinMuxDriver* getInstance();

private:
    static PinMuxDriver* _instance;
    static const DriverInfo _driverInfo;

    // Pin database
    PinDatabaseEntry* _pinDatabase;
    size_t _numPins;

    // Statistics
    PinMuxStats _stats;

    // Current config file path
    char _configPath[PINMUX_MAX_CONFIG_PATH];

    // ========================================================================
    // IOCTL Handlers
    // ========================================================================
    DriverResult handleLoadConfig(void* arg);
    DriverResult handleReloadConfig();
    DriverResult handleGetConfigPath(void* arg);
    DriverResult handleSetPinMux(void* arg);
    DriverResult handleGetPinMux(void* arg);
    DriverResult handleSetPinConfig(void* arg);
    DriverResult handleGetPinConfig(void* arg);
    DriverResult handleApplyPeripheral(void* arg);
    DriverResult handleReleasePeripheral(void* arg);
    DriverResult handleGetPeripheralPins(void* arg);
    DriverResult handleGetPinInfo(void* arg);
    DriverResult handleGetPinAltFuncs(void* arg);
    DriverResult handleIsPinAvailable(void* arg);
    DriverResult handleSetGpioMode(void* arg);
    DriverResult handleGetStats(void* arg);
    DriverResult handleGetPinCount(void* arg);
    DriverResult handleGetPinByIndex(void* arg);

    // ========================================================================
    // Internal Methods
    // ========================================================================
    
    /**
     * @brief Initialize pin database
     */
    void initPinDatabase();

    /**
     * @brief Load ALT function names from JSON config file
     * Called during init to populate altFunctions arrays from /config/pinmux.json
     */
    void loadAltFunctionsFromJson();

    /**
     * @brief Synchronize database with actual hardware state
     * Reads current MUX mode from IOMUXC registers for all pins
     */
    void syncWithHardware();

    /**
     * @brief Find pin entry by name
     * @param name Pin name (e.g., "GPIO_AD_B0_00")
     * @return Pointer to entry or nullptr if not found
     */
    PinDatabaseEntry* findPinByName(const char* name);

    /**
     * @brief Set mux for a pin by name
     * @param name Pin name
     * @param altFunc ALT function (0-7)
     * @param sion Software Input On
     * @return true on success
     */
    bool setMuxByName(const char* name, uint8_t altFunc, bool sion);

    /**
     * @brief Set pad config for a pin by name
     * @param name Pin name
     * @param config Pad configuration
     * @return true on success
     */
    bool setPadConfigByName(const char* name, const PinPadConfig& config);

    /**
     * @brief Parse JSON configuration file
     * @param path File path on SD card
     * @return true on success
     */
    bool parseConfigFile(const char* path);

    /**
     * @brief Claim a pin for a peripheral
     * @param pinName Pin name
     * @param peripheralName Peripheral claiming the pin
     * @return true on success
     */
    bool claimPin(const char* pinName, const char* peripheralName);

    /**
     * @brief Release a pin
     * @param pinName Pin name
     * @return true on success
     */
    bool releasePin(const char* pinName);

    /**
     * @brief Check if pin is available
     * @param pinName Pin name
     * @return true if pin is free
     */
    bool isPinAvailable(const char* pinName);

    /**
     * @brief Helper to copy string safely
     */
    static void safeCopyString(char* dest, const char* src, size_t maxLen);
};

/**
 * @brief Module entry point
 */
extern "C" DriverBase* driver_module_init();

/**
 * @brief Module exit point
 */
extern "C" void driver_module_exit(DriverBase* driver);

} // namespace Drivers
} // namespace CRTOS

#endif // PINMUX_DRIVER_HPP
