/*
 * Queue.cpp - CRTOS Queue Implementation
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

#include "Queue.hpp"
#include "CRTOS.hpp"
#include "CRTOS_Internal.hpp"
#include "HeapAllocator.hpp"

// External declarations from CRTOS.cpp
extern HeapAllocator mem;
extern volatile TaskControlBlock *sCurrentTCB;
extern "C" uint32_t getInterruptMask(void);
extern "C" void setInterruptMask(uint32_t mask);
extern "C" void memcpy_optimized(void *d, void *s, uint32_t len);
extern uint32_t GetSystemTime(void);
extern bool isHigherPrioTaskPending(void);

CRTOS::Queue::Queue(uint32_t maxsize, uint32_t element_size)
    : mFront(0u), mRear(0u), mSize(0u), mMaxSize(maxsize), mElementSize(element_size)
{
    mQueue = reinterpret_cast<uint8_t *>(mem.allocate(maxsize * element_size));
}

CRTOS::Queue::~Queue(void)
{
    mem.deallocate(mQueue);
}

CRTOS::Result CRTOS::Queue::Send(void *item)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;

    do
    {
        if (item == nullptr)
        {
            result = CRTOS::Result::RESULT_BAD_PARAMETER;
            continue;
        }

        if (mQueue == nullptr)
        {
            result = CRTOS::Result::RESULT_NO_MEMORY;
            continue;
        }

        if (mSize == mMaxSize)
        {
            result = CRTOS::Result::RESULT_QUEUE_FULL;
            continue;
        }

        uint32_t mask = getInterruptMask();

        if (listOfTasksWaitingToRecv != nullptr)
        {
            TaskControlBlock *tmp = *(TaskControlBlock **)(listOfTasksWaitingToRecv->data);
            if (tmp->state == TaskState::TASK_BLOCKED_BY_QUEUE)
            {
                tmp->state = TaskState::TASK_READY;
            }
            ListDeleteAtBeginning(listOfTasksWaitingToRecv);
        }

        memcpy_optimized(mQueue + (mRear * mElementSize), item, mElementSize);
        mRear = (mRear + 1) % mMaxSize;
        mSize++;

        setInterruptMask(mask);
    } while (0u);

    return result;
}

CRTOS::Result CRTOS::Queue::Receive(void *item, uint32_t timeout)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    uint32_t time = GetSystemTime();
    uint32_t stimeout = time + timeout;
    bool isBlocked = false;

    uint32_t mask = getInterruptMask();

    for (;;)
    {
        time = GetSystemTime();

        if (item == nullptr)
        {
            setInterruptMask(mask);
            result = CRTOS::Result::RESULT_BAD_PARAMETER;
            return result;
        }

        if (mQueue == nullptr)
        {
            setInterruptMask(mask);
            result = CRTOS::Result::RESULT_NO_MEMORY;
            return result;
        }

        if (mSize > 0u)
        {
            memcpy_optimized(item, mQueue + (mFront * mElementSize), mElementSize);
            mFront = (mFront + 1) % mMaxSize;
            mSize--;

            setInterruptMask(mask);

            result = CRTOS::Result::RESULT_SUCCESS;
            return result;
        }
        else
        {
            if (timeout == 0u)
            {
                setInterruptMask(mask);

                result = CRTOS::Result::RESULT_QUEUE_TIMEOUT;
                return result;
            }
            if (isBlocked == false)
            {
                sCurrentTCB->timeout = stimeout;
                sCurrentTCB->state = TaskState::TASK_BLOCKED_BY_QUEUE;
                ListInsertAtEnd(listOfTasksWaitingToRecv, (uint32_t **)&sCurrentTCB);
                isBlocked = true;
            }
        }

        setInterruptMask(mask);

        if (time < stimeout)
        {
            if (mSize > 0u)
            {
                mask = getInterruptMask();
                if (listOfTasksWaitingToRecv != nullptr)
                {
                    TaskControlBlock *tmp = *(TaskControlBlock **)(listOfTasksWaitingToRecv->data);
                    if (tmp->state == TaskState::TASK_BLOCKED_BY_QUEUE)
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

            result = CRTOS::Result::RESULT_QUEUE_TIMEOUT;
            return result;
        }
    }
}
