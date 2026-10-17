/*
 * stm32h7rsxx_hal_conf.h - the STM32CubeH7RS HAL modules CRTOS uses on the STM32H7S78-DK.
 *
 * The boot loader (boot/) uses the HAL for the power supply, the clock tree and the two XSPI
 * memories; driver modules may use single HAL/LL drivers (crtos_module SDK_DRIVERS). The kernel
 * itself takes only the CMSIS device header. HAL_GetTick() and HAL_Delay() come from the user
 * of the HAL (the boot loader: the cycle counter; a module defines its own).
 */
#ifndef STM32H7RSXX_HAL_CONF_H
#define STM32H7RSXX_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_XSPI_MODULE_ENABLED

/* the board's oscillators */
#define HSE_VALUE            24000000UL     /* X3: 24 MHz oscillator (bypass) */
#define HSE_STARTUP_TIMEOUT  100UL
#define HSI_VALUE            64000000UL
#define CSI_VALUE            4000000UL
#define HSI48_VALUE          48000000UL
#define LSI_VALUE            32000UL
#define LSE_VALUE            32768UL
#define LSE_STARTUP_TIMEOUT  5000UL
#define EXTERNAL_CLOCK_VALUE 12288000UL     /* I2S_CKIN */

#define VDD_VALUE            3300UL
#define TICK_INT_PRIORITY    ((1UL << __NVIC_PRIO_BITS) - 1UL)
#define USE_RTOS             0U

#define USE_HAL_XSPI_REGISTER_CALLBACKS 0U
#define USE_SPI_CRC                     0U
#define USE_SD_TRANSCEIVER              0U

#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32h7rsxx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32h7rsxx_hal_gpio.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32h7rsxx_hal_dma.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32h7rsxx_hal_cortex.h"
#endif
#ifdef HAL_EXTI_MODULE_ENABLED
#include "stm32h7rsxx_hal_exti.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32h7rsxx_hal_flash.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32h7rsxx_hal_pwr.h"
#endif
#ifdef HAL_XSPI_MODULE_ENABLED
#include "stm32h7rsxx_hal_xspi.h"
#endif

#define assert_param(expr) ((void)0U)

#ifdef __cplusplus
}
#endif

#endif
