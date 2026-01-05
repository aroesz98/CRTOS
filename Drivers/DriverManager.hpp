/*
 * DriverManager.hpp - CRTOS Driver Manager
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
 * Singleton class that manages driver registration, lookup, and lifecycle.
 * Drivers register themselves with the manager and can be looked up by name.
 * Supports both static registration (at compile time) and dynamic loading.
 */

#ifndef DRIVER_MANAGER_HPP
#define DRIVER_MANAGER_HPP

#include "DriverBase.hpp"
#include <cstdint>
#include <cstddef>

namespace CRTOS
{
namespace Drivers
{

// Maximum number of drivers that can be registered
constexpr size_t MAX_DRIVERS = 16;

/**
 * @brief Driver registration entry
 */
struct DriverEntry
{
    DriverBase* driver;         // Pointer to driver instance
    bool        isDynamic;      // True if dynamically loaded (should be freed on unload)
    bool        isValid;        // True if this entry is in use
};

/**
 * @brief Driver Manager singleton
 *
 * Central registry for all drivers in the system. Provides:
 * - Driver registration
 * - Driver lookup by name
 * - Driver lifecycle management
 * - Enumeration of registered drivers
 */
class DriverManager
{
public:
    /**
     * @brief Get the singleton instance
     *
     * @return Reference to the global DriverManager instance
     */
    static DriverManager& getInstance();

    /**
     * @brief Initialize the driver manager
     *
     * @return DriverResult::SUCCESS on success
     */
    DriverResult init();

    /**
     * @brief Register a driver with the manager
     *
     * The driver's init() method will be called during registration.
     *
     * @param driver    Pointer to driver instance
     * @param isDynamic True if driver was dynamically allocated
     * @return DriverResult::SUCCESS on success
     */
    DriverResult registerDriver(DriverBase* driver, bool isDynamic = false);

    /**
     * @brief Unregister a driver by name
     *
     * The driver's deinit() method will be called. If the driver was
     * dynamically allocated, it will be deleted.
     *
     * @param name  Driver name
     * @return DriverResult::SUCCESS on success
     */
    DriverResult unregisterDriver(const char* name);

    /**
     * @brief Find a driver by name
     *
     * @param name  Driver name to search for
     * @return Pointer to driver, or nullptr if not found
     */
    DriverBase* findDriver(const char* name);

    /**
     * @brief Get driver at specified index
     *
     * Used for enumeration of drivers.
     *
     * @param index     Index (0 to getDriverCount()-1)
     * @return Pointer to driver, or nullptr if index invalid
     */
    DriverBase* getDriver(size_t index);

    /**
     * @brief Get number of registered drivers
     *
     * @return Number of drivers
     */
    size_t getDriverCount() const { return _driverCount; }

    /**
     * @brief Initialize all registered drivers
     *
     * Calls init() on all drivers that are not yet initialized.
     *
     * @return DriverResult::SUCCESS if all succeeded
     */
    DriverResult initAll();

    /**
     * @brief Deinitialize all registered drivers
     *
     * Calls deinit() on all initialized drivers.
     *
     * @return DriverResult::SUCCESS if all succeeded
     */
    DriverResult deinitAll();

    /**
     * @brief Print list of registered drivers (debug)
     */
    void listDrivers();

private:
    // Private constructor (singleton)
    DriverManager();
    ~DriverManager();

    // Prevent copying
    DriverManager(const DriverManager&) = delete;
    DriverManager& operator=(const DriverManager&) = delete;

    // Driver registry
    DriverEntry _drivers[MAX_DRIVERS];
    size_t      _driverCount;
    bool        _initialized;
};

/**
 * @brief Helper class for static driver registration
 *
 * Use the REGISTER_DRIVER macro to create instances of this class.
 * The constructor runs before main() and registers the driver.
 */
template <typename T>
class StaticDriverRegistrar
{
public:
    StaticDriverRegistrar()
    {
        static T driverInstance;
        DriverManager::getInstance().registerDriver(&driverInstance, false);
    }
};

/**
 * @brief Macro for static driver registration
 *
 * Place this in your driver's .cpp file to auto-register the driver:
 * REGISTER_DRIVER(MyUartDriver);
 */
#define REGISTER_DRIVER(DriverClass) \
    static CRTOS::Drivers::StaticDriverRegistrar<DriverClass> _##DriverClass##_registrar

/**
 * @brief Convenience function to get a driver by name
 *
 * @param name  Driver name
 * @return Pointer to driver, or nullptr if not found
 */
inline DriverBase* getDriver(const char* name)
{
    return DriverManager::getInstance().findDriver(name);
}

} // namespace Drivers
} // namespace CRTOS

#endif // DRIVER_MANAGER_HPP
