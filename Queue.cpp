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
extern volatile TaskControlBlock *sCurrentTCB;
extern "C" uint32_t getInterruptMask(void);
extern "C" void setInterruptMask(uint32_t mask);
extern "C" void memcpy_optimized(void *d, void *s, uint32_t len);
extern uint32_t GetSystemTime(void);
extern bool isHigherPrioTaskPending(void);

CRTOS::Queue::Queue(uint32_t maxsize, uint32_t element_size)
    : mFront(0u), mRear(0u), mSize(0u), mMaxSize(maxsize), mElementSize(element_size)
{
    mQueue = reinterpret_cast<uint8_t *>(HeapAllocator::Allocate(maxsize * element_size));
}

CRTOS::Queue::~Queue(void)
{
    HeapAllocator::Free(mQueue);
}

CRTOS::Result CRTOS::Queue::Send(void *item)
{
    if (item == nullptr)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    if (mQueue == nullptr)
    {
        return CRTOS::Result::RESULT_NO_MEMORY;
    }

    uint32_t mask = getInterruptMask();

    if (mSize == mMaxSize)
    {
        setInterruptMask(mask);
        return CRTOS::Result::RESULT_QUEUE_FULL;
    }

    // Add item to queue
    memcpy_optimized(mQueue + (mRear * mElementSize), item, mElementSize);
    mRear = (mRear + 1) % mMaxSize;
    mSize++;

    // Wake up waiting receiver if any
    if (listOfTasksWaitingToRecv != nullptr)
    {
        TaskControlBlock *waitingTask = reinterpret_cast<TaskControlBlock*>(listOfTasksWaitingToRecv->data);
        
        // Mark as NOT timed out (woken by Send)
        waitingTask->wokenByTimeout = false;
        waitingTask->state = TaskState::TASK_READY;
        waitingTask->blockingNode = nullptr;
        
        ListDeleteAtBeginning(listOfTasksWaitingToRecv);
        
        setInterruptMask(mask);
        
        // Trigger scheduler
        *ICSR_REG = NVIC_PENDSV_BIT;
        __DSB();
        __ISB();
    }
    else
    {
        setInterruptMask(mask);
    }

    return CRTOS::Result::RESULT_SUCCESS;
}

CRTOS::Result CRTOS::Queue::Receive(void *item, uint32_t timeout)
{
    if (item == nullptr)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    if (mQueue == nullptr)
    {
        return CRTOS::Result::RESULT_NO_MEMORY;
    }

    uint32_t mask = getInterruptMask();

    // 1. Success: Data available immediately
    if (mSize > 0u)
    {
        memcpy_optimized(item, mQueue + (mFront * mElementSize), mElementSize);
        mFront = (mFront + 1) % mMaxSize;
        mSize--;
        setInterruptMask(mask);
        return CRTOS::Result::RESULT_SUCCESS;
    }

    // 2. Immediate Failure: Polling only, no wait
    if (timeout == 0u)
    {
        setInterruptMask(mask);
        return CRTOS::Result::RESULT_QUEUE_TIMEOUT;
    }

    // 3. Blocking with Timeout
    TaskControlBlock* currentTask = (TaskControlBlock*)sCurrentTCB;
    
    // Add task to queue's waiting list
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
    
    // Set timeout
    currentTask->timeout = (timeout == 0xFFFFFFFF) ? 0xFFFFFFFF : (GetSystemTime() + timeout);
    currentTask->wokenByTimeout = false;
    currentTask->state = TaskState::TASK_BLOCKED_BY_QUEUE;
    
    setInterruptMask(mask);
    
    // Yield - give up CPU
    *ICSR_REG = NVIC_PENDSV_BIT;
    __DSB();
    __ISB();
    
    // --- TASK RESUMES HERE ---
    
    mask = getInterruptMask();
    
    if (currentTask->wokenByTimeout)
    {
        setInterruptMask(mask);
        return CRTOS::Result::RESULT_QUEUE_TIMEOUT;
    }
    
    // We were woken by Send - data should be available
    if (mSize > 0u)
    {
        memcpy_optimized(item, mQueue + (mFront * mElementSize), mElementSize);
        mFront = (mFront + 1) % mMaxSize;
        mSize--;
        setInterruptMask(mask);
        return CRTOS::Result::RESULT_SUCCESS;
    }
    
    setInterruptMask(mask);
    return CRTOS::Result::RESULT_QUEUE_TIMEOUT;
}
