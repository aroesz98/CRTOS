/*
 * hal_uart.h - UART HAL Interface for Driver Module
 * Author: Arkadiusz Szlanta
 * Date: 26 Dec 2025
 *
 * Description:
 * Local HAL interface header for UART driver module.
 * This allows the driver to be compiled separately while
 * still linking to the kernel's HAL implementation.
 * 
 * This is a pure HAL - no kernel dependencies.
 */

#ifndef DRIVER_HAL_UART_H
#define DRIVER_HAL_UART_H

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
    HAL_UART_1 = 0,
    HAL_UART_2 = 1,
    HAL_UART_3 = 2,
    HAL_UART_4 = 3,
    HAL_UART_5 = 4,
    HAL_UART_6 = 5,
    HAL_UART_7 = 6,
    HAL_UART_8 = 7,
    HAL_UART_COUNT = 8
} hal_uart_instance_t;

/**
 * @brief UART configuration structure
 */
typedef struct {
    uint32_t baudrate;
    uint8_t  dataBits;
    uint8_t  stopBits;
    uint8_t  parity;
    bool     enableRx;
    bool     enableTx;
} hal_uart_config_t;

/**
 * @brief UART status flags
 */
typedef enum {
    HAL_UART_STATUS_RX_READY     = (1 << 0),
    HAL_UART_STATUS_TX_READY     = (1 << 1),
    HAL_UART_STATUS_TX_COMPLETE  = (1 << 2),
    HAL_UART_STATUS_RX_OVERRUN   = (1 << 3),
    HAL_UART_STATUS_FRAMING_ERR  = (1 << 4),
    HAL_UART_STATUS_PARITY_ERR   = (1 << 5),
    HAL_UART_STATUS_NOISE_ERR    = (1 << 6),
} hal_uart_status_t;

// ============================================================================
// Initialization API
// ============================================================================

void hal_uart_get_default_config(hal_uart_config_t* config);
bool hal_uart_init_instance(hal_uart_instance_t instance, const hal_uart_config_t* config);
void hal_uart_deinit_instance(hal_uart_instance_t instance);
bool hal_uart_is_initialized_instance(hal_uart_instance_t instance);

// ============================================================================
// Data Transfer API
// ============================================================================

void hal_uart_write_byte_instance(hal_uart_instance_t instance, uint8_t byte);
void hal_uart_write_instance(hal_uart_instance_t instance, const uint8_t* data, size_t size);
bool hal_uart_read_byte_instance(hal_uart_instance_t instance, uint8_t* byte);
void hal_uart_flush_tx_instance(hal_uart_instance_t instance);

// ============================================================================
// Status & IRQ API (for driver interrupt handling)
// ============================================================================

uint32_t hal_uart_get_status(hal_uart_instance_t instance);
void hal_uart_clear_errors(hal_uart_instance_t instance);
uint32_t hal_uart_get_irq_number(hal_uart_instance_t instance);
void hal_uart_enable_rx_irq(hal_uart_instance_t instance);
void hal_uart_disable_rx_irq(hal_uart_instance_t instance);
void hal_uart_enable_tx_irq(hal_uart_instance_t instance);
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

#endif // DRIVER_HAL_UART_H
