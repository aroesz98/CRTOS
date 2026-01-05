/*
 * CRTOS
 * Author: Arkadiusz Szlanta
 * Date: 17 Dec 2024
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 */

#ifndef RTOS_HPP
#define RTOS_HPP

#include <cstdint>
#include <atomic>

#include "Task.hpp"
#include "Mutex.hpp"
#include "BinarySemaphore.hpp"
#include "Futex.hpp"
#include "Timer.hpp"
#include "Queue.hpp"
#include "CircularBuffer.hpp"
#include "InterruptDPC.hpp"
#include "DPCWorker.hpp"
#include "Syscall/SystemCall.hpp"

template <typename T>
class Node;

namespace CRTOS
{
    enum class Result : uint8_t
    {
        RESULT_SUCCESS = 0,
        RESULT_BAD_PARAMETER,
        RESULT_NO_MEMORY,
        RESULT_MEMORY_NOT_INITIALIZED,
        RESULT_SEMAPHORE_BUSY,
        RESULT_SEMAPHORE_TIMEOUT,
        RESULT_SEMAPHORE_NO_OWNER,
        RESULT_TIMER_ALREADY_ACTIVE,
        RESULT_TIMER_ALREADY_STOPPED,
        RESULT_QUEUE_TIMEOUT,
        RESULT_QUEUE_FULL,
        RESULT_QUEUE_EMPTY,
        RESULT_CIRCULAR_BUFFER_TIMEOUT,
        RESULT_CIRCULAR_BUFFER_FULL,
		RESULT_CIRCULAR_BUFFER_EMPTY,
        RESULT_TASK_NOT_FOUND,
        RESULT_IPC_TIMEOUT,
        RESULT_IPC_EMPTY,
        RESULT_CRC_NOT_INITIALIZED,
        RESULT_CRC_ALREADY_INITIALIZED,
        RESULT_NOT_FOUND,
        RESULT_DENIED,
        RESULT_IO_ERROR
    };

    struct HeapInfo
    {
        uint32_t totalSize;          // Total heap size in bytes
        uint32_t freeMemory;          // Available free memory in bytes
        uint32_t allocatedMemory;     // Currently allocated memory in bytes
        uint32_t utilizationPercent;  // Heap utilization percentage (0-100)
    };

    struct TaskStackInfo
    {
        char name[20];               // Task name
        uint32_t stackSize;          // Total stack size in bytes
        uint32_t stackUsed;          // Used stack in bytes
        uint32_t stackFree;          // Free stack in bytes
        uint32_t utilizationPercent; // Stack utilization percentage (0-100)
        uint32_t heapAllocated;      // Heap memory allocated by this task in bytes
        void* taskHandle;            // Task handle
    };

    enum class ModuleState : uint8_t
    {
        MODULE_RUNNING = 0,
        MODULE_PAUSED,
        MODULE_STOPPED,
        MODULE_FAILED
    };

    struct ModuleInfo
    {
        char name[20];               // Module/Task name
        uint32_t baseAddress;        // Module base address in memory (start of binary image)
        uint32_t entryPoint;         // Entry point address (with Thumb bit)
        
        // Section addresses and sizes
        uint32_t textAddr;           // .text section address (code)
        uint32_t textSize;           // .text section size
        uint32_t dataAddr;           // .data section address (initialized data in RAM)
        uint32_t dataSize;           // .data section size
        uint32_t bssAddr;            // .bss section address (uninitialized data in RAM)
        uint32_t bssSize;            // .bss section size
        uint32_t stackAddr;          // Stack base address
        uint32_t stackSize;          // Stack size
        
        uint32_t totalSize;          // Total module size (binary + RAM)
        void* taskHandle;            // Associated task handle
        ModuleState state;           // Current module state
        uint32_t loadTime;           // Time when module was loaded (system ticks)
        void* sharedMemory;          // Pointer to shared memory region
    };

    namespace Config
    {
        void SetCoreClock(uint32_t ClockInMHz);
        void SetTickRate(uint32_t TicksPerSecond);
        void SetTimeSlice(uint32_t ticks);  // Set time slice quantum (in ticks)
        void* Allocate(uint32_t size);
        void Deallocate(void *ptr);
        uint32_t GetFreeMemory(void);
        uint32_t GetAllocatedMemory(void);
        uint32_t GetTotalHeapSize(void);
        void GetHeapInfo(HeapInfo &info);
        void DefragmentHeap(void);
    }

    namespace Scheduler
    {
        Result Start(void);
    };

    // Task IRQ registration - allows current task to associate itself with IRQ numbers
    // Multiple IRQs can be registered per task (stored as linked list)
    void RegisterCurrentTaskIRQ(uint32_t irqNumber);
    void UnregisterCurrentTaskIRQ(void);  // Unregister all IRQs for current task
    void UnregisterCurrentTaskIRQ(uint32_t irqNumber);  // Unregister specific IRQ
    
    // Task memory region tracking - for proper cleanup on task delete
    // These track module memory allocations (binary, shared mem, etc.)
    void TrackTaskMemory(void* address, uint32_t size);
    void UntrackTaskMemory(void* address);
    
    // Versions with explicit TCB pointer and static allocator flag (for module_malloc)
    void TrackTaskMemory(const volatile void* tcb, uint32_t address, uint32_t size, bool useStaticAllocator = true);
    void UntrackTaskMemory(const volatile void* tcb, uint32_t address);

    namespace CRC32
    {
        namespace
        {
            static const uint32_t sCrcTableSize = 256u;
            __attribute__((used)) static uint32_t *sCrcTable = nullptr;
            static const uint32_t sPolynomial = 0xEDB88320u;
        }

        CRTOS::Result Init(void);
        CRTOS::Result Calculate(const uint8_t* data, uint32_t length, uint32_t &output, uint32_t previousCrc = 0xFFFFFFFFu);
        CRTOS::Result Deinit(void);
    }
};

#endif /* RTOS_HPP */
