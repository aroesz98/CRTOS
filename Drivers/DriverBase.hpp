/*
 * DriverBase.hpp - CRTOS Base Driver Interface
 * Author: Arkadiusz Szlanta
 * Date: 26 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * Abstract base class for all kernel drivers. Provides a unified interface
 * for driver operations including initialization, open/close, read/write, and ioctl.
 * Drivers inherit from this class and implement the virtual methods.
 */

#ifndef DRIVER_BASE_HPP
#define DRIVER_BASE_HPP

#include <cstdint>
#include <cstddef>

/**
 * @brief Helper to call a method on a dynamically loaded driver
 *
 * For dynamic drivers, we need to set r9 (GOT base) before calling any method.
 * This macro saves r9, sets it to the driver's GOT base, calls the method,
 * and restores r9.
 *
 * Usage: CALL_DRIVER_METHOD(driver, getInfo);
 *        CALL_DRIVER_METHOD(driver, read, buffer, size);
 */
#if defined(__GNUC__) && defined(__arm__)

// Internal helper function to save/restore r9
// Using naked/noinline to prevent compiler from optimizing away r9 handling
static inline __attribute__((always_inline)) uint32_t _driver_save_r9(void) {
    uint32_t saved;
    __asm__ volatile("mov %0, r9" : "=r"(saved) : : "memory");
    return saved;
}

static inline __attribute__((always_inline)) void _driver_set_r9(uint32_t val) {
    __asm__ volatile("mov r9, %0" : : "r"(val) : "r9", "memory");
}

static inline __attribute__((always_inline)) void _driver_restore_r9(uint32_t val) {
    __asm__ volatile("mov r9, %0" : : "r"(val) : "r9", "memory");
}

// Helper macro to call driver method with proper GOT setup
// For dynamic drivers, we need to set r9 (GOT base) before calling any method
// and restore it afterward to avoid corrupting kernel's GOT pointer
//
// IMPORTANT: We save r9 BEFORE evaluating gotBase because getGotBase() 
// might use r9 (kernel's GOT) to access the driver object
#define CALL_DRIVER_METHOD(driver, method, ...) \
    ([&]() -> decltype(auto) { \
        auto* _drv = (driver); \
        uint32_t _savedR9 = _driver_save_r9(); \
        uint32_t _gotBase = _drv->getGotBase(); \
        if (_gotBase != 0) { \
            _driver_set_r9(_gotBase); \
            auto _result = _drv->method(__VA_ARGS__); \
            _driver_restore_r9(_savedR9); \
            return _result; \
        } else { \
            _driver_restore_r9(_savedR9); \
            return _drv->method(__VA_ARGS__); \
        } \
    }())

// Void version for methods that return void
#define CALL_DRIVER_METHOD_VOID(driver, method, ...) \
    do { \
        auto* _drv = (driver); \
        uint32_t _savedR9 = _driver_save_r9(); \
        uint32_t _gotBase = _drv->getGotBase(); \
        if (_gotBase != 0) { \
            _driver_set_r9(_gotBase); \
            _drv->method(__VA_ARGS__); \
            _driver_restore_r9(_savedR9); \
        } else { \
            _driver_restore_r9(_savedR9); \
        } \
    } while(0)

#else
// For non-ARM targets, just call directly
#define CALL_DRIVER_METHOD(driver, method, ...) ((driver)->method(__VA_ARGS__))
#define CALL_DRIVER_METHOD_VOID(driver, method, ...) ((driver)->method(__VA_ARGS__))
#endif

namespace CRTOS
{
namespace Drivers
{

// Driver operation result codes
enum class DriverResult : int32_t
{
    SUCCESS         = 0,
    ERROR_GENERIC   = -1,
    ERROR_NOT_INIT  = -2,
    ERROR_BUSY      = -3,
    ERROR_TIMEOUT   = -4,
    ERROR_NO_DATA   = -5,
    ERROR_OVERFLOW  = -6,
    ERROR_INVALID   = -7,
    ERROR_NOT_OPEN  = -8,
    ERROR_NO_MEMORY = -9,
    ERROR_NOT_SUPPORTED = -10
};

// Driver state
enum class DriverState : uint8_t
{
    UNINITIALIZED,
    INITIALIZED,
    OPENED,
    RUNNING,
    ERROR
};

// Driver type for identification
enum class DriverType : uint8_t
{
    UNKNOWN,
    CHAR_DEVICE,    // Character device (UART, SPI, I2C, etc.)
    BLOCK_DEVICE,   // Block device (SD card, Flash, etc.)
    NETWORK,        // Network device (Ethernet, WiFi, etc.)
    DISPLAY,        // Display device
    INPUT           // Input device (keyboard, touch, etc.)
};

// IOCTL command base - each driver adds specific commands starting from these bases
constexpr uint32_t IOCTL_BASE_COMMON    = 0x0000;
constexpr uint32_t IOCTL_BASE_UART      = 0x0100;
constexpr uint32_t IOCTL_BASE_SPI       = 0x0200;
constexpr uint32_t IOCTL_BASE_I2C       = 0x0300;
constexpr uint32_t IOCTL_BASE_GPIO      = 0x0400;
constexpr uint32_t IOCTL_BASE_DISPLAY   = 0x0500;

// Common IOCTL commands
enum class CommonIoctl : uint32_t
{
    GET_STATE       = IOCTL_BASE_COMMON + 0,
    GET_VERSION     = IOCTL_BASE_COMMON + 1,
    GET_NAME        = IOCTL_BASE_COMMON + 2,
    RESET           = IOCTL_BASE_COMMON + 3,
    FLUSH           = IOCTL_BASE_COMMON + 4,
    SET_BLOCKING    = IOCTL_BASE_COMMON + 5,
    SET_NONBLOCKING = IOCTL_BASE_COMMON + 6,
    GET_RX_PENDING  = IOCTL_BASE_COMMON + 7,
    GET_TX_PENDING  = IOCTL_BASE_COMMON + 8
};

// Driver information structure
struct DriverInfo
{
    const char*     name;           // Driver name
    const char*     description;    // Driver description
    uint16_t        versionMajor;   // Major version
    uint16_t        versionMinor;   // Minor version
    DriverType      type;           // Driver type
};

/**
 * @brief Abstract base class for all kernel drivers
 *
 * This class defines the standard interface that all drivers must implement.
 * Drivers do not directly access hardware registers - they use HAL functions.
 */
class DriverBase
{
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~DriverBase() = default;

