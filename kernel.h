/*
 * kernel.h - Module API
 * Copy from CRTOS/kernel.h for module compilation
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
    COMMAND_GET_TASK_COUNT,
    COMMAND_GET_TASK_INFO,
    COMMAND_GET_HEAP_INFO,
    COMMAND_MODULE_MALLOC,
    COMMAND_MODULE_FREE,
    COMMAND_REGISTER_TASK_IRQ,
    COMMAND_UNREGISTER_TASK_IRQ,
    COMMAND_SEMAPHORE_CREATE,
    COMMAND_SEMAPHORE_WAIT,
    COMMAND_SEMAPHORE_SIGNAL,
    COMMAND_SEMAPHORE_DELETE,
    COMMAND_DPC_REGISTER_HANDLER,
    COMMAND_DPC_UNREGISTER_HANDLER,
    COMMAND_DPC_ENQUEUE_WORK,
    COMMAND_DPC_GET_STATS,
    COMMAND_GET_MEMORY_REGIONS,
    COMMAND_MODULE_FIND,           // Find another module's shared memory by name
    COMMAND_MODULE_ALLOC_SHARED,   // Allocate named shared memory

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

// Get pointer to shared memory (called from module)
ModuleSharedMemory* module_get_shared_memory(void);

// Send message to host (sets flag and increments counter)
void module_send_to_host(uint32_t value);

// Read message from host (clears flag)
uint32_t module_read_from_host(void);

// Check if host has data
uint32_t module_host_has_data(void);

// Module logging - prints to kernel console via SVC
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

// Start a timer
uint32_t module_timer_start(ModuleSoftwareTimer* timer);

// Stop a timer
uint32_t module_timer_stop(ModuleSoftwareTimer* timer);

// Task information structure for monitoring
typedef struct ModuleTaskInfo
{
    char name[24];
    uint32_t priority;
    uint32_t state;          // 0=Ready, 1=Running, 2=Blocked, 3=Suspended, 4=Deleted
    uint32_t stackSize;
    uint32_t stackUsed;
    uint32_t stackFree;
    uint32_t stackPercent;
    uint32_t heapAllocated;  // Heap memory allocated by this task
    uint32_t runtimeCycles;  // Total CPU cycles consumed by this task
    uint32_t registeredIRQ;  // IRQ number this task is registered for (0xFFFFFFFF if none)
} ModuleTaskInfo;

// Heap information structure
typedef struct ModuleHeapInfo
{
    uint32_t totalSize;
    uint32_t freeMemory;
    uint32_t allocatedMemory;
    uint32_t utilizationPercent;
} ModuleHeapInfo;

// Memory region information structure (for per-region display)
#define MAX_MEMORY_REGIONS 8
#define MEMORY_REGION_NAME_LEN 16

typedef struct ModuleMemoryRegion
{
    char name[MEMORY_REGION_NAME_LEN];  // Region name (e.g., "KERNEL", "SDRAM")
    uint32_t baseAddress;               // Start address of region
    uint32_t totalSize;                 // Total size in bytes
    uint32_t freeMemory;                // Available memory in bytes
    uint32_t allocatedMemory;           // Used memory in bytes
    uint32_t flags;                     // Memory flags (DMA capable, cached, etc.)
    uint8_t initialized;                // 1 if region is active
    uint8_t padding[3];                 // Alignment padding
} ModuleMemoryRegion;

// Get total number of tasks in system
uint32_t module_get_task_count(void);

// Get information about a specific task by index
// Returns 0 on success, -1 if index out of range
int32_t module_get_task_info(uint32_t task_index, ModuleTaskInfo* info);

// Get heap information
void module_get_heap_info(ModuleHeapInfo* info);

// Get memory region information
// Returns number of regions filled in the array
uint32_t module_get_memory_regions(ModuleMemoryRegion* regions, uint32_t maxRegions);

// Module memory allocation (uses kernel heap)
void* module_malloc(uint32_t size);

// Module memory deallocation
void module_free(void* ptr);

// ============================================================
// Privilege Mode Operations
// ============================================================

/**
 * @brief Drop privileges to user mode (one-way operation)
 * 
 * This is a security feature - once privileges are dropped, they cannot be
 * regained. Use this for sandboxing untrusted code.
 * 
 * @return 0 on success
 */
