/*
 * SPI.cpp - CRTOS Hardware Abstraction Layer - SPI Driver Implementation
 * Author: Arkadiusz Szlanta
 * Date: 04 Jan 2026
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 */

#include "SPI.hpp"
#include "../../device/MIMXRT1052.h"
#include "../../drivers/fsl_lpspi.h"
#include "../../drivers/fsl_lpspi_edma.h"
#include "../../drivers/fsl_dmamux.h"
#include "../../drivers/fsl_edma.h"
#include "../../drivers/fsl_clock.h"
#include <string.h>
#include <stdio.h>

// External tick count from CRTOS.cpp
extern volatile uint32_t tickCount;

namespace CRTOS
{
namespace HAL
{
    // Global SPI instances (created on demand)
    SPI* GlobalSPI[static_cast<int>(SPIInstance::COUNT)] = { nullptr };
    
    // DMA channel assignments for each SPI instance
    // Using channels 2-9 (0-1 reserved for display or other peripherals)
    static const uint8_t s_spiDmaRxChannels[4] = { 2, 4, 6, 8 };
    static const uint8_t s_spiDmaTxChannels[4] = { 3, 5, 7, 9 };
    
    // DMA request sources for each LPSPI
    static const uint32_t s_spiDmaRxSources[4] = {
        kDmaRequestMuxLPSPI1Rx,
        kDmaRequestMuxLPSPI2Rx,
        kDmaRequestMuxLPSPI3Rx,
        kDmaRequestMuxLPSPI4Rx
    };
    static const uint32_t s_spiDmaTxSources[4] = {
        kDmaRequestMuxLPSPI1Tx,
        kDmaRequestMuxLPSPI2Tx,
        kDmaRequestMuxLPSPI3Tx,
        kDmaRequestMuxLPSPI4Tx
    };
    
    // IRQ numbers for each LPSPI
    static const IRQn_Type s_spiIRQs[4] = {
        LPSPI1_IRQn,
        LPSPI2_IRQn,
        LPSPI3_IRQn,
        LPSPI4_IRQn
    };
    
    // Context for callbacks
    static SPI* s_spiContexts[4] = { nullptr };
    
    // ========================================================================
    // Interrupt callback wrapper
    // ========================================================================
    static void LPSPI_MasterIRQCallback(LPSPI_Type* base, 
                                         lpspi_master_handle_t* handle,
                                         status_t status, 
                                         void* userData)
    {
        SPI* spi = static_cast<SPI*>(userData);
        if (spi)
        {
            SPIStatus result = (status == kStatus_Success) ? SPIStatus::Complete : SPIStatus::Error;
            spi->OnTransferComplete(result);
        }
    }
    
    // ========================================================================
    // DMA callback wrapper
    // ========================================================================
    static void LPSPI_MasterDMACallback(LPSPI_Type* base, 
                                         lpspi_master_edma_handle_t* handle,
                                         status_t status, 
                                         void* userData)
    {
        SPI* spi = static_cast<SPI*>(userData);
        if (spi)
        {
            SPIStatus result = (status == kStatus_Success) ? SPIStatus::Complete : SPIStatus::Error;
            spi->OnTransferComplete(result);
        }
    }
    
    // ========================================================================
    // SPI Implementation
    // ========================================================================
    
    LPSPI_Type* SPI::GetBaseAddress(SPIInstance instance)
    {
        switch (instance)
        {
            case SPIInstance::SPI1: return LPSPI1;
            case SPIInstance::SPI2: return LPSPI2;
            case SPIInstance::SPI3: return LPSPI3;
            case SPIInstance::SPI4: return LPSPI4;
            default: return nullptr;
        }
    }
    
    SPI::SPI(SPIInstance instance)
        : m_instance(instance)
        , m_base(GetBaseAddress(instance))
        , m_initialized(false)
        , m_transferMode(SPITransferMode::Blocking)
        , m_status(SPIStatus::Idle)
        , m_callback(nullptr)
        , m_userData(nullptr)
        , m_completeSemaphore(nullptr)
        , m_irqHandle(nullptr)
        , m_dmaHandle(nullptr)
        , m_dmaRxHandle(nullptr)
        , m_dmaTxHandle(nullptr)
        , m_dmaRxChannel(0)
        , m_dmaTxChannel(0)
    {
        memset(&m_config, 0, sizeof(m_config));
    }
    
    SPI::~SPI()
    {
        Deinitialize();
    }
    
