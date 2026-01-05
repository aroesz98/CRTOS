/*
 * HAL_SPI.cpp - Hardware Abstraction Layer for SPI (C Wrappers)
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
 * C-linkage wrapper functions for SPI class.
 * These are exported to loadable modules via ModuleLoader.
 */

#include "HAL_SPI.hpp"
#include "SPI.hpp"

using namespace CRTOS::HAL;

// ============================================================================
// Helper Functions
// ============================================================================

static SPIInstance ConvertInstance(hal_spi_instance_t inst)
{
    switch (inst)
    {
        case HAL_SPI_INSTANCE_1: return SPIInstance::SPI1;
        case HAL_SPI_INSTANCE_2: return SPIInstance::SPI2;
        case HAL_SPI_INSTANCE_3: return SPIInstance::SPI3;
        case HAL_SPI_INSTANCE_4: return SPIInstance::SPI4;
        default: return SPIInstance::SPI1;
    }
}

static SPIMode ConvertMode(hal_spi_mode_t mode)
{
    switch (mode)
    {
        case HAL_SPI_MODE_0: return SPIMode::Mode0;
        case HAL_SPI_MODE_1: return SPIMode::Mode1;
        case HAL_SPI_MODE_2: return SPIMode::Mode2;
        case HAL_SPI_MODE_3: return SPIMode::Mode3;
        default: return SPIMode::Mode0;
    }
}

static SPIPCS ConvertPCS(hal_spi_pcs_t pcs)
{
    switch (pcs)
    {
        case HAL_SPI_PCS_0: return SPIPCS::PCS0;
        case HAL_SPI_PCS_1: return SPIPCS::PCS1;
        case HAL_SPI_PCS_2: return SPIPCS::PCS2;
        case HAL_SPI_PCS_3: return SPIPCS::PCS3;
        default: return SPIPCS::PCS0;
    }
}

static SPITransferMode ConvertTransferMode(hal_spi_transfer_mode_t mode)
{
    switch (mode)
    {
        case HAL_SPI_TRANSFER_BLOCKING: return SPITransferMode::Blocking;
        case HAL_SPI_TRANSFER_INTERRUPT: return SPITransferMode::Interrupt;
        case HAL_SPI_TRANSFER_DMA: return SPITransferMode::DMA;
        default: return SPITransferMode::Blocking;
    }
}

static hal_spi_status_t ConvertStatus(SPIStatus status)
{
    switch (status)
    {
        case SPIStatus::Idle: return HAL_SPI_STATUS_IDLE;
        case SPIStatus::Busy: return HAL_SPI_STATUS_BUSY;
        case SPIStatus::Complete: return HAL_SPI_STATUS_COMPLETE;
        case SPIStatus::Error: return HAL_SPI_STATUS_ERROR;
        default: return HAL_SPI_STATUS_IDLE;
    }
}

// Callback wrapper context
struct CallbackContext
{
    hal_spi_callback_t callback;
    void* userData;
};

static CallbackContext s_callbackContexts[HAL_SPI_INSTANCE_COUNT] = {};

static void InternalCallback(SPIInstance instance, SPIStatus status, void* userData)
{
    int idx = static_cast<int>(instance);
    if (idx < HAL_SPI_INSTANCE_COUNT && s_callbackContexts[idx].callback)
    {
        s_callbackContexts[idx].callback(
            static_cast<hal_spi_instance_t>(idx),
            ConvertStatus(status),
            s_callbackContexts[idx].userData
        );
    }
}

// ============================================================================
// Initialization API
// ============================================================================

void hal_spi_get_default_config(hal_spi_config_t* config)
{
    if (config == nullptr)
    {
        return;
    }
    
    SPIConfig defaultCfg = SPI::GetDefaultConfig();
    
    config->baudRate = defaultCfg.baudRate;
    config->mode = static_cast<hal_spi_mode_t>(defaultCfg.mode);
    config->pcs = static_cast<hal_spi_pcs_t>(defaultCfg.pcs);
    config->transferMode = static_cast<hal_spi_transfer_mode_t>(defaultCfg.transferMode);
    config->bitOrder = defaultCfg.lsbFirst ? HAL_SPI_LSB_FIRST : HAL_SPI_MSB_FIRST;
    config->bitsPerFrame = defaultCfg.bitsPerFrame;
    config->pcsToSckDelayNs = defaultCfg.pcsToSckDelayNs;
    config->sckToPcsDelayNs = defaultCfg.sckToPcsDelayNs;
    config->betweenTransferDelayNs = defaultCfg.betweenTransferDelayNs;
    config->pcsContinuous = defaultCfg.pcsContinuous;
}

