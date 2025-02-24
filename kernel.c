/*
 * kernel.c
 *
 *  Created on: 7 lut 2025
 *      Author: Administrator
 */

#include "kernel.h"

void delay(uint32_t ticks)
{
    __asm volatile
    (
        ".syntax unified    \n"
        "svc %0             \n"
        :: "i" (COMMAND_TASK_DELAY) : "memory"
    );
}
