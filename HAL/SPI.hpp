/*
 * SPI.hpp - CRTOS Hardware Abstraction Layer - SPI Driver
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
 * Hardware abstraction layer for LPSPI controller.
 * Supports blocking, interrupt-driven, and DMA (EDMA) transfer modes.
 * Master mode only (slave mode can be added if needed).
 */

#ifndef CRTOS_HAL_SPI_HPP
#define CRTOS_HAL_SPI_HPP

#include <stdint.h>
#include <stddef.h>
#include "../CRTOS.hpp"
#include "../BinarySemaphore.hpp"
#include "../../device/MIMXRT1052.h"

// Forward declarations for SDK types
struct _lpspi_master_handle;
struct _lpspi_master_edma_handle;
struct _edma_handle;

namespace CRTOS
{
namespace HAL
{
    /**
     * @brief SPI instance enumeration
     */
    enum class SPIInstance
    {
        SPI1 = 0,
        SPI2 = 1,
        SPI3 = 2,
        SPI4 = 3,
        COUNT
    };
    
    /**
     * @brief SPI clock mode (CPOL/CPHA)
     */
    enum class SPIMode
    {
        Mode0 = 0,  ///< CPOL=0, CPHA=0
        Mode1 = 1,  ///< CPOL=0, CPHA=1
        Mode2 = 2,  ///< CPOL=1, CPHA=0
        Mode3 = 3,  ///< CPOL=1, CPHA=1
    };
    
    /**
     * @brief SPI chip select pin
     */
    enum class SPIPCS
    {
        PCS0 = 0,
        PCS1 = 1,
        PCS2 = 2,
        PCS3 = 3,
    };
    
    /**
     * @brief SPI transfer mode
     */
    enum class SPITransferMode
    {
        Blocking,    ///< Polling mode
        Interrupt,   ///< Interrupt-driven
        DMA,         ///< EDMA transfer
    };
    
    /**
     * @brief SPI transfer status
     */
    enum class SPIStatus
    {
        Idle,
        Busy,
        Complete,
        Error,
    };
    
    /**
     * @brief SPI configuration structure
     */
    struct SPIConfig
    {
        uint32_t baudRate;                ///< Baud rate in Hz
        SPIMode mode;                     ///< Clock polarity/phase
        SPIPCS pcs;                       ///< Chip select pin
        SPITransferMode transferMode;     ///< Transfer mode
        uint8_t bitsPerFrame;             ///< Bits per frame (8-32)
        uint32_t pcsToSckDelayNs;         ///< PCS to SCK delay
        uint32_t sckToPcsDelayNs;         ///< Last SCK to PCS delay
        uint32_t betweenTransferDelayNs;  ///< Inter-transfer delay
        bool pcsContinuous;               ///< Keep PCS asserted
        bool lsbFirst;                    ///< LSB first (false = MSB first)
    };
    
    /**
     * @brief SPI transfer descriptor
     */
    struct SPITransfer
    {
        const uint8_t* txData;  ///< TX data (NULL for RX-only)
        uint8_t* rxData;        ///< RX buffer (NULL for TX-only)
        size_t dataSize;        ///< Transfer size in bytes
    };
    
    /**
     * @brief SPI transfer callback type
     */
    using SPICallback = void (*)(SPIInstance instance, SPIStatus status, void* userData);
    
    /**
     * @brief SPI driver class
     * 
     * Provides hardware-abstracted SPI operations using LPSPI controller.
     * Supports master mode with blocking, interrupt, and DMA transfers.
     */
    class SPI
    {
    public:
        /**
         * @brief Constructor
         * @param instance SPI instance (SPI1-SPI4)
         */
        explicit SPI(SPIInstance instance);
        
        /**
         * @brief Destructor
         */
        ~SPI();
        
        /**
         * @brief Initialize SPI with configuration
         * @param config SPI configuration
         * @return Result::RESULT_SUCCESS on success
         */
        Result Initialize(const SPIConfig& config);
        
        /**
         * @brief Deinitialize SPI
         */
        void Deinitialize();
        
        /**
         * @brief Check if initialized
         * @return true if initialized
         */
        bool IsInitialized() const { return m_initialized; }
        
        // ==================== Configuration ====================
        
        /**
         * @brief Set baud rate
         * @param baudRate Baud rate in Hz
         * @return Actual baud rate achieved
         */
        uint32_t SetBaudRate(uint32_t baudRate);
        
        /**
         * @brief Set SPI mode
         * @param mode SPI mode (0-3)
         * @return Result::RESULT_SUCCESS on success
         */
        Result SetMode(SPIMode mode);
        
