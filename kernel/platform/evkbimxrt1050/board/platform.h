/*
 * platform.h - what the generic kernel takes from the board support of the MIMXRT1050-EVKB:
 * the device header (CMSIS core and the i.MX RT1052 registers) and a few constants. The
 * board's functions (platform_*) are declared in kernel/rtos/kernel.h.
 */
#ifndef CRTOS_PLATFORM_H
#define CRTOS_PLATFORM_H

#include "fsl_device_registers.h"

#define PLATFORM_TEST_IRQ       Reserved68_IRQn     /* an NVIC line no device drives (kernel tests) */
#define PLATFORM_POOL_ONCHIP    "ocram"             /* kmalloc pool names (kmon mem) */
#define PLATFORM_POOL_EXT       "sdram"

#endif
