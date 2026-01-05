/*
 * Futex.cpp - CRTOS Futex (Fast Userspace Mutex) Implementation
 * Author: Arkadiusz Szlanta
 * Date: 03 Jan 2026
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * Implementation of futex (fast userspace mutex) operations.
 * Provides Linux-like futex semantics for efficient synchronization.
 */

#include "Futex.hpp"
#include "CRTOS.hpp"
#include "CRTOS_Internal.hpp"
#include "HeapAllocator.hpp"
#include <cstring>

// External declarations from CRTOS.cpp
extern volatile TaskControlBlock *sCurrentTCB;
extern "C" uint32_t getInterruptMask(void);
extern "C" void setInterruptMask(uint32_t mask);
extern volatile uint32_t tickCount;

namespace CRTOS
{
    // Static member initialization
    FutexWaiter* Futex::waitQueues[FUTEX_HASH_SIZE] = {nullptr};
    bool Futex::initialized = false;

    void Futex::Init(void)
    {
        if (initialized)
            return;

        // Initialize all hash buckets to empty
        for (uint32_t i = 0; i < FUTEX_HASH_SIZE; i++)
        {
            waitQueues[i] = nullptr;
        }
        initialized = true;
    }

    Result Futex::Wait(uint32_t* uaddr, uint32_t expectedVal, uint32_t timeoutTicks)
    {
        if (uaddr == nullptr)
        {
            return Result::RESULT_BAD_PARAMETER;
        }

        // Check if running in user mode (unprivileged)
        // Read CONTROL register - bit 0 (nPRIV) indicates unprivileged mode
        uint32_t control;
        __asm__ volatile ("mrs %0, control" : "=r" (control));
        
        if (control & 0x01)
        {
            // USER MODE: Must use syscall (SVC) to access kernel structures
            // SYS_FUTEX_WAIT = 180
            register uint32_t r0 __asm__("r0") = reinterpret_cast<uint32_t>(uaddr);
            register uint32_t r1 __asm__("r1") = expectedVal;
            register uint32_t r2 __asm__("r2") = timeoutTicks;
            register int32_t result __asm__("r0");
            __asm__ volatile (
                "svc #180"
                : "=r" (result)
                : "r" (r0), "r" (r1), "r" (r2)
                : "memory"
            );
            if (result == 0) return Result::RESULT_SUCCESS;
            if (result == -11) return Result::RESULT_SEMAPHORE_TIMEOUT;  // EAGAIN/timeout
            if (result == -22) return Result::RESULT_BAD_PARAMETER;
            return Result::RESULT_DENIED;
        }

        // PRIVILEGED MODE: Call internal implementation
        return WaitInternal(uaddr, expectedVal, timeoutTicks);
    }

