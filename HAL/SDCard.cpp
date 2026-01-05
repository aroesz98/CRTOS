/*
 * SDCard.cpp - CRTOS Hardware Abstraction Layer - SD Card Driver
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
 * Implementation of SD Card HAL driver using NXP USDHC controller.
 * Based on NXP SDK sdmmc driver with CRTOS integration.
 */

#include "SDCard.hpp"
#include "../CRTOS.hpp"
#include "../InterruptDPC.hpp"

// NXP SDK includes
extern "C" {
#include "fsl_usdhc.h"
#include "fsl_gpio.h"
#include "fsl_iomuxc.h"
#include "fsl_clock.h"
#include "fsl_common.h"
}

namespace CRTOS
{
namespace HAL
{

/*******************************************************************************
 * Definitions
 ******************************************************************************/

// USDHC instance for SD card (USDHC1 on EVKB)
#define SDCARD_USDHC_BASE       USDHC1
#define SDCARD_USDHC_IRQ        USDHC1_IRQn

// Card detect GPIO (GPIO2_IO28 on EVKB)
#define SDCARD_CD_GPIO          GPIO2
#define SDCARD_CD_PIN           28U
#define SDCARD_CD_INSERT_LEVEL  0U

// Power control GPIO (GPIO1_IO05 on EVKB)
#define SDCARD_PWR_GPIO         GPIO1
#define SDCARD_PWR_PIN          5U

// DMA descriptor buffer size
#define SDCARD_DMA_BUFFER_SIZE  32U

// Block size
#define SDCARD_BLOCK_SIZE       512U

// Timeouts
#define SDCARD_INIT_TIMEOUT_MS  1000U
#define SDCARD_CMD_TIMEOUT_MS   100U
#define SDCARD_XFER_TIMEOUT_MS  5000U

// SD commands
#define SD_CMD_GO_IDLE_STATE        0
#define SD_CMD_SEND_OP_COND         1
#define SD_CMD_ALL_SEND_CID         2
#define SD_CMD_SET_RELATIVE_ADDR    3
#define SD_CMD_SET_DSR              4
#define SD_CMD_SELECT_CARD          7
#define SD_CMD_SEND_IF_COND         8
#define SD_CMD_SEND_CSD             9
#define SD_CMD_SEND_CID             10
#define SD_CMD_STOP_TRANSMISSION    12
#define SD_CMD_SEND_STATUS          13
#define SD_CMD_SET_BLOCKLEN         16
#define SD_CMD_READ_SINGLE_BLOCK    17
#define SD_CMD_READ_MULTIPLE_BLOCK  18
#define SD_CMD_WRITE_SINGLE_BLOCK   24
#define SD_CMD_WRITE_MULTIPLE_BLOCK 25
#define SD_CMD_ERASE_WR_BLK_START   32
#define SD_CMD_ERASE_WR_BLK_END     33
#define SD_CMD_ERASE                38
#define SD_CMD_APP_CMD              55
#define SD_CMD_READ_OCR             58

// App commands (after CMD55)
#define SD_ACMD_SET_BUS_WIDTH       6
#define SD_ACMD_SD_STATUS           13
#define SD_ACMD_SEND_NUM_WR_BLKS    22
#define SD_ACMD_SET_WR_BLK_ERASE_COUNT 23
#define SD_ACMD_SD_SEND_OP_COND     41
#define SD_ACMD_SET_CLR_CARD_DETECT 42
#define SD_ACMD_SEND_SCR            51

/*******************************************************************************
 * Static variables
 ******************************************************************************/

// DMA descriptor buffer (must be non-cacheable)
AT_NONCACHEABLE_SECTION_ALIGN(static uint32_t s_dmaBuffer[SDCARD_DMA_BUFFER_SIZE], 
                              USDHC_ADMA2_ADDRESS_ALIGN);

// Singleton instance
static SDCard* s_sdCardInstance = nullptr;

// Card detect status
static volatile bool s_cardInserted = false;

// Forward declaration for DPC callback
static void USDHC1_DPC_Callback(void* context);

/*******************************************************************************
 * SDCard Implementation
 ******************************************************************************/

SDCard::SDCard()
    : m_state(SDCardState::NotInitialized)
    , m_cardInfo{}
    , m_config{}
    , m_initialized(false)
    , m_mutex(nullptr)
    , m_transferComplete(nullptr)
    , m_cardInsertSem(nullptr)
    , m_cardDetectCallback(nullptr)
    , m_callbackUserData(nullptr)
{
    s_sdCardInstance = this;
}

SDCard::~SDCard()
{
    if (m_initialized)
    {
        Deinitialize();
    }
    s_sdCardInstance = nullptr;
}

Result SDCard::Initialize(const SDCardConfig& config)
{
    if (m_initialized)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    m_config = config;
    
    // Create synchronization primitives
    m_mutex = new Mutex();
    m_transferComplete = new BinarySemaphore();
    m_cardInsertSem = new BinarySemaphore();
    
    if (!m_mutex || !m_transferComplete || !m_cardInsertSem)
    {
        Deinitialize();
        return Result::RESULT_NO_MEMORY;
    }
    
    // Initialize pins
    Result res = InitializePins();
    if (res != Result::RESULT_SUCCESS)
    {
        Deinitialize();
        return res;
    }
    
    // Initialize USDHC host
    res = InitializeHost();
    if (res != Result::RESULT_SUCCESS)
    {
        Deinitialize();
        return res;
    }
    
    m_initialized = true;
    m_state = SDCardState::NotInitialized;
    
    // Check if card is already inserted
    if (IsCardInserted())
    {
        s_cardInserted = true;
        
        // Initialize the card
        res = InitializeCard();
        if (res == Result::RESULT_SUCCESS)
        {
            m_state = SDCardState::Ready;
        }
    }
    
    return Result::RESULT_SUCCESS;
}

void SDCard::Deinitialize()
{
    if (m_initialized)
    {
        // Disable USDHC
        USDHC_Deinit(SDCARD_USDHC_BASE);
        NVIC_DisableIRQ(SDCARD_USDHC_IRQ);
        
        m_initialized = false;
    }
    
    if (m_mutex)
    {
        delete m_mutex;
        m_mutex = nullptr;
    }
    
    if (m_transferComplete)
    {
        delete m_transferComplete;
        m_transferComplete = nullptr;
    }
    
    if (m_cardInsertSem)
    {
        delete m_cardInsertSem;
        m_cardInsertSem = nullptr;
    }
    
    m_state = SDCardState::NotInitialized;
}

Result SDCard::InitializePins()
{
    // Enable IOMUXC clock
    CLOCK_EnableClock(kCLOCK_Iomuxc);
    
    // Configure SD card pins for USDHC1
    // CMD pin
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_00_USDHC1_CMD, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_00_USDHC1_CMD, 
                        IOMUXC_SW_PAD_CTL_PAD_SPEED(1U) | 
                        IOMUXC_SW_PAD_CTL_PAD_SRE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_PKE_MASK | 
                        IOMUXC_SW_PAD_CTL_PAD_PUE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_HYS_MASK | 
                        IOMUXC_SW_PAD_CTL_PAD_PUS(1U) |
                        IOMUXC_SW_PAD_CTL_PAD_DSE(7U));
    
