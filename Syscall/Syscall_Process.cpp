/*
 * Syscall_Process.cpp - CRTOS System Call Process Management Implementations
 * Author: Arkadiusz Szlanta
 * Date: 27 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 */

#include "SystemCall.hpp"
#include "../CRTOS.hpp"
#include "../CRTOS_Internal.hpp"
#include "../Task.hpp"

// Global TCB pointer (defined in CRTOS.cpp)
extern volatile TaskControlBlock *sCurrentTCB;

// Global tick counter (defined in CRTOS.cpp)
extern volatile uint32_t tickCount;

namespace CRTOS
{
    using namespace Syscall; // For SyscallError and other types
    
    //
    // SYS_EXIT: Exit current process
    //
    int32_t sys_exit(int32_t exitCode)
    {
        // TODO: When process management is implemented:
        // - Mark process as ZOMBIE
        // - Wake up parent if waiting
        // - Cleanup resources
        // - Delete all threads
        
        // For now, just delete current task
        Task::Delete();
        
        // Should never reach here
        return 0;
    }
    
    //
    // SYS_GETPID: Get process ID
    //
    int32_t sys_getpid(void)
    {
        // TODO: Return actual PID from current process
        // For now, return task handle as a temporary ID
        Task::TaskHandle handle = Task::GetCurrentTaskHandle();
        return (int32_t)handle;
    }
    
    // External kernel functions for direct privileged access
    extern "C" void setTimeSliceExpired(void);
    
    //
    // SYS_YIELD: Yield CPU to other processes
    // NOTE: This runs in privileged (handler) mode, so we use direct kernel access
    //
    int32_t sys_yield(void)
    {
        // Direct kernel access - we're in SVC handler (privileged mode)
        setTimeSliceExpired();
        
        // Trigger PendSV for context switch
        volatile uint32_t* ICSR = (volatile uint32_t*)0xE000ED04;
        *ICSR = (1UL << 28);  // PENDSVSET
        
        __asm__ volatile ("dsb");
        __asm__ volatile ("isb");
        
        return 0;
    }
    
    //
    // SYS_SLEEP: Sleep for specified ticks
    // NOTE: This runs in privileged (handler) mode, so we use direct kernel access
    //
    int32_t sys_sleep(uint32_t ticks)
    {
        if (ticks == 0)
        {
            // Sleep(0) is same as yield
            setTimeSliceExpired();
            volatile uint32_t* ICSR = (volatile uint32_t*)0xE000ED04;
            *ICSR = (1UL << 28);
            __asm__ volatile ("dsb");
            __asm__ volatile ("isb");
            return 0;
        }
        
        // Direct kernel access - we're in SVC handler (privileged mode)
        sCurrentTCB->state = TaskState::TASK_DELAYED;
        sCurrentTCB->delayUpTo = tickCount + ticks;
        
        // Trigger PendSV for context switch
        volatile uint32_t* ICSR = (volatile uint32_t*)0xE000ED04;
        *ICSR = (1UL << 28);  // PENDSVSET
        
        __asm__ volatile ("dsb");
        __asm__ volatile ("isb");
        
        return 0;
    }

    //
    // SYS_DROP_PRIVILEGES: Drop to user mode (one-way operation)
    // This is a security feature - once privileges are dropped, they cannot be regained
    //
    int32_t sys_drop_privileges(void)
    {
        // Get current task and mark it as unprivileged
        // The actual CONTROL register change happens at next context switch
        Task::TaskHandle handle = Task::GetCurrentTaskHandle();
        if (handle == nullptr)
        {
            return static_cast<int32_t>(SyscallError::ERR_INVAL);
        }
        
        // Drop privileges - this is a one-way operation
        // sCurrentTCB is declared extern at file scope
        sCurrentTCB->isPrivileged = false;
        
        // Trigger context switch to apply the change immediately
        // The PendSV handler will set CONTROL register appropriately
        // Direct kernel call - we're in SVC handler
        setTimeSliceExpired();
        volatile uint32_t* ICSR = (volatile uint32_t*)0xE000ED04;
        *ICSR = (1UL << 28);
        __asm__ volatile ("dsb");
        __asm__ volatile ("isb");
        
        return 0;
    }

    //
    // SYS_GET_PRIVILEGE: Check if running in privileged mode
    // Returns: 1 if privileged, 0 if user mode
    // NOTE: Uses direct kernel access since we're in SVC handler
    //
    int32_t sys_get_privilege(void)
    {
        // Direct access to TCB - we're in SVC handler (privileged mode)
        return sCurrentTCB->isPrivileged ? 1 : 0;
    }

    //
    // SYS_ELEVATE_PRIVILEGES: Temporarily elevate to privileged mode
    // This allows USER mode tasks to temporarily access hardware (e.g., UART for debug)
    // The task can drop back to user mode with drop_privileges
    // NOTE: In a production system, this would require authentication/authorization
    //
    int32_t sys_elevate_privileges(void)
    {
        // Elevate privileges - set the flag in TCB
        sCurrentTCB->isPrivileged = true;
        
        // Trigger context switch to apply the change immediately
        // The PendSV handler will set CONTROL register appropriately
        setTimeSliceExpired();
        volatile uint32_t* ICSR = (volatile uint32_t*)0xE000ED04;
        *ICSR = (1UL << 28);
        __asm__ volatile ("dsb");
        __asm__ volatile ("isb");
        
        return 0;
    }

} // namespace CRTOS