    Result Futex::WaitInternal(uint32_t* uaddr, uint32_t expectedVal, uint32_t timeoutTicks)
    {
        // DIRECT KERNEL ACCESS - Only call from privileged/handler mode!
        uint32_t mask = getInterruptMask();

        // Check if value matches expected (atomic check)
        // If not, return immediately - this is the spurious wakeup prevention
        if (*uaddr != expectedVal)
        {
            setInterruptMask(mask);
            return Result::RESULT_DENIED;
        }

        // Value matches, we need to wait
        // Allocate waiter structure
        FutexWaiter* waiter = reinterpret_cast<FutexWaiter*>(
            HeapAllocator::Allocate(sizeof(FutexWaiter)));
        
        if (waiter == nullptr)
        {
            setInterruptMask(mask);
            return Result::RESULT_NO_MEMORY;
        }

        // Initialize waiter
        TaskControlBlock* currentTask = (TaskControlBlock*)sCurrentTCB;
        waiter->tcb = currentTask;
        waiter->uaddr = uaddr;
        waiter->wakeValue = 0xFFFFFFFF; // Match any (for basic FUTEX_WAIT)
        waiter->wokenByTimeout = false;
        waiter->wokenByWake = false;
        waiter->next = nullptr;
        waiter->prev = nullptr;

        // Add to hash bucket (FIFO order - add at end for fairness)
        uint32_t bucket = HashAddress(uaddr);
        if (waitQueues[bucket] == nullptr)
        {
            waitQueues[bucket] = waiter;
        }
        else
        {
            // Find end of list
            FutexWaiter* tail = waitQueues[bucket];
            while (tail->next != nullptr)
            {
                tail = tail->next;
            }
            tail->next = waiter;
            waiter->prev = tail;
        }

        // Store waiter pointer in TCB for timeout handling
        currentTask->blockingNode = waiter;
        currentTask->timeout = (timeoutTicks == 0xFFFFFFFF) ? 0xFFFFFFFF : (tickCount + timeoutTicks);
        currentTask->wokenByTimeout = false;
        currentTask->state = TaskState::TASK_BLOCKED_BY_SEMAPHORE; // Reuse existing blocked state

        setInterruptMask(mask);

        // Yield to scheduler
        *ICSR_REG = NVIC_PENDSV_BIT;
        __DSB();
        __ISB();

        // --- TASK RESUMES HERE AFTER BEING WOKEN ---

        mask = getInterruptMask();

        // Check how we were woken
        if (currentTask->wokenByTimeout)
        {
            // Timeout occurred - remove ourselves from wait queue if still there
            // (might have already been removed by Wake())
            FutexWaiter* w = waitQueues[bucket];
            FutexWaiter* prevW = nullptr;
            while (w != nullptr)
            {
                if (w == waiter)
                {
                    // Remove from list
                    if (prevW == nullptr)
                    {
                        waitQueues[bucket] = w->next;
                    }
                    else
                    {
                        prevW->next = w->next;
                    }
                    if (w->next != nullptr)
                    {
                        w->next->prev = prevW;
                    }
                    break;
                }
                prevW = w;
                w = w->next;
            }

            HeapAllocator::Free(waiter);
            currentTask->blockingNode = nullptr;
            setInterruptMask(mask);
            return Result::RESULT_SEMAPHORE_TIMEOUT;
        }

        // Woken by FUTEX_WAKE - waiter was already removed from queue
        HeapAllocator::Free(waiter);
        currentTask->blockingNode = nullptr;
        setInterruptMask(mask);
        return Result::RESULT_SUCCESS;
    }

    uint32_t Futex::Wake(uint32_t* uaddr, uint32_t numWake)
    {
        if (uaddr == nullptr || numWake == 0)
        {
            return 0;
        }

        // Check if running in user mode (unprivileged)
        // Read CONTROL register - bit 0 (nPRIV) indicates unprivileged mode
        uint32_t control;
        __asm__ volatile ("mrs %0, control" : "=r" (control));
        
        if (control & 0x01)
        {
            // USER MODE: Must use syscall (SVC) to access kernel structures
            // SYS_FUTEX_WAKE = 181
            register uint32_t r0 __asm__("r0") = reinterpret_cast<uint32_t>(uaddr);
            register uint32_t r1 __asm__("r1") = numWake;
            register int32_t result __asm__("r0");
            __asm__ volatile (
                "svc #181"
                : "=r" (result)
                : "r" (r0), "r" (r1)
                : "memory"
            );
            return (result >= 0) ? static_cast<uint32_t>(result) : 0;
        }

        // PRIVILEGED MODE: Call internal implementation
        return WakeInternal(uaddr, numWake);
    }

