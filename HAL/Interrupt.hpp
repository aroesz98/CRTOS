/*
 * Interrupt.hpp - CRTOS Hardware Abstraction Layer - Interrupt Controller
 * Author: Arkadiusz Szlanta
 * Date: 27 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * Hardware Abstraction Layer for interrupt controller (NVIC).
 * Provides unified interface for interrupt registration and handling.
 * Supports IRQ Event Queue for user-space applications.
 */

#ifndef CRTOS_HAL_INTERRUPT_HPP
#define CRTOS_HAL_INTERRUPT_HPP

#include <cstdint>
#include "../Syscall/SystemCall.hpp"

namespace CRTOS
{
    // Forward declarations
    namespace Task { }
    
namespace HAL
{
    // Forward declarations
    class IRQEventQueue;
    
    // IRQ handler callback type
    typedef void (*IRQHandler)(uint32_t irq_number, void* context);
    
    /**
     * @brief IRQ Event for user-space processes
     * 
     * When an interrupt occurs, the HAL creates an IRQEvent
     * and places it in the event queue for waiting processes.
     */
    struct IRQEvent
    {
        uint32_t irq_number;    // IRQ number
        uint32_t timestamp;     // Tick count when IRQ occurred
        uint32_t data;          // Optional data (e.g., register value)
        void* context;          // Optional context pointer
        
        IRQEvent() : irq_number(0), timestamp(0), data(0), context(nullptr) {}
    };
    
    /**
     * @brief Process waiting for IRQ event
     */
    struct IRQWaiter
    {
        void* task;             // Task waiting for event (Task::TaskHandle)
        uint32_t irq_number;    // IRQ number to wait for
        IRQEvent* event_buffer; // Buffer to store event
        uint32_t timeout;       // Timeout in ticks (0xFFFFFFFF = infinite)
        uint32_t start_tick;    // Tick when wait started
        bool ready;             // Event ready flag
        
        IRQWaiter* next;        // Linked list
        
        IRQWaiter() : task(nullptr), irq_number(0), event_buffer(nullptr),
                      timeout(0), start_tick(0), ready(false), next(nullptr) {}
    };
    
    /**
     * @brief IRQ Event Queue
     * 
     * Manages pending IRQ events and waiting processes.
     * When an IRQ occurs, it wakes up all processes waiting for that IRQ.
     */
    class IRQEventQueue
    {
    public:
        IRQEventQueue();
        ~IRQEventQueue() = default;
        
        /**
         * @brief Register process to wait for IRQ event
         * 
         * @param task Task to wait
         * @param irq_number IRQ number to wait for
         * @param event_buffer Buffer to store event data
         * @param timeout Timeout in ticks (0xFFFFFFFF = infinite)
         * @return 0 on success, negative error code on failure
         */
        int32_t WaitForIRQ(void* task, uint32_t irq_number, IRQEvent* event_buffer, uint32_t timeout);
        
        /**
         * @brief Post IRQ event (called from ISR)
         * 
         * Wakes up all processes waiting for this IRQ.
         * 
         * @param irq_number IRQ number
         * @param data Optional data
         * @param context Optional context
         */
        void PostEvent(uint32_t irq_number, uint32_t data, void* context);
        
        /**
         * @brief Check for timeout on waiting processes
         * 
         * Should be called periodically from timer interrupt.
         */
        void CheckTimeouts();
        
        /**
         * @brief Remove task from wait queue (e.g., when task exits)
         * 
         * @param task Task to remove
         */
        void RemoveTask(void* task);
        
    private:
        IRQWaiter* m_waiters;   // Linked list of waiting tasks
        uint32_t m_waiterCount; // Number of waiting tasks
        
        void RemoveWaiter(IRQWaiter* waiter);
    };
    
    /**
     * @brief Interrupt Controller HAL
     * 
     * Abstracts NVIC (Nested Vectored Interrupt Controller).
     * Manages interrupt registration, priorities, and routing.
     */
    class InterruptController
    {
    public:
        InterruptController();
        ~InterruptController() = default;
        
        /**
         * @brief Register IRQ handler
         * 
         * @param irq_number IRQ number (platform-specific)
         * @param handler Handler function (called from ISR context)
         * @param context Context pointer passed to handler
         * @param priority Priority (0-255, lower = higher priority)
         * @return 0 on success, negative error code on failure
         */
        int32_t RegisterHandler(uint32_t irq_number, IRQHandler handler, void* context, uint8_t priority);
        
        /**
         * @brief Unregister IRQ handler
         * 
         * @param irq_number IRQ number
         * @return 0 on success, negative error code on failure
         */
        int32_t UnregisterHandler(uint32_t irq_number);
        
        /**
         * @brief Enable IRQ
         * 
         * @param irq_number IRQ number
         */
        void EnableIRQ(uint32_t irq_number);
        
        /**
         * @brief Disable IRQ
         * 
         * @param irq_number IRQ number
         */
        void DisableIRQ(uint32_t irq_number);
        
        /**
         * @brief Set IRQ priority
         * 
         * @param irq_number IRQ number
         * @param priority Priority (0-255, lower = higher priority)
         */
        void SetPriority(uint32_t irq_number, uint8_t priority);
        
        /**
         * @brief Trigger software interrupt (for testing)
         * 
         * @param irq_number IRQ number
         */
        void TriggerIRQ(uint32_t irq_number);
        
        /**
         * @brief Clear pending interrupt
         * 
         * @param irq_number IRQ number
         */
        void ClearPending(uint32_t irq_number);
        
        /**
         * @brief Process IRQ (called from ISR)
         * 
         * This is called by the actual ISR to dispatch to registered handlers
         * and post events to waiting processes.
         * 
         * @param irq_number IRQ number
         */
        void ProcessIRQ(uint32_t irq_number);
        
        /**
         * @brief Get IRQ Event Queue
         * 
         * @return Reference to IRQ event queue
         */
        IRQEventQueue& GetEventQueue() { return m_eventQueue; }
        
    private:
        struct IRQHandlerInfo
        {
            IRQHandler handler;
            void* context;
            bool registered;
        };
        
        static constexpr uint32_t MAX_IRQ_COUNT = 256; // Maximum IRQ number
        
        IRQHandlerInfo m_handlers[MAX_IRQ_COUNT];
        IRQEventQueue m_eventQueue;
    };
    
    // Global interrupt controller instance
    extern InterruptController GlobalInterruptController;
    
} // namespace HAL
} // namespace CRTOS

#endif // CRTOS_HAL_INTERRUPT_HPP