bool hal_spi_init(hal_spi_instance_t instance, const hal_spi_config_t* config)
{
    if (config == nullptr || instance >= HAL_SPI_INSTANCE_COUNT)
    {
        return false;
    }
    
    SPIConfig cppConfig;
    cppConfig.baudRate = config->baudRate;
    cppConfig.mode = ConvertMode(config->mode);
    cppConfig.pcs = ConvertPCS(config->pcs);
    cppConfig.transferMode = ConvertTransferMode(config->transferMode);
    cppConfig.bitsPerFrame = config->bitsPerFrame;
    cppConfig.pcsToSckDelayNs = config->pcsToSckDelayNs;
    cppConfig.sckToPcsDelayNs = config->sckToPcsDelayNs;
    cppConfig.betweenTransferDelayNs = config->betweenTransferDelayNs;
    cppConfig.pcsContinuous = config->pcsContinuous;
    cppConfig.lsbFirst = (config->bitOrder == HAL_SPI_LSB_FIRST);
    
    SPI& spi = GetSPI(ConvertInstance(instance));
    CRTOS::Result result = spi.Initialize(cppConfig);
    
    return (result == CRTOS::Result::RESULT_SUCCESS);
}

void hal_spi_deinit(hal_spi_instance_t instance)
{
    if (instance >= HAL_SPI_INSTANCE_COUNT)
    {
        return;
    }
    
    int idx = static_cast<int>(instance);
    if (GlobalSPI[idx] != nullptr)
    {
        GlobalSPI[idx]->Deinitialize();
    }
}

bool hal_spi_is_initialized(hal_spi_instance_t instance)
{
    if (instance >= HAL_SPI_INSTANCE_COUNT)
    {
        return false;
    }
    
    int idx = static_cast<int>(instance);
    if (GlobalSPI[idx] == nullptr)
    {
        return false;
    }
    
    return GlobalSPI[idx]->IsInitialized();
}

// ============================================================================
// Configuration API
// ============================================================================

uint32_t hal_spi_set_baudrate(hal_spi_instance_t instance, uint32_t baudRate)
{
    if (instance >= HAL_SPI_INSTANCE_COUNT)
    {
        return 0;
    }
    
    int idx = static_cast<int>(instance);
    if (GlobalSPI[idx] == nullptr || !GlobalSPI[idx]->IsInitialized())
    {
        return 0;
    }
    
    return GlobalSPI[idx]->SetBaudRate(baudRate);
}

bool hal_spi_set_mode(hal_spi_instance_t instance, hal_spi_mode_t mode)
{
    if (instance >= HAL_SPI_INSTANCE_COUNT)
    {
        return false;
    }
    
    int idx = static_cast<int>(instance);
    if (GlobalSPI[idx] == nullptr || !GlobalSPI[idx]->IsInitialized())
    {
        return false;
    }
    
    CRTOS::Result result = GlobalSPI[idx]->SetMode(ConvertMode(mode));
    return (result == CRTOS::Result::RESULT_SUCCESS);
}

bool hal_spi_set_pcs(hal_spi_instance_t instance, hal_spi_pcs_t pcs)
{
    if (instance >= HAL_SPI_INSTANCE_COUNT)
    {
        return false;
    }
    
    int idx = static_cast<int>(instance);
    if (GlobalSPI[idx] == nullptr || !GlobalSPI[idx]->IsInitialized())
    {
        return false;
    }
    
    CRTOS::Result result = GlobalSPI[idx]->SetPCS(ConvertPCS(pcs));
    return (result == CRTOS::Result::RESULT_SUCCESS);
}

bool hal_spi_set_transfer_mode(hal_spi_instance_t instance, hal_spi_transfer_mode_t mode)
{
    if (instance >= HAL_SPI_INSTANCE_COUNT)
    {
        return false;
    }
    
    int idx = static_cast<int>(instance);
    if (GlobalSPI[idx] == nullptr || !GlobalSPI[idx]->IsInitialized())
    {
        return false;
    }
    
    CRTOS::Result result = GlobalSPI[idx]->SetTransferMode(ConvertTransferMode(mode));
    return (result == CRTOS::Result::RESULT_SUCCESS);
}

// ============================================================================
// Transfer API
// ============================================================================

bool hal_spi_transfer_blocking(hal_spi_instance_t instance, hal_spi_transfer_t* transfer)
{
    if (instance >= HAL_SPI_INSTANCE_COUNT || transfer == nullptr)
    {
        return false;
    }
    
    int idx = static_cast<int>(instance);
    if (GlobalSPI[idx] == nullptr || !GlobalSPI[idx]->IsInitialized())
    {
        return false;
    }
    
    SPITransfer xfer;
    xfer.txData = transfer->txData;
    xfer.rxData = transfer->rxData;
    xfer.dataSize = transfer->dataSize;
    
    CRTOS::Result result = GlobalSPI[idx]->TransferBlocking(xfer);
    return (result == CRTOS::Result::RESULT_SUCCESS);
}

