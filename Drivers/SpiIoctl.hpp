/*
 * SpiIoctl.hpp - SPI Driver IOCTL Definitions
 * Author: Arkadiusz Szlanta
 * Date: 04 Jan 2026
 *
 * Description:
 * Shared IOCTL command definitions and structures for SPI driver.
 * This header can be included by both the driver module and applications.
 */

#ifndef SPI_IOCTL_HPP
#define SPI_IOCTL_HPP

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// IOCTL base for SPI (matches DriverBase.hpp)
#ifndef IOCTL_BASE_SPI
#define IOCTL_BASE_SPI 0x0200
#endif

namespace CRTOS
{
namespace Drivers
{

// SPI-specific IOCTL commands
enum class SpiIoctl : uint32_t
{
    SET_BAUDRATE      = IOCTL_BASE_SPI + 0,   ///< Set baud rate (arg: uint32_t*)
    GET_BAUDRATE      = IOCTL_BASE_SPI + 1,   ///< Get actual baud rate (arg: uint32_t*)
    SET_MODE          = IOCTL_BASE_SPI + 2,   ///< Set SPI mode 0-3 (arg: uint8_t*)
    GET_MODE          = IOCTL_BASE_SPI + 3,   ///< Get SPI mode (arg: uint8_t*)
    SET_PCS           = IOCTL_BASE_SPI + 4,   ///< Set chip select (arg: uint8_t*)
    GET_PCS           = IOCTL_BASE_SPI + 5,   ///< Get chip select (arg: uint8_t*)
    SET_TRANSFER_MODE = IOCTL_BASE_SPI + 6,   ///< Set transfer mode (arg: SpiTransferMode*)
    GET_TRANSFER_MODE = IOCTL_BASE_SPI + 7,   ///< Get transfer mode (arg: SpiTransferMode*)
    SET_BITS_PER_FRAME= IOCTL_BASE_SPI + 8,   ///< Set bits per frame (arg: uint8_t*)
    GET_BITS_PER_FRAME= IOCTL_BASE_SPI + 9,   ///< Get bits per frame (arg: uint8_t*)
    SET_PCS_CONTINUOUS= IOCTL_BASE_SPI + 10,  ///< Set PCS continuous mode (arg: bool*)
    GET_STATUS        = IOCTL_BASE_SPI + 11,  ///< Get transfer status (arg: SpiStatus*)
    ABORT_TRANSFER    = IOCTL_BASE_SPI + 12,  ///< Abort ongoing transfer
    WAIT_COMPLETE     = IOCTL_BASE_SPI + 13,  ///< Wait for transfer complete (arg: SpiWaitArg*)
    TRANSFER          = IOCTL_BASE_SPI + 14,  ///< Full-duplex transfer (arg: SpiTransferArg*)
    GET_CLOCK_FREQ    = IOCTL_BASE_SPI + 15,  ///< Get source clock frequency (arg: uint32_t*)
    SET_DELAYS        = IOCTL_BASE_SPI + 16,  ///< Set timing delays (arg: SpiDelays*)
    GET_DELAYS        = IOCTL_BASE_SPI + 17,  ///< Get timing delays (arg: SpiDelays*)
};

/**
 * @brief SPI mode (clock polarity and phase)
 */
enum class SpiMode : uint8_t
{
    Mode0 = 0,  ///< CPOL=0, CPHA=0: Clock idle low, sample on rising edge
    Mode1 = 1,  ///< CPOL=0, CPHA=1: Clock idle low, sample on falling edge
    Mode2 = 2,  ///< CPOL=1, CPHA=0: Clock idle high, sample on falling edge
    Mode3 = 3,  ///< CPOL=1, CPHA=1: Clock idle high, sample on rising edge
};

/**
 * @brief SPI chip select selection
 */
enum class SpiPcs : uint8_t
{
    PCS0 = 0,
    PCS1 = 1,
    PCS2 = 2,
    PCS3 = 3,
};

/**
 * @brief SPI transfer mode
 */
enum class SpiTransferMode : uint8_t
{
    Blocking  = 0,  ///< Blocking (polling) transfer
    Interrupt = 1,  ///< Interrupt-driven transfer
    DMA       = 2,  ///< DMA (EDMA) transfer
};

/**
 * @brief SPI transfer status
 */
enum class SpiStatus : uint8_t
{
    Idle     = 0,
    Busy     = 1,
    Complete = 2,
    Error    = 3,
};

/**
 * @brief SPI timing delays
 */
struct SpiDelays
{
    uint32_t pcsToSckNs;       ///< PCS to SCK delay in nanoseconds
    uint32_t sckToPcsNs;       ///< Last SCK to PCS delay in nanoseconds
    uint32_t betweenTransferNs;///< Delay between transfers in nanoseconds
};

/**
 * @brief SPI transfer argument for IOCTL
 */
struct SpiTransferArg
{
    const uint8_t* txData;  ///< TX data (NULL for RX-only)
    uint8_t* rxData;        ///< RX buffer (NULL for TX-only)
    size_t dataSize;        ///< Transfer size in bytes
    bool blocking;          ///< True for blocking transfer
    uint32_t timeoutMs;     ///< Timeout for blocking transfer
};

/**
 * @brief Wait for completion argument
 */
struct SpiWaitArg
{
    uint32_t timeoutMs;  ///< Timeout in milliseconds (0 = forever)
    bool success;        ///< Output: true if completed successfully
};

/**
 * @brief SPI configuration structure (for driver init)
 */
struct SpiConfig
{
    uint32_t baudRate;            ///< Baud rate in Hz
    SpiMode mode;                 ///< Clock polarity/phase mode
    SpiPcs pcs;                   ///< Chip select pin
    SpiTransferMode transferMode; ///< Transfer mode (blocking/interrupt/DMA)
    uint8_t bitsPerFrame;         ///< Bits per frame (8-32)
    bool pcsContinuous;           ///< Keep PCS asserted between transfers
    SpiDelays delays;             ///< Timing delays
};

} // namespace Drivers
} // namespace CRTOS

#endif // SPI_IOCTL_HPP
