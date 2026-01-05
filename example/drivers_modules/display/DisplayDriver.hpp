/*
 * DisplayDriver.hpp - CRTOS Display Driver Module
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
 * Display driver module implementing the DriverBase interface.
 * This driver is compiled separately from the kernel.
 *
 * Features:
 * - Uses HAL layer for hardware access (eLCDIF controller)
 * - Supports RGB565, RGB888, XRGB8888 pixel formats
 * - Double buffering with VSYNC synchronization via HAL
 */

#ifndef DISPLAY_DRIVER_HPP
#define DISPLAY_DRIVER_HPP

#include <CRTOS/Drivers/DriverBase.hpp>
#include <CRTOS/Drivers/DisplayIoctl.hpp>
#include "hal_display.h"

namespace CRTOS
{
namespace Drivers
{

/**
 * @brief Display Driver Class
 *
 * Implements the DriverBase interface for eLCDIF display controller.
 * Uses HAL functions for hardware access.
 * VSYNC synchronization is handled via hal_display_wait_vsync().
 */
class DisplayDriver : public DriverBase
{
public:
    DisplayDriver();
    virtual ~DisplayDriver();

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
    static DisplayDriver* getInstance();

private:
    static DisplayDriver* _instance;

    hal_display_config_t _config;

    struct Stats
    {
        uint32_t frameCount;
        uint32_t vsyncCount;
        uint32_t swapCount;
    } _stats;

    static const DriverInfo _driverInfo;
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

#endif // DISPLAY_DRIVER_HPP
