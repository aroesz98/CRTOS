/*
 * DPCWorker.hpp - CRTOS Deferred Procedure Call Worker Task
 * Author: Arkadiusz Szlanta
 * Date: 26 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * This module provides a centralized DPC Worker task that processes
 * deferred work items from interrupts. Instead of each interrupt handler
 * directly signaling individual tasks, interrupts add work items to a
 * queue which the DPC Worker processes asynchronously.
 *
 * Usage:
 * 1. Initialize the DPC Worker with DPCWorker::Init()
 * 2. Start the DPC Worker task with DPCWorker::Start()
 * 3. From ISR context, call DPCWorker::EnqueueWork() to add work items
 * 4. The DPC Worker will process them in order
 */

#ifndef DPC_WORKER_HPP
#define DPC_WORKER_HPP

#include <cstdint>
#include "BinarySemaphore.hpp"

namespace CRTOS
{
    // Forward declarations
    enum class Result : uint8_t;

    // Maximum number of pending DPC work items
    #define DPC_WORKER_QUEUE_SIZE           32

    // DPC Worker task priority (should be high to process interrupts quickly)
    #define DPC_WORKER_TASK_PRIORITY        9

    // DPC Worker stack size in words
    #define DPC_WORKER_STACK_SIZE           512

    // DPC Work item types
    enum class DPCWorkType : uint8_t
    {
        DPC_WORK_CALLBACK,       // Execute a callback function
        DPC_WORK_SIGNAL_SEM,     // Signal a semaphore
        DPC_WORK_CUSTOM          // Custom work item with handler
    };

    // Work handler callback type
    // This runs in the DPC Worker task context (not ISR)
    typedef void (*DPCWorkHandler)(uint32_t irqNumber, void* context, uint32_t param);

    // Work item structure
    struct DPCWorkItem
    {
        DPCWorkType type;            // Type of work item
        uint32_t irqNumber;          // Source IRQ number
        DPCWorkHandler handler;      // Handler function (for CALLBACK type)
        void* context;               // Context pointer for handler
        uint32_t param;              // Additional parameter
        BinarySemaphore* semaphore;  // Semaphore to signal (for SIGNAL_SEM type)
        uint32_t timestamp;          // Timestamp when enqueued (in system ticks or cycles)
        bool valid;                  // Whether this entry is valid
    };

    // DPC Worker statistics
    struct DPCWorkerStats
    {
        uint32_t totalItemsProcessed;     // Total work items processed
        uint32_t currentQueueDepth;       // Current items in queue
        uint32_t maxQueueDepth;           // Maximum queue depth reached
        uint32_t queueOverflows;          // Number of times queue was full
        uint32_t lastProcessingTime;      // Last item processing time in cycles
        uint32_t avgProcessingTime;       // Average processing time in cycles
        uint32_t totalInterrupts;         // Total interrupts that added work
    };

    class DPCWorker
    {
    public:
        DPCWorker();
        ~DPCWorker() = default;

        // Initialize the DPC Worker (allocates resources)
        Result Init();

        // Start the DPC Worker task
        Result Start();

        // Stop the DPC Worker task
        Result Stop();

        // Enqueue a work item from ISR context
        // This is ISR-safe and will not block
        // Parameters:
        //   irqNumber - Source IRQ number
        //   handler   - Function to call in DPC Worker context
        //   context   - Context pointer passed to handler
        //   param     - Additional parameter
        // Returns: RESULT_SUCCESS or RESULT_QUEUE_FULL
        Result EnqueueWork(uint32_t irqNumber, 
                          DPCWorkHandler handler, 
                          void* context = nullptr,
                          uint32_t param = 0);

        // Enqueue a semaphore signal from ISR context
        // Parameters:
        //   irqNumber - Source IRQ number
        //   semaphore - Semaphore to signal
        // Returns: RESULT_SUCCESS or RESULT_QUEUE_FULL
        Result EnqueueSignal(uint32_t irqNumber, 
                            BinarySemaphore* semaphore);

        // Get current statistics
        void GetStats(DPCWorkerStats& stats) const;

        // Reset statistics
        void ResetStats();

        // Check if DPC Worker is running
        bool IsRunning() const { return _running; }

        // Get current queue depth
        uint32_t GetQueueDepth() const { return _itemCount; }

    private:
        // Circular queue of work items
        DPCWorkItem _queue[DPC_WORKER_QUEUE_SIZE];
        volatile uint32_t _head;         // Write position (producer - ISR)
        volatile uint32_t _tail;         // Read position (consumer - Worker task)
        volatile uint32_t _itemCount;    // Number of items in queue

        // Semaphore to wake up worker when items are available
        BinarySemaphore* _workAvailable;

        // Worker task handle
        void* _taskHandle;

        // State flags
        volatile bool _initialized;
        volatile bool _running;
        volatile bool _stopRequested;

        // Statistics
        DPCWorkerStats _stats;

        // Worker task function (static to be used as task entry)
        static void WorkerTaskEntry(void* arg);

        // Internal work processing
        void ProcessWorkItem(const DPCWorkItem& item);
        void ProcessQueue();
    };

    // Global DPC Worker instance
    extern DPCWorker GlobalDPCWorker;

    // Convenience functions for ISR use (wraps GlobalDPCWorker)
    namespace DPC
    {
        // Initialize and start the global DPC Worker
        Result Init();
        Result Start();
        Result Stop();

        // Enqueue work from ISR
        Result EnqueueWork(uint32_t irqNumber, 
                          DPCWorkHandler handler, 
                          void* context = nullptr,
                          uint32_t param = 0);

        // Enqueue semaphore signal from ISR
        Result EnqueueSignal(uint32_t irqNumber, 
                            BinarySemaphore* semaphore);

        // Get statistics
        void GetStats(DPCWorkerStats& stats);
    }
}

#endif /* DPC_WORKER_HPP */