    uint32_t Futex::WakeInternal(uint32_t* uaddr, uint32_t numWake)
    {
        // DIRECT KERNEL ACCESS - Only call from privileged/handler mode!
        uint32_t mask = getInterruptMask();
        uint32_t bucket = HashAddress(uaddr);
        uint32_t wokenCount = 0;

        FutexWaiter* waiter = waitQueues[bucket];
        FutexWaiter* prevWaiter = nullptr;

        while (waiter != nullptr && wokenCount < numWake)
        {
            FutexWaiter* nextWaiter = waiter->next;

            // Check if this waiter is for the same address
            if (waiter->uaddr == uaddr)
            {
                // Remove from wait queue
                if (prevWaiter == nullptr)
                {
                    waitQueues[bucket] = nextWaiter;
                }
                else
                {
                    prevWaiter->next = nextWaiter;
                }
                if (nextWaiter != nullptr)
                {
                    nextWaiter->prev = prevWaiter;
                }

                // Wake the task
                TaskControlBlock* waitingTask = reinterpret_cast<TaskControlBlock*>(waiter->tcb);
                waitingTask->wokenByTimeout = false;
                waiter->wokenByWake = true;
                waitingTask->state = TaskState::TASK_READY;
                waitingTask->blockingNode = nullptr;

                wokenCount++;

                // Don't update prevWaiter - we removed current node
            }
            else
            {
                prevWaiter = waiter;
            }

            waiter = nextWaiter;
        }

        setInterruptMask(mask);

        // Trigger scheduler if we woke any tasks
        if (wokenCount > 0)
        {
            *ICSR_REG = NVIC_PENDSV_BIT;
            __DSB();
            __ISB();
        }

        return wokenCount;
    }

    uint32_t Futex::Requeue(uint32_t* uaddr, uint32_t* uaddr2, 
                             uint32_t numWake, uint32_t numRequeue)
    {
        if (uaddr == nullptr || uaddr2 == nullptr)
        {
            return 0;
        }

        uint32_t mask = getInterruptMask();
        uint32_t srcBucket = HashAddress(uaddr);
        uint32_t dstBucket = HashAddress(uaddr2);
        uint32_t affectedCount = 0;
        uint32_t wokenCount = 0;
        uint32_t requeuedCount = 0;

        FutexWaiter* waiter = waitQueues[srcBucket];
        FutexWaiter* prevWaiter = nullptr;

        while (waiter != nullptr && (wokenCount < numWake || requeuedCount < numRequeue))
        {
            FutexWaiter* nextWaiter = waiter->next;

            if (waiter->uaddr == uaddr)
            {
                // Remove from source queue
                if (prevWaiter == nullptr)
                {
                    waitQueues[srcBucket] = nextWaiter;
                }
                else
                {
                    prevWaiter->next = nextWaiter;
                }
                if (nextWaiter != nullptr)
                {
                    nextWaiter->prev = prevWaiter;
                }

                if (wokenCount < numWake)
                {
                    // Wake this task
                    TaskControlBlock* waitingTask = reinterpret_cast<TaskControlBlock*>(waiter->tcb);
                    waitingTask->wokenByTimeout = false;
                    waiter->wokenByWake = true;
                    waitingTask->state = TaskState::TASK_READY;
                    waitingTask->blockingNode = nullptr;
                    wokenCount++;
                }
                else if (requeuedCount < numRequeue)
                {
                    // Requeue to destination
                    waiter->uaddr = uaddr2;
                    waiter->next = nullptr;
                    waiter->prev = nullptr;

                    if (waitQueues[dstBucket] == nullptr)
                    {
                        waitQueues[dstBucket] = waiter;
                    }
                    else
                    {
                        FutexWaiter* tail = waitQueues[dstBucket];
                        while (tail->next != nullptr)
                        {
                            tail = tail->next;
                        }
                        tail->next = waiter;
                        waiter->prev = tail;
                    }
                    requeuedCount++;
                }

                affectedCount++;
            }
            else
            {
                prevWaiter = waiter;
            }

            waiter = nextWaiter;
        }

        setInterruptMask(mask);

        // Trigger scheduler if we woke any tasks
        if (wokenCount > 0)
        {
            *ICSR_REG = NVIC_PENDSV_BIT;
            __DSB();
            __ISB();
        }

        return affectedCount;
    }

