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
    COMMAND_MODULE_GET_SHARED_MEM,
    COMMAND_MODULE_SEND_MESSAGE,
    COMMAND_MODULE_LOG,
    COMMAND_TIMER_INIT,
    COMMAND_TIMER_START,
    COMMAND_TIMER_STOP,

    COMMAND_UNKNOWN = 0xFFFFFFFFu
};

// Shared memory structure between module and host
// Maximum 256 bytes to keep it reasonable
#define MODULE_SHARED_DATA_SIZE 240

typedef struct ModuleSharedMemory
{
    volatile uint32_t flags;              // Status/control flags
    volatile uint32_t moduleToHost;       // Data from module to host
    volatile uint32_t hostToModule;       // Data from host to module  
    volatile uint32_t messageCount;       // Number of messages exchanged
    volatile uint8_t  data[MODULE_SHARED_DATA_SIZE];  // Generic data buffer
} ModuleSharedMemory;

// Flag definitions for ModuleSharedMemory.flags
#define MODULE_FLAG_DATA_READY      (1 << 0)  // Module has new data
#define MODULE_FLAG_HOST_ACK        (1 << 1)  // Host acknowledged data
#define MODULE_FLAG_HOST_HAS_DATA   (1 << 2)  // Host has data for module
#define MODULE_FLAG_MODULE_ACK      (1 << 3)  // Module acknowledged host data

void delay(uint32_t ticks) __attribute__ ((naked));

// Get pointer to shared memory (called from module)
ModuleSharedMemory* module_get_shared_memory(void);

// Send message to host (sets flag and increments counter)
void module_send_to_host(uint32_t value);

// Read message from host (clears flag)
uint32_t module_read_from_host(void);

// Check if host has data
uint32_t module_host_has_data(void);

// Module logging (called from module code)
void module_log(const char* message);

// Software timer structure - must match CRTOS::Timer::SoftwareTimer
typedef struct ModuleSoftwareTimer
{
    uint32_t timeoutTicks;
    uint32_t elapsedTicks;
    uint32_t isActive;  // Using uint32_t instead of bool for C compatibility
    void (*callback)(void*);
    void *callbackArgs;
    uint32_t autoReload;  // Using uint32_t instead of bool for C compatibility
} ModuleSoftwareTimer;

// Initialize a software timer (returns 0 on success)
uint32_t module_timer_init(ModuleSoftwareTimer* timer, uint32_t timeoutTicks, void (*callback)(void*), void* callbackArgs, uint32_t autoReload);

// Start a software timer
void module_timer_start(ModuleSoftwareTimer* timer);

// Stop a software timer
void module_timer_stop(ModuleSoftwareTimer* timer);

#if defined(__cplusplus)
}
#endif

#endif /* KERNEL_H_ */
