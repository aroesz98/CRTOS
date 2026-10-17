/*
 * platform.h - what the generic kernel takes from the board support of the STM32H7S78-DK: the
 * device header (CMSIS core and the STM32H7S7 registers) and a few constants. The board's
 * functions (platform_*) are declared in kernel/rtos/kernel.h.
 */
#ifndef CRTOS_PLATFORM_H
#define CRTOS_PLATFORM_H

#include "stm32h7rsxx.h"

#define PLATFORM_TEST_IRQ       ((IRQn_Type)111)    /* an NVIC line no device drives (kernel tests) */
#define PLATFORM_POOL_ONCHIP    "axisram"           /* kmalloc pool names (kmon mem) */
#define PLATFORM_POOL_EXT       "psram"

#endif
