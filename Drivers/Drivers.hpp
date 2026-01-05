/*
 * Drivers.hpp - CRTOS Driver System Header
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
 * Main header file for the CRTOS driver system.
 * Include this file to get access to driver interfaces.
 * 
 * Note: Driver implementations are compiled separately in drivers_modules/
 *
 * Usage:
 *   #include "CRTOS/Drivers/Drivers.hpp"
 *   
 *   // Get UART driver
 *   auto uart = CRTOS::Drivers::getDriver("uart");
 *   if (uart) {
 *       uart->open();
 *       uart->write("Hello", 5);
 *       uart->close();
 *   }
 */

#ifndef CRTOS_DRIVERS_HPP
#define CRTOS_DRIVERS_HPP

#include "DriverBase.hpp"
#include "DriverManager.hpp"
#include "RingBuffer.hpp"

namespace CRTOS
{
namespace Drivers
{

/**
 * @brief Initialize the driver system
 *
 * Must be called before using any drivers.
 * Typically called during system startup.
 *
 * @return DriverResult::SUCCESS on success
 */
inline DriverResult initDriverSystem()
{
    return DriverManager::getInstance().init();
}

/**
 * @brief Shutdown the driver system
 *
 * Deinitializes all drivers and releases resources.
 * Called during system shutdown.
 *
 * @return DriverResult::SUCCESS on success
 */
inline DriverResult shutdownDriverSystem()
{
    return DriverManager::getInstance().deinitAll();
}

/**
 * @brief Load a driver module
 *
 * For statically linked drivers, call driver_module_init() and register.
 * For dynamic loading, use ELFParser to load the module.
 *
 * @param moduleInit    Pointer to driver_module_init function
 * @return DriverResult::SUCCESS on success
 */
inline DriverResult loadDriverModule(DriverModuleInit moduleInit)
{
    if (moduleInit == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    DriverBase* driver = moduleInit();
    if (driver == nullptr)
    {
        return DriverResult::ERROR_NO_MEMORY;
    }
    
    return DriverManager::getInstance().registerDriver(driver, true);
}

/**
 * @brief Unload a driver module
 *
 * @param name          Driver name
 * @param moduleExit    Pointer to driver_module_exit function (optional)
 * @return DriverResult::SUCCESS on success
 */
inline DriverResult unloadDriverModule(const char* name, DriverModuleExit moduleExit = nullptr)
{
    DriverBase* driver = DriverManager::getInstance().findDriver(name);
    if (driver == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }
    
    DriverResult result = DriverManager::getInstance().unregisterDriver(name);
    
    if (result == DriverResult::SUCCESS && moduleExit != nullptr)
    {
        moduleExit(driver);
    }
    
    return result;
}

/**
 * @brief Get a driver by name - typed version
 *
 * @tparam T        Driver type to cast to
 * @param name      Driver name
 * @return Pointer to driver of type T, or nullptr if not found/wrong type
 */
template <typename T>
T* getTypedDriver(const char* name)
{
    DriverBase* driver = DriverManager::getInstance().findDriver(name);
    return static_cast<T*>(driver);
}

/**
 * @brief Print list of registered drivers (debug)
 */
inline void listAllDrivers()
{
    DriverManager::getInstance().listDrivers();
}

} // namespace Drivers
} // namespace CRTOS

#endif // CRTOS_DRIVERS_HPP