    // CLK pin
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_01_USDHC1_CLK, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_01_USDHC1_CLK,
                        IOMUXC_SW_PAD_CTL_PAD_SPEED(1U) | 
                        IOMUXC_SW_PAD_CTL_PAD_SRE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_HYS_MASK | 
                        IOMUXC_SW_PAD_CTL_PAD_PUS(0U) |
                        IOMUXC_SW_PAD_CTL_PAD_DSE(7U));
    
    // DATA0 pin
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_02_USDHC1_DATA0, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_02_USDHC1_DATA0,
                        IOMUXC_SW_PAD_CTL_PAD_SPEED(1U) | 
                        IOMUXC_SW_PAD_CTL_PAD_SRE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_PKE_MASK | 
                        IOMUXC_SW_PAD_CTL_PAD_PUE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_HYS_MASK | 
                        IOMUXC_SW_PAD_CTL_PAD_PUS(1U) |
                        IOMUXC_SW_PAD_CTL_PAD_DSE(7U));
    
    // DATA1 pin
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_03_USDHC1_DATA1, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_03_USDHC1_DATA1,
                        IOMUXC_SW_PAD_CTL_PAD_SPEED(1U) | 
                        IOMUXC_SW_PAD_CTL_PAD_SRE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_PKE_MASK | 
                        IOMUXC_SW_PAD_CTL_PAD_PUE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_HYS_MASK | 
                        IOMUXC_SW_PAD_CTL_PAD_PUS(1U) |
                        IOMUXC_SW_PAD_CTL_PAD_DSE(7U));
    
    // DATA2 pin
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_04_USDHC1_DATA2, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_04_USDHC1_DATA2,
                        IOMUXC_SW_PAD_CTL_PAD_SPEED(1U) | 
                        IOMUXC_SW_PAD_CTL_PAD_SRE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_PKE_MASK | 
                        IOMUXC_SW_PAD_CTL_PAD_PUE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_HYS_MASK | 
                        IOMUXC_SW_PAD_CTL_PAD_PUS(1U) |
                        IOMUXC_SW_PAD_CTL_PAD_DSE(7U));
    
    // DATA3 pin
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_05_USDHC1_DATA3, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_05_USDHC1_DATA3,
                        IOMUXC_SW_PAD_CTL_PAD_SPEED(1U) | 
                        IOMUXC_SW_PAD_CTL_PAD_SRE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_PKE_MASK | 
                        IOMUXC_SW_PAD_CTL_PAD_PUE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_HYS_MASK | 
                        IOMUXC_SW_PAD_CTL_PAD_PUS(1U) |
                        IOMUXC_SW_PAD_CTL_PAD_DSE(7U));
    
    // Card detect GPIO (GPIO2_IO28)
    IOMUXC_SetPinMux(IOMUXC_GPIO_B1_12_GPIO2_IO28, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_12_GPIO2_IO28,
                        IOMUXC_SW_PAD_CTL_PAD_SPEED(1U) | 
                        IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_PUE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_HYS_MASK | 
                        IOMUXC_SW_PAD_CTL_PAD_PUS(1U));
    
    // Configure CD pin as input
    gpio_pin_config_t cdPinConfig = {
        .direction = kGPIO_DigitalInput,
        .outputLogic = 0U,
        .interruptMode = kGPIO_IntRisingOrFallingEdge
    };
    GPIO_PinInit(SDCARD_CD_GPIO, SDCARD_CD_PIN, &cdPinConfig);
    
    // Power control GPIO (GPIO1_IO05)
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_05_GPIO1_IO05, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_05_GPIO1_IO05, 0x10B0U);
    
    // Configure power pin as output
    gpio_pin_config_t pwrPinConfig = {
        .direction = kGPIO_DigitalOutput,
        .outputLogic = 1U,  // Power on
        .interruptMode = kGPIO_NoIntmode
    };
    GPIO_PinInit(SDCARD_PWR_GPIO, SDCARD_PWR_PIN, &pwrPinConfig);
    
    // VSELECT pin (GPIO_B1_14 for voltage selection)
    IOMUXC_SetPinMux(IOMUXC_GPIO_B1_14_USDHC1_VSELECT, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_14_USDHC1_VSELECT, 0x0170A1U);
    
    return Result::RESULT_SUCCESS;
}

