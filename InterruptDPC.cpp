/*
 * InterruptDPC.cpp - CRTOS Interrupt DPC Dispatcher Implementation
 * Author: Arkadiusz Szlanta
 * Date: 26 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 */

#include "InterruptDPC.hpp"
#include "CRTOS.hpp"
#include <cstring>

// External functions for interrupt control
extern "C" uint32_t getInterruptMask(void);
extern "C" void setInterruptMask(uint32_t mask);

namespace CRTOS
{
    // Global DPC dispatcher instance
    InterruptDPC GlobalDPCDispatcher;

    InterruptDPC::InterruptDPC() : _sourceCount(0)
    {
        // Initialize all sources as unregistered
        for (uint32_t i = 0; i < INTERRUPT_DPC_MAX_SOURCES; i++)
        {
            _sources[i].registered = false;
            _sources[i].handlerCount = 0;
            _sources[i].irqNumber = 0;
            
            for (uint32_t j = 0; j < INTERRUPT_DPC_MAX_HANDLERS_PER_IRQ; j++)
            {
                _sources[i].handlers[j].semaphore = nullptr;
                _sources[i].handlers[j].callback = nullptr;
                _sources[i].handlers[j].context = nullptr;
                _sources[i].handlers[j].active = false;
            }
        }
    }

    InterruptSource* InterruptDPC::FindSource(uint32_t irqNumber)
    {
        for (uint32_t i = 0; i < INTERRUPT_DPC_MAX_SOURCES; i++)
        {
            if (_sources[i].registered && _sources[i].irqNumber == irqNumber)
            {
                return &_sources[i];
            }
        }
        return nullptr;
    }

    const InterruptSource* InterruptDPC::FindSource(uint32_t irqNumber) const
    {
        for (uint32_t i = 0; i < INTERRUPT_DPC_MAX_SOURCES; i++)
        {
            if (_sources[i].registered && _sources[i].irqNumber == irqNumber)
            {
                return &_sources[i];
            }
        }
        return nullptr;
    }

    Result InterruptDPC::RegisterInterruptSource(uint32_t irqNumber)
    {
        uint32_t mask = getInterruptMask();

        // Check if already registered
        if (FindSource(irqNumber) != nullptr)
        {
            setInterruptMask(mask);
            return Result::RESULT_SUCCESS; // Already registered, not an error
        }

        // Find a free slot
        if (_sourceCount >= INTERRUPT_DPC_MAX_SOURCES)
        {
            setInterruptMask(mask);
            return Result::RESULT_NO_MEMORY;
        }

        // Find first unregistered slot
        for (uint32_t i = 0; i < INTERRUPT_DPC_MAX_SOURCES; i++)
        {
            if (!_sources[i].registered)
            {
                _sources[i].irqNumber = irqNumber;
                _sources[i].registered = true;
                _sources[i].handlerCount = 0;
                _sourceCount++;
                
                setInterruptMask(mask);
                return Result::RESULT_SUCCESS;
            }
        }

        setInterruptMask(mask);
        return Result::RESULT_NO_MEMORY;
    }

    Result InterruptDPC::UnregisterInterruptSource(uint32_t irqNumber)
    {
        uint32_t mask = getInterruptMask();

        InterruptSource* source = FindSource(irqNumber);
        if (source == nullptr)
        {
            setInterruptMask(mask);
            return Result::RESULT_TASK_NOT_FOUND;
        }

        // Clear all handlers
        for (uint32_t i = 0; i < INTERRUPT_DPC_MAX_HANDLERS_PER_IRQ; i++)
        {
            source->handlers[i].semaphore = nullptr;
            source->handlers[i].callback = nullptr;
            source->handlers[i].context = nullptr;
            source->handlers[i].active = false;
        }

        source->handlerCount = 0;
        source->registered = false;
        _sourceCount--;

        setInterruptMask(mask);
        return Result::RESULT_SUCCESS;
    }