    SPIConfig SPI::GetDefaultConfig()
    {
        SPIConfig config;
        config.baudRate = 500000;                    // 500 kHz
        config.mode = SPIMode::Mode0;                // CPOL=0, CPHA=0
        config.pcs = SPIPCS::PCS0;                   // Use PCS0
        config.transferMode = SPITransferMode::DMA;  // Use DMA
        config.bitsPerFrame = 8;                     // 8 bits
        config.pcsToSckDelayNs = 1000;               // 1us delays
        config.sckToPcsDelayNs = 1000;
        config.betweenTransferDelayNs = 1000;
        config.pcsContinuous = false;
        config.lsbFirst = false;                     // MSB first
        return config;
    }
    
    Result SPI::Initialize(const SPIConfig& config)
    {
        if (m_initialized)
        {
            return Result::RESULT_CRC_ALREADY_INITIALIZED;
        }
        
        if (m_base == nullptr)
        {
            return Result::RESULT_BAD_PARAMETER;
        }
        
        m_config = config;
        m_transferMode = config.transferMode;
        
        // Get source clock
        uint32_t srcClock = GetClockFrequency();
        
        // Configure LPSPI master
        lpspi_master_config_t masterConfig;
        LPSPI_MasterGetDefaultConfig(&masterConfig);
        
        masterConfig.baudRate = config.baudRate;
        masterConfig.bitsPerFrame = config.bitsPerFrame;
        
        // Set clock polarity/phase
        switch (config.mode)
        {
            case SPIMode::Mode0:
                masterConfig.cpol = kLPSPI_ClockPolarityActiveHigh;
                masterConfig.cpha = kLPSPI_ClockPhaseFirstEdge;
                break;
            case SPIMode::Mode1:
                masterConfig.cpol = kLPSPI_ClockPolarityActiveHigh;
                masterConfig.cpha = kLPSPI_ClockPhaseSecondEdge;
                break;
            case SPIMode::Mode2:
                masterConfig.cpol = kLPSPI_ClockPolarityActiveLow;
                masterConfig.cpha = kLPSPI_ClockPhaseFirstEdge;
                break;
            case SPIMode::Mode3:
                masterConfig.cpol = kLPSPI_ClockPolarityActiveLow;
                masterConfig.cpha = kLPSPI_ClockPhaseSecondEdge;
                break;
        }
        
        // Set chip select
        masterConfig.whichPcs = static_cast<lpspi_which_pcs_t>(config.pcs);
        
        // Set delays
        masterConfig.pcsToSckDelayInNanoSec = config.pcsToSckDelayNs;
        masterConfig.lastSckToPcsDelayInNanoSec = config.sckToPcsDelayNs;
        masterConfig.betweenTransferDelayInNanoSec = config.betweenTransferDelayNs;
        
        // Bit order
        masterConfig.direction = config.lsbFirst ? kLPSPI_LsbFirst : kLPSPI_MsbFirst;
        
        // Initialize hardware
        LPSPI_MasterInit(m_base, &masterConfig, srcClock);
        
        printf("[SPI%d] Initialized: %lu Hz, mode %d\r\n", 
               static_cast<int>(m_instance) + 1, config.baudRate, static_cast<int>(config.mode));
        
        // Create completion semaphore
        m_completeSemaphore = new BinarySemaphore();
        if (!m_completeSemaphore)
        {
            LPSPI_Deinit(m_base);
            return Result::RESULT_NO_MEMORY;
        }
        
        // Configure transfer mode
        if (config.transferMode == SPITransferMode::Interrupt)
        {
            // Allocate interrupt handle
            m_irqHandle = new lpspi_master_handle_t();
            if (!m_irqHandle)
            {
                delete m_completeSemaphore;
                m_completeSemaphore = nullptr;
                LPSPI_Deinit(m_base);
                return Result::RESULT_NO_MEMORY;
            }
            
            memset(m_irqHandle, 0, sizeof(lpspi_master_handle_t));
            
            // Create transfer handle
            LPSPI_MasterTransferCreateHandle(m_base, m_irqHandle, 
                                              LPSPI_MasterIRQCallback, this);
            
            // Enable IRQ
            NVIC_SetPriority(s_spiIRQs[static_cast<int>(m_instance)], 5);
            NVIC_EnableIRQ(s_spiIRQs[static_cast<int>(m_instance)]);
            
            printf("[SPI%d] Interrupt mode configured\r\n", static_cast<int>(m_instance) + 1);
        }
        else if (config.transferMode == SPITransferMode::DMA)
        {
            Result dmaResult = ConfigureDMA();
            if (dmaResult != Result::RESULT_SUCCESS)
            {
                delete m_completeSemaphore;
                m_completeSemaphore = nullptr;
                LPSPI_Deinit(m_base);
                return dmaResult;
            }
            
            printf("[SPI%d] DMA mode configured (RX ch%d, TX ch%d)\r\n", 
                   static_cast<int>(m_instance) + 1, m_dmaRxChannel, m_dmaTxChannel);
        }
        
        // Store context for callbacks
        s_spiContexts[static_cast<int>(m_instance)] = this;
        
        m_initialized = true;
        m_status = SPIStatus::Idle;
        
        return Result::RESULT_SUCCESS;
    }
    
