/*
 * BinarySemaphore.cpp - CRTOS Binary Semaphore Implementation
 * Author: Arkadiusz Szlanta
 * Date: 25 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 */

#include "BinarySemaphore.hpp"
#include "CRTOS.hpp"
#include "CRTOS_Internal.hpp"
#include "stdio.h"

// External declarations from CRTOS.cpp
extern volatile TaskControlBlock *sCurrentTCB;
extern "C" uint32_t getInterruptMask(void);
extern "C" void setInterruptMask(uint32_t mask);
extern volatile uint32_t tickCount;
extern bool isHigherPrioTaskPending();

CRTOS::Result CRTOS::BinarySemaphore::signal(void)
{
    uint32_t mask = getInterruptMask();

    if (listOfTasksWaitingToRecv != nullptr)
    {
        // Tasks are waiting: wake up the first one (FIFO order)
        TaskControlBlock *waitingTask = reinterpret_cast<TaskControlBlock*>(listOfTasksWaitingToRecv->data);
        
        // Mark task as NOT timed out (it was woken by signal)
        waitingTask->wokenByTimeout = false;
        waitingTask->state = TaskState::TASK_READY;
        waitingTask->blockingNode = nullptr;
        
        // Remove from waiting list
        ListDeleteAtBeginning(listOfTasksWaitingToRecv);
        
        setInterruptMask(mask);
        
        // Trigger scheduler to potentially switch to woken task
        *ICSR_REG = NVIC_PENDSV_BIT;
        __DSB();
        __ISB();
    }
    else
    {
        // No tasks waiting: just set the semaphore
        _val = 1;
        setInterruptMask(mask);
    }

    return CRTOS::Result::RESULT_SUCCESS;
}

CRTOS::Result CRTOS::BinarySemaphore::wait(uint32_t ticks)
{
    uint32_t mask = getInterruptMask();

    // 1. Success: Resource available immediately
    if (_val > 0)
    {
        _val = 0;
        setInterruptMask(mask);
        return CRTOS::Result::RESULT_SUCCESS;
    }
    
    // 2. Immediate Failure: Polling only, no wait
    if (ticks == 0)
    {
        setInterruptMask(mask);
        return CRTOS::Result::RESULT_SEMAPHORE_TIMEOUT;
    }
    
    // 3. Blocking with Timeout
    TaskControlBlock* currentTask = (TaskControlBlock*)sCurrentTCB;
    
    // Add task to semaphore's waiting list
    Node<uint32_t*> *newNode = reinterpret_cast<Node<uint32_t*>*>(HeapAllocator::Allocate(sizeof(Node<uint32_t*>)));
    if (newNode != nullptr)
    {
        newNode->data = reinterpret_cast<uint32_t**>(currentTask);
        newNode->next = listOfTasksWaitingToRecv;
        newNode->prev = nullptr;
        if (listOfTasksWaitingToRecv != nullptr)
        {
            listOfTasksWaitingToRecv->prev = newNode;
        }
        listOfTasksWaitingToRecv = newNode;
        currentTask->blockingNode = newNode;
    }
    
    // Set timeout (scheduler will wake us if timeout expires)
    currentTask->timeout = (ticks == 0xFFFFFFFF) ? 0xFFFFFFFF : (tickCount + ticks);
    currentTask->wokenByTimeout = false;
    currentTask->state = TaskState::TASK_BLOCKED_BY_SEMAPHORE;
    
    setInterruptMask(mask);
    
    // Yield - give up CPU, scheduler will run next task
    *ICSR_REG = NVIC_PENDSV_BIT;
    __DSB();
    __ISB();
    
    // --- TASK RESUMES HERE AFTER BEING WOKEN ---
    
    mask = getInterruptMask();
    
    // Check if we woke up because of signal or timeout
    if (currentTask->wokenByTimeout)
    {
        // Timeout occurred - we were already removed from wait list by scheduler
        setInterruptMask(mask);
        return CRTOS::Result::RESULT_SEMAPHORE_TIMEOUT;
    }
    
    // We got the semaphore!
    setInterruptMask(mask);
    return CRTOS::Result::RESULT_SUCCESS;
}
