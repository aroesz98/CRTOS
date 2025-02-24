/*
 * kernel.h
 *
 *  Created on: 7 lut 2025
 *      Author: Administrator
 */

#ifndef KERNEL_H_
#define KERNEL_H_

#include "stdint.h"

#if defined(__cplusplus)
extern "C" {
#endif

enum SVC_Commands
{
    COMMAND_START_SCHEDULER = 0u,
    COMMAND_TASK_DELAY,
    COMMAND_TASK_SUSPEND,
    COMMAND_TASK_RESUME,

    COMMAND_UNKNOWN = 0xFFFFFFFFu
};

void delay(uint32_t ticks) __attribute__ ((naked));

#if defined(__cplusplus)
}
#endif

#endif /* KERNEL_H_ */
