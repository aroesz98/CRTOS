/*
 * SDCard.hpp - CRTOS Hardware Abstraction Layer - SD Card Driver
 * Author: Arkadiusz Szlanta
 * Date: 28 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * Hardware abstraction layer for SD Card via USDHC controller.
 * Provides block-level read/write operations for SD/SDHC/SDXC cards.
 */

#ifndef CRTOS_HAL_SDCARD_HPP
#define CRTOS_HAL_SDCARD_HPP

#include <stdint.h>
#include "../CRTOS.hpp"
#include "../BinarySemaphore.hpp"
#include "../Mutex.hpp"

namespace CRTOS
{
namespace HAL
{
    /**
     * @brief SD Card type enumeration
     */
    enum class SDCardType
    {
        Unknown,     ///< Unknown or not detected
        SDSC,        ///< SD Standard Capacity (up to 2GB)
        SDHC,        ///< SD High Capacity (2GB to 32GB)
        SDXC         ///< SD Extended Capacity (32GB to 2TB)
    };

    /**
     * @brief SD Card state enumeration
     */
    enum class SDCardState
    {
        NotInitialized, ///< Card not initialized
        Ready,          ///< Card ready for operations
        Busy,           ///< Card busy with operation
        Error,          ///< Card in error state
        Removed         ///< Card removed
    };

    /**
     * @brief SD Card information structure
     */
    struct SDCardInfo
    {
        SDCardType type;         ///< Card type
        uint32_t blockCount;     ///< Total number of blocks
        uint32_t blockSize;      ///< Block size in bytes (typically 512)
        uint64_t capacityBytes;  ///< Total capacity in bytes
        uint32_t speedClass;     ///< Speed class (2, 4, 6, 10)
        bool isWriteProtected;   ///< Write protection status
        char manufacturerID[4];  ///< Manufacturer ID string
        char productName[8];     ///< Product name
        uint8_t productRevision; ///< Product revision
        uint32_t serialNumber;   ///< Card serial number
    };

    /**
     * @brief SD Card configuration structure
     */
    struct SDCardConfig
    {
        uint32_t maxFrequencyHz; ///< Maximum bus frequency (e.g., 50MHz, 200MHz)
        bool use4BitBus;         ///< Use 4-bit data bus (vs 1-bit)
        bool enableHighSpeed;    ///< Enable high-speed mode
        uint32_t irqPriority;    ///< USDHC interrupt priority
    };

    /**
     * @brief SD Card driver class
     * 
     * Provides hardware-accelerated SD card operations using USDHC controller.
     * Supports SD/SDHC/SDXC cards with configurable speed modes.
     * Thread-safe with internal mutex for concurrent access.
     */
    class SDCard
    {
    public:
        /**
         * @brief Default configuration
         */
        static constexpr SDCardConfig DefaultConfig = {
            .maxFrequencyHz = 50000000,  // 50 MHz (SD high-speed)
            .use4BitBus = true,
            .enableHighSpeed = true,
            .irqPriority = 5
        };

        /**
         * @brief Constructor
         */
        SDCard();

        /**
         * @brief Destructor
         */
        ~SDCard();

        /**
         * @brief Initialize SD card controller
         * 
         * @param config Configuration parameters
         * @return Result::RESULT_SUCCESS on success
         */
        Result Initialize(const SDCardConfig& config = DefaultConfig);

        /**
         * @brief Deinitialize SD card and release resources
         */
        void Deinitialize();

        /**
         * @brief Check if card is inserted
         * 
         * @return true if card is detected
         */
        bool IsCardInserted() const;

        /**
         * @brief Wait for card insertion
         * 
         * @param timeoutMs Maximum time to wait in milliseconds (0 = infinite)
         * @return Result::RESULT_SUCCESS if card inserted
         */
        Result WaitForCardInserted(uint32_t timeoutMs = 0);

        /**
         * @brief Get card state
         * 
         * @return Current card state
         */
        SDCardState GetState() const;

        /**
         * @brief Get card information
         * 
         * @param info Output structure to fill with card information
         * @return Result::RESULT_SUCCESS on success
         */
        Result GetCardInfo(SDCardInfo& info) const;

        /**
         * @brief Read blocks from SD card
         * 
         * @param buffer Destination buffer (must be aligned to 4 bytes)
         * @param startBlock Starting block number
         * @param blockCount Number of blocks to read
         * @return Result::RESULT_SUCCESS on success
         */
        Result ReadBlocks(uint8_t* buffer, uint32_t startBlock, uint32_t blockCount);

        /**
         * @brief Write blocks to SD card
         * 
         * @param buffer Source buffer (must be aligned to 4 bytes)
         * @param startBlock Starting block number
         * @param blockCount Number of blocks to write
         * @return Result::RESULT_SUCCESS on success
         */
        Result WriteBlocks(const uint8_t* buffer, uint32_t startBlock, uint32_t blockCount);

        /**
         * @brief Erase blocks on SD card
         * 
         * @param startBlock Starting block number
         * @param blockCount Number of blocks to erase
         * @return Result::RESULT_SUCCESS on success
         */
        Result EraseBlocks(uint32_t startBlock, uint32_t blockCount);

        /**
         * @brief Sync any pending writes
         * 
         * @return Result::RESULT_SUCCESS on success
         */
        Result Sync();

        /**
         * @brief Get block size
         * 
         * @return Block size in bytes
         */
        uint32_t GetBlockSize() const;

        /**
         * @brief Get total block count
         * 
         * @return Number of blocks on card
         */
        uint32_t GetBlockCount() const;

        /**
         * @brief Set card detect callback
         * 
         * @param callback Function to call when card is inserted/removed
         * @param userData User data to pass to callback
         */
        void SetCardDetectCallback(void (*callback)(bool inserted, void* userData), void* userData);

        /**
         * @brief Handle USDHC interrupt (called from ISR)
         */
        void HandleInterrupt();

        /**
         * @brief Handle card detect interrupt (called from ISR)
         */
        void HandleCardDetectInterrupt();

    private:
        // Internal state
        SDCardState m_state;
        SDCardInfo m_cardInfo;
        SDCardConfig m_config;
        bool m_initialized;
        
        // Synchronization
        Mutex* m_mutex;
        BinarySemaphore* m_transferComplete;
        BinarySemaphore* m_cardInsertSem;
        
        // Callback
        void (*m_cardDetectCallback)(bool inserted, void* userData);
        void* m_callbackUserData;
        
        // Internal methods
        Result InitializeHost();
        Result InitializePins();
        Result InitializeCard();
        Result SetBusWidth(bool use4Bit);
        Result SetBusFrequency(uint32_t frequencyHz);
        Result SendCommand(uint32_t cmd, uint32_t arg, uint32_t* response);
        Result WaitTransferComplete(uint32_t timeoutMs);
        void PowerOn();
        void PowerOff();
    };

    /**
     * @brief Get global SD Card instance
     * 
     * @return Reference to singleton SDCard instance
     */
    SDCard& GetSDCard();

} // namespace HAL
} // namespace CRTOS

#endif // CRTOS_HAL_SDCARD_HPP
