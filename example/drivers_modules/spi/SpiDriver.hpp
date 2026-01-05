/*
 * SpiDriver.hpp - CRTOS SPI Driver Module
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
 * SPI driver module implementing the DriverBase interface.
 * This driver is compiled separately from the kernel.
 *
 * Features:
 * - Uses HAL layer for hardware access (LPSPI controller)
 * - Supports LPSPI1-4 instances
 * - Blocking, interrupt, and DMA transfer modes
 * - Configurable SPI mode (0-3), baud rate, and chip select
 */

#ifndef SPI_DRIVER_HPP
#define SPI_DRIVER_HPP

#include <CRTOS/Drivers/DriverBase.hpp>
#include <CRTOS/Drivers/SpiIoctl.hpp>
#include "hal_spi.h"

namespace CRTOS
{
namespace Drivers
{

/**
 * @brief SPI Driver Class
 *
 * Implements the DriverBase interface for LPSPI controller.
 * Uses HAL functions for hardware access.
 * Each instance of this driver manages one LPSPI peripheral.
 */
class SpiDriver : public DriverBase
{
public:
    /**
     * @brief Constructor
     * @param instance SPI instance (1-4)
     */
    explicit SpiDriver(hal_spi_instance_t instance = HAL_SPI_INSTANCE_1);
    virtual ~SpiDriver();

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
     * @brief Get singleton instance for SPI1
     * @note For multiple SPI instances, create separate driver instances
     */
    static SpiDriver* getInstance();

    /**
     * @brief Set SPI instance
     * @param instance SPI instance (1-4)
     */
    void setInstance(hal_spi_instance_t instance) { _spiInstance = instance; }

    /**
     * @brief Get current SPI instance
     */
    hal_spi_instance_t getSpiInstance() const { return _spiInstance; }

private:
    static SpiDriver* _instance;

    hal_spi_instance_t _spiInstance;
    hal_spi_config_t _config;

    struct Stats
    {
        uint32_t txBytes;
        uint32_t rxBytes;
        uint32_t transferCount;
        uint32_t errorCount;
    } _stats;

    static const DriverInfo _driverInfo;

    // Helper methods
    DriverResult handleSetBaudrate(void* arg);
    DriverResult handleGetBaudrate(void* arg);
    DriverResult handleSetMode(void* arg);
    DriverResult handleGetMode(void* arg);
    DriverResult handleSetPcs(void* arg);
    DriverResult handleGetPcs(void* arg);
    DriverResult handleSetTransferMode(void* arg);
    DriverResult handleGetTransferMode(void* arg);
    DriverResult handleSetBitsPerFrame(void* arg);
    DriverResult handleGetBitsPerFrame(void* arg);
    DriverResult handleSetPcsContinuous(void* arg);
    DriverResult handleGetStatus(void* arg);
    DriverResult handleAbortTransfer();
    DriverResult handleWaitComplete(void* arg);
    DriverResult handleTransfer(void* arg);
    DriverResult handleGetClockFreq(void* arg);
    DriverResult handleSetDelays(void* arg);
    DriverResult handleGetDelays(void* arg);
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

#endif // SPI_DRIVER_HPP
