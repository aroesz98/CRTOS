/*
 * Futex.hpp - CRTOS Futex (Fast Userspace Mutex) Class
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
 * Futex (Fast Userspace Mutex) implementation based on Linux futex semantics.
 * Provides efficient synchronization primitives that avoid kernel transitions
 * in the uncontended case.
 * 
 * Operations:
 * - FUTEX_WAIT: If *uaddr == val, block current task until woken
 * - FUTEX_WAKE: Wake up to 'val' tasks waiting on uaddr
 * - FUTEX_WAIT_BITSET: Wait with bitmask matching (future extension)
 * - FUTEX_WAKE_BITSET: Wake with bitmask matching (future extension)
 */

#ifndef FUTEX_HPP
#define FUTEX_HPP

#include <cstdint>
#include <atomic>

// Forward declarations
template <typename T> class Node;

namespace CRTOS
{
    enum class Result : uint8_t;

    // Futex operation codes (matching Linux semantics)
    enum class FutexOp : uint32_t
    {
        FUTEX_WAIT = 0,         // Wait if *uaddr == val
        FUTEX_WAKE = 1,         // Wake up to val waiters
        FUTEX_WAIT_BITSET = 9,  // Wait with bitmask (future extension)
        FUTEX_WAKE_BITSET = 10, // Wake with bitmask (future extension)
        FUTEX_REQUEUE = 3,      // Requeue waiters to another futex
    };

    // Structure to track tasks waiting on a futex address
    struct FutexWaiter
    {
        void* tcb;                  // Pointer to TaskControlBlock
        uint32_t* uaddr;            // Address being waited on
        uint32_t wakeValue;         // Value to match for wakeup (for bitset operations)
        bool wokenByTimeout;        // True if woken by timeout
        bool wokenByWake;           // True if woken by FUTEX_WAKE
        FutexWaiter* next;          // Next waiter in list
        FutexWaiter* prev;          // Previous waiter in list
    };

    // Global futex wait queue (hash table for better performance)
    #define FUTEX_HASH_SIZE 32

    class Futex
    {
        public:
            /**
             * @brief Initialize the futex subsystem
             * Must be called once during CRTOS initialization
             */
            static void Init(void);

            /**
             * @brief Wait on a futex address
             * Auto-detects privilege mode and uses syscall if in user mode.
             * 
             * @param uaddr Pointer to the futex variable in user memory
             * @param expectedVal Expected value at uaddr (only wait if *uaddr == expectedVal)
             * @param timeoutTicks Timeout in system ticks (0xFFFFFFFF = infinite)
             * @return Result::RESULT_SUCCESS if woken by FUTEX_WAKE
             *         Result::RESULT_SEMAPHORE_TIMEOUT if timeout occurred
             *         Result::RESULT_BAD_PARAMETER if uaddr is invalid
             *         Result::RESULT_DENIED if *uaddr != expectedVal (spurious wakeup prevention)
             */
            static Result Wait(uint32_t* uaddr, uint32_t expectedVal, uint32_t timeoutTicks);

            /**
             * @brief Wake tasks waiting on a futex address
             * Auto-detects privilege mode and uses syscall if in user mode.
             * 
             * @param uaddr Pointer to the futex variable
             * @param numWake Maximum number of waiters to wake (1 for mutex, INT_MAX for broadcast)
             * @return Number of tasks actually woken
             */
            static uint32_t Wake(uint32_t* uaddr, uint32_t numWake);

            /**
             * @brief Internal Wait - Direct kernel access (for syscall handlers)
             * WARNING: Only call from privileged/handler mode!
             */
            static Result WaitInternal(uint32_t* uaddr, uint32_t expectedVal, uint32_t timeoutTicks);

            /**
             * @brief Internal Wake - Direct kernel access (for syscall handlers)
             * WARNING: Only call from privileged/handler mode!
             */
            static uint32_t WakeInternal(uint32_t* uaddr, uint32_t numWake);

            /**
             * @brief Requeue waiters from one futex to another
             * Used for condition variable implementation
             * 
             * @param uaddr Source futex address
             * @param uaddr2 Destination futex address  
             * @param numWake Number of waiters to wake (usually 0 or 1)
             * @param numRequeue Number of waiters to requeue to uaddr2
             * @return Number of tasks affected (woken + requeued)
             */
            static uint32_t Requeue(uint32_t* uaddr, uint32_t* uaddr2, 
                                     uint32_t numWake, uint32_t numRequeue);

            /**
             * @brief Check if any tasks are waiting on this futex
             * 
             * @param uaddr Futex address to check
             * @return True if at least one task is waiting
             */
            static bool HasWaiters(uint32_t* uaddr);

            /**
             * @brief Get number of tasks waiting on this futex
             * 
             * @param uaddr Futex address to check
             * @return Number of waiting tasks
             */
            static uint32_t GetWaiterCount(uint32_t* uaddr);

        private:
            // Hash function for futex address
            static inline uint32_t HashAddress(uint32_t* uaddr)
            {
                // Simple hash based on address shifted by 2 bits (word alignment)
                return (reinterpret_cast<uint32_t>(uaddr) >> 2) % FUTEX_HASH_SIZE;
            }

            // Global wait queue (hash table)
            static FutexWaiter* waitQueues[FUTEX_HASH_SIZE];
            static bool initialized;
    };

    /**
     * @brief Minimal Futex Mutex class for userspace
     * 
     * This is a convenience wrapper that implements a Linux-like futex mutex.
     * It uses atomic operations in userspace for the fast path (uncontended case)
     * and falls back to kernel futex operations when contention is detected.
     */
    class FutexMutex
    {
        public:
            FutexMutex(void) : state(0) {}
            ~FutexMutex(void) = default;

            /**
             * @brief Lock the mutex
             * Fast path: atomic CAS in userspace
             * Slow path: FUTEX_WAIT syscall to sleep
             */
            void Lock(void);

            /**
             * @brief Unlock the mutex
             * Atomically releases and wakes one waiter if needed
             */
            void Unlock(void);

            /**
             * @brief Try to lock without blocking
             * @return true if lock acquired, false otherwise
             */
            bool TryLock(void);

            /**
             * @brief Check if mutex is currently locked
             */
            bool IsLocked(void) const { return state.load(std::memory_order_acquire) != 0; }

        private:
            // State: 0 = unlocked, 1 = locked (no waiters), 2 = locked (with waiters)
            std::atomic<int32_t> state;
    };

    /**
     * @brief Futex-based condition variable
     * 
     * Implements condition variable semantics using futex operations.
     * Supports wait, signal (wake one), and broadcast (wake all).
     */
    class FutexCondVar
    {
        public:
            FutexCondVar(void) : sequence(0) {}
            ~FutexCondVar(void) = default;

            /**
             * @brief Wait on condition variable
             * Must be called with mutex locked. Releases mutex while waiting,
             * reacquires before returning.
             * 
             * @param mutex The mutex to release/reacquire
             * @param timeoutTicks Timeout in ticks (0xFFFFFFFF = infinite)
             * @return Result::RESULT_SUCCESS or Result::RESULT_SEMAPHORE_TIMEOUT
             */
            Result Wait(FutexMutex& mutex, uint32_t timeoutTicks = 0xFFFFFFFF);

            /**
             * @brief Signal one waiting task
             */
            void Signal(void);

            /**
             * @brief Wake all waiting tasks
             */
            void Broadcast(void);

        private:
            // Sequence counter - incremented on each signal/broadcast
            std::atomic<uint32_t> sequence;
    };

} // namespace CRTOS

#endif /* FUTEX_HPP */