    bool Futex::HasWaiters(uint32_t* uaddr)
    {
        if (uaddr == nullptr)
        {
            return false;
        }

        uint32_t mask = getInterruptMask();
        uint32_t bucket = HashAddress(uaddr);

        FutexWaiter* waiter = waitQueues[bucket];
        while (waiter != nullptr)
        {
            if (waiter->uaddr == uaddr)
            {
                setInterruptMask(mask);
                return true;
            }
            waiter = waiter->next;
        }

        setInterruptMask(mask);
        return false;
    }

    uint32_t Futex::GetWaiterCount(uint32_t* uaddr)
    {
        if (uaddr == nullptr)
        {
            return 0;
        }

        uint32_t mask = getInterruptMask();
        uint32_t bucket = HashAddress(uaddr);
        uint32_t count = 0;

        FutexWaiter* waiter = waitQueues[bucket];
        while (waiter != nullptr)
        {
            if (waiter->uaddr == uaddr)
            {
                count++;
            }
            waiter = waiter->next;
        }

        setInterruptMask(mask);
        return count;
    }

    // ============================================================
    // FutexMutex implementation
    // ============================================================

    void FutexMutex::Lock(void)
    {
        int32_t expected = 0;

        // Fast path: try to acquire lock with atomic CAS
        // 0 -> 1: unlocked to locked (no waiters)
        if (state.compare_exchange_strong(expected, 1, 
            std::memory_order_acquire, std::memory_order_relaxed))
        {
            return; // Got the lock!
        }

        // Slow path: contention detected
        // Set state to 2 (locked with waiters) and sleep
        do
        {
            // If state was 1 (locked, no waiters), change to 2 (locked with waiters)
            if (expected == 1)
            {
                state.compare_exchange_strong(expected, 2,
                    std::memory_order_relaxed, std::memory_order_relaxed);
            }

            // Wait on the futex (only if state == 2)
            if (state.load(std::memory_order_relaxed) == 2)
            {
                Futex::Wait(reinterpret_cast<uint32_t*>(&state), 2, 0xFFFFFFFF);
            }

            // Try to acquire again
            expected = 0;
        } while (!state.compare_exchange_strong(expected, 2,
            std::memory_order_acquire, std::memory_order_relaxed));
    }

    void FutexMutex::Unlock(void)
    {
        // Atomically set state to 0 (unlocked)
        int32_t prevState = state.exchange(0, std::memory_order_release);

        // If there were waiters (state was 2), wake one
        if (prevState == 2)
        {
            Futex::Wake(reinterpret_cast<uint32_t*>(&state), 1);
        }
    }

    bool FutexMutex::TryLock(void)
    {
        int32_t expected = 0;
        return state.compare_exchange_strong(expected, 1,
            std::memory_order_acquire, std::memory_order_relaxed);
    }

    // ============================================================
    // FutexCondVar implementation
    // ============================================================

    Result FutexCondVar::Wait(FutexMutex& mutex, uint32_t timeoutTicks)
    {
        // Save current sequence number
        uint32_t seq = sequence.load(std::memory_order_acquire);

        // Release the mutex
        mutex.Unlock();

        // Wait for signal (sequence change)
        Result result = Futex::Wait(reinterpret_cast<uint32_t*>(&sequence), 
                                     seq, timeoutTicks);

        // Reacquire mutex before returning
        mutex.Lock();

        // FUTEX_WAIT returns DENIED if sequence already changed (spurious wakeup ok)
        if (result == Result::RESULT_DENIED)
        {
            return Result::RESULT_SUCCESS;
        }

        return result;
    }

    void FutexCondVar::Signal(void)
    {
        // Increment sequence to invalidate current waiters' expected value
        sequence.fetch_add(1, std::memory_order_release);

        // Wake one waiter
        Futex::Wake(reinterpret_cast<uint32_t*>(&sequence), 1);
    }

    void FutexCondVar::Broadcast(void)
    {
        // Increment sequence
        sequence.fetch_add(1, std::memory_order_release);

        // Wake all waiters
        Futex::Wake(reinterpret_cast<uint32_t*>(&sequence), 0xFFFFFFFF);
    }

} // namespace CRTOS
