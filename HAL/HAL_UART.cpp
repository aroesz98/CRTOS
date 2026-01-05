/*
 * HAL_UART.cpp - Hardware Abstraction Layer for UART
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
 * Hardware abstraction layer for UART communication on i.MX RT1052.
 * Uses NXP LPUART driver for hardware access.
 * Supports all LPUART instances (1-8).
 * 
 * This is a pure HAL - no kernel/driver dependencies.
 */

#include "HAL_UART.hpp"
#include "../../device/fsl_device_registers.h"
#include "../../drivers/fsl_lpuart.h"
#include "../../drivers/fsl_clock.h"
#include "../../board/board.h"
#include "fsl_debug_console.h"

// Debug macro for UART HAL
#define HAL_UART_DEBUG 0
#if HAL_UART_DEBUG
#define HAL_UART_LOG(...) PRINTF(__VA_ARGS__)
#else
#define HAL_UART_LOG(...)
#endif

// Default instance for legacy API
#define HAL_UART_DEFAULT_INSTANCE   HAL_UART_2

// LPUART base addresses
static LPUART_Type* const s_lpuartBases[HAL_UART_COUNT] = {
    LPUART1, LPUART2, LPUART3, LPUART4,
    LPUART5, LPUART6, LPUART7, LPUART8
};

// LPUART clock gate identifiers
static const clock_ip_name_t s_lpuartClocks[HAL_UART_COUNT] = {
    kCLOCK_Lpuart1, kCLOCK_Lpuart2, kCLOCK_Lpuart3, kCLOCK_Lpuart4,
    kCLOCK_Lpuart5, kCLOCK_Lpuart6, kCLOCK_Lpuart7, kCLOCK_Lpuart8
};

// LPUART IRQ numbers
static const IRQn_Type s_lpuartIRQs[HAL_UART_COUNT] = {
    LPUART1_IRQn, LPUART2_IRQn, LPUART3_IRQn, LPUART4_IRQn,
    LPUART5_IRQn, LPUART6_IRQn, LPUART7_IRQn, LPUART8_IRQn
};

// Per-instance initialization state
static bool s_initialized[HAL_UART_COUNT] = {false};

// Get clock frequency for LPUART instance
static uint32_t GetLpuartClockFreq(hal_uart_instance_t instance)
{
    (void)instance;
    return BOARD_DebugConsoleSrcFreq();
}

// ============================================================================
// Initialization API
// ============================================================================

void hal_uart_get_default_config(hal_uart_config_t* config)
{
    if (config == nullptr) return;
    
    config->baudrate = 115200;
    config->dataBits = 8;
    config->stopBits = 1;
    config->parity = 0;
    config->enableRx = true;
    config->enableTx = true;
}

bool hal_uart_init_instance(hal_uart_instance_t instance, const hal_uart_config_t* config)
{
    if (instance >= HAL_UART_COUNT || config == nullptr)
    {
        HAL_UART_LOG("[HAL_UART] Init failed: invalid params (inst=%d)\r\n", instance);
        return false;
    }
    
    LPUART_Type* base = s_lpuartBases[instance];
    HAL_UART_LOG("[HAL_UART] Init LPUART%d @ 0x%08lX, baud=%lu\r\n", 
                 instance + 1, (uint32_t)base, config->baudrate);
    
    if (s_initialized[instance])
    {
        // Already initialized - just update baudrate
        HAL_UART_LOG("[HAL_UART] Already init, updating baudrate\r\n");
        LPUART_SetBaudRate(base, config->baudrate, GetLpuartClockFreq(instance));
        return true;
    }
    
    // Check if LPUART is already initialized (e.g., LPUART1 by debug console)
    if (base->CTRL & LPUART_CTRL_TE_MASK)
    {
        // Already configured, just mark as initialized
        HAL_UART_LOG("[HAL_UART] TX already enabled, marking as init\r\n");
        s_initialized[instance] = true;
        return true;
    }
    
    lpuart_config_t lpuartConfig;
    LPUART_GetDefaultConfig(&lpuartConfig);
    
    lpuartConfig.baudRate_Bps = config->baudrate;
    lpuartConfig.enableTx = config->enableTx;
    lpuartConfig.enableRx = config->enableRx;
    
    // Configure parity
    switch (config->parity)
    {
        case 1: lpuartConfig.parityMode = kLPUART_ParityOdd; break;
        case 2: lpuartConfig.parityMode = kLPUART_ParityEven; break;
        default: lpuartConfig.parityMode = kLPUART_ParityDisabled; break;
    }
    
    // Configure stop bits
    lpuartConfig.stopBitCount = (config->stopBits == 2) ? 
        kLPUART_TwoStopBit : kLPUART_OneStopBit;
    
    // Enable clock gate for LPUART instance (may be disabled in clock_config.c)
    HAL_UART_LOG("[HAL_UART] Enabling clock for LPUART%d\r\n", instance + 1);
    CLOCK_EnableClock(s_lpuartClocks[instance]);
    
    // Get and log clock frequency
    uint32_t clkFreq = GetLpuartClockFreq(instance);
    HAL_UART_LOG("[HAL_UART] Clock freq = %lu Hz\r\n", clkFreq);
    
    // Initialize LPUART
    status_t status = LPUART_Init(base, &lpuartConfig, clkFreq);
    if (status != kStatus_Success)
    {
        HAL_UART_LOG("[HAL_UART] LPUART_Init failed: %d\r\n", status);
        return false;
    }
    
    HAL_UART_LOG("[HAL_UART] LPUART%d init OK, CTRL=0x%08lX\r\n", instance + 1, base->CTRL);
    s_initialized[instance] = true;
    return true;
}