bool hal_spi_transfer_nonblocking(hal_spi_instance_t instance, 
                                   hal_spi_transfer_t* transfer,
                                   hal_spi_callback_t callback,
                                   void* userData)
{
    if (instance >= HAL_SPI_INSTANCE_COUNT || transfer == nullptr)
    {
        return false;
    }
    
    int idx = static_cast<int>(instance);
    if (GlobalSPI[idx] == nullptr || !GlobalSPI[idx]->IsInitialized())
    {
        return false;
    }
    
    // Store callback context
    s_callbackContexts[idx].callback = callback;
    s_callbackContexts[idx].userData = userData;
    
    SPITransfer xfer;
    xfer.txData = transfer->txData;
    xfer.rxData = transfer->rxData;
    xfer.dataSize = transfer->dataSize;
    
    CRTOS::Result result = GlobalSPI[idx]->TransferNonBlocking(
        xfer, 
        callback ? InternalCallback : nullptr, 
        nullptr
    );
    
    return (result == CRTOS::Result::RESULT_SUCCESS);
}

hal_spi_status_t hal_spi_get_status(hal_spi_instance_t instance)
{
    if (instance >= HAL_SPI_INSTANCE_COUNT)
    {
        return HAL_SPI_STATUS_ERROR;
    }
    
    int idx = static_cast<int>(instance);
    if (GlobalSPI[idx] == nullptr)
    {
        return HAL_SPI_STATUS_IDLE;
    }
    
    return ConvertStatus(GlobalSPI[idx]->GetStatus());
}

void hal_spi_abort_transfer(hal_spi_instance_t instance)
{
    if (instance >= HAL_SPI_INSTANCE_COUNT)
    {
        return;
    }
    
    int idx = static_cast<int>(instance);
    if (GlobalSPI[idx] != nullptr)
    {
        GlobalSPI[idx]->AbortTransfer();
    }
}

bool hal_spi_wait_complete(hal_spi_instance_t instance, uint32_t timeout_ms)
{
    if (instance >= HAL_SPI_INSTANCE_COUNT)
    {
        return false;
    }
    
    int idx = static_cast<int>(instance);
    if (GlobalSPI[idx] == nullptr || !GlobalSPI[idx]->IsInitialized())
    {
        return false;
    }
    
    CRTOS::Result result = GlobalSPI[idx]->WaitComplete(timeout_ms);
    return (result == CRTOS::Result::RESULT_SUCCESS);
}

// ============================================================================
// Simple Transfer API
// ============================================================================

bool hal_spi_write(hal_spi_instance_t instance, const uint8_t* data, size_t len)
{
    if (instance >= HAL_SPI_INSTANCE_COUNT || data == nullptr)
    {
        return false;
    }
    
    int idx = static_cast<int>(instance);
    if (GlobalSPI[idx] == nullptr || !GlobalSPI[idx]->IsInitialized())
    {
        return false;
    }
    
    CRTOS::Result result = GlobalSPI[idx]->Write(data, len);
    return (result == CRTOS::Result::RESULT_SUCCESS);
}

bool hal_spi_read(hal_spi_instance_t instance, uint8_t* data, size_t len)
{
    if (instance >= HAL_SPI_INSTANCE_COUNT || data == nullptr)
    {
        return false;
    }
    
    int idx = static_cast<int>(instance);
    if (GlobalSPI[idx] == nullptr || !GlobalSPI[idx]->IsInitialized())
    {
        return false;
    }
    
    CRTOS::Result result = GlobalSPI[idx]->Read(data, len);
    return (result == CRTOS::Result::RESULT_SUCCESS);
}

bool hal_spi_write_read(hal_spi_instance_t instance, 
                         const uint8_t* txData, size_t txLen,
                         uint8_t* rxData, size_t rxLen)
{
    if (instance >= HAL_SPI_INSTANCE_COUNT)
    {
        return false;
    }
    
    int idx = static_cast<int>(instance);
    if (GlobalSPI[idx] == nullptr || !GlobalSPI[idx]->IsInitialized())
    {
        return false;
    }
    
    // First write TX data
    if (txData != nullptr && txLen > 0)
    {
        CRTOS::Result result = GlobalSPI[idx]->Write(txData, txLen);
        if (result != CRTOS::Result::RESULT_SUCCESS)
        {
            return false;
        }
    }
    
    // Then read RX data
    if (rxData != nullptr && rxLen > 0)
    {
        CRTOS::Result result = GlobalSPI[idx]->Read(rxData, rxLen);
        if (result != CRTOS::Result::RESULT_SUCCESS)
        {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// Info API
// ============================================================================

uint32_t hal_spi_get_irq_number(hal_spi_instance_t instance)
{
    if (instance >= HAL_SPI_INSTANCE_COUNT)
    {
        return 0;
    }
    
    SPI& spi = GetSPI(ConvertInstance(instance));
    return spi.GetIRQNumber();
}

uint32_t hal_spi_get_clock_freq(hal_spi_instance_t instance)
{
    if (instance >= HAL_SPI_INSTANCE_COUNT)
    {
        return 0;
    }
    
    SPI& spi = GetSPI(ConvertInstance(instance));
    return spi.GetClockFrequency();
}