    Result SPI::ConfigureDMA()
    {
        int idx = static_cast<int>(m_instance);
        
        m_dmaRxChannel = s_spiDmaRxChannels[idx];
        m_dmaTxChannel = s_spiDmaTxChannels[idx];
        
        // Initialize DMAMUX
        DMAMUX_Init(DMAMUX);
        
        // Configure RX channel
        DMAMUX_SetSource(DMAMUX, m_dmaRxChannel, s_spiDmaRxSources[idx]);
        DMAMUX_EnableChannel(DMAMUX, m_dmaRxChannel);
        
        // Configure TX channel
        DMAMUX_SetSource(DMAMUX, m_dmaTxChannel, s_spiDmaTxSources[idx]);
        DMAMUX_EnableChannel(DMAMUX, m_dmaTxChannel);
        
        // Initialize EDMA (use default config)
        edma_config_t edmaConfig;
        EDMA_GetDefaultConfig(&edmaConfig);
        EDMA_Init(DMA0, &edmaConfig);
        
        // Allocate handles
        m_dmaRxHandle = new edma_handle_t();
        m_dmaTxHandle = new edma_handle_t();
        m_dmaHandle = new lpspi_master_edma_handle_t();
        
        if (!m_dmaRxHandle || !m_dmaTxHandle || !m_dmaHandle)
        {
            FreeDMA();
            return Result::RESULT_NO_MEMORY;
        }
        
        memset(m_dmaRxHandle, 0, sizeof(edma_handle_t));
        memset(m_dmaTxHandle, 0, sizeof(edma_handle_t));
        memset(m_dmaHandle, 0, sizeof(lpspi_master_edma_handle_t));
        
        // Create EDMA handles
        EDMA_CreateHandle(m_dmaRxHandle, DMA0, m_dmaRxChannel);
        EDMA_CreateHandle(m_dmaTxHandle, DMA0, m_dmaTxChannel);
        
        // Create LPSPI EDMA handle
        LPSPI_MasterTransferCreateHandleEDMA(m_base, m_dmaHandle,
                                              LPSPI_MasterDMACallback, this,
                                              m_dmaRxHandle, m_dmaTxHandle);
        
        return Result::RESULT_SUCCESS;
    }
    
    void SPI::FreeDMA()
    {
        // Disable DMAMUX channels
        DMAMUX_DisableChannel(DMAMUX, m_dmaRxChannel);
        DMAMUX_DisableChannel(DMAMUX, m_dmaTxChannel);
        
        // Free handles
        if (m_dmaHandle)
        {
            delete m_dmaHandle;
            m_dmaHandle = nullptr;
        }
        if (m_dmaRxHandle)
        {
            delete m_dmaRxHandle;
            m_dmaRxHandle = nullptr;
        }
        if (m_dmaTxHandle)
        {
            delete m_dmaTxHandle;
            m_dmaTxHandle = nullptr;
        }
    }
    
    void SPI::Deinitialize()
    {
        if (!m_initialized)
        {
            return;
        }
        
        // Abort any pending transfer
        AbortTransfer();
        
        // Free mode-specific resources
        if (m_transferMode == SPITransferMode::Interrupt)
        {
            NVIC_DisableIRQ(s_spiIRQs[static_cast<int>(m_instance)]);
            if (m_irqHandle)
            {
                delete m_irqHandle;
                m_irqHandle = nullptr;
            }
        }
        else if (m_transferMode == SPITransferMode::DMA)
        {
            FreeDMA();
        }
        
        // Free semaphore
        if (m_completeSemaphore)
        {
            delete m_completeSemaphore;
            m_completeSemaphore = nullptr;
        }
        
        // Deinitialize LPSPI hardware
        LPSPI_Deinit(m_base);
        
        // Clear context
        s_spiContexts[static_cast<int>(m_instance)] = nullptr;
        
        m_initialized = false;
        
        printf("[SPI%d] Deinitialized\r\n", static_cast<int>(m_instance) + 1);
    }
    