void hal_uart_deinit_instance(hal_uart_instance_t instance)
{
    if (instance >= HAL_UART_COUNT || !s_initialized[instance])
    {
        return;
    }
    
    LPUART_Type* base = s_lpuartBases[instance];
    
    // Disable all LPUART interrupts
    LPUART_DisableInterrupts(base, kLPUART_RxDataRegFullInterruptEnable | 
                                    kLPUART_TxDataRegEmptyInterruptEnable);
    
    LPUART_Deinit(base);
    s_initialized[instance] = false;
}

bool hal_uart_is_initialized_instance(hal_uart_instance_t instance)
{
    if (instance >= HAL_UART_COUNT)
    {
        return false;
    }
    return s_initialized[instance];
}

// ============================================================================
// Data Transfer API
// ============================================================================

void hal_uart_write_byte_instance(hal_uart_instance_t instance, uint8_t byte)
{
    if (instance >= HAL_UART_COUNT || !s_initialized[instance])
    {
        HAL_UART_LOG("[HAL_UART] Write_byte: not initialized (inst=%d, init=%d)\r\n", 
                     instance, s_initialized[instance]);
        return;
    }
    
    LPUART_Type* base = s_lpuartBases[instance];
    
    // Wait for TX buffer ready
    while (!(base->STAT & LPUART_STAT_TDRE_MASK)) {}
    
    base->DATA = byte;
}

void hal_uart_write_instance(hal_uart_instance_t instance, const uint8_t* data, size_t size)
{
    if (data == nullptr) return;
    
    HAL_UART_LOG("[HAL_UART] Write %u bytes to LPUART%d\r\n", size, instance + 1);
    for (size_t i = 0; i < size; i++)
    {
        hal_uart_write_byte_instance(instance, data[i]);
    }
}

bool hal_uart_read_byte_instance(hal_uart_instance_t instance, uint8_t* byte)
{
    if (instance >= HAL_UART_COUNT || byte == nullptr)
    {
        return false;
    }
    
    LPUART_Type* base = s_lpuartBases[instance];
    
    if (base->STAT & LPUART_STAT_RDRF_MASK)
    {
        *byte = (uint8_t)(base->DATA);
        return true;
    }
    
    return false;
}

void hal_uart_flush_tx_instance(hal_uart_instance_t instance)
{
    if (instance >= HAL_UART_COUNT || !s_initialized[instance])
    {
        return;
    }
    
    LPUART_Type* base = s_lpuartBases[instance];
    
    // Wait for TX complete
    while (!(base->STAT & LPUART_STAT_TC_MASK)) {}
}

// ============================================================================
// Status & IRQ API
// ============================================================================

