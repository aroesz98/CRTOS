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
        "mov r0, %0         \n"  /* pass ticks in r0 for SVC */
        "dsb                \n"
        "isb                \n"
        "svc %1             \n"
        "nop                \n"
        :
        : "r" (ticks), "i" (COMMAND_TASK_DELAY)
        : "r0", "memory"
    );
}

// Module shared memory pointer (set by host during module initialization)
static ModuleSharedMemory* g_module_shared_mem = 0;

ModuleSharedMemory* module_get_shared_memory(void)
{
    if (g_module_shared_mem == 0)
    {
        // First call - get pointer from host via SVC
        register uint32_t result __asm("r0");
        __asm volatile
        (
            ".syntax unified               \n"
            "svc %1                        \n"
            : "=r" (result)
            : "i" (COMMAND_MODULE_GET_SHARED_MEM)
            : "memory"
        );
        g_module_shared_mem = (ModuleSharedMemory*)result;
    }
    return g_module_shared_mem;
}

void module_send_to_host(uint32_t value)
{
    ModuleSharedMemory* mem = module_get_shared_memory();
    if (mem != 0)
    {
        mem->moduleToHost = value;
        mem->messageCount++;
        mem->flags |= MODULE_FLAG_DATA_READY;
        mem->flags &= ~MODULE_FLAG_HOST_ACK;
    }
}

uint32_t module_read_from_host(void)
{
    ModuleSharedMemory* mem = module_get_shared_memory();
    if (mem != 0)
    {
        uint32_t value = mem->hostToModule;
        mem->flags &= ~MODULE_FLAG_HOST_HAS_DATA;
        mem->flags |= MODULE_FLAG_MODULE_ACK;
        return value;
    }
    return 0;
}

uint32_t module_host_has_data(void)
{
    ModuleSharedMemory* mem = module_get_shared_memory();
    if (mem != 0)
    {
        return (mem->flags & MODULE_FLAG_HOST_HAS_DATA) ? 1 : 0;
    }
    return 0;
}
