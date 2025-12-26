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
extern HeapAllocator mem;

CRTOS::Result CRTOS::BinarySemaphore::signal(void)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;

    uint32_t mask = getInterruptMask();

    if (_val > 0)
    {
        result = CRTOS::Result::RESULT_SEMAPHORE_BUSY;
    }
    else
    {
        if (listOfTasksWaitingToRecv != nullptr)
        {
            // The data field contains the TCB pointer directly (not pointer-to-pointer)
            TaskControlBlock *tmp = reinterpret_cast<TaskControlBlock*>(listOfTasksWaitingToRecv->data);
            if (tmp->state == TaskState::TASK_BLOCKED_BY_SEMAPHORE)
            {
                tmp->state = TaskState::TASK_READY;
            }
            ListDeleteAtBeginning(listOfTasksWaitingToRecv);
        }

        _val = 1;
    }

    setInterruptMask(mask);
    
    // Always trigger PendSV to let scheduler check for ready tasks
    *ICSR_REG = NVIC_PENDSV_BIT;
    __DSB();
    __ISB();

    return result;
}

CRTOS::Result CRTOS::BinarySemaphore::wait(uint32_t ticks)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    uint32_t time = tickCount;
    uint32_t timeout = time + ticks;
    bool isBlocked = false;

    for (;;)
    {
        uint32_t mask = getInterruptMask();

        time = tickCount;

        if (_val > 0u)
        {
            _val = 0u;
            setInterruptMask(mask);
            return result;
        }
        else
        {
            if (ticks == 0u)
            {
                setInterruptMask(mask);
                result = CRTOS::Result::RESULT_SEMAPHORE_TIMEOUT;
                return result;
            }

            if (isBlocked == false)
            {
                // Save the current TCB pointer before changing state
                TaskControlBlock* tcbToBlock = (TaskControlBlock*)sCurrentTCB;
                
                tcbToBlock->timeout = timeout;
                tcbToBlock->state = TaskState::TASK_BLOCKED_BY_SEMAPHORE;
                
                // Manual insertion to avoid static tail pointer corruption
                Node<uint32_t*> *newNode = reinterpret_cast<Node<uint32_t*> *>(mem.allocate(sizeof(Node<uint32_t*>)));
                if (newNode != nullptr)
                {
                    // Store the TCB pointer value directly (not &sCurrentTCB which changes!)
                    newNode->data = reinterpret_cast<uint32_t**>(tcbToBlock);
                    newNode->next = listOfTasksWaitingToRecv;
                    newNode->prev = nullptr;
                    if (listOfTasksWaitingToRecv != nullptr)
                    {
                        listOfTasksWaitingToRecv->prev = newNode;
                    }
                    listOfTasksWaitingToRecv = newNode;
                }
                
                isBlocked = true;
            }
        }

        setInterruptMask(mask);

        if (time < timeout)
        {
            if (_val > 0u)
            {
                mask = getInterruptMask();
                if (listOfTasksWaitingToRecv != nullptr)
                {
                    TaskControlBlock *tmp = reinterpret_cast<TaskControlBlock*>(listOfTasksWaitingToRecv->data);
                    if (tmp->state == TaskState::TASK_BLOCKED_BY_SEMAPHORE)
                    {
                        tmp->state = TaskState::TASK_READY;
                    }
                    ListDeleteAtBeginning(listOfTasksWaitingToRecv);
                }
                setInterruptMask(mask);

                if (isHigherPrioTaskPending() == true)
                {
                    *ICSR_REG = NVIC_PENDSV_BIT;

                    __DSB();
                    __ISB();
                }
            }
        }
        else
        {
            // Timeout occurred - remove ourselves from waiting list
            mask = getInterruptMask();

            // Find and remove current task from waiting list
            Node<uint32_t *> *temp = listOfTasksWaitingToRecv;
            if (temp != nullptr)
            {
                ListDeleteAtBeginning(listOfTasksWaitingToRecv);
            }

            sCurrentTCB->state = TaskState::TASK_READY;
            setInterruptMask(mask);

            result = CRTOS::Result::RESULT_SEMAPHORE_TIMEOUT;
            return result;
        }
    }
}
