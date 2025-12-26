/*
 * kernel.c
 *
 *  Created on: 7 lut 2025
 *      Author: Administrator
 */

#include <stddef.h>
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
// Initialize to NULL explicitly to ensure it's in .bss
static ModuleSharedMemory* g_module_shared_mem = (void*)0;

ModuleSharedMemory* module_get_shared_memory(void)
{
    if (g_module_shared_mem == NULL || g_module_shared_mem == (ModuleSharedMemory*)0xDEADBEEF)
    {
        // First call or invalid value - get pointer from host via SVC
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

void module_log(const char* message)
{
    if (message != 0)
    {
        __asm volatile
        (
            ".syntax unified               \n"
            "mov r0, %0                    \n"  /* pass message pointer in r0 */
            "svc %1                        \n"
            :
            : "r" (message), "i" (COMMAND_MODULE_LOG)
            : "r0", "memory"
        );
    }
}

uint32_t module_timer_init(ModuleSoftwareTimer* timer, uint32_t timeoutTicks, void (*callback)(void*), void* callbackArgs, uint32_t autoReload)
{
    if (timer == 0 || callback == 0)
    {
        return 1; // Error
    }
    
    // Store parameters in timer structure - SVC handler will read them from there
    timer->timeoutTicks = timeoutTicks;
    timer->callback = callback;
    timer->callbackArgs = callbackArgs;
    timer->autoReload = autoReload;
    timer->elapsedTicks = 0;
    timer->isActive = 0;
    
    register uint32_t result __asm("r0");
    __asm volatile
    (
        ".syntax unified               \n"
        "mov r0, %1                    \n"  /* timer pointer - SVC handler reads config from it */
        "svc %2                        \n"
        : "=r" (result)
        : "r" (timer), "i" (COMMAND_TIMER_INIT)
        : "memory"
    );
    return result;
}

uint32_t module_timer_start(ModuleSoftwareTimer* timer)
{
    register uint32_t result __asm("r0");
    __asm volatile
    (
        ".syntax unified               \n"
        "mov r0, %1                    \n"
        "svc %2                        \n"
        : "=r" (result)
        : "r" (timer), "i" (COMMAND_TIMER_START)
        : "memory"
    );
    return result;
}

uint32_t module_timer_stop(ModuleSoftwareTimer* timer)
{
    register uint32_t result __asm("r0");
    __asm volatile
    (
        ".syntax unified               \n"
        "mov r0, %1                    \n"
        "svc %2                        \n"
        : "=r" (result)
        : "r" (timer), "i" (COMMAND_TIMER_STOP)
        : "memory"
    );
    return result;
}

uint32_t module_get_task_count(void)
{
    register uint32_t result __asm("r0");
    __asm volatile
    (
        ".syntax unified               \n"
        "svc %1                        \n"
        : "=r" (result)
        : "i" (COMMAND_GET_TASK_COUNT)
        : "memory"
    );
    return result;
}

int32_t module_get_task_info(uint32_t task_index, ModuleTaskInfo* info)
{
    if (info == 0)
    {
        return -1;
    }
    
    register int32_t result __asm("r0");
    __asm volatile
    (
        ".syntax unified               \n"
        "mov r0, %1                    \n"  /* task index */
        "mov r1, %2                    \n"  /* info pointer */
        "svc %3                        \n"
        : "=r" (result)
        : "r" (task_index), "r" (info), "i" (COMMAND_GET_TASK_INFO)
        : "r1", "memory"
    );
    return result;
}

void module_get_heap_info(ModuleHeapInfo* info)
{
    if (info == 0)
    {
        return;
    }
    
    __asm volatile
    (
        ".syntax unified               \n"
        "mov r0, %0                    \n"  /* info pointer */
        "svc %1                        \n"
        :
        : "r" (info), "i" (COMMAND_GET_HEAP_INFO)
        : "r0", "memory"
    );
}

void* module_malloc(uint32_t size)
{
    register void* result __asm("r0");
    __asm volatile
    (
        ".syntax unified               \n"
        "mov r0, %1                    \n"  /* size */
        "svc %2                        \n"
        : "=r" (result)
        : "r" (size), "i" (COMMAND_MODULE_MALLOC)
        : "memory"
    );
    return result;
}

void module_free(void* ptr)
{
    if (ptr == 0)
    {
        return;
    }
    
    __asm volatile
    (
        ".syntax unified               \n"
        "mov r0, %0                    \n"  /* pointer */
        "svc %1                        \n"
        :
        : "r" (ptr), "i" (COMMAND_MODULE_FREE)
        : "r0", "memory"
    );
}

// IRQ registration functions - use syscalls to register with kernel
void RegisterCurrentTaskIRQ(uint32_t irqNumber)
{
    __asm volatile
    (
        ".syntax unified               \n"
        "mov r0, %0                    \n"  /* IRQ number */
        "svc %1                        \n"
        :
        : "r" (irqNumber), "i" (COMMAND_REGISTER_TASK_IRQ)
        : "r0", "memory"
    );
}

void UnregisterCurrentTaskIRQ(void)
{
    __asm volatile
    (
        ".syntax unified               \n"
        "svc %0                        \n"
        :
        : "i" (COMMAND_UNREGISTER_TASK_IRQ)
        : "memory"
    );
}

// Semaphore operations
SemaphoreHandle module_semaphore_create(void)
{
    register uint32_t result __asm("r0");
    __asm volatile
    (
        ".syntax unified               \n"
        "svc %1                        \n"
        : "=r" (result)
        : "i" (COMMAND_SEMAPHORE_CREATE)
        : "memory"
    );
    return (SemaphoreHandle)result;
}

int32_t module_semaphore_wait(SemaphoreHandle sem, uint32_t timeout)
{
    register uint32_t result __asm("r0");
    __asm volatile
    (
        ".syntax unified               \n"
        "mov r0, %1                    \n"  /* semaphore handle */
        "mov r1, %2                    \n"  /* timeout */
        "svc %3                        \n"
        : "=r" (result)
        : "r" (sem), "r" (timeout), "i" (COMMAND_SEMAPHORE_WAIT)
        : "r1", "memory"
    );
    return (int32_t)result;
}

void module_semaphore_signal(SemaphoreHandle sem)
{
    __asm volatile
    (
        ".syntax unified               \n"
        "mov r0, %0                    \n"  /* semaphore handle */
        "svc %1                        \n"
        :
        : "r" (sem), "i" (COMMAND_SEMAPHORE_SIGNAL)
        : "r0", "memory"
    );
}

void module_semaphore_delete(SemaphoreHandle sem)
{
    __asm volatile
    (
        ".syntax unified               \n"
        "mov r0, %0                    \n"  /* semaphore handle */
        "svc %1                        \n"
        :
        : "r" (sem), "i" (COMMAND_SEMAPHORE_DELETE)
        : "r0", "memory"
    );
}

// DPC operations
int32_t module_dpc_register_handler(uint32_t irqNumber, SemaphoreHandle sem, DPCCallback callback, void* context)
{
    register uint32_t result __asm("r0");
    __asm volatile
    (
        ".syntax unified               \n"
        "mov r0, %1                    \n"  /* IRQ number */
        "mov r1, %2                    \n"  /* semaphore handle */
        "mov r2, %3                    \n"  /* callback */
        "mov r3, %4                    \n"  /* context */
        "svc %5                        \n"
        : "=r" (result)
        : "r" (irqNumber), "r" (sem), "r" (callback), "r" (context), "i" (COMMAND_DPC_REGISTER_HANDLER)
        : "memory"
    );
    return (int32_t)result;
}

void module_dpc_unregister_handler(uint32_t irqNumber, SemaphoreHandle sem)
{
    __asm volatile
    (
        ".syntax unified               \n"
        "mov r0, %0                    \n"  /* IRQ number */
        "mov r1, %1                    \n"  /* semaphore handle */
        "svc %2                        \n"
        :
        : "r" (irqNumber), "r" (sem), "i" (COMMAND_DPC_UNREGISTER_HANDLER)
        : "r0", "r1", "memory"
    );
}