Result SDCard::InitializeHost()
{
    // Enable USDHC1 clock gate first
    CLOCK_EnableClock(kCLOCK_Usdhc1);
    
    // Configure USDHC clock
    // Use System PLL PFD0 (396 MHz), divide by 2 for 198 MHz
    CLOCK_InitSysPfd(kCLOCK_Pfd0, 24U);  // 528 * 18 / 24 = 396 MHz
    CLOCK_SetDiv(kCLOCK_Usdhc1Div, 1U);  // Divide by 2
    CLOCK_SetMux(kCLOCK_Usdhc1Mux, 1U);  // Select PFD0
    
    // USDHC configuration - manually initialize (no USDHC_GetDefaultConfig in driver)
    usdhc_config_t usdhcConfig;
    usdhcConfig.dataTimeout = 0xFU;
    usdhcConfig.endianMode = kUSDHC_EndianModeLittle;
    usdhcConfig.readWatermarkLevel = 128U;
    usdhcConfig.writeWatermarkLevel = 128U;
#if !(defined(FSL_FEATURE_USDHC_HAS_NO_RW_BURST_LEN) && FSL_FEATURE_USDHC_HAS_NO_RW_BURST_LEN)
    usdhcConfig.readBurstLen = 8U;
    usdhcConfig.writeBurstLen = 8U;
#endif
    
    USDHC_Init(SDCARD_USDHC_BASE, &usdhcConfig);
    
    // Enable interrupt status flags (for polling or ISR)
    USDHC_EnableInterruptStatus(SDCARD_USDHC_BASE,
        kUSDHC_CommandCompleteFlag | 
        kUSDHC_DataCompleteFlag |
        kUSDHC_CommandErrorFlag | 
        kUSDHC_DataErrorFlag |
        kUSDHC_CardInsertionFlag | 
        kUSDHC_CardRemovalFlag);
    
    // Only enable interrupt SIGNALS for card detect events
    // Do NOT enable signals for command/data complete - we use blocking (polling) transfers
    // Enabling those signals would cause the ISR to clear flags before polling sees them
    USDHC_EnableInterruptSignal(SDCARD_USDHC_BASE,
        kUSDHC_CardInsertionFlag | 
        kUSDHC_CardRemovalFlag);
    
    // Enable NVIC interrupt
    NVIC_SetPriority(SDCARD_USDHC_IRQ, m_config.irqPriority);
    NVIC_EnableIRQ(SDCARD_USDHC_IRQ);
    
    // Register with DPC system for USDHC1 interrupt
    Result dpcResult = GlobalDPCDispatcher.RegisterInterruptSource(SDCARD_USDHC_IRQ);
    if (dpcResult != Result::RESULT_SUCCESS && dpcResult != Result::RESULT_CRC_ALREADY_INITIALIZED)
    {
        // Non-fatal - we can still work without DPC
    }
    
    // Register handler with ISR callback for hardware flag clearing
    // Pass 'this' as context so callback can access instance
    dpcResult = GlobalDPCDispatcher.RegisterHandler(SDCARD_USDHC_IRQ, m_cardInsertSem, 
                                                    USDHC1_DPC_Callback, this);
    
    return Result::RESULT_SUCCESS;
}

