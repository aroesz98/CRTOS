/*
 * Syscall_IRQ.cpp - IRQ-related system calls
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

#include "Syscall_Handlers.hpp"
#include "../HAL/Interrupt.hpp"
#include "../Task.hpp"
#include "../CRTOS_Internal.hpp"  // For TaskControlBlock

using namespace CRTOS::HAL;
using namespace CRTOS::Syscall;

namespace CRTOS
{
    /**
     * @brief Wait for IRQ event (blocking)
     * 
     * @param irq_number IRQ number to wait for
     * @param event_buffer Pointer to IRQEvent structure (user-space)
     * @param timeout Timeout in milliseconds (0xFFFFFFFF = infinite)
     * @return 0 on success, negative error code on failure
     */
    int32_t sys_wait_irq(uint32_t irq_number, HAL::IRQEvent* event_buffer, uint32_t timeout)
    {
        // Validate parameters
        if (!event_buffer)
            return -(int32_t)SyscallError::ERR_INVAL;
        
        if (irq_number >= 256)
            return -(int32_t)SyscallError::ERR_INVAL;
        
        // Get current task
        void* current_task = Task::GetCurrentTaskHandle();
        if (!current_task)
            return -(int32_t)SyscallError::ERR_SRCH;
        
        // Convert timeout from ms to ticks
        uint32_t timeout_ticks = timeout;
        if (timeout != 0xFFFFFFFF)
        {
            timeout_ticks = (timeout * 1000) / 1000; // Assuming 1ms tick
        }
        
        // Wait for event - this returns immediately with EWOULDBLOCK if no event yet
        // The caller (user-space) should retry in a loop with sleep() between attempts
        int32_t result = GlobalInterruptController.GetEventQueue().WaitForIRQ(
            current_task, irq_number, event_buffer, timeout_ticks);
        
        // If EWOULDBLOCK, the waiter is registered and will be notified when event arrives
        // User-space should call this syscall again after sleeping
        if (result == -(int32_t)SyscallError::ERR_WOULDBLOCK)
        {
            // Yield to allow other tasks to run
            Task::Delay(1);
            
            // Try again immediately - if event arrived it will return success
            result = GlobalInterruptController.GetEventQueue().WaitForIRQ(
                current_task, irq_number, event_buffer, timeout_ticks);
            
            // If still blocking, user needs to call again
        }
        
        return result;
    }
    
    /**
     * @brief Register IRQ handler (kernel-level)
     * 
     * This syscall allows user-space to enable an IRQ.
     * The actual handler runs in kernel context and posts events.
     * 
     * @param irq_number IRQ number
     * @param priority Priority (0-255)
     * @return 0 on success, negative error code on failure
     */
    int32_t sys_register_irq(uint32_t irq_number, uint8_t priority)
    {
        if (irq_number >= 256)
            return -(int32_t)SyscallError::ERR_INVAL;
        
        // Get current task
        void* current_task = Task::GetCurrentTaskHandle();
        if (!current_task)
            return -(int32_t)SyscallError::ERR_SRCH;
        
        // Register IRQ in task's linked list
        RegisterCurrentTaskIRQ(irq_number);
        
        // Simple handler that just posts event
        auto simple_handler = [](uint32_t irq, void* ctx) {
            // Event posted automatically by ProcessIRQ
            (void)irq;
            (void)ctx;
        };
        
        // Register handler (will enable IRQ)
        return GlobalInterruptController.RegisterHandler(
            irq_number, simple_handler, nullptr, priority);
    }
    
    /**
     * @brief Unregister IRQ handler
     * 
     * @param irq_number IRQ number
     * @return 0 on success, negative error code on failure
     */
    int32_t sys_unregister_irq(uint32_t irq_number)
    {
        if (irq_number >= 256)
            return -(int32_t)SyscallError::ERR_INVAL;
        
        return GlobalInterruptController.UnregisterHandler(irq_number);
    }
    
    /**
     * @brief Poll for IRQ event (non-blocking)
     * 
     * @param irq_number IRQ number
     * @param event_buffer Pointer to IRQEvent structure
     * @return 0 if event ready, -EAGAIN if no event, negative error code on failure
     */
    int32_t sys_poll_irq(uint32_t irq_number, HAL::IRQEvent* event_buffer)
    {
        // Poll with zero timeout
        return sys_wait_irq(irq_number, event_buffer, 0);
    }
    
} // namespace CRTOS
