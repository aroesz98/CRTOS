/*
 * DriverManager.cpp - CRTOS Driver Manager Implementation
 * Author: Arkadiusz Szlanta
 * Date: 26 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 */

#include "DriverManager.hpp"
#include <cstring>
#include <cstdio>
#include "fsl_debug_console.h"  // For PRINTF

namespace CRTOS
{
namespace Drivers
{

// Singleton instance
DriverManager& DriverManager::getInstance()
{
    static DriverManager instance;
    return instance;
}

DriverManager::DriverManager()
    : _driverCount(0)
    , _initialized(false)
{
    memset(_drivers, 0, sizeof(_drivers));
}

DriverManager::~DriverManager()
{
    deinitAll();
}

DriverResult DriverManager::init()
{
    if (_initialized)
    {
        return DriverResult::SUCCESS;
    }

    // Clear driver registry
    memset(_drivers, 0, sizeof(_drivers));
    _driverCount = 0;
    _initialized = true;

    return DriverResult::SUCCESS;
}

DriverResult DriverManager::registerDriver(DriverBase* driver, bool isDynamic)
{
    if (driver == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }

    // Initialize manager if not already done
    if (!_initialized)
    {
        init();
    }

    // Check for duplicate registration - use CALL_DRIVER_METHOD for dynamic driver
    const char* name = CALL_DRIVER_METHOD(driver, getInfo)->name;
    for (size_t i = 0; i < MAX_DRIVERS; i++)
    {
        if (_drivers[i].isValid && _drivers[i].driver != nullptr)
        {
            if (strcmp(CALL_DRIVER_METHOD(_drivers[i].driver, getInfo)->name, name) == 0)
            {
                return DriverResult::ERROR_BUSY; // Already registered
            }
        }
    }

    // Find empty slot
    for (size_t i = 0; i < MAX_DRIVERS; i++)
    {
        if (!_drivers[i].isValid)
        {
            _drivers[i].driver = driver;
            _drivers[i].isDynamic = isDynamic;
            _drivers[i].isValid = true;
            _driverCount++;

            // Initialize the driver (use macro for dynamic drivers)
            DriverResult result = CALL_DRIVER_METHOD(driver, init);
            if (result != DriverResult::SUCCESS)
            {
                // Registration succeeded but init failed - keep it registered
                // The driver is in ERROR state and caller should handle it
            }

            return DriverResult::SUCCESS;
        }
    }

    return DriverResult::ERROR_NO_MEMORY; // No free slots
}

DriverResult DriverManager::unregisterDriver(const char* name)
{
    if (name == nullptr)
    {
        return DriverResult::ERROR_INVALID;
    }

    for (size_t i = 0; i < MAX_DRIVERS; i++)
    {
        if (_drivers[i].isValid && _drivers[i].driver != nullptr)
        {
            if (strcmp(CALL_DRIVER_METHOD(_drivers[i].driver, getInfo)->name, name) == 0)
            {
                // Deinitialize the driver (use macro for dynamic drivers)
                (void)CALL_DRIVER_METHOD(_drivers[i].driver, deinit);

                // Delete if dynamically allocated
                if (_drivers[i].isDynamic)
                {
                    delete _drivers[i].driver;
                }

                // Clear the slot
                _drivers[i].driver = nullptr;
                _drivers[i].isDynamic = false;
                _drivers[i].isValid = false;
                _driverCount--;

                return DriverResult::SUCCESS;
            }
        }
    }

    return DriverResult::ERROR_INVALID; // Not found
}

DriverBase* DriverManager::findDriver(const char* name)
{
    if (name == nullptr)
    {
        return nullptr;
    }

    for (size_t i = 0; i < MAX_DRIVERS; i++)
    {
        if (_drivers[i].isValid && _drivers[i].driver != nullptr)
        {
            // Debug: Get driver info safely
            const DriverInfo* info = CALL_DRIVER_METHOD(_drivers[i].driver, getInfo);
            if (info == nullptr)
            {
                PRINTF("[DriverMgr] findDriver: driver[%u] getInfo returned nullptr\r\n", (unsigned)i);
                continue;
            }
            
            // Debug: Check info->name pointer
            // Valid addresses: Flash (0x6000xxxx), SDRAM (0x8000xxxx), SRAM (0x2000xxxx)
            uint32_t nameAddr = (uint32_t)info->name;
            if (info->name == nullptr || 
                (nameAddr < 0x20000000) ||
                (nameAddr >= 0x30000000 && nameAddr < 0x60000000) ||
                (nameAddr >= 0x70000000 && nameAddr < 0x80000000))
            {
                PRINTF("[DriverMgr] findDriver: driver[%u] info->name invalid: 0x%08lX\r\n", 
                       (unsigned)i, nameAddr);
                continue;
            }
            
            if (strcmp(info->name, name) == 0)
            {
                return _drivers[i].driver;
            }
        }
    }

    return nullptr;
}

DriverBase* DriverManager::getDriver(size_t index)
{
    size_t count = 0;
    for (size_t i = 0; i < MAX_DRIVERS; i++)
    {
        if (_drivers[i].isValid && _drivers[i].driver != nullptr)
        {
            if (count == index)
            {
                return _drivers[i].driver;
            }
            count++;
        }
    }
    return nullptr;
}

DriverResult DriverManager::initAll()
{
    DriverResult overallResult = DriverResult::SUCCESS;

    for (size_t i = 0; i < MAX_DRIVERS; i++)
    {
        if (_drivers[i].isValid && _drivers[i].driver != nullptr)
        {
            DriverBase* driver = _drivers[i].driver;
            DriverState state = CALL_DRIVER_METHOD(driver, getState);
            if (state == DriverState::UNINITIALIZED)
            {
                DriverResult result = CALL_DRIVER_METHOD(driver, init);
                if (result != DriverResult::SUCCESS)
                {
                    overallResult = result;
                }
            }
        }
    }

    return overallResult;
}

DriverResult DriverManager::deinitAll()
{
    DriverResult overallResult = DriverResult::SUCCESS;

    for (size_t i = 0; i < MAX_DRIVERS; i++)
    {
        if (_drivers[i].isValid && _drivers[i].driver != nullptr)
        {
            DriverBase* driver = _drivers[i].driver;
            DriverState state = CALL_DRIVER_METHOD(driver, getState);
            if (state != DriverState::UNINITIALIZED)
            {
                DriverResult result = CALL_DRIVER_METHOD(driver, deinit);
                if (result != DriverResult::SUCCESS)
                {
                    overallResult = result;
                }
            }

            // Delete dynamic drivers
            if (_drivers[i].isDynamic)
            {
                delete driver;
            }

            _drivers[i].driver = nullptr;
            _drivers[i].isValid = false;
        }
    }

    _driverCount = 0;
    return overallResult;
}

void DriverManager::listDrivers()
{
    PRINTF("=== Registered Drivers (%u) ===\r\n", (unsigned)_driverCount);
    
    for (size_t i = 0; i < MAX_DRIVERS; i++)
    {
        if (_drivers[i].isValid && _drivers[i].driver != nullptr)
        {
            // Use CALL_DRIVER_METHOD for dynamic drivers (sets r9 to GOT base)
            const DriverInfo* info = CALL_DRIVER_METHOD(_drivers[i].driver, getInfo);
            DriverState state = CALL_DRIVER_METHOD(_drivers[i].driver, getState);
            const char* stateStr = "UNKNOWN";
            
            switch (state)
            {
                case DriverState::UNINITIALIZED: stateStr = "UNINIT"; break;
                case DriverState::INITIALIZED:   stateStr = "INIT"; break;
                case DriverState::OPENED:        stateStr = "OPEN"; break;
                case DriverState::RUNNING:       stateStr = "RUN"; break;
                case DriverState::ERROR:         stateStr = "ERR"; break;
            }

            PRINTF("  [%u] %-12s v%u.%u [%s] %s\r\n",
                   (unsigned)i,
                   info->name,
                   info->versionMajor,
                   info->versionMinor,
                   stateStr,
                   _drivers[i].isDynamic ? "(dynamic)" : "(static)");
        }
    }
}

} // namespace Drivers
} // namespace CRTOS