uint32_t hal_uart_get_status(hal_uart_instance_t instance)
{
    if (instance >= HAL_UART_COUNT)
    {
        return 0;
    }
    
    LPUART_Type* base = s_lpuartBases[instance];
    uint32_t hwStatus = base->STAT;
    uint32_t result = 0;
    
    if (hwStatus & LPUART_STAT_RDRF_MASK)  result |= HAL_UART_STATUS_RX_READY;
    if (hwStatus & LPUART_STAT_TDRE_MASK)  result |= HAL_UART_STATUS_TX_READY;
    if (hwStatus & LPUART_STAT_TC_MASK)    result |= HAL_UART_STATUS_TX_COMPLETE;
    if (hwStatus & LPUART_STAT_OR_MASK)    result |= HAL_UART_STATUS_RX_OVERRUN;
    if (hwStatus & LPUART_STAT_FE_MASK)    result |= HAL_UART_STATUS_FRAMING_ERR;
    if (hwStatus & LPUART_STAT_PF_MASK)    result |= HAL_UART_STATUS_PARITY_ERR;
    if (hwStatus & LPUART_STAT_NF_MASK)    result |= HAL_UART_STATUS_NOISE_ERR;
    
    return result;
}

void hal_uart_clear_errors(hal_uart_instance_t instance)
{
    if (instance >= HAL_UART_COUNT)
    {
        return;
    }
    
    LPUART_Type* base = s_lpuartBases[instance];
    
    LPUART_ClearStatusFlags(base, kLPUART_RxOverrunFlag | 
                                   kLPUART_FramingErrorFlag | 
                                   kLPUART_ParityErrorFlag |
                                   kLPUART_NoiseErrorFlag);
    __DSB();
}

uint32_t hal_uart_get_irq_number(hal_uart_instance_t instance)
{
    if (instance >= HAL_UART_COUNT)
    {
        return 0;
    }
    return (uint32_t)s_lpuartIRQs[instance];
}

void hal_uart_enable_rx_irq(hal_uart_instance_t instance)
{
    if (instance >= HAL_UART_COUNT || !s_initialized[instance])
    {
        return;
    }
    LPUART_EnableInterrupts(s_lpuartBases[instance], kLPUART_RxDataRegFullInterruptEnable);
}

void hal_uart_disable_rx_irq(hal_uart_instance_t instance)
{
    if (instance >= HAL_UART_COUNT)
    {
        return;
    }
    LPUART_DisableInterrupts(s_lpuartBases[instance], kLPUART_RxDataRegFullInterruptEnable);
}

void hal_uart_enable_tx_irq(hal_uart_instance_t instance)
{
    if (instance >= HAL_UART_COUNT || !s_initialized[instance])
    {
        return;
    }
    LPUART_EnableInterrupts(s_lpuartBases[instance], kLPUART_TxDataRegEmptyInterruptEnable);
}

void hal_uart_disable_tx_irq(hal_uart_instance_t instance)
{
    if (instance >= HAL_UART_COUNT)
    {
        return;
    }
    LPUART_DisableInterrupts(s_lpuartBases[instance], kLPUART_TxDataRegEmptyInterruptEnable);
}

// ============================================================================
// Legacy API - uses HAL_UART_2
// ============================================================================

void hal_uart_init(uint32_t baudrate)
{
    hal_uart_config_t config;
    hal_uart_get_default_config(&config);
    config.baudrate = baudrate;
    hal_uart_init_instance(HAL_UART_DEFAULT_INSTANCE, &config);
}

void hal_uart_deinit(void)
{
    hal_uart_deinit_instance(HAL_UART_DEFAULT_INSTANCE);
}

void hal_uart_write_byte(uint8_t byte)
{
    hal_uart_write_byte_instance(HAL_UART_DEFAULT_INSTANCE, byte);
}

void hal_uart_write(const uint8_t* data, size_t size)
{
    hal_uart_write_instance(HAL_UART_DEFAULT_INSTANCE, data, size);
}

bool hal_uart_read_byte(uint8_t* byte)
{
    return hal_uart_read_byte_instance(HAL_UART_DEFAULT_INSTANCE, byte);
}

bool hal_uart_is_initialized(void)
{
    return hal_uart_is_initialized_instance(HAL_UART_DEFAULT_INSTANCE);
}

void hal_uart_flush_tx(void)
{
    hal_uart_flush_tx_instance(HAL_UART_DEFAULT_INSTANCE);
}
