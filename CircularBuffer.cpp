/*
 * CircularBuffer.cpp - CRTOS Circular Buffer Implementation
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

#include "CircularBuffer.hpp"
#include "CRTOS.hpp"
#include "CRTOS_Internal.hpp"
#include "HeapAllocator.hpp"

// External declarations from CRTOS.cpp
extern HeapAllocator mem;
extern volatile TaskControlBlock *sCurrentTCB;
extern "C" uint32_t getInterruptMask(void);
extern "C" void setInterruptMask(uint32_t mask);
extern "C" void memcpy_optimized(void *d, const void *s, uint32_t len);
extern volatile uint32_t tickCount;

CRTOS::CircularBuffer::CircularBuffer(uint32_t mBuffer_size)
    : mBuffer(nullptr),
      mHead(0u),
      mTail(0u),
      mCurrentSize(0u),
      mBufferSize(mBuffer_size)
{
}

CRTOS::CircularBuffer::CircularBuffer(const CircularBuffer &old)
{
    uint32_t mask = getInterruptMask();

    mHead = old.mHead;
    mTail = old.mTail;
    mCurrentSize = old.mCurrentSize;
    mBufferSize = old.mBufferSize;
    mBuffer = reinterpret_cast<uint8_t *>(mem.allocate(mBufferSize));
    memcpy_optimized(&mBuffer[0], &(old.mBuffer[0]), mBufferSize);

    setInterruptMask(mask);
}

CRTOS::CircularBuffer::~CircularBuffer(void)
{
    mem.deallocate(mBuffer);
}

CRTOS::Result CRTOS::CircularBuffer::Init(void)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;

    uint32_t mask = getInterruptMask();

    do
    {
        if (mBufferSize == 0)
        {
            setInterruptMask(mask);

            result = CRTOS::Result::RESULT_BAD_PARAMETER;
            continue;
        }

        mBuffer = reinterpret_cast<uint8_t *>(mem.allocate(mBufferSize));

        if (mBuffer == nullptr)
        {
            setInterruptMask(mask);

            result = CRTOS::Result::RESULT_NO_MEMORY;
            continue;
        }

        setInterruptMask(mask);
    } while (0u);

    return result;
}

CRTOS::Result CRTOS::CircularBuffer::Send(const uint8_t *data, uint32_t size)
{
    if ((data == nullptr) || (size == 0u))
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    if (mBuffer == nullptr)
    {
        return CRTOS::Result::RESULT_NO_MEMORY;
    }

    uint32_t mask = getInterruptMask();

    if (mCurrentSize + size > mBufferSize)
    {
        setInterruptMask(mask);
        return CRTOS::Result::RESULT_CIRCULAR_BUFFER_FULL;
    }

    // Copy data to buffer
    if (mHead + size <= mBufferSize)
    {
        memcpy_optimized(&mBuffer[mHead], data, size);
    }
    else
    {
        uint32_t firstPartSize = mBufferSize - mHead;
        memcpy_optimized(&mBuffer[mHead], data, firstPartSize);
        memcpy_optimized(mBuffer, &data[firstPartSize], size - firstPartSize);
    }

    mHead = (mHead + size) % mBufferSize;
    mCurrentSize += size;

    // Wake up waiting receiver if any
    bool shouldSwitch = false;
    if (listOfTasksWaitingToRecv != nullptr)
    {
        TaskControlBlock *tmp = *(TaskControlBlock **)(listOfTasksWaitingToRecv->data);
        if (tmp->state == TaskState::TASK_BLOCKED_BY_CIRC_BUFFER)
        {
            tmp->state = TaskState::TASK_READY;
            // Check if woken task has higher priority
            if (tmp->priority > sCurrentTCB->priority)
            {
                shouldSwitch = true;
            }
        }
        ListDeleteAtBeginning(listOfTasksWaitingToRecv);
    }

    setInterruptMask(mask);

    // Trigger context switch if higher priority task was woken
    if (shouldSwitch)
    {
        *ICSR_REG = NVIC_PENDSV_BIT;
        __DSB();
        __ISB();
    }

    return CRTOS::Result::RESULT_SUCCESS;
}

CRTOS::Result CRTOS::CircularBuffer::Receive(uint8_t *data, uint32_t size, uint32_t timeout)
{
    if ((data == nullptr) || (size == 0u))
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    if (mBuffer == nullptr)
    {
        return CRTOS::Result::RESULT_NO_MEMORY;
    }

    uint32_t stimeout = tickCount + timeout;

    while (1)
    {
        uint32_t mask = getInterruptMask();

        // Check if data is available
        if (mCurrentSize >= size)
        {
            // Data available - copy it out
            if (mTail + size <= mBufferSize)
            {
                memcpy_optimized(data, &mBuffer[mTail], size);
            }
            else
            {
                uint32_t firstPartSize = mBufferSize - mTail;
                memcpy_optimized(data, &mBuffer[mTail], firstPartSize);
                memcpy_optimized(&data[firstPartSize], mBuffer, size - firstPartSize);
            }

            mTail = (mTail + size) % mBufferSize;
            mCurrentSize -= size;

            setInterruptMask(mask);
            return CRTOS::Result::RESULT_SUCCESS;
        }

        // No data available - check timeout first before blocking
        if (timeout == 0u || tickCount >= stimeout)
        {
            setInterruptMask(mask);
            return CRTOS::Result::RESULT_CIRCULAR_BUFFER_TIMEOUT;
        }

        // Block this task and add to waiting list
        sCurrentTCB->timeout = stimeout;
        sCurrentTCB->state = TaskState::TASK_BLOCKED_BY_CIRC_BUFFER;
        ListInsertAtEnd(listOfTasksWaitingToRecv, (uint32_t **)&sCurrentTCB);

        setInterruptMask(mask);

        // Trigger context switch - when we resume, sender has either:
        // 1. Sent data and woken us up, OR
        // 2. Timeout occurred and scheduler woke us
        *ICSR_REG = NVIC_PENDSV_BIT;
        __DSB();
        __ISB();

        // When we get here, we've been woken up - loop back to check data/timeout
    }
}
