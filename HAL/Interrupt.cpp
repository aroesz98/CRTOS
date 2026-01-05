/*
 * Interrupt.cpp - CRTOS Hardware Abstraction Layer - Interrupt Controller Implementation
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

#include "Interrupt.hpp"
#include "../CRTOS.hpp"
#include "../Task.hpp"
#include "../../device/MIMXRT1052.h"

using namespace CRTOS::Syscall;

extern volatile uint32_t tickCount;

namespace CRTOS
{
namespace HAL
{
    // ===========================
    // IRQEventQueue Implementation
    // ===========================
    
    IRQEventQueue::IRQEventQueue()
        : m_waiters(nullptr), m_waiterCount(0)
    {
    }
    
    int32_t IRQEventQueue::WaitForIRQ(void* task, uint32_t irq_number, IRQEvent* event_buffer, uint32_t timeout)
    {
        if (!task || !event_buffer)
            return -(int32_t)SyscallError::ERR_INVAL;
        
        // Check if there's already an event pending for this IRQ
        __disable_irq();
        IRQWaiter* waiter = m_waiters;
        IRQWaiter* found_waiter = nullptr;
        while (waiter)
        {
            if (waiter->task == task && waiter->irq_number == irq_number)
            {
                if (waiter->ready)
                {
                    // Event already arrived! Copy it to user buffer and clean up
                    found_waiter = waiter;
                    break;
                }
                else
                {
                    // Waiter already exists but not ready yet - this is an error
                    __enable_irq();
                    return -(int32_t)SyscallError::ERR_BUSY;
                }
            }
            waiter = waiter->next;
        }
        
        if (found_waiter)
        {
            // Copy event data (waiter->event_buffer was filled by PostEvent)
            event_buffer->irq_number = found_waiter->event_buffer->irq_number;
            event_buffer->timestamp = found_waiter->event_buffer->timestamp;
            event_buffer->data = found_waiter->event_buffer->data;
            event_buffer->context = found_waiter->event_buffer->context;
            
            RemoveWaiter(found_waiter);
            __enable_irq();
            delete found_waiter;
            return 0;
        }
        __enable_irq();
        
        // No pending event - allocate waiter structure
        waiter = new IRQWaiter();
        if (!waiter)
            return -(int32_t)SyscallError::ERR_NOMEM;
        
        // Initialize waiter and clear event buffer
        waiter->task = task;
        waiter->irq_number = irq_number;
        waiter->event_buffer = event_buffer;
        waiter->timeout = timeout;
        waiter->start_tick = tickCount;
        waiter->ready = false;
        
        // Clear the event buffer to avoid stale data
        event_buffer->irq_number = 0;
        event_buffer->timestamp = 0;
        event_buffer->data = 0;
        event_buffer->context = nullptr;
        
        // Add to linked list
        __disable_irq();
        waiter->next = m_waiters;
        m_waiters = waiter;
        m_waiterCount++;
        __enable_irq();
        
        // Return EWOULDBLOCK to indicate the task should be rescheduled
        // The caller (syscall handler) should handle this by yielding
        return -(int32_t)SyscallError::ERR_WOULDBLOCK;
    }
    
    void IRQEventQueue::PostEvent(uint32_t irq_number, uint32_t data, void* context)
    {
        // Called from ISR - wake up all tasks waiting for this IRQ
        __disable_irq();
        
        // Capture precise timestamp using DWT cycle counter
        uint32_t timestamp = DWT->CYCCNT;
        
        IRQWaiter* waiter = m_waiters;
        while (waiter)
        {
            if (waiter->irq_number == irq_number && !waiter->ready)
            {
                // Fill event buffer
                waiter->event_buffer->irq_number = irq_number;
                waiter->event_buffer->timestamp = timestamp;
                waiter->event_buffer->data = data;
                waiter->event_buffer->context = context;
                
                // Mark ready
                waiter->ready = true;
            }
            waiter = waiter->next;
        }
        
        __enable_irq();
    }
    
    void IRQEventQueue::CheckTimeouts()
    {
        __disable_irq();
        
        IRQWaiter* waiter = m_waiters;
        while (waiter)
        {
            if (waiter->timeout != 0xFFFFFFFF && !waiter->ready)
            {
                uint32_t elapsed = tickCount - waiter->start_tick;
                if (elapsed >= waiter->timeout)
                {
                    // Timeout - mark ready with special error
                    waiter->ready = true;
                }
            }
            waiter = waiter->next;
        }
        
        __enable_irq();
    }
    
    void IRQEventQueue::RemoveTask(void* task)
    {
        if (!task)
            return;
        
        __disable_irq();
        
        IRQWaiter** prev = &m_waiters;
        IRQWaiter* waiter = m_waiters;
        
        while (waiter)
        {
            if (waiter->task == task)
            {
                *prev = waiter->next;
                delete waiter;
                m_waiterCount--;
                waiter = *prev;
            }
            else
            {
                prev = &waiter->next;
                waiter = waiter->next;
            }
        }
        
        __enable_irq();
    }
    
    void IRQEventQueue::RemoveWaiter(IRQWaiter* waiter)
    {
        if (!waiter)
            return;
        
        __disable_irq();
        
        IRQWaiter** prev = &m_waiters;
        IRQWaiter* current = m_waiters;
        
        while (current)
        {
            if (current == waiter)
            {
                *prev = current->next;
                m_waiterCount--;
                break;
            }
            prev = &current->next;
            current = current->next;
        }
        
        __enable_irq();
    }
    
    // ===========================
    // InterruptController Implementation
    // ===========================
    
    InterruptController::InterruptController()
    {
        // Initialize all handlers to unregistered
        for (uint32_t i = 0; i < MAX_IRQ_COUNT; i++)
        {
            m_handlers[i].handler = nullptr;
            m_handlers[i].context = nullptr;
            m_handlers[i].registered = false;
        }
    }
    
    int32_t InterruptController::RegisterHandler(uint32_t irq_number, IRQHandler handler, void* context, uint8_t priority)
    {
        if (irq_number >= MAX_IRQ_COUNT)
            return -(int32_t)SyscallError::ERR_INVAL;
        
        if (!handler)
            return -(int32_t)SyscallError::ERR_INVAL;
        
        __disable_irq();
        
        // Check if already registered
        if (m_handlers[irq_number].registered)
        {
            __enable_irq();
            return -(int32_t)SyscallError::ERR_BUSY;
        }
        
        // Register handler
        m_handlers[irq_number].handler = handler;
        m_handlers[irq_number].context = context;
        m_handlers[irq_number].registered = true;
        
        __enable_irq();
        
        // Configure NVIC
        SetPriority(irq_number, priority);
        EnableIRQ(irq_number);
        
        return 0;
    }
    
    int32_t InterruptController::UnregisterHandler(uint32_t irq_number)
    {
        if (irq_number >= MAX_IRQ_COUNT)
            return -(int32_t)SyscallError::ERR_INVAL;
        
        __disable_irq();
        
        if (!m_handlers[irq_number].registered)
        {
            __enable_irq();
            return -(int32_t)SyscallError::ERR_NOENT;
        }
        
        // Disable IRQ
        DisableIRQ(irq_number);
        
        // Unregister handler
        m_handlers[irq_number].handler = nullptr;
        m_handlers[irq_number].context = nullptr;
        m_handlers[irq_number].registered = false;
        
        __enable_irq();
        
        return 0;
    }
    
    void InterruptController::EnableIRQ(uint32_t irq_number)
    {
        if (irq_number < MAX_IRQ_COUNT)
        {
            NVIC_EnableIRQ((IRQn_Type)irq_number);
        }
    }
    
    void InterruptController::DisableIRQ(uint32_t irq_number)
    {
        if (irq_number < MAX_IRQ_COUNT)
        {
            NVIC_DisableIRQ((IRQn_Type)irq_number);
        }
    }
    
    void InterruptController::SetPriority(uint32_t irq_number, uint8_t priority)
    {
        if (irq_number < MAX_IRQ_COUNT)
        {
            NVIC_SetPriority((IRQn_Type)irq_number, priority);
        }
    }
    
    void InterruptController::TriggerIRQ(uint32_t irq_number)
    {
        if (irq_number < MAX_IRQ_COUNT)
        {
            NVIC_SetPendingIRQ((IRQn_Type)irq_number);
        }
    }
    
    void InterruptController::ClearPending(uint32_t irq_number)
    {
        if (irq_number < MAX_IRQ_COUNT)
        {
            NVIC_ClearPendingIRQ((IRQn_Type)irq_number);
        }
    }
    
    void InterruptController::ProcessIRQ(uint32_t irq_number)
    {
        if (irq_number >= MAX_IRQ_COUNT)
            return;
        
        // Call registered handler if present
        if (m_handlers[irq_number].registered && m_handlers[irq_number].handler)
        {
            m_handlers[irq_number].handler(irq_number, m_handlers[irq_number].context);
        }
        
        // Post event to waiting processes
        m_eventQueue.PostEvent(irq_number, 0, nullptr);
    }
    
    // Global instance
    InterruptController GlobalInterruptController;
    
} // namespace HAL
} // namespace CRTOS
