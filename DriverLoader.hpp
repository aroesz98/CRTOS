/*
 * DriverLoader.hpp - CRTOS Driver Module Loader Helper
 * Author: Arkadiusz Szlanta
 * Date: 03 Jan 2026
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * Helper functions to load driver modules from SD card at CRTOS startup.
 */

#ifndef DRIVER_LOADER_HPP
#define DRIVER_LOADER_HPP

#include <cstdint>
#include <cstddef>

namespace CRTOS
{

/**
 * @brief Default path for driver modules on SD card
 */
constexpr const char* DEFAULT_DRIVERS_PATH = "/drivers";

/**
 * @brief Initialize driver loading subsystem
 *
 * This function:
 * 1. Initializes ModuleLoader
 * 2. Registers kernel symbols for use by modules
 *
 * Should be called once during system startup, before loading modules.
 */
void InitDriverLoader();

/**
 * @brief Load all driver modules from SD card
 *
 * This function:
 * 1. Checks if SD card is mounted
 * 2. Scans the drivers directory for .ko files
 * 3. Loads each module using ModuleLoader
 *
 * @param driversPath Path to drivers directory (default: "/drivers")
 * @return Number of successfully loaded modules
 */
size_t LoadDriverModules(const char* driversPath = DEFAULT_DRIVERS_PATH);

/**
 * @brief Load a single driver module by name
 *
 * @param moduleName Module name without path or extension (e.g., "uart")
 * @param driversPath Path to drivers directory (default: "/drivers")
 * @return true if module loaded successfully, false otherwise
 */
bool LoadDriver(const char* moduleName, const char* driversPath = DEFAULT_DRIVERS_PATH);

/**
 * @brief Unload a driver module by name
 *
 * @param moduleName Module name (e.g., "uart")
 * @return true if module unloaded successfully, false otherwise
 */
bool UnloadDriver(const char* moduleName);

/**
 * @brief Print loaded drivers info
 */
void PrintLoadedDrivers();

} // namespace CRTOS

#endif // DRIVER_LOADER_HPP