    /**
     * @brief Initialize the driver
     * 
     * Called once when the driver is loaded. Sets up internal data structures
     * and configures the HAL layer.
     *
     * @return DriverResult::SUCCESS on success, error code otherwise
     */
    virtual DriverResult init() = 0;

    /**
     * @brief Deinitialize the driver
     *
     * Called when driver is being unloaded. Releases all resources.
     *
     * @return DriverResult::SUCCESS on success, error code otherwise
     */
    virtual DriverResult deinit() = 0;

    /**
     * @brief Open the driver for use
     *
     * Must be called before read/write operations. May be called multiple times
     * for reference counting.
     *
     * @return DriverResult::SUCCESS on success, error code otherwise
     */
    virtual DriverResult open() = 0;

    /**
     * @brief Close the driver
     *
     * Decrements reference count. When count reaches zero, driver may release resources.
     *
     * @return DriverResult::SUCCESS on success, error code otherwise
     */
    virtual DriverResult close() = 0;

    /**
     * @brief Read data from the driver
     *
     * @param buffer    Pointer to buffer to store read data
     * @param size      Maximum number of bytes to read
     * @param timeoutMs Timeout in milliseconds (0 = non-blocking)
     * @return Number of bytes read, or negative error code
     */
    virtual int32_t read(void* buffer, size_t size, uint32_t timeoutMs = 0) = 0;

    /**
     * @brief Write data to the driver
     *
     * @param buffer    Pointer to data to write
     * @param size      Number of bytes to write
     * @param timeoutMs Timeout in milliseconds (0 = non-blocking)
     * @return Number of bytes written, or negative error code
     */
    virtual int32_t write(const void* buffer, size_t size, uint32_t timeoutMs = 0) = 0;

    /**
     * @brief Perform I/O control operation
     *
     * @param cmd   Command code (driver-specific)
     * @param arg   Command argument (interpretation depends on cmd)
     * @return DriverResult::SUCCESS or driver-specific value on success, error code otherwise
     */
    virtual DriverResult ioctl(uint32_t cmd, void* arg) = 0;

    /**
     * @brief Get driver information
     *
     * @return Pointer to driver info structure
     */
    virtual const DriverInfo* getInfo() const = 0;

    /**
     * @brief Get current driver state
     *
     * @return Current state of the driver
     */
    DriverState getState() const { return _state; }

    /**
     * @brief Get driver name
     *
     * @return Driver name string
     */
    const char* getName() const { return getInfo()->name; }

    /**
     * @brief Set GOT base for dynamically loaded drivers
     *
     * This is called by ModuleLoader after loading a driver module.
     * For statically linked drivers, this is not used (remains 0).
     *
     * @param gotBase The GOT base address for the module
     */
    void setGotBase(uint32_t gotBase) { _gotBase = gotBase; }

    /**
     * @brief Get GOT base for this driver
     *
     * @return GOT base address (0 for static drivers)
     */
    uint32_t getGotBase() const { return _gotBase; }

    /**
     * @brief Check if this is a dynamically loaded driver
     *
     * @return true if driver was loaded from a module
     */
    bool isDynamic() const { return _gotBase != 0; }

protected:
    DriverState _state = DriverState::UNINITIALIZED;
    uint32_t _openCount = 0;
    bool _isBlocking = true;
    uint32_t _gotBase = 0;  ///< GOT base for dynamic modules (0 for static)
};

/**
 * @brief Driver module entry point function type
 *
 * Each dynamically loadable driver module exports a function of this type
 * named "driver_module_init" that creates and returns the driver instance.
 */
typedef DriverBase* (*DriverModuleInit)();

/**
 * @brief Driver module exit point function type
 *
 * Called when unloading a driver module to clean up.
 */
typedef void (*DriverModuleExit)(DriverBase* driver);

} // namespace Drivers
} // namespace CRTOS

#endif // DRIVER_BASE_HPP
