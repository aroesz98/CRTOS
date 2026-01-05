/*
 * HAL_SPI.hpp - Hardware Abstraction Layer for SPI (C Interface)
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
 * C-linkage interface for SPI HAL, used for symbol export to loadable modules.
 * These functions wrap the C++ SPI class.
 * Supports LPSPI with interrupt and EDMA transfer modes.
 */

#ifndef HAL_SPI_HPP
#define HAL_SPI_HPP

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SPI instance enumeration (i.MX RT1052 has LPSPI1-4)
 */
typedef enum {
    HAL_SPI_INSTANCE_1 = 0,
    HAL_SPI_INSTANCE_2 = 1,
    HAL_SPI_INSTANCE_3 = 2,
    HAL_SPI_INSTANCE_4 = 3,
    HAL_SPI_INSTANCE_COUNT
} hal_spi_instance_t;

/**
 * @brief SPI mode (clock polarity and phase)
 */
typedef enum {
    HAL_SPI_MODE_0 = 0,  ///< CPOL=0, CPHA=0: Clock idle low, sample on rising edge
    HAL_SPI_MODE_1 = 1,  ///< CPOL=0, CPHA=1: Clock idle low, sample on falling edge
    HAL_SPI_MODE_2 = 2,  ///< CPOL=1, CPHA=0: Clock idle high, sample on falling edge
    HAL_SPI_MODE_3 = 3,  ///< CPOL=1, CPHA=1: Clock idle high, sample on rising edge
} hal_spi_mode_t;

/**
 * @brief SPI chip select (PCS) selection
 */
typedef enum {
    HAL_SPI_PCS_0 = 0,
    HAL_SPI_PCS_1 = 1,
    HAL_SPI_PCS_2 = 2,
    HAL_SPI_PCS_3 = 3,
} hal_spi_pcs_t;

/**
 * @brief SPI transfer mode
 */
typedef enum {
    HAL_SPI_TRANSFER_BLOCKING = 0,   ///< Blocking (polling) transfer
    HAL_SPI_TRANSFER_INTERRUPT = 1,  ///< Interrupt-driven transfer
    HAL_SPI_TRANSFER_DMA = 2,        ///< DMA (EDMA) transfer
} hal_spi_transfer_mode_t;

/**
 * @brief SPI bit order
 */
typedef enum {
    HAL_SPI_MSB_FIRST = 0,
    HAL_SPI_LSB_FIRST = 1,
} hal_spi_bit_order_t;

/**
 * @brief SPI configuration structure
 */
typedef struct {
    uint32_t baudRate;                   ///< Baud rate in Hz
    hal_spi_mode_t mode;                 ///< Clock polarity/phase mode
    hal_spi_pcs_t pcs;                   ///< Chip select pin
    hal_spi_transfer_mode_t transferMode;///< Transfer mode (blocking/interrupt/DMA)
    hal_spi_bit_order_t bitOrder;        ///< Bit order (MSB/LSB first)
    uint8_t bitsPerFrame;                ///< Bits per frame (8-32)
    uint32_t pcsToSckDelayNs;            ///< PCS to SCK delay in nanoseconds
    uint32_t sckToPcsDelayNs;            ///< Last SCK to PCS delay in nanoseconds
    uint32_t betweenTransferDelayNs;     ///< Delay between transfers in nanoseconds
    bool pcsContinuous;                  ///< Keep PCS asserted between transfers
} hal_spi_config_t;

/**
 * @brief SPI transfer descriptor
 */
typedef struct {
    const uint8_t* txData;  ///< Pointer to TX data (can be NULL for RX-only)
    uint8_t* rxData;        ///< Pointer to RX buffer (can be NULL for TX-only)
    size_t dataSize;        ///< Transfer size in bytes
} hal_spi_transfer_t;

/**
 * @brief SPI transfer status
 */
typedef enum {
    HAL_SPI_STATUS_IDLE = 0,
    HAL_SPI_STATUS_BUSY = 1,
    HAL_SPI_STATUS_COMPLETE = 2,
    HAL_SPI_STATUS_ERROR = 3,
} hal_spi_status_t;

/**
 * @brief SPI transfer callback type
 * @param instance SPI instance
 * @param status Transfer result status
 * @param userData User-provided context
 */
typedef void (*hal_spi_callback_t)(hal_spi_instance_t instance, hal_spi_status_t status, void* userData);

// ============================================================================
// Initialization API
// ============================================================================

/**
 * @brief Get default SPI configuration
 * @param config Pointer to config structure to fill
 */
void hal_spi_get_default_config(hal_spi_config_t* config);