    uint32_t SPI::SetBaudRate(uint32_t baudRate)
    {
        if (!m_initialized)
        {
            return 0;
        }
        
        uint32_t srcClock = GetClockFrequency();
        uint32_t tcrPrescale = 0;
        uint32_t actual = LPSPI_MasterSetBaudRate(m_base, baudRate, srcClock, &tcrPrescale);
        m_config.baudRate = actual;
        
        return actual;
    }
    
    Result SPI::SetMode(SPIMode mode)
    {
        if (!m_initialized)
        {
            return Result::RESULT_BAD_PARAMETER;
        }
        
        // Must disable to change mode
        LPSPI_Enable(m_base, false);
        
        uint32_t tcr = m_base->TCR;
        tcr &= ~(LPSPI_TCR_CPOL_MASK | LPSPI_TCR_CPHA_MASK);
        
        switch (mode)
        {
            case SPIMode::Mode0:
                // CPOL=0, CPHA=0
                break;
            case SPIMode::Mode1:
                tcr |= LPSPI_TCR_CPHA_MASK;
                break;
            case SPIMode::Mode2:
                tcr |= LPSPI_TCR_CPOL_MASK;
                break;
            case SPIMode::Mode3:
                tcr |= LPSPI_TCR_CPOL_MASK | LPSPI_TCR_CPHA_MASK;
                break;
        }
        
        m_base->TCR = tcr;
        m_config.mode = mode;
        
        LPSPI_Enable(m_base, true);
        
        return Result::RESULT_SUCCESS;
    }
    
    Result SPI::SetPCS(SPIPCS pcs)
    {
        if (!m_initialized)
        {
            return Result::RESULT_BAD_PARAMETER;
        }
        
        // PCS is set per-transfer in TCR
        m_config.pcs = pcs;
        
        return Result::RESULT_SUCCESS;
    }
    
    Result SPI::SetTransferMode(SPITransferMode mode)
    {
        if (!m_initialized)
        {
            return Result::RESULT_BAD_PARAMETER;
        }
        
        // Cannot change mode on the fly - would need reinit
        // For now, just store it for future transfers
        if (mode != m_transferMode)
        {
            printf("[SPI%d] Warning: transfer mode change requires reinit\r\n", 
                   static_cast<int>(m_instance) + 1);
        }
        
        return Result::RESULT_SUCCESS;
    }
    
    Result SPI::TransferBlocking(SPITransfer& transfer)
    {
        if (!m_initialized)
        {
            return Result::RESULT_BAD_PARAMETER;
        }
        
        if (m_status == SPIStatus::Busy)
        {
            return Result::RESULT_SEMAPHORE_BUSY;
        }
        
        m_status = SPIStatus::Busy;
        
        lpspi_transfer_t xfer;
        xfer.txData = const_cast<uint8_t*>(transfer.txData);
        xfer.rxData = transfer.rxData;
        xfer.dataSize = transfer.dataSize;
        xfer.configFlags = static_cast<uint32_t>(m_config.pcs) << LPSPI_MASTER_PCS_SHIFT;
        
        if (m_config.pcsContinuous)
        {
            xfer.configFlags |= kLPSPI_MasterPcsContinuous;
        }
        
        status_t status = LPSPI_MasterTransferBlocking(m_base, &xfer);
        
        m_status = (status == kStatus_Success) ? SPIStatus::Complete : SPIStatus::Error;
        
        return (status == kStatus_Success) ? Result::RESULT_SUCCESS : Result::RESULT_IO_ERROR;
    }
    