Result SDCard::InitializeCard()
{
    usdhc_transfer_t transfer;
    usdhc_command_t command;
    status_t status;
    
    // Power cycle the card
    PowerOff();
    Task::Delay(50);  // Wait 50ms
    PowerOn();
    Task::Delay(100); // Wait for card to power up
    
    // Set initial clock to 400 kHz for identification
    USDHC_SetSdClock(SDCARD_USDHC_BASE, 198000000U, 400000U);
    
    // CMD0: Go idle state
    memset(&command, 0, sizeof(command));
    command.index = SD_CMD_GO_IDLE_STATE;
    command.argument = 0U;
    command.type = kCARD_CommandTypeNormal;
    command.responseType = kCARD_ResponseTypeNone;
    
    transfer.command = &command;
    transfer.data = nullptr;
    
    status = USDHC_TransferBlocking(SDCARD_USDHC_BASE, nullptr, &transfer);
    if (status != kStatus_Success)
    {
        m_state = SDCardState::Error;
        return Result::RESULT_BAD_PARAMETER;
    }
    
    Task::Delay(10);  // Wait 10ms
    
    // CMD8: Send interface condition (check for SD v2.0+)
    bool isSDv2 = false;
    memset(&command, 0, sizeof(command));
    command.index = SD_CMD_SEND_IF_COND;
    command.argument = 0x1AAU;  // Supply voltage 2.7-3.6V, check pattern
    command.type = kCARD_CommandTypeNormal;
    command.responseType = kCARD_ResponseTypeR7;
    
    transfer.command = &command;
    transfer.data = nullptr;
    
    status = USDHC_TransferBlocking(SDCARD_USDHC_BASE, nullptr, &transfer);
    if (status == kStatus_Success)
    {
        if ((command.response[0] & 0xFFU) == 0xAAU)
        {
            isSDv2 = true;
        }
    }
    
    // ACMD41: SD send operation condition (with retry)
    uint32_t ocr = 0;
    for (int retry = 0; retry < 100; retry++)
    {
        // CMD55: App command prefix
        memset(&command, 0, sizeof(command));
        command.index = SD_CMD_APP_CMD;
        command.argument = 0U;
        command.type = kCARD_CommandTypeNormal;
        command.responseType = kCARD_ResponseTypeR1;
        
        transfer.command = &command;
        transfer.data = nullptr;
        
        status = USDHC_TransferBlocking(SDCARD_USDHC_BASE, nullptr, &transfer);
        if (status != kStatus_Success)
        {
            continue;
        }
        
        // ACMD41: SD send op cond
        memset(&command, 0, sizeof(command));
        command.index = SD_ACMD_SD_SEND_OP_COND;
        command.argument = isSDv2 ? 0x40FF8000U : 0x00FF8000U;  // HCS bit for v2
        command.type = kCARD_CommandTypeNormal;
        command.responseType = kCARD_ResponseTypeR3;
        
        transfer.command = &command;
        transfer.data = nullptr;
        
        status = USDHC_TransferBlocking(SDCARD_USDHC_BASE, nullptr, &transfer);
        if (status == kStatus_Success)
        {
            ocr = command.response[0];
            if (ocr & 0x80000000U)  // Card ready
            {
                break;
            }
        }
        
        Task::Delay(10);  // Wait 10ms
    }
    
    if (!(ocr & 0x80000000U))
    {
        m_state = SDCardState::Error;
        return Result::RESULT_BAD_PARAMETER;
    }
    
    // Determine card type
    if (isSDv2 && (ocr & 0x40000000U))
    {
        m_cardInfo.type = SDCardType::SDHC;
    }
    else
    {
        m_cardInfo.type = SDCardType::SDSC;
    }
    
    // CMD2: All send CID
    memset(&command, 0, sizeof(command));
    command.index = SD_CMD_ALL_SEND_CID;
    command.argument = 0U;
    command.type = kCARD_CommandTypeNormal;
    command.responseType = kCARD_ResponseTypeR2;
    
    transfer.command = &command;
    transfer.data = nullptr;
    
    status = USDHC_TransferBlocking(SDCARD_USDHC_BASE, nullptr, &transfer);
    if (status != kStatus_Success)
    {
        m_state = SDCardState::Error;
        return Result::RESULT_BAD_PARAMETER;
    }
    
    // Parse CID
    m_cardInfo.serialNumber = ((command.response[2] & 0xFFFFFFU) << 8) | 
                               ((command.response[3] >> 24) & 0xFFU);
    
    // CMD3: Send relative address
    memset(&command, 0, sizeof(command));
    command.index = SD_CMD_SET_RELATIVE_ADDR;
    command.argument = 0U;
    command.type = kCARD_CommandTypeNormal;
    command.responseType = kCARD_ResponseTypeR6;
    
    transfer.command = &command;
    transfer.data = nullptr;
    
    status = USDHC_TransferBlocking(SDCARD_USDHC_BASE, nullptr, &transfer);
    if (status != kStatus_Success)
    {
        m_state = SDCardState::Error;
        return Result::RESULT_BAD_PARAMETER;
    }
    
    uint32_t rca = command.response[0] >> 16;
    
    // CMD9: Send CSD
    memset(&command, 0, sizeof(command));
    command.index = SD_CMD_SEND_CSD;
    command.argument = rca << 16;
    command.type = kCARD_CommandTypeNormal;
    command.responseType = kCARD_ResponseTypeR2;
    
    transfer.command = &command;
    transfer.data = nullptr;
    
    status = USDHC_TransferBlocking(SDCARD_USDHC_BASE, nullptr, &transfer);
    if (status != kStatus_Success)
    {
        m_state = SDCardState::Error;
        return Result::RESULT_BAD_PARAMETER;
    }
    
    // Parse CSD for capacity
    // USDHC R2 response format (following NXP SDK convention):
    // response[3] = CSD bits 127:96 (high bits, contains CSD_STRUCTURE)
    // response[2] = CSD bits 95:64
    // response[1] = CSD bits 63:32
    // response[0] = CSD bits 31:0
    
    uint32_t csdVersion = (command.response[3] >> 30) & 0x3U;
    
    if (csdVersion == 0)
    {
        // CSD v1.0 (SDSC)
        // C_SIZE is at bits 73:62 (12 bits)
        // C_SIZE_MULT is at bits 49:47 (3 bits)
        // READ_BL_LEN is at bits 83:80 (4 bits)
        uint32_t cSize = ((command.response[2] & 0x3FFU) << 2) | 
                         ((command.response[1] >> 30) & 0x3U);
        uint32_t cSizeMult = (command.response[1] >> 15) & 0x7U;
        uint32_t readBlLen = (command.response[2] >> 16) & 0xFU;
        
        m_cardInfo.blockCount = (cSize + 1) * (1U << (cSizeMult + 2));
        m_cardInfo.blockSize = 1U << readBlLen;
        
        // Normalize to 512-byte blocks if needed
        if (m_cardInfo.blockSize != SDCARD_BLOCK_SIZE)
        {
            m_cardInfo.blockCount = (m_cardInfo.blockCount * m_cardInfo.blockSize) / SDCARD_BLOCK_SIZE;
            m_cardInfo.blockSize = SDCARD_BLOCK_SIZE;
        }
    }
    else if (csdVersion == 1)
    {
        // CSD v2.0 (SDHC/SDXC)
        // C_SIZE is at bits 69:48 (22 bits)
        // response[2] bits 5:0 = C_SIZE bits 21:16
        // response[1] bits 31:16 = C_SIZE bits 15:0
        uint32_t cSize = ((command.response[2] & 0x3FU) << 16) | 
                         ((command.response[1] >> 16) & 0xFFFFU);
        
        m_cardInfo.blockCount = (cSize + 1) * 1024;
        m_cardInfo.blockSize = SDCARD_BLOCK_SIZE;
        
        // SDXC cards have C_SIZE >= 0xFFFF (capacity >= 32GB)
        if (cSize >= 0xFFFFU)
        {
            m_cardInfo.type = SDCardType::SDXC;
        }
    }
    else
    {
        // Unknown CSD version - try to use SDHC parsing if card was detected as SDHC
        if (m_cardInfo.type == SDCardType::SDHC || m_cardInfo.type == SDCardType::SDXC)
        {
            uint32_t cSize = ((command.response[2] & 0x3FU) << 16) | 
                             ((command.response[1] >> 16) & 0xFFFFU);
            m_cardInfo.blockCount = (cSize + 1) * 1024;
            m_cardInfo.blockSize = SDCARD_BLOCK_SIZE;
        }
    }
    
    m_cardInfo.capacityBytes = (uint64_t)m_cardInfo.blockCount * m_cardInfo.blockSize;
    
    // CMD7: Select card
    memset(&command, 0, sizeof(command));
    command.index = SD_CMD_SELECT_CARD;
    command.argument = rca << 16;
    command.type = kCARD_CommandTypeNormal;
    command.responseType = kCARD_ResponseTypeR1b;
    
    transfer.command = &command;
    transfer.data = nullptr;
    
    status = USDHC_TransferBlocking(SDCARD_USDHC_BASE, nullptr, &transfer);
    if (status != kStatus_Success)
    {
        m_state = SDCardState::Error;
        return Result::RESULT_BAD_PARAMETER;
    }
    
    // Set block size for SDSC cards
    if (m_cardInfo.type == SDCardType::SDSC)
    {
        memset(&command, 0, sizeof(command));
        command.index = SD_CMD_SET_BLOCKLEN;
        command.argument = SDCARD_BLOCK_SIZE;
        command.type = kCARD_CommandTypeNormal;
        command.responseType = kCARD_ResponseTypeR1;
        
        transfer.command = &command;
        transfer.data = nullptr;
        
        USDHC_TransferBlocking(SDCARD_USDHC_BASE, nullptr, &transfer);
    }
    
    // Enable 4-bit bus if configured
    if (m_config.use4BitBus)
    {
        // CMD55 + ACMD6: Set bus width
        memset(&command, 0, sizeof(command));
        command.index = SD_CMD_APP_CMD;
        command.argument = rca << 16;
        command.type = kCARD_CommandTypeNormal;
        command.responseType = kCARD_ResponseTypeR1;
        
        transfer.command = &command;
        transfer.data = nullptr;
        
        status = USDHC_TransferBlocking(SDCARD_USDHC_BASE, nullptr, &transfer);
        if (status == kStatus_Success)
        {
            memset(&command, 0, sizeof(command));
            command.index = SD_ACMD_SET_BUS_WIDTH;
            command.argument = 2U;  // 4-bit mode
            command.type = kCARD_CommandTypeNormal;
            command.responseType = kCARD_ResponseTypeR1;
            
            transfer.command = &command;
            transfer.data = nullptr;
            
            status = USDHC_TransferBlocking(SDCARD_USDHC_BASE, nullptr, &transfer);
            if (status == kStatus_Success)
            {
                USDHC_SetDataBusWidth(SDCARD_USDHC_BASE, kUSDHC_DataBusWidth4Bit);
            }
        }
    }
    
    // Increase clock speed for data transfer
    uint32_t targetFreq = m_config.enableHighSpeed ? 50000000U : 25000000U;
    if (targetFreq > m_config.maxFrequencyHz)
    {
        targetFreq = m_config.maxFrequencyHz;
    }
    USDHC_SetSdClock(SDCARD_USDHC_BASE, 198000000U, targetFreq);
    
    m_state = SDCardState::Ready;
    return Result::RESULT_SUCCESS;
}