/**
 * @brief Initialize SPI instance
 * @param instance SPI instance (1-4)
 * @param config Configuration structure
 * @return true on success
 */
bool hal_spi_init(hal_spi_instance_t instance, const hal_spi_config_t* config);

/**
 * @brief Deinitialize SPI instance
 * @param instance SPI instance
 */
void hal_spi_deinit(hal_spi_instance_t instance);

/**
 * @brief Check if SPI instance is initialized
 * @param instance SPI instance
 * @return true if initialized
 */
bool hal_spi_is_initialized(hal_spi_instance_t instance);

// ============================================================================
// Configuration API
// ============================================================================

/**
 * @brief Set baud rate
 * @param instance SPI instance
 * @param baudRate Baud rate in Hz
 * @return Actual baud rate achieved
 */
uint32_t hal_spi_set_baudrate(hal_spi_instance_t instance, uint32_t baudRate);

/**
 * @brief Set SPI mode (CPOL/CPHA)
 * @param instance SPI instance
 * @param mode SPI mode (0-3)
 * @return true on success
 */
bool hal_spi_set_mode(hal_spi_instance_t instance, hal_spi_mode_t mode);

/**
 * @brief Set chip select
 * @param instance SPI instance
 * @param pcs Chip select pin
 * @return true on success
 */
bool hal_spi_set_pcs(hal_spi_instance_t instance, hal_spi_pcs_t pcs);

/**
 * @brief Set transfer mode
 * @param instance SPI instance
 * @param mode Transfer mode (blocking/interrupt/DMA)
 * @return true on success
 */
bool hal_spi_set_transfer_mode(hal_spi_instance_t instance, hal_spi_transfer_mode_t mode);

// ============================================================================
// Transfer API
// ============================================================================

/**
 * @brief Perform blocking SPI transfer
 * @param instance SPI instance
 * @param transfer Transfer descriptor
 * @return true on success
 */
bool hal_spi_transfer_blocking(hal_spi_instance_t instance, hal_spi_transfer_t* transfer);

/**
 * @brief Start non-blocking SPI transfer (interrupt or DMA)
 * @param instance SPI instance
 * @param transfer Transfer descriptor
 * @param callback Completion callback (can be NULL)
 * @param userData User data passed to callback
 * @return true if transfer started
 */
bool hal_spi_transfer_nonblocking(hal_spi_instance_t instance, 
                                   hal_spi_transfer_t* transfer,
                                   hal_spi_callback_t callback,
                                   void* userData);

/**
 * @brief Get current transfer status
 * @param instance SPI instance
 * @return Current status
 */
hal_spi_status_t hal_spi_get_status(hal_spi_instance_t instance);

/**
 * @brief Abort ongoing transfer
 * @param instance SPI instance
 */
void hal_spi_abort_transfer(hal_spi_instance_t instance);

/**
 * @brief Wait for transfer completion
 * @param instance SPI instance
 * @param timeout_ms Timeout in milliseconds (0 = wait forever)
 * @return true if completed, false if timeout
 */
bool hal_spi_wait_complete(hal_spi_instance_t instance, uint32_t timeout_ms);

// ============================================================================
// Simple Transfer API (convenience functions)
// ============================================================================

/**
 * @brief Write data (TX only)
 * @param instance SPI instance
 * @param data Data to send
 * @param len Data length
 * @return true on success
 */
bool hal_spi_write(hal_spi_instance_t instance, const uint8_t* data, size_t len);

/**
 * @brief Read data (RX only, sends dummy bytes)
 * @param instance SPI instance
 * @param data Buffer to receive data
 * @param len Data length
 * @return true on success
 */
bool hal_spi_read(hal_spi_instance_t instance, uint8_t* data, size_t len);

/**
 * @brief Write then read (full duplex or half-duplex)
 * @param instance SPI instance
 * @param txData Data to send
 * @param txLen TX data length
 * @param rxData Buffer to receive data
 * @param rxLen RX data length
 * @return true on success
 */
bool hal_spi_write_read(hal_spi_instance_t instance, 
                         const uint8_t* txData, size_t txLen,
                         uint8_t* rxData, size_t rxLen);

// ============================================================================
// Info API
// ============================================================================

/**
 * @brief Get IRQ number for SPI instance
 * @param instance SPI instance
 * @return IRQ number
 */
uint32_t hal_spi_get_irq_number(hal_spi_instance_t instance);

/**
 * @brief Get clock frequency for SPI instance
 * @param instance SPI instance
 * @return Clock frequency in Hz
 */
uint32_t hal_spi_get_clock_freq(hal_spi_instance_t instance);

#ifdef __cplusplus
}
#endif

#endif // HAL_SPI_HPP
