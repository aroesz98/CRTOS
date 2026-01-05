/*
 * UartDriver.hpp - CRTOS UART Driver Module
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
 * UART driver module implementing the DriverBase interface.
 * This driver is compiled separately from the kernel.
 *
 * Features:
 * - Uses HAL layer for hardware access (no direct register manipulation)
 * - Supports multiple UART instances (LPUART1-8)
 * - Interrupt-driven RX with DPC for deferred processing
 * - Ring buffers for RX and TX
 * - Configurable baud rate via ioctl
 */

#ifndef UART_DRIVER_HPP
#define UART_DRIVER_HPP

#include <CRTOS/Drivers/DriverBase.hpp>
#include <CRTOS/Drivers/RingBuffer.hpp>
#include <CRTOS/BinarySemaphore.hpp>
#include "hal_uart.h"

namespace CRTOS
{
namespace Drivers
{

// UART-specific IOCTL commands
enum class UartIoctl : uint32_t
{
    SET_BAUDRATE    = IOCTL_BASE_UART + 0,
    GET_BAUDRATE    = IOCTL_BASE_UART + 1,
    SET_PARITY      = IOCTL_BASE_UART + 2,
    GET_PARITY      = IOCTL_BASE_UART + 3,
    SET_STOP_BITS   = IOCTL_BASE_UART + 4,
    GET_STOP_BITS   = IOCTL_BASE_UART + 5,
    SET_DATA_BITS   = IOCTL_BASE_UART + 6,
    GET_DATA_BITS   = IOCTL_BASE_UART + 7,
    CLEAR_RX_BUFFER = IOCTL_BASE_UART + 8,
    CLEAR_TX_BUFFER = IOCTL_BASE_UART + 9,
    GET_RX_COUNT    = IOCTL_BASE_UART + 10,
    GET_TX_COUNT    = IOCTL_BASE_UART + 11,
    SET_RX_TIMEOUT  = IOCTL_BASE_UART + 12,
    SET_INSTANCE    = IOCTL_BASE_UART + 13,  // Set UART instance (before open)
    GET_INSTANCE    = IOCTL_BASE_UART + 14,  // Get current instance
    WAIT_TX_COMPLETE = IOCTL_BASE_UART + 15
};

// Buffer sizes
constexpr size_t UART_RX_BUFFER_SIZE = 256;
constexpr size_t UART_TX_BUFFER_SIZE = 256;

/**
 * @brief UART Driver Class
 *
 * Implements the DriverBase interface for UART communication.
 * Uses HAL functions for hardware access.
 * Driver handles DPC registration and interrupt processing.
 */
class UartDriver : public DriverBase
{
public:
    UartDriver();
    virtual ~UartDriver();

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
     * @brief DPC callback - called from ISR context
     * Reads data from UART and stores in buffer
     */
    void handleIrq();

    /**
     * @brief Get singleton instance
     */
    static UartDriver* getInstance();

private:
    static UartDriver* _instance;
    static void staticDpcCallback(void* context);

    RingBuffer<uint8_t, UART_RX_BUFFER_SIZE> _rxBuffer;
    RingBuffer<uint8_t, UART_TX_BUFFER_SIZE> _txBuffer;

    BinarySemaphore* _rxDataAvailable;

    hal_uart_instance_t _uartInstance;  // Which LPUART to use
    hal_uart_config_t   _config;
    uint32_t _defaultRxTimeout;
    uint32_t _irqNumber;

    struct Stats
    {
        uint32_t rxBytes;
        uint32_t txBytes;
        uint32_t rxOverflows;
        uint32_t rxErrors;
        uint32_t irqCount;
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

#endif // UART_DRIVER_HPP
