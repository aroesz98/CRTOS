/*
 * InterruptDPC.hpp - CRTOS Interrupt DPC (Deferred Procedure Call) Dispatcher
 * Author: Arkadiusz Szlanta
 * Date: 26 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * This module provides a DPC (Deferred Procedure Call) dispatcher for handling
 * interrupts in a multi-tasking environment. It allows one interrupt source to
 * notify multiple tasks through binary semaphores, ensuring minimal interrupt
 * latency while providing flexible task notification.
 *
 * Features:
 * - Register multiple tasks to be notified by a single interrupt
 * - ISR-safe operation with minimal overhead
 * - Support for multiple interrupt sources
 * - Automatic task notification via binary semaphores
 * - Dynamic registration/unregistration of handlers
 */

#ifndef INTERRUPT_DPC_HPP
#define INTERRUPT_DPC_HPP

#include <cstdint>
#include "BinarySemaphore.hpp"

namespace CRTOS
{
    enum class Result : uint8_t;

    // Maximum number of task handlers per interrupt source
    #ifndef INTERRUPT_DPC_MAX_HANDLERS_PER_IRQ
    #define INTERRUPT_DPC_MAX_HANDLERS_PER_IRQ 8
    #endif

    // Maximum number of interrupt sources
    #ifndef INTERRUPT_DPC_MAX_SOURCES
    #define INTERRUPT_DPC_MAX_SOURCES 16
    #endif

    // DPC handler callback type
    // This callback runs in ISR context - keep it minimal!
    // Use it only for reading hardware registers or setting flags
    // The actual work should be done in the task that gets signaled
    typedef void (*DPCCallback)(void* context);

    // Handler entry for a single task
    struct DPCHandler
    {
        BinarySemaphore* semaphore;  // Semaphore to signal the task
        DPCCallback callback;         // Optional callback (runs in ISR context)
        void* context;                // Context passed to callback
        uint32_t gotBase;             // GOT base for PIC modules (0 = kernel code)
        bool active;                  // Whether this handler is active
    };

    // Interrupt source descriptor
    struct InterruptSource
    {
        uint32_t irqNumber;                                      // IRQ number
        DPCHandler handlers[INTERRUPT_DPC_MAX_HANDLERS_PER_IRQ]; // Registered handlers
        uint32_t handlerCount;                                   // Number of active handlers
        bool registered;                                         // Whether this source is registered
    };

    class InterruptDPC
    {
    public:
        InterruptDPC();
        ~InterruptDPC() = default;

        // Register an interrupt source
        // This should be called before registering any handlers for this IRQ
        Result RegisterInterruptSource(uint32_t irqNumber);

        // Unregister an interrupt source
        // This will remove all handlers for this IRQ
        Result UnregisterInterruptSource(uint32_t irqNumber);

        // Register a task handler for an interrupt
        // Parameters:
        //   irqNumber  - The IRQ number to handle
        //   semaphore  - Semaphore to signal when interrupt occurs
        //   callback   - Optional callback to run in ISR context (can be nullptr)
        //   context    - Context pointer passed to callback
        //   gotBase    - GOT base for PIC modules (0 = kernel code, no r9 setup needed)
        Result RegisterHandler(uint32_t irqNumber, 
                              BinarySemaphore* semaphore,
                              DPCCallback callback = nullptr,
                              void* context = nullptr,
                              uint32_t gotBase = 0);

        // Unregister a specific handler
        Result UnregisterHandler(uint32_t irqNumber, BinarySemaphore* semaphore);

        // Dispatch the interrupt - call this from your ISR
        // This will:
        // 1. Call all registered callbacks for this IRQ
        // 2. Signal all registered semaphores
        // Returns the number of tasks notified
        uint32_t DispatchInterrupt(uint32_t irqNumber);

        // Get statistics
        uint32_t GetHandlerCount(uint32_t irqNumber) const;
        uint32_t GetTotalSources() const;

    private:
        InterruptSource _sources[INTERRUPT_DPC_MAX_SOURCES];
        uint32_t _sourceCount;

        // Helper function to find a source by IRQ number
        InterruptSource* FindSource(uint32_t irqNumber);
        const InterruptSource* FindSource(uint32_t irqNumber) const;
    };

    // Global DPC dispatcher instance
    // You can use this directly or create your own instance
    extern InterruptDPC GlobalDPCDispatcher;
}

#endif /* INTERRUPT_DPC_HPP */
