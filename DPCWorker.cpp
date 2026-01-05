/*
 * DPCWorker.cpp - CRTOS Deferred Procedure Call Worker Task Implementation
 * Author: Arkadiusz Szlanta
 * Date: 26 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 */

#include "DPCWorker.hpp"
#include "CRTOS.hpp"
#include <cstring>
#include <cstdio>  // For PRINTF debug

// External functions for interrupt control and timing
extern "C" uint32_t getInterruptMask(void);
extern "C" void setInterruptMask(uint32_t mask);
extern "C" uint32_t GetSystemTicks(void);

// Cycle counter for timing (DWT->CYCCNT)
#define DWT_CYCCNT (*((volatile uint32_t*)0xE0001004))

namespace CRTOS
{
    // Global DPC Worker instance
    DPCWorker GlobalDPCWorker;

    DPCWorker::DPCWorker()
        : _head(0)
        , _tail(0)
        , _itemCount(0)
        , _workAvailable(nullptr)
        , _taskHandle(nullptr)
        , _initialized(false)
        , _running(false)
        , _stopRequested(false)
    {
        // Clear the queue
        memset(_queue, 0, sizeof(_queue));
        
        // Clear statistics
        memset(&_stats, 0, sizeof(_stats));
    }

    Result DPCWorker::Init()
    {
        if (_initialized)
        {
            return Result::RESULT_SUCCESS; // Already initialized
        }

        // Allocate semaphore for work notification
        _workAvailable = new BinarySemaphore();
        if (_workAvailable == nullptr)
        {
            return Result::RESULT_NO_MEMORY;
        }

        // Reset queue state
        _head = 0;
        _tail = 0;
        _itemCount = 0;

        // Reset statistics
        memset(&_stats, 0, sizeof(_stats));

        _initialized = true;
        _stopRequested = false;

        return Result::RESULT_SUCCESS;
    }

    Result DPCWorker::Start()
    {
        if (!_initialized)
        {
            return Result::RESULT_MEMORY_NOT_INITIALIZED;
        }

        if (_running)
        {
            return Result::RESULT_SUCCESS; // Already running
        }

        _stopRequested = false;

        // Create the worker task
        Result result = Task::Create(
            WorkerTaskEntry,
            "DPCWorker",
            DPC_WORKER_STACK_SIZE,
            this,
            DPC_WORKER_TASK_PRIORITY,
            reinterpret_cast<Task::TaskHandle*>(&_taskHandle)
        );

        if (result == Result::RESULT_SUCCESS)
        {
            _running = true;
        }

        return result;
    }

    Result DPCWorker::Stop()
    {
        if (!_running)
        {
            return Result::RESULT_SUCCESS;
        }

        _stopRequested = true;

        // Signal semaphore to wake up worker so it can check stop flag
        if (_workAvailable != nullptr)
        {
            _workAvailable->signal();
        }

        // Note: The task will delete itself when it sees the stop request
        // We don't wait here to avoid blocking

        return Result::RESULT_SUCCESS;
    }

    Result DPCWorker::EnqueueWork(uint32_t irqNumber, 
                                   DPCWorkHandler handler, 
                                   void* context,
                                   uint32_t param)
    {
        if (!_initialized || handler == nullptr)
        {
            return Result::RESULT_BAD_PARAMETER;
        }

        // Disable interrupts for atomic queue operation
        uint32_t mask = getInterruptMask();

        // Check if queue is full
        if (_itemCount >= DPC_WORKER_QUEUE_SIZE)
        {
            _stats.queueOverflows++;
            setInterruptMask(mask);
            return Result::RESULT_QUEUE_FULL;
        }

        // Add work item to queue
        uint32_t writePos = _head;
        _queue[writePos].type = DPCWorkType::DPC_WORK_CALLBACK;
        _queue[writePos].irqNumber = irqNumber;
        _queue[writePos].handler = handler;
        _queue[writePos].context = context;
        _queue[writePos].param = param;
        _queue[writePos].semaphore = nullptr;
        _queue[writePos].timestamp = DWT_CYCCNT;
        _queue[writePos].valid = true;

        // Update head pointer (circular)
        _head = (writePos + 1) % DPC_WORKER_QUEUE_SIZE;
        _itemCount++;

        // Update statistics
        _stats.totalInterrupts++;
        if (_itemCount > _stats.maxQueueDepth)
        {
            _stats.maxQueueDepth = _itemCount;
        }
        _stats.currentQueueDepth = _itemCount;

        setInterruptMask(mask);

        // Signal worker that work is available
        if (_workAvailable != nullptr)
        {
            _workAvailable->signal();
        }

        return Result::RESULT_SUCCESS;
    }