bool SDCard::IsCardInserted() const
{
    return GPIO_PinRead(SDCARD_CD_GPIO, SDCARD_CD_PIN) == SDCARD_CD_INSERT_LEVEL;
}

Result SDCard::WaitForCardInserted(uint32_t timeoutMs)
{
    if (IsCardInserted())
    {
        return Result::RESULT_SUCCESS;
    }
    
    if (m_cardInsertSem && timeoutMs > 0)
    {
        Result res = m_cardInsertSem->wait(timeoutMs);
        if (res == Result::RESULT_SUCCESS)
        {
            return Result::RESULT_SUCCESS;
        }
        return Result::RESULT_SEMAPHORE_TIMEOUT;
    }
    
    return Result::RESULT_BAD_PARAMETER;
}

SDCardState SDCard::GetState() const
{
    return m_state;
}

Result SDCard::GetCardInfo(SDCardInfo& info) const
{
    if (m_state != SDCardState::Ready)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    info = m_cardInfo;
    return Result::RESULT_SUCCESS;
}

Result SDCard::ReadBlocks(uint8_t* buffer, uint32_t startBlock, uint32_t blockCount)
{
    if (!m_initialized || m_state != SDCardState::Ready)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    if (!buffer || blockCount == 0)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    m_mutex->Lock();
    m_state = SDCardState::Busy;
    
    usdhc_transfer_t transfer;
    usdhc_command_t command;
    usdhc_data_t data;
    usdhc_adma_config_t dmaConfig;
    
    memset(&command, 0, sizeof(command));
    memset(&data, 0, sizeof(data));
    memset(&dmaConfig, 0, sizeof(dmaConfig));
    
    // Configure DMA
    dmaConfig.dmaMode = kUSDHC_DmaModeAdma2;
    dmaConfig.burstLen = kUSDHC_EnBurstLenForINCR;
    dmaConfig.admaTable = s_dmaBuffer;
    dmaConfig.admaTableWords = SDCARD_DMA_BUFFER_SIZE;
    
    // Configure data
    data.enableAutoCommand12 = (blockCount > 1);
    data.enableAutoCommand23 = false;
    data.blockSize = SDCARD_BLOCK_SIZE;
    data.blockCount = blockCount;
    data.rxData = (uint32_t*)buffer;
    
    // Configure command
    if (blockCount == 1)
    {
        command.index = SD_CMD_READ_SINGLE_BLOCK;
    }
    else
    {
        command.index = SD_CMD_READ_MULTIPLE_BLOCK;
    }
    
    // Address is block number for SDHC/SDXC, byte address for SDSC
    if (m_cardInfo.type == SDCardType::SDSC)
    {
        command.argument = startBlock * SDCARD_BLOCK_SIZE;
    }
    else
    {
        command.argument = startBlock;
    }
    
    command.type = kCARD_CommandTypeNormal;
    command.responseType = kCARD_ResponseTypeR1;
    command.responseErrorFlags = 0xFFFFE008U;  // Error flags mask
    
    transfer.command = &command;
    transfer.data = &data;
    
    status_t status = USDHC_TransferBlocking(SDCARD_USDHC_BASE, &dmaConfig, &transfer);
    
    m_state = SDCardState::Ready;
    m_mutex->Unlock();
    
    return (status == kStatus_Success) ? Result::RESULT_SUCCESS : Result::RESULT_BAD_PARAMETER;
}