int32_t module_drop_privileges(void);

/**
 * @brief Check if module is running in privileged mode
 * 
 * @return 1 if privileged, 0 if user mode
 */
int32_t module_is_privileged(void);

// Register current task for an IRQ (for display in htop)
void RegisterCurrentTaskIRQ(uint32_t irqNumber);

// Unregister current task from IRQ
void UnregisterCurrentTaskIRQ(void);

// Semaphore operations (opaque handle)
typedef void* SemaphoreHandle;

// Create a binary semaphore (returns handle or NULL on failure)
SemaphoreHandle module_semaphore_create(void);

// Wait on semaphore (timeout in ticks, 0xFFFFFFFF = infinite)
// Returns 0 on success, -1 on timeout
int32_t module_semaphore_wait(SemaphoreHandle sem, uint32_t timeout);

// Signal a semaphore
void module_semaphore_signal(SemaphoreHandle sem);

// Delete a semaphore
void module_semaphore_delete(SemaphoreHandle sem);

// ============================================================
// Futex (Fast Userspace Mutex) Operations
// ============================================================
// Futex provides efficient synchronization primitives that avoid
// kernel transitions in the uncontended case.

// Futex operation codes
#define FUTEX_WAIT          0   // Wait if *uaddr == val
#define FUTEX_WAKE          1   // Wake up to val waiters
#define FUTEX_WAIT_BITSET   9   // Wait with bitmask matching
#define FUTEX_WAKE_BITSET   10  // Wake with bitmask matching
#define FUTEX_REQUEUE       3   // Requeue waiters to another futex

/**
 * @brief Wait on futex if value matches
 * 
 * If *uaddr == expected_val, the calling task is suspended until:
 * - Another task calls module_futex_wake() on the same address
 * - The timeout expires
 * - A spurious wakeup occurs (caller should recheck condition)
 * 
 * @param uaddr     Pointer to the futex variable (must be word-aligned)
 * @param expected_val Expected value - only wait if *uaddr == expected_val
 * @param timeout   Timeout in system ticks (0xFFFFFFFF = infinite wait)
 * @return 0 on success (woken by wake), -ETIMEDOUT on timeout, -EAGAIN if value mismatch
 */
int32_t module_futex_wait(volatile uint32_t* uaddr, uint32_t expected_val, uint32_t timeout);

/**
 * @brief Wake tasks waiting on a futex
 * 
 * Wakes up to num_wake tasks that are waiting on the futex at uaddr.
 * 
 * @param uaddr     Pointer to the futex variable
 * @param num_wake  Maximum number of waiters to wake (1 for mutex, 0x7FFFFFFF for broadcast)
 * @return Number of tasks actually woken (>= 0)
 */
int32_t module_futex_wake(volatile uint32_t* uaddr, uint32_t num_wake);

/**
 * @brief Requeue waiters from one futex to another
 * 
 * Atomically wakes num_wake waiters and moves num_requeue waiters
 * to a different futex address. Used for efficient condition variable implementation.
 * 
 * @param uaddr       Source futex address
 * @param uaddr2      Destination futex address
 * @param num_wake    Number of waiters to wake
 * @param num_requeue Number of waiters to move to uaddr2
 * @return Number of affected waiters (woken + requeued)
 */
int32_t module_futex_requeue(volatile uint32_t* uaddr, volatile uint32_t* uaddr2,
                              uint32_t num_wake, uint32_t num_requeue);

// DPC operations
typedef void (*DPCCallback)(void* context);

// Register DPC handler for an IRQ
// sem: semaphore to signal when interrupt fires
// callback: optional callback to run in ISR context (can be NULL)
// context: context pointer passed to callback
// Returns 0 on success, -1 on failure
int32_t module_dpc_register_handler(uint32_t irqNumber, SemaphoreHandle sem, DPCCallback callback, void* context);

// Unregister DPC handler
void module_dpc_unregister_handler(uint32_t irqNumber, SemaphoreHandle sem);

#if defined(__cplusplus)
}
#endif

#endif /* KERNEL_H_ */