    Result DPCWorker::EnqueueSignal(uint32_t irqNumber, 
                                     BinarySemaphore* semaphore)
    {
        if (!_initialized || semaphore == nullptr)
        {
            return Result::RESULT_BAD_PARAMETER;
        }

        // Disable interrupts for atomic queue operation
        uint32_t mask = getInterruptMask();

        // Check if queue is full
        if (_itemCount >= DPC_WORKER_QUEUE_SIZE)
        {
            _stats.queueOverflows++;
            setInterruptMask(mask);
            printf("[DPCWorker] Queue OVERFLOW! IRQ %lu\r\n", irqNumber);
            return Result::RESULT_QUEUE_FULL;
        }

        // Add work item to queue
        uint32_t writePos = _head;
        _queue[writePos].type = DPCWorkType::DPC_WORK_SIGNAL_SEM;
        _queue[writePos].irqNumber = irqNumber;
        _queue[writePos].handler = nullptr;
        _queue[writePos].context = nullptr;
        _queue[writePos].param = 0;
        _queue[writePos].semaphore = semaphore;
        _queue[writePos].timestamp = DWT_CYCCNT;
        _queue[writePos].valid = true;

        // Update head pointer (circular)
        _head = (writePos + 1) % DPC_WORKER_QUEUE_SIZE;
        _itemCount++;

        // Update statistics
        _stats.totalInterrupts++;
        if (_itemCount > _stats.maxQueueDepth)
        {
            _stats.maxQueueDepth = _itemCount;
        }
        _stats.currentQueueDepth = _itemCount;

        setInterruptMask(mask);

        // Signal worker that work is available
        if (_workAvailable != nullptr)
        {
            _workAvailable->signal();
        }

        return Result::RESULT_SUCCESS;
    }

    void DPCWorker::GetStats(DPCWorkerStats& stats) const
    {
        // Disable interrupts for consistent read
        uint32_t mask = getInterruptMask();
        stats = _stats;
        stats.currentQueueDepth = _itemCount;
        setInterruptMask(mask);
    }

    void DPCWorker::ResetStats()
    {
        uint32_t mask = getInterruptMask();
        memset(&_stats, 0, sizeof(_stats));
        setInterruptMask(mask);
    }

    void DPCWorker::WorkerTaskEntry(void* arg)
    {
        DPCWorker* worker = static_cast<DPCWorker*>(arg);
        
        printf("[DPCWorker] Task started.\r\n");
        
        while (!worker->_stopRequested)
        {
            // printf("[DPCWorker] Waiting on semaphore (queue depth: %lu)...\r\n", 
            //                   worker->_itemCount);
            
            // Wait for work to be available (0xFFFFFFFF = wait forever)
            if (worker->_workAvailable != nullptr)
            {
                (void)worker->_workAvailable->wait(0xFFFFFFFF);
            }

            // Check stop flag after waking up
            if (worker->_stopRequested)
            {
                break;
            }
            
            // Process all pending work items
            worker->ProcessQueue();
        }

        // Clean up
        worker->_running = false;
        worker->_taskHandle = nullptr;

        // Delete self
        Task::Delete();
    }

    void DPCWorker::ProcessQueue()
    {
        while (_itemCount > 0)
        {
            // Disable interrupts to read from queue
            uint32_t mask = getInterruptMask();

            if (_itemCount == 0)
            {
                setInterruptMask(mask);
                break;
            }

            // Get work item from queue
            uint32_t readPos = _tail;
            DPCWorkItem item = _queue[readPos];

            // Update tail pointer (circular)
            _queue[readPos].valid = false;
            _tail = (readPos + 1) % DPC_WORKER_QUEUE_SIZE;
            _itemCount--;
            _stats.currentQueueDepth = _itemCount;

            setInterruptMask(mask);

            // Process the work item (interrupts enabled)
            if (item.valid)
            {
                uint32_t startTime = DWT_CYCCNT;
                ProcessWorkItem(item);
                uint32_t endTime = DWT_CYCCNT;

                // Update timing statistics
                uint32_t processingTime = endTime - startTime;
                _stats.lastProcessingTime = processingTime;
                
                // Update average (simple moving average)
                if (_stats.totalItemsProcessed == 0)
                {
                    _stats.avgProcessingTime = processingTime;
                }
                else
                {
                    _stats.avgProcessingTime = 
                        (_stats.avgProcessingTime * 7 + processingTime) / 8;
                }
                
                _stats.totalItemsProcessed++;
            }
        }
    }

    void DPCWorker::ProcessWorkItem(const DPCWorkItem& item)
    {
        switch (item.type)
        {
            case DPCWorkType::DPC_WORK_CALLBACK:
                if (item.handler != nullptr)
                {
                    item.handler(item.irqNumber, item.context, item.param);
                }
                break;

            case DPCWorkType::DPC_WORK_SIGNAL_SEM:
                if (item.semaphore != nullptr)
                {
                    item.semaphore->signal();
                }
                break;

            case DPCWorkType::DPC_WORK_CUSTOM:
                if (item.handler != nullptr)
                {
                    item.handler(item.irqNumber, item.context, item.param);
                }
                break;

            default:
                // Unknown work type, ignore
                break;
        }
    }

    // Convenience namespace functions
    namespace DPC
    {
        Result Init()
        {
            return GlobalDPCWorker.Init();
        }

        Result Start()
        {
            return GlobalDPCWorker.Start();
        }

        Result Stop()
        {
            return GlobalDPCWorker.Stop();
        }

        Result EnqueueWork(uint32_t irqNumber, 
                          DPCWorkHandler handler, 
                          void* context,
                          uint32_t param)
        {
            return GlobalDPCWorker.EnqueueWork(irqNumber, handler, context, param);
        }

        Result EnqueueSignal(uint32_t irqNumber, 
                            BinarySemaphore* semaphore)
        {
            return GlobalDPCWorker.EnqueueSignal(irqNumber, semaphore);
        }

        void GetStats(DPCWorkerStats& stats)
        {
            GlobalDPCWorker.GetStats(stats);
        }
    }
}
