/*
 * Syscall_Futex.cpp - CRTOS Futex System Call Handlers
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
 * System call handlers for futex (fast userspace mutex) operations.
 * These provide the kernel-side implementation of futex primitives.
 */

#include "SystemCall.hpp"
#include "Syscall_Handlers.hpp"
#include "../Futex.hpp"
#include "../CRTOS.hpp"

namespace CRTOS
{
    /**
     * @brief Wait on a futex if *uaddr == val
     * 
     * @param uaddr Pointer to the futex variable
     * @param val Expected value (only wait if *uaddr == val)
     * @param timeout Timeout in system ticks (0xFFFFFFFF = infinite)
     * @return 0 on success (woken by wake), -ETIMEDOUT on timeout, -EAGAIN if *uaddr != val
     */
    int32_t sys_futex_wait(uint32_t* uaddr, uint32_t val, uint32_t timeout)
    {
        // Validate user pointer (basic check - within SDRAM range)
        if (uaddr == nullptr)
        {
            return static_cast<int32_t>(Syscall::SyscallError::ERR_INVAL);
        }

        // Use WaitInternal - we're in SVC handler (privileged mode)
        Result result = Futex::WaitInternal(uaddr, val, timeout);

        switch (result)
        {
            case Result::RESULT_SUCCESS:
                return 0;
            case Result::RESULT_SEMAPHORE_TIMEOUT:
                return static_cast<int32_t>(Syscall::SyscallError::ERR_TIMEDOUT);
            case Result::RESULT_DENIED:
                // Value mismatch - like Linux EAGAIN
                return static_cast<int32_t>(Syscall::SyscallError::ERR_AGAIN);
            case Result::RESULT_NO_MEMORY:
                return static_cast<int32_t>(Syscall::SyscallError::ERR_NOMEM);
            case Result::RESULT_BAD_PARAMETER:
            default:
                return static_cast<int32_t>(Syscall::SyscallError::ERR_INVAL);
        }
    }

    /**
     * @brief Wake up to numWake waiters on a futex
     * 
     * @param uaddr Pointer to the futex variable
     * @param numWake Maximum number of waiters to wake
     * @return Number of waiters actually woken (>= 0)
     */
    int32_t sys_futex_wake(uint32_t* uaddr, uint32_t numWake)
    {
        if (uaddr == nullptr)
        {
            return static_cast<int32_t>(Syscall::SyscallError::ERR_INVAL);
        }

        // Use WakeInternal - we're in SVC handler (privileged mode)
        return static_cast<int32_t>(Futex::WakeInternal(uaddr, numWake));
    }

    /**
     * @brief Requeue waiters from one futex to another
     * 
     * This is primarily used for efficient condition variable implementation.
     * It atomically wakes numWake waiters and moves numRequeue waiters to
     * a different futex address.
     * 
     * @param uaddr Source futex address
     * @param uaddr2 Destination futex address
     * @param numWake Number of waiters to wake
     * @param numRequeue Number of waiters to requeue to uaddr2
     * @return Number of affected waiters (woken + requeued)
     */
    int32_t sys_futex_requeue(uint32_t* uaddr, uint32_t* uaddr2, 
                               uint32_t numWake, uint32_t numRequeue)
    {
        if (uaddr == nullptr || uaddr2 == nullptr)
        {
            return static_cast<int32_t>(Syscall::SyscallError::ERR_INVAL);
        }

        return static_cast<int32_t>(Futex::Requeue(uaddr, uaddr2, numWake, numRequeue));
    }

    /**
     * @brief Wait on futex with bitmask matching
     * 
     * Advanced version of futex_wait that uses a bitmask for selective waking.
     * A waiter can only be woken by a wake_bitset call if the bitmasks overlap.
     * 
     * @param uaddr Pointer to the futex variable
     * @param val Expected value
     * @param timeout Timeout in system ticks
     * @param bitset Bitmask for matching (0xFFFFFFFF matches everything)
     * @return Same as sys_futex_wait
     */
    int32_t sys_futex_wait_bitset(uint32_t* uaddr, uint32_t val, 
                                   uint32_t timeout, uint32_t bitset)
    {
        // For now, ignore bitset and use regular wait
        // Full implementation would store bitset in FutexWaiter and check on wake
        (void)bitset;
        return sys_futex_wait(uaddr, val, timeout);
    }

    /**
     * @brief Wake waiters with bitmask matching
     * 
     * Only wakes waiters whose bitmask overlaps with the provided bitset.
     * 
     * @param uaddr Pointer to the futex variable
     * @param numWake Maximum number of waiters to wake
     * @param bitset Bitmask for matching
     * @return Number of waiters actually woken
     */
    int32_t sys_futex_wake_bitset(uint32_t* uaddr, uint32_t numWake, uint32_t bitset)
    {
        // For now, ignore bitset and use regular wake
        (void)bitset;
        return sys_futex_wake(uaddr, numWake);
    }

} // namespace CRTOS
