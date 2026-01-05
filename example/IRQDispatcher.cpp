/*
 * IRQDispatcher.cpp - CRTOS Interrupt Dispatcher for DPC Worker
 * Author: Arkadiusz Szlanta
 * Date: 29 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * This file overrides the weak IntDefaultHandler from startup code.
 * All unhandled interrupts are automatically routed through the DPC dispatcher
 * which enqueues work items for the DPC Worker task to process.
 *
 * How it works:
 * 1. startup_mimxrt1052.cpp defines DriverIRQHandler functions aliased to IntDefaultHandler
 * 2. When an interrupt fires and no specific handler exists, IntDefaultHandler is called
 * 3. We override IntDefaultHandler to dispatch to DPC Worker
 * 4. IPSR register is used to determine which IRQ triggered the handler
 */

#include "CRTOS.hpp"
#include "DPCWorker.hpp"
#include "HAL/Interrupt.hpp"  // For IRQ event queue
#include "MIMXRT1052.h"  // Provides __DSB() via core_cm7.h

// External callback pointer from startup
typedef void (*IntDefaultHandlerCallback_t)(void);
extern IntDefaultHandlerCallback_t g_IntDefaultHandlerCallback;

// Forward declaration of our handler
static void CRTOS_IntDefaultHandler(void);

// DPC Dispatcher callback table
// Maps IRQ numbers to user-registered callbacks
namespace
{
    struct IRQCallbackEntry
    {
        CRTOS::DPCWorkHandler handler;
        void* context;
        bool registered;
    };

    // Support up to 168 IRQs (MIMXRT1052 has 168 IRQ lines)
    #define MAX_IRQ_COUNT 168
    static IRQCallbackEntry s_irqCallbacks[MAX_IRQ_COUNT] = {0};
    static volatile bool s_dispatcherInitialized = false;
}

// Initialize the IRQ dispatcher
extern "C" void CRTOS_IRQDispatcher_Init(void)
{
    for (uint32_t i = 0; i < MAX_IRQ_COUNT; i++)
    {
        s_irqCallbacks[i].handler = nullptr;
        s_irqCallbacks[i].context = nullptr;
        s_irqCallbacks[i].registered = false;
    }
    
    // Register our handler with startup's IntDefaultHandler
    g_IntDefaultHandlerCallback = CRTOS_IntDefaultHandler;
    
    s_dispatcherInitialized = true;
}

// Register a callback for an IRQ
CRTOS::Result CRTOS_IRQDispatcher_RegisterCallback(uint32_t irqNumber, 
                                                     CRTOS::DPCWorkHandler handler, 
                                                     void* context)
{
    if (irqNumber >= MAX_IRQ_COUNT)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    s_irqCallbacks[irqNumber].handler = handler;
    s_irqCallbacks[irqNumber].context = context;
    s_irqCallbacks[irqNumber].registered = (handler != nullptr);

    return CRTOS::Result::RESULT_SUCCESS;
}

// Unregister a callback for an IRQ
CRTOS::Result CRTOS_IRQDispatcher_UnregisterCallback(uint32_t irqNumber)
{
    if (irqNumber >= MAX_IRQ_COUNT)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    s_irqCallbacks[irqNumber].handler = nullptr;
    s_irqCallbacks[irqNumber].context = nullptr;
    s_irqCallbacks[irqNumber].registered = false;

    return CRTOS::Result::RESULT_SUCCESS;
}

// Generic DPC dispatch function - called from ISR context
// This function is exported so custom IRQ handlers can use DPC dispatch
void CRTOS_DPC_DispatchIRQ(uint32_t irqNumber)
{
    if (!s_dispatcherInitialized || irqNumber >= MAX_IRQ_COUNT)
    {
        return;
    }

    // First, dispatch to the legacy GlobalDPCDispatcher (for backward compatibility)
    CRTOS::GlobalDPCDispatcher.DispatchInterrupt(irqNumber);

    // Post event to IRQ event queue for user-space tasks waiting on this IRQ
    CRTOS::HAL::GlobalInterruptController.GetEventQueue().PostEvent(irqNumber, 0, nullptr);

    // Then, if there's a registered handler, enqueue work for DPC Worker
    if (s_irqCallbacks[irqNumber].registered && s_irqCallbacks[irqNumber].handler != nullptr)
    {
        CRTOS::GlobalDPCWorker.EnqueueWork(
            irqNumber,
            s_irqCallbacks[irqNumber].handler,
            s_irqCallbacks[irqNumber].context,
            0
        );
    }

    __DSB();
}

//*****************************************************************************
// CRTOS_IntDefaultHandler - called from IntDefaultHandler in startup
// This handler is called for all interrupts that don't have a specific handler.
// We use IPSR register to determine which IRQ triggered the interrupt.
//
// IPSR contains the exception number:
//   0 = Thread mode (not in exception)
//   1-15 = System exceptions (NMI, HardFault, etc.)
//   16+ = External interrupts (IRQ 0 = exception 16, IRQ 1 = exception 17, etc.)
//
// IRQ number = IPSR - 16
//*****************************************************************************
static void CRTOS_IntDefaultHandler(void)
{
    // Read IPSR to get exception number
    uint32_t ipsr;
    __asm volatile ("MRS %0, IPSR" : "=r" (ipsr));
    
    // Convert exception number to IRQ number
    // External interrupts start at exception number 16
    if (ipsr >= 16)
    {
        uint32_t irqNumber = ipsr - 16;
        
        // Dispatch to DPC Worker
        CRTOS_DPC_DispatchIRQ(irqNumber);
    }
    // For system exceptions (1-15), we don't dispatch to DPC
    // They should have their own handlers (HardFault, NMI, etc.)
}
