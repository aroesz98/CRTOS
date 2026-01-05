/*
 * DriverLoader.cpp - CRTOS Driver Module Loader Helper Implementation
 * Author: Arkadiusz Szlanta
 * Date: 03 Jan 2026
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 */

#include "DriverLoader.hpp"
#include "ModuleLoader.hpp"
#include "Drivers/DriverManager.hpp"
#include "HAL/FileSystem.hpp"
#include "fsl_debug_console.h"
#include <cstdio>
#include <cstring>

namespace CRTOS
{

static bool s_driverLoaderInitialized = false;

void InitDriverLoader()
{
    if (s_driverLoaderInitialized)
    {
        return;
    }

    PRINTF("DriverLoader: Initializing driver loading subsystem...\r\n");

    // Initialize ModuleLoader (registers default symbols)
    ModuleLoader& loader = ModuleLoader::getInstance();
    loader.init();

    // Register additional kernel symbols via KernelExports
    RegisterKernelSymbols();

    s_driverLoaderInitialized = true;
    PRINTF("DriverLoader: Initialization complete\r\n");
}

size_t LoadDriverModules(const char* driversPath)
{
    // Ensure driver loader is initialized
    if (!s_driverLoaderInitialized)
    {
        InitDriverLoader();
    }

    // Check if filesystem is mounted
    HAL::FileSystem& fs = HAL::GetFileSystem();
    if (!fs.IsMounted())
    {
        PRINTF("DriverLoader: Filesystem not mounted, cannot load drivers\r\n");
        return 0;
    }

    PRINTF("DriverLoader: Loading modules from '%s'...\r\n", driversPath);

    // Use ModuleLoader to scan and load all modules from directory
    ModuleLoader& loader = ModuleLoader::getInstance();
    size_t count = loader.loadModulesFromDirectory(driversPath);

    PRINTF("DriverLoader: Loaded %u driver module(s)\r\n", (unsigned)count);

    return count;
}

bool LoadDriver(const char* moduleName, const char* driversPath)
{
    // Ensure driver loader is initialized
    if (!s_driverLoaderInitialized)
    {
        InitDriverLoader();
    }

    // Check if filesystem is mounted
    HAL::FileSystem& fs = HAL::GetFileSystem();
    if (!fs.IsMounted())
    {
        PRINTF("DriverLoader: Filesystem not mounted, cannot load driver '%s'\r\n", moduleName);
        return false;
    }

    // Build full path
    char fullPath[256];
    snprintf(fullPath, sizeof(fullPath), "%s/%s.ko", driversPath, moduleName);

    PRINTF("DriverLoader: Loading driver from '%s'...\r\n", fullPath);

    ModuleLoader& loader = ModuleLoader::getInstance();
    ModuleResult result = loader.loadModuleFromFile(fullPath);

    if (result == ModuleResult::SUCCESS)
    {
        PRINTF("DriverLoader: Driver '%s' loaded successfully\r\n", moduleName);
        return true;
    }
    else
    {
        PRINTF("DriverLoader: Failed to load driver '%s' (error: %d)\r\n", moduleName, (int)result);
        return false;
    }
}

bool UnloadDriver(const char* moduleName)
{
    if (!s_driverLoaderInitialized)
    {
        return false;
    }

    ModuleLoader& loader = ModuleLoader::getInstance();
    ModuleResult result = loader.unloadModule(moduleName);

    if (result == ModuleResult::SUCCESS)
    {
        PRINTF("DriverLoader: Driver '%s' unloaded successfully\r\n", moduleName);
        return true;
    }
    else
    {
        PRINTF("DriverLoader: Failed to unload driver '%s' (error: %d)\r\n", moduleName, (int)result);
        return false;
    }
}

void PrintLoadedDrivers()
{
    PRINTF("=== Loaded Drivers ===\r\n");
    
    // Print modules from ModuleLoader
    ModuleLoader& loader = ModuleLoader::getInstance();
    loader.listModules();

    // Print registered drivers from DriverManager
    Drivers::DriverManager& mgr = Drivers::DriverManager::getInstance();
    mgr.listDrivers();
}

} // namespace CRTOS