    Result InterruptDPC::RegisterHandler(uint32_t irqNumber, 
                                        BinarySemaphore* semaphore,
                                        DPCCallback callback,
                                        void* context)
    {
        if (semaphore == nullptr)
        {
            return Result::RESULT_BAD_PARAMETER;
        }

        uint32_t mask = getInterruptMask();

        // Find the interrupt source
        InterruptSource* source = FindSource(irqNumber);
        if (source == nullptr)
        {
            setInterruptMask(mask);
            return Result::RESULT_TASK_NOT_FOUND; // IRQ not registered
        }

        // Check if this semaphore is already registered
        for (uint32_t i = 0; i < INTERRUPT_DPC_MAX_HANDLERS_PER_IRQ; i++)
        {
            if (source->handlers[i].active && 
                source->handlers[i].semaphore == semaphore)
            {
                // Already registered, update callback and context
                source->handlers[i].callback = callback;
                source->handlers[i].context = context;
                setInterruptMask(mask);
                return Result::RESULT_SUCCESS;
            }
        }

        // Find a free handler slot
        for (uint32_t i = 0; i < INTERRUPT_DPC_MAX_HANDLERS_PER_IRQ; i++)
        {
            if (!source->handlers[i].active)
            {
                source->handlers[i].semaphore = semaphore;
                source->handlers[i].callback = callback;
                source->handlers[i].context = context;
                source->handlers[i].active = true;
                source->handlerCount++;
                
                setInterruptMask(mask);
                return Result::RESULT_SUCCESS;
            }
        }

        setInterruptMask(mask);
        return Result::RESULT_NO_MEMORY; // No free handler slots
    }

    Result InterruptDPC::UnregisterHandler(uint32_t irqNumber, BinarySemaphore* semaphore)
    {
        if (semaphore == nullptr)
        {
            return Result::RESULT_BAD_PARAMETER;
        }

        uint32_t mask = getInterruptMask();

        InterruptSource* source = FindSource(irqNumber);
        if (source == nullptr)
        {
            setInterruptMask(mask);
            return Result::RESULT_TASK_NOT_FOUND;
        }

        // Find and remove the handler
        for (uint32_t i = 0; i < INTERRUPT_DPC_MAX_HANDLERS_PER_IRQ; i++)
        {
            if (source->handlers[i].active && 
                source->handlers[i].semaphore == semaphore)
            {
                source->handlers[i].semaphore = nullptr;
                source->handlers[i].callback = nullptr;
                source->handlers[i].context = nullptr;
                source->handlers[i].active = false;
                source->handlerCount--;
                
                setInterruptMask(mask);
                return Result::RESULT_SUCCESS;
            }
        }

        setInterruptMask(mask);
        return Result::RESULT_TASK_NOT_FOUND; // Handler not found
    }

    uint32_t InterruptDPC::DispatchInterrupt(uint32_t irqNumber)
    {
        // Note: This function is called from ISR context
        // No need to disable interrupts here as we're already in an ISR
        
        InterruptSource* source = FindSource(irqNumber);
        if (source == nullptr)
        {
            return 0; // No handlers registered
        }

        uint32_t notifiedCount = 0;

        // Process all active handlers
        for (uint32_t i = 0; i < INTERRUPT_DPC_MAX_HANDLERS_PER_IRQ; i++)
        {
            if (source->handlers[i].active)
            {
                // Call the optional callback first (in ISR context)
                if (source->handlers[i].callback != nullptr)
                {
                    source->handlers[i].callback(source->handlers[i].context);
                }

                // Signal the semaphore to wake up the waiting task
                if (source->handlers[i].semaphore != nullptr)
                {
                    source->handlers[i].semaphore->signal();
                    notifiedCount++;
                }
            }
        }

        return notifiedCount;
    }

    uint32_t InterruptDPC::GetHandlerCount(uint32_t irqNumber) const
    {
        const InterruptSource* source = FindSource(irqNumber);
        if (source == nullptr)
        {
            return 0;
        }
        return source->handlerCount;
    }

    uint32_t InterruptDPC::GetTotalSources() const
    {
        return _sourceCount;
    }

} // namespace CRTOS