Result SDCard::WriteBlocks(const uint8_t* buffer, uint32_t startBlock, uint32_t blockCount)
{
    if (!m_initialized || m_state != SDCardState::Ready)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    if (!buffer || blockCount == 0)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    if (m_cardInfo.isWriteProtected)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    m_mutex->Lock();
    m_state = SDCardState::Busy;
    
    usdhc_transfer_t transfer;
    usdhc_command_t command;
    usdhc_data_t data;
    usdhc_adma_config_t dmaConfig;
    
    memset(&command, 0, sizeof(command));
    memset(&data, 0, sizeof(data));
    memset(&dmaConfig, 0, sizeof(dmaConfig));
    
    // Configure DMA
    dmaConfig.dmaMode = kUSDHC_DmaModeAdma2;
    dmaConfig.burstLen = kUSDHC_EnBurstLenForINCR;
    dmaConfig.admaTable = s_dmaBuffer;
    dmaConfig.admaTableWords = SDCARD_DMA_BUFFER_SIZE;
    
    // Configure data
    data.enableAutoCommand12 = (blockCount > 1);
    data.enableAutoCommand23 = false;
    data.blockSize = SDCARD_BLOCK_SIZE;
    data.blockCount = blockCount;
    data.txData = (const uint32_t*)buffer;
    
    // Configure command
    if (blockCount == 1)
    {
        command.index = SD_CMD_WRITE_SINGLE_BLOCK;
    }
    else
    {
        command.index = SD_CMD_WRITE_MULTIPLE_BLOCK;
    }
    
    // Address is block number for SDHC/SDXC, byte address for SDSC
    if (m_cardInfo.type == SDCardType::SDSC)
    {
        command.argument = startBlock * SDCARD_BLOCK_SIZE;
    }
    else
    {
        command.argument = startBlock;
    }
    
    command.type = kCARD_CommandTypeNormal;
    command.responseType = kCARD_ResponseTypeR1;
    command.responseErrorFlags = 0xFFFFE008U;
    
    transfer.command = &command;
    transfer.data = &data;
    
    status_t status = USDHC_TransferBlocking(SDCARD_USDHC_BASE, &dmaConfig, &transfer);
    
    m_state = SDCardState::Ready;
    m_mutex->Unlock();
    
    return (status == kStatus_Success) ? Result::RESULT_SUCCESS : Result::RESULT_BAD_PARAMETER;
}