        /**
         * @brief Set chip select
         * @param pcs Chip select pin
         * @return Result::RESULT_SUCCESS on success
         */
        Result SetPCS(SPIPCS pcs);
        
        /**
         * @brief Set transfer mode
         * @param mode Transfer mode
         * @return Result::RESULT_SUCCESS on success
         */
        Result SetTransferMode(SPITransferMode mode);
        
        // ==================== Transfer Operations ====================
        
        /**
         * @brief Perform blocking transfer
         * @param transfer Transfer descriptor
         * @return Result::RESULT_SUCCESS on success
         */
        Result TransferBlocking(SPITransfer& transfer);
        
        /**
         * @brief Start non-blocking transfer
         * @param transfer Transfer descriptor
         * @param callback Completion callback
         * @param userData User data for callback
         * @return Result::RESULT_SUCCESS if started
         */
        Result TransferNonBlocking(SPITransfer& transfer, 
                                    SPICallback callback = nullptr,
                                    void* userData = nullptr);
        
        /**
         * @brief Get current transfer status
         * @return Current status
         */
        SPIStatus GetStatus() const { return m_status; }
        
        /**
         * @brief Abort ongoing transfer
         */
        void AbortTransfer();
        
        /**
         * @brief Wait for transfer completion
         * @param timeout_ms Timeout in milliseconds (0 = forever)
         * @return Result::RESULT_SUCCESS on complete
         */
        Result WaitComplete(uint32_t timeout_ms = 0);
        
        // ==================== Simple Operations ====================
        
        /**
         * @brief Write data (TX only)
         * @param data Data to send
         * @param len Length in bytes
         * @return Result::RESULT_SUCCESS on success
         */
        Result Write(const uint8_t* data, size_t len);
        
        /**
         * @brief Read data (RX only)
         * @param data Buffer for received data
         * @param len Length in bytes
         * @return Result::RESULT_SUCCESS on success
         */
        Result Read(uint8_t* data, size_t len);
        
        /**
         * @brief Full duplex transfer
         * @param txData Data to send
         * @param rxData Buffer for received data
         * @param len Length in bytes
         * @return Result::RESULT_SUCCESS on success
         */
        Result Transfer(const uint8_t* txData, uint8_t* rxData, size_t len);
        
        // ==================== Info ====================
        
        /**
         * @brief Get SPI instance
         * @return Instance enum value
         */
        SPIInstance GetInstance() const { return m_instance; }
        
        /**
         * @brief Get IRQ number
         * @return IRQ number for this SPI
         */
        uint32_t GetIRQNumber() const;
        
        /**
         * @brief Get source clock frequency
         * @return Clock frequency in Hz
         */
        uint32_t GetClockFrequency() const;
        
        /**
         * @brief Get default configuration
         * @return Default SPIConfig
         */
        static SPIConfig GetDefaultConfig();
        
        // ==================== Internal (for callbacks) ====================
        
        /**
         * @brief Called from interrupt/DMA callback
         * @param status Transfer status
         */
        void OnTransferComplete(SPIStatus status);
        
    private:
        SPIInstance m_instance;
        LPSPI_Type* m_base;
        bool m_initialized;
        SPITransferMode m_transferMode;
        volatile SPIStatus m_status;
        SPIConfig m_config;
        
        // Callback
        SPICallback m_callback;
        void* m_userData;
        
        // Synchronization
        BinarySemaphore* m_completeSemaphore;
        
        // Interrupt mode handle
        _lpspi_master_handle* m_irqHandle;
        
        // DMA mode handles
        _lpspi_master_edma_handle* m_dmaHandle;
        _edma_handle* m_dmaRxHandle;
        _edma_handle* m_dmaTxHandle;
        
        // DMA channels
        uint8_t m_dmaRxChannel;
        uint8_t m_dmaTxChannel;
        
        /**
         * @brief Get LPSPI base address for instance
         */
        static LPSPI_Type* GetBaseAddress(SPIInstance instance);
        
        /**
         * @brief Configure DMA for transfers
         */
        Result ConfigureDMA();
        
        /**
         * @brief Free DMA resources
         */
        void FreeDMA();
    };
    
    // Global SPI instances
    extern SPI* GlobalSPI[static_cast<int>(SPIInstance::COUNT)];
    
    /**
     * @brief Get SPI instance
     * @param instance SPI instance number
     * @return Reference to SPI object (creates if needed)
     */
    SPI& GetSPI(SPIInstance instance);
    
} // namespace HAL
} // namespace CRTOS

#endif // CRTOS_HAL_SPI_HPP
