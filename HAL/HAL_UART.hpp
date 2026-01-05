/*
 * HAL_UART.hpp - Hardware Abstraction Layer for UART
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
 * Hardware abstraction layer interface for UART communication.
 * Provides a hardware-independent API for UART operations.
 * Supports multiple UART instances (LPUART1-8 on i.MX RT1052).
 * 
 * This is a pure HAL layer - no kernel/driver dependencies.
 * Drivers should use these functions and handle DPC registration themselves.
 */

#ifndef HAL_UART_HPP
#define HAL_UART_HPP

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UART instance identifiers
 */
typedef enum {
    HAL_UART_1 = 0,  // LPUART1 (typically debug console)
    HAL_UART_2 = 1,  // LPUART2
    HAL_UART_3 = 2,  // LPUART3
    HAL_UART_4 = 3,  // LPUART4
    HAL_UART_5 = 4,  // LPUART5
    HAL_UART_6 = 5,  // LPUART6
    HAL_UART_7 = 6,  // LPUART7
    HAL_UART_8 = 7,  // LPUART8
    HAL_UART_COUNT = 8
} hal_uart_instance_t;

/**
 * @brief UART configuration structure
 */
typedef struct {
    uint32_t baudrate;      // Baudrate (e.g., 115200)
    uint8_t  dataBits;      // Data bits: 7, 8, or 9
    uint8_t  stopBits;      // Stop bits: 1 or 2
    uint8_t  parity;        // Parity: 0=none, 1=odd, 2=even
    bool     enableRx;      // Enable RX
    bool     enableTx;      // Enable TX
} hal_uart_config_t;

/**
 * @brief UART status flags
 */
typedef enum {
    HAL_UART_STATUS_RX_READY     = (1 << 0),  // Data available in RX register
    HAL_UART_STATUS_TX_READY     = (1 << 1),  // TX register ready for data
    HAL_UART_STATUS_TX_COMPLETE  = (1 << 2),  // Transmission complete
    HAL_UART_STATUS_RX_OVERRUN   = (1 << 3),  // RX overrun error
    HAL_UART_STATUS_FRAMING_ERR  = (1 << 4),  // Framing error
    HAL_UART_STATUS_PARITY_ERR   = (1 << 5),  // Parity error
    HAL_UART_STATUS_NOISE_ERR    = (1 << 6),  // Noise error
} hal_uart_status_t;

// ============================================================================
// Initialization API
// ============================================================================

/**
 * @brief Get default configuration
 */
void hal_uart_get_default_config(hal_uart_config_t* config);

/**
 * @brief Initialize UART instance with configuration
 */
bool hal_uart_init_instance(hal_uart_instance_t instance, const hal_uart_config_t* config);

/**
 * @brief Deinitialize UART instance
 */
void hal_uart_deinit_instance(hal_uart_instance_t instance);

/**
 * @brief Check if UART instance is initialized
 */
bool hal_uart_is_initialized_instance(hal_uart_instance_t instance);

// ============================================================================
// Data Transfer API
// ============================================================================

/**
 * @brief Write a single byte (blocking - waits for TX ready)
 */
void hal_uart_write_byte_instance(hal_uart_instance_t instance, uint8_t byte);

/**
 * @brief Write multiple bytes (blocking)
 */
void hal_uart_write_instance(hal_uart_instance_t instance, const uint8_t* data, size_t size);

/**
 * @brief Read a single byte if available (non-blocking)
 * @return true if byte was read, false if no data
 */
bool hal_uart_read_byte_instance(hal_uart_instance_t instance, uint8_t* byte);

/**
 * @brief Flush TX - wait until all data transmitted
 */
void hal_uart_flush_tx_instance(hal_uart_instance_t instance);

// ============================================================================
// Status & IRQ API (for driver interrupt handling)
// ============================================================================

/**
 * @brief Get current status flags
 * @return Bitmask of hal_uart_status_t flags
 */
uint32_t hal_uart_get_status(hal_uart_instance_t instance);

/**
 * @brief Clear error flags (overrun, framing, parity, noise)
 */
void hal_uart_clear_errors(hal_uart_instance_t instance);

/**
 * @brief Get IRQ number for this UART instance
 * 
 * Driver uses this to register with DPC dispatcher.
 */
uint32_t hal_uart_get_irq_number(hal_uart_instance_t instance);

/**
 * @brief Enable RX data interrupt at peripheral level
 */
void hal_uart_enable_rx_irq(hal_uart_instance_t instance);

/**
 * @brief Disable RX data interrupt at peripheral level
 */
void hal_uart_disable_rx_irq(hal_uart_instance_t instance);

/**
 * @brief Enable TX ready interrupt at peripheral level
 */
void hal_uart_enable_tx_irq(hal_uart_instance_t instance);

/**
 * @brief Disable TX ready interrupt at peripheral level
 */
void hal_uart_disable_tx_irq(hal_uart_instance_t instance);

// ============================================================================
// Legacy API (backward compatibility) - uses HAL_UART_2
// ============================================================================

void hal_uart_init(uint32_t baudrate);
void hal_uart_deinit(void);
void hal_uart_write_byte(uint8_t byte);
void hal_uart_write(const uint8_t* data, size_t size);
bool hal_uart_read_byte(uint8_t* byte);
bool hal_uart_is_initialized(void);
void hal_uart_flush_tx(void);

#ifdef __cplusplus
}
#endif

#endif // HAL_UART_HPP