Result SDCard::EraseBlocks(uint32_t startBlock, uint32_t blockCount)
{
    if (!m_initialized || m_state != SDCardState::Ready)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    if (blockCount == 0)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    m_mutex->Lock();
    m_state = SDCardState::Busy;
    
    usdhc_transfer_t transfer;
    usdhc_command_t command;
    status_t status;
    
    uint32_t endBlock = startBlock + blockCount - 1;
    
    // CMD32: Set erase start block
    memset(&command, 0, sizeof(command));
    command.index = SD_CMD_ERASE_WR_BLK_START;
    command.argument = (m_cardInfo.type == SDCardType::SDSC) ? 
                       (startBlock * SDCARD_BLOCK_SIZE) : startBlock;
    command.type = kCARD_CommandTypeNormal;
    command.responseType = kCARD_ResponseTypeR1;
    
    transfer.command = &command;
    transfer.data = nullptr;
    
    status = USDHC_TransferBlocking(SDCARD_USDHC_BASE, nullptr, &transfer);
    if (status != kStatus_Success)
    {
        m_state = SDCardState::Ready;
        m_mutex->Unlock();
        return Result::RESULT_BAD_PARAMETER;
    }
    
    // CMD33: Set erase end block
    memset(&command, 0, sizeof(command));
    command.index = SD_CMD_ERASE_WR_BLK_END;
    command.argument = (m_cardInfo.type == SDCardType::SDSC) ? 
                       (endBlock * SDCARD_BLOCK_SIZE) : endBlock;
    command.type = kCARD_CommandTypeNormal;
    command.responseType = kCARD_ResponseTypeR1;
    
    transfer.command = &command;
    transfer.data = nullptr;
    
    status = USDHC_TransferBlocking(SDCARD_USDHC_BASE, nullptr, &transfer);
    if (status != kStatus_Success)
    {
        m_state = SDCardState::Ready;
        m_mutex->Unlock();
        return Result::RESULT_BAD_PARAMETER;
    }
    
    // CMD38: Erase
    memset(&command, 0, sizeof(command));
    command.index = SD_CMD_ERASE;
    command.argument = 0U;
    command.type = kCARD_CommandTypeNormal;
    command.responseType = kCARD_ResponseTypeR1b;
    
    transfer.command = &command;
    transfer.data = nullptr;
    
    status = USDHC_TransferBlocking(SDCARD_USDHC_BASE, nullptr, &transfer);
    
    m_state = SDCardState::Ready;
    m_mutex->Unlock();
    
    return (status == kStatus_Success) ? Result::RESULT_SUCCESS : Result::RESULT_BAD_PARAMETER;
}

Result SDCard::Sync()
{
    // SD cards don't have explicit sync, operations are blocking
    return Result::RESULT_SUCCESS;
}

uint32_t SDCard::GetBlockSize() const
{
    return m_cardInfo.blockSize;
}

uint32_t SDCard::GetBlockCount() const
{
    return m_cardInfo.blockCount;
}

void SDCard::SetCardDetectCallback(void (*callback)(bool, void*), void* userData)
{
    m_cardDetectCallback = callback;
    m_callbackUserData = userData;
}