    Result SPI::TransferNonBlocking(SPITransfer& transfer, 
                                     SPICallback callback,
                                     void* userData)
    {
        if (!m_initialized)
        {
            return Result::RESULT_BAD_PARAMETER;
        }
        
        if (m_status == SPIStatus::Busy)
        {
            return Result::RESULT_SEMAPHORE_BUSY;
        }
        
        m_callback = callback;
        m_userData = userData;
        m_status = SPIStatus::Busy;
        
        lpspi_transfer_t xfer;
        xfer.txData = const_cast<uint8_t*>(transfer.txData);
        xfer.rxData = transfer.rxData;
        xfer.dataSize = transfer.dataSize;
        xfer.configFlags = static_cast<uint32_t>(m_config.pcs) << LPSPI_MASTER_PCS_SHIFT;
        
        if (m_config.pcsContinuous)
        {
            xfer.configFlags |= kLPSPI_MasterPcsContinuous;
        }
        
        status_t status;
        
        if (m_transferMode == SPITransferMode::DMA && m_dmaHandle)
        {
            status = LPSPI_MasterTransferEDMA(m_base, m_dmaHandle, &xfer);
        }
        else if (m_transferMode == SPITransferMode::Interrupt && m_irqHandle)
        {
            status = LPSPI_MasterTransferNonBlocking(m_base, m_irqHandle, &xfer);
        }
        else
        {
            // Fallback to blocking
            status = LPSPI_MasterTransferBlocking(m_base, &xfer);
            m_status = (status == kStatus_Success) ? SPIStatus::Complete : SPIStatus::Error;
            
            if (callback)
            {
                callback(m_instance, m_status, userData);
            }
        }
        
        if (status != kStatus_Success)
        {
            m_status = SPIStatus::Error;
            return Result::RESULT_IO_ERROR;
        }
        
        return Result::RESULT_SUCCESS;
    }
    
    void SPI::AbortTransfer()
    {
        if (!m_initialized)
        {
            return;
        }
        
        if (m_transferMode == SPITransferMode::DMA && m_dmaHandle)
        {
            LPSPI_MasterTransferAbortEDMA(m_base, m_dmaHandle);
        }
        else if (m_transferMode == SPITransferMode::Interrupt && m_irqHandle)
        {
            LPSPI_MasterTransferAbort(m_base, m_irqHandle);
        }
        
        m_status = SPIStatus::Idle;
    }
    
    Result SPI::WaitComplete(uint32_t timeout_ms)
    {
        if (!m_initialized)
        {
            return Result::RESULT_BAD_PARAMETER;
        }
        
        if (m_status != SPIStatus::Busy)
        {
            return (m_status == SPIStatus::Complete) ? 
                   Result::RESULT_SUCCESS : Result::RESULT_IO_ERROR;
        }
        
        // Simple polling wait
        uint32_t start = ::tickCount;
        uint32_t timeout = (timeout_ms == 0) ? 0xFFFFFFFF : timeout_ms;
        
        while (m_status == SPIStatus::Busy)
        {
            if ((::tickCount - start) >= timeout)
            {
                return Result::RESULT_SEMAPHORE_TIMEOUT;
            }
        }
        
        return (m_status == SPIStatus::Complete) ? 
               Result::RESULT_SUCCESS : Result::RESULT_IO_ERROR;
    }
    
    void SPI::OnTransferComplete(SPIStatus status)
    {
        m_status = status;
        
        // Signal semaphore
        if (m_completeSemaphore)
        {
            m_completeSemaphore->signal();
        }
        
        // Call user callback
        if (m_callback)
        {
            m_callback(m_instance, status, m_userData);
        }
    }
    
    Result SPI::Write(const uint8_t* data, size_t len)
    {
        SPITransfer xfer = { data, nullptr, len };
        return TransferBlocking(xfer);
    }
    
    Result SPI::Read(uint8_t* data, size_t len)
    {
        SPITransfer xfer = { nullptr, data, len };
        return TransferBlocking(xfer);
    }
    
    Result SPI::Transfer(const uint8_t* txData, uint8_t* rxData, size_t len)
    {
        SPITransfer xfer = { txData, rxData, len };
        return TransferBlocking(xfer);
    }
    
    uint32_t SPI::GetIRQNumber() const
    {
        return static_cast<uint32_t>(s_spiIRQs[static_cast<int>(m_instance)]);
    }
    
    uint32_t SPI::GetClockFrequency() const
    {
        // LPSPI uses USB1 PLL PFD0 as clock source
        // Divider is typically 7+1 = 8
        return CLOCK_GetFreq(kCLOCK_Usb1PllPfd0Clk) / 8;
    }
    
    SPI& GetSPI(SPIInstance instance)
    {
        int idx = static_cast<int>(instance);
        if (idx >= static_cast<int>(SPIInstance::COUNT))
        {
            idx = 0;
        }
        
        if (GlobalSPI[idx] == nullptr)
        {
            GlobalSPI[idx] = new SPI(instance);
        }
        
        return *GlobalSPI[idx];
    }

} // namespace HAL
} // namespace CRTOS