void SDCard::HandleInterrupt()
{
    uint32_t flags = USDHC_GetInterruptStatusFlags(SDCARD_USDHC_BASE);
    
    // Card insertion
    if (flags & kUSDHC_CardInsertionFlag)
    {
        USDHC_ClearInterruptStatusFlags(SDCARD_USDHC_BASE, kUSDHC_CardInsertionFlag);
        s_cardInserted = true;
        
        if (m_cardInsertSem)
        {
            m_cardInsertSem->signal();
        }
        
        if (m_cardDetectCallback)
        {
            m_cardDetectCallback(true, m_callbackUserData);
        }
    }
    
    // Card removal
    if (flags & kUSDHC_CardRemovalFlag)
    {
        USDHC_ClearInterruptStatusFlags(SDCARD_USDHC_BASE, kUSDHC_CardRemovalFlag);
        s_cardInserted = false;
        m_state = SDCardState::Removed;
        
        if (m_cardDetectCallback)
        {
            m_cardDetectCallback(false, m_callbackUserData);
        }
    }
    
    // Transfer complete
    if (flags & (kUSDHC_CommandCompleteFlag | kUSDHC_DataCompleteFlag))
    {
        USDHC_ClearInterruptStatusFlags(SDCARD_USDHC_BASE, 
            kUSDHC_CommandCompleteFlag | kUSDHC_DataCompleteFlag);
        
        if (m_transferComplete)
        {
            m_transferComplete->signal();
        }
    }
    
    // Errors
    if (flags & (kUSDHC_CommandErrorFlag | kUSDHC_DataErrorFlag))
    {
        USDHC_ClearInterruptStatusFlags(SDCARD_USDHC_BASE, 
            kUSDHC_CommandErrorFlag | kUSDHC_DataErrorFlag);
        m_state = SDCardState::Error;
    }
}

void SDCard::HandleCardDetectInterrupt()
{
    bool inserted = IsCardInserted();
    
    if (inserted && !s_cardInserted)
    {
        s_cardInserted = true;
        if (m_cardInsertSem)
        {
            m_cardInsertSem->signal();
        }
        if (m_cardDetectCallback)
        {
            m_cardDetectCallback(true, m_callbackUserData);
        }
    }
    else if (!inserted && s_cardInserted)
    {
        s_cardInserted = false;
        m_state = SDCardState::Removed;
        if (m_cardDetectCallback)
        {
            m_cardDetectCallback(false, m_callbackUserData);
        }
    }
}

void SDCard::PowerOn()
{
    GPIO_PinWrite(SDCARD_PWR_GPIO, SDCARD_PWR_PIN, 1U);
}

void SDCard::PowerOff()
{
    GPIO_PinWrite(SDCARD_PWR_GPIO, SDCARD_PWR_PIN, 0U);
}

// Singleton access
static SDCard g_sdCard;

SDCard& GetSDCard()
{
    return g_sdCard;
}

// Static DPC callback for USDHC1 interrupt - runs in ISR context via IntDefaultHandler
static void USDHC1_DPC_Callback(void* context)
{
    SDCard* sdCard = static_cast<SDCard*>(context);
    if (sdCard)
    {
        sdCard->HandleInterrupt();
    }
}

} // namespace HAL
} // namespace CRTOS
/*-----------------------------------------------------------------------*/
/* C Interface for FatFS diskio.c                                        */
/*-----------------------------------------------------------------------*/

extern "C" {

int CRTOS_SDCard_IsReady(void)
{
    return CRTOS::HAL::GetSDCard().GetState() == CRTOS::HAL::SDCardState::Ready ? 1 : 0;
}

int CRTOS_SDCard_ReadBlocks(unsigned char* buffer, unsigned long sector, unsigned int count)
{
    CRTOS::Result res = CRTOS::HAL::GetSDCard().ReadBlocks(buffer, (uint32_t)sector, (uint32_t)count);
    return (res == CRTOS::Result::RESULT_SUCCESS) ? 0 : -1;
}

int CRTOS_SDCard_WriteBlocks(const unsigned char* buffer, unsigned long sector, unsigned int count)
{
    CRTOS::Result res = CRTOS::HAL::GetSDCard().WriteBlocks(buffer, (uint32_t)sector, (uint32_t)count);
    return (res == CRTOS::Result::RESULT_SUCCESS) ? 0 : -1;
}

int CRTOS_SDCard_GetSectorCount(unsigned long* count)
{
    CRTOS::HAL::SDCardInfo info;
    CRTOS::Result res = CRTOS::HAL::GetSDCard().GetCardInfo(info);
    if (res == CRTOS::Result::RESULT_SUCCESS)
    {
        *count = (unsigned long)(info.capacityBytes / 512);
        return 0;
    }
    return -1;
}

int CRTOS_SDCard_GetSectorSize(unsigned int* size)
{
    *size = 512;
    return 0;
}

int CRTOS_SDCard_GetBlockSize(unsigned int* size)
{
    *size = 1;  /* Erase block size in sectors (1 for simple case) */
    return 0;
}

int CRTOS_SDCard_Sync(void)
{
    /* No buffering, always synced */
    return 0;
}

} /* extern "C" */
