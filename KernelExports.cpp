/*
 * KernelExports.cpp - Kernel Symbol Exports for Dynamic Modules
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
 * This file exports kernel functions with C linkage for use by
 * dynamically loaded modules. The ModuleLoader registers these
 * symbols so modules can call kernel functions.
 */

#include "CRTOS.hpp"
#include "Task.hpp"
#include "BinarySemaphore.hpp"
#include "DPCWorker.hpp"
#include "InterruptDPC.hpp"
#include "ModuleLoader.hpp"
#include "HeapAllocator.hpp"
#include "HAL/HAL_UART.hpp"
#include <cstring>
#include <cstdlib>

// CMSIS for NVIC
#include "../device/fsl_device_registers.h"

// Get system time from CRTOS.cpp (defined in global namespace, C++ linkage)
extern uint32_t GetSystemTime(void);

namespace CRTOS
{

// ============================================================================
// Task Functions (C wrapper)
// ============================================================================

extern "C" void crtos_task_delay(uint32_t ms)
{
    Task::Delay(ms);
}

extern "C" void crtos_task_yield(void)
{
    Task::Yield();
}

extern "C" uint32_t crtos_get_tick_count(void)
{
    return ::GetSystemTime();
}

// ============================================================================
// Semaphore Functions (C wrapper)
// ============================================================================

extern "C" void* crtos_semaphore_create(void)
{
    return new BinarySemaphore();
}

extern "C" void crtos_semaphore_delete(void* sem)
{
    if (sem != nullptr)
    {
        delete static_cast<BinarySemaphore*>(sem);
    }
}

extern "C" int crtos_semaphore_wait(void* sem, uint32_t timeout)
{
    if (sem == nullptr)
    {
        return -1;
    }
    Result result = static_cast<BinarySemaphore*>(sem)->wait(timeout);
    return (result == Result::RESULT_SUCCESS) ? 0 : -1;
}

extern "C" void crtos_semaphore_signal(void* sem)
{
    if (sem != nullptr)
    {
        static_cast<BinarySemaphore*>(sem)->signal();
    }
}

// ============================================================================
// DPC Functions (C wrapper)
// ============================================================================

extern "C" int crtos_dpc_enqueue(uint32_t irq, void (*handler)(uint32_t, void*, uint32_t), 
                                  void* context, uint32_t param)
{
    Result result = DPC::EnqueueWork(irq, handler, context, param);
    return (result == Result::RESULT_SUCCESS) ? 0 : -1;
}

extern "C" int crtos_dpc_register_irq(uint32_t irq)
{
    Result result = GlobalDPCDispatcher.RegisterInterruptSource(irq);
    return (result == Result::RESULT_SUCCESS) ? 0 : -1;
}

extern "C" int crtos_dpc_unregister_irq(uint32_t irq)
{
    Result result = GlobalDPCDispatcher.UnregisterInterruptSource(irq);
    return (result == Result::RESULT_SUCCESS) ? 0 : -1;
}

extern "C" int crtos_dpc_register_handler(uint32_t irq, void* sem, 
                                          void (*callback)(void*), void* ctx)
{
    Result result = GlobalDPCDispatcher.RegisterHandler(irq, 
        static_cast<BinarySemaphore*>(sem), 
        reinterpret_cast<DPCCallback>(callback), ctx);
    return (result == Result::RESULT_SUCCESS) ? 0 : -1;
}

extern "C" int crtos_dpc_unregister_handler(uint32_t irq, void* sem)
{
    Result result = GlobalDPCDispatcher.UnregisterHandler(irq, 
        static_cast<BinarySemaphore*>(sem));
    return (result == Result::RESULT_SUCCESS) ? 0 : -1;
}

// ============================================================================
// NVIC Functions (C wrapper for CMSIS)
// ============================================================================

extern "C" void crtos_nvic_enable_irq(uint32_t irq)
{
    NVIC_EnableIRQ(static_cast<IRQn_Type>(irq));
}

extern "C" void crtos_nvic_disable_irq(uint32_t irq)
{
    NVIC_DisableIRQ(static_cast<IRQn_Type>(irq));
}

extern "C" void crtos_nvic_set_priority(uint32_t irq, uint32_t priority)
{
    NVIC_SetPriority(static_cast<IRQn_Type>(irq), priority);
}

// ============================================================================
// Memory Functions (exported from libc)
// ============================================================================

// Memory allocation wrappers using HeapAllocator
extern "C" void* crtos_malloc(uint32_t size)
{
    return HeapAllocator::Allocate(size);
}

extern "C" void crtos_free(void* ptr)
{
    HeapAllocator::Free(ptr);
}

// C++ new/delete operators for modules
extern "C" void* __wrap_new(unsigned int size)
{
    return HeapAllocator::Allocate(size);
}

extern "C" void __wrap_delete(void* ptr, unsigned int size)
{
    (void)size;  // sized delete ignores size
    HeapAllocator::Free(ptr);
}

extern "C" void __wrap_delete_void(void* ptr)
{
    HeapAllocator::Free(ptr);
}

// ============================================================================
// Symbol Registration
// ============================================================================

void RegisterKernelSymbols()
{
    ModuleLoader& loader = ModuleLoader::getInstance();
    
    // Task functions
    loader.registerSymbol("crtos_task_delay", (void*)&crtos_task_delay);
    loader.registerSymbol("crtos_task_yield", (void*)&crtos_task_yield);
    loader.registerSymbol("crtos_get_tick_count", (void*)&crtos_get_tick_count);
    
    // Semaphore functions
    loader.registerSymbol("crtos_semaphore_create", (void*)&crtos_semaphore_create);
    loader.registerSymbol("crtos_semaphore_delete", (void*)&crtos_semaphore_delete);
    loader.registerSymbol("crtos_semaphore_wait", (void*)&crtos_semaphore_wait);
    loader.registerSymbol("crtos_semaphore_signal", (void*)&crtos_semaphore_signal);
    
    // DPC functions
    loader.registerSymbol("crtos_dpc_enqueue", (void*)&crtos_dpc_enqueue);
    loader.registerSymbol("crtos_dpc_register_irq", (void*)&crtos_dpc_register_irq);
    loader.registerSymbol("crtos_dpc_unregister_irq", (void*)&crtos_dpc_unregister_irq);
    loader.registerSymbol("crtos_dpc_register_handler", (void*)&crtos_dpc_register_handler);
    loader.registerSymbol("crtos_dpc_unregister_handler", (void*)&crtos_dpc_unregister_handler);
    
    // NVIC functions
    loader.registerSymbol("crtos_nvic_enable_irq", (void*)&crtos_nvic_enable_irq);
    loader.registerSymbol("crtos_nvic_disable_irq", (void*)&crtos_nvic_disable_irq);
    loader.registerSymbol("crtos_nvic_set_priority", (void*)&crtos_nvic_set_priority);
    
    // HAL UART functions (multi-instance API)
    loader.registerSymbol("hal_uart_get_default_config", (void*)&hal_uart_get_default_config);
    loader.registerSymbol("hal_uart_init_instance", (void*)&hal_uart_init_instance);
    loader.registerSymbol("hal_uart_deinit_instance", (void*)&hal_uart_deinit_instance);
    loader.registerSymbol("hal_uart_is_initialized_instance", (void*)&hal_uart_is_initialized_instance);
    loader.registerSymbol("hal_uart_write_byte_instance", (void*)&hal_uart_write_byte_instance);
    loader.registerSymbol("hal_uart_write_instance", (void*)&hal_uart_write_instance);
    loader.registerSymbol("hal_uart_read_byte_instance", (void*)&hal_uart_read_byte_instance);
    loader.registerSymbol("hal_uart_flush_tx_instance", (void*)&hal_uart_flush_tx_instance);
    loader.registerSymbol("hal_uart_get_status", (void*)&hal_uart_get_status);
    loader.registerSymbol("hal_uart_clear_errors", (void*)&hal_uart_clear_errors);
    loader.registerSymbol("hal_uart_get_irq_number", (void*)&hal_uart_get_irq_number);
    loader.registerSymbol("hal_uart_enable_rx_irq", (void*)&hal_uart_enable_rx_irq);
    loader.registerSymbol("hal_uart_disable_rx_irq", (void*)&hal_uart_disable_rx_irq);
    loader.registerSymbol("hal_uart_enable_tx_irq", (void*)&hal_uart_enable_tx_irq);
    loader.registerSymbol("hal_uart_disable_tx_irq", (void*)&hal_uart_disable_tx_irq);
    
    // HAL UART legacy API (backwards compatibility)
    loader.registerSymbol("hal_uart_init", (void*)&hal_uart_init);
    loader.registerSymbol("hal_uart_deinit", (void*)&hal_uart_deinit);
    loader.registerSymbol("hal_uart_write_byte", (void*)&hal_uart_write_byte);
    loader.registerSymbol("hal_uart_write", (void*)&hal_uart_write);
    loader.registerSymbol("hal_uart_read_byte", (void*)&hal_uart_read_byte);
    loader.registerSymbol("hal_uart_is_initialized", (void*)&hal_uart_is_initialized);
    loader.registerSymbol("hal_uart_flush_tx", (void*)&hal_uart_flush_tx);
    
    // Memory allocation (via HeapAllocator)
    loader.registerSymbol("malloc", (void*)&crtos_malloc);
    loader.registerSymbol("free", (void*)&crtos_free);
    loader.registerSymbol("memset", (void*)&memset);
    loader.registerSymbol("memcpy", (void*)&memcpy);
    loader.registerSymbol("strlen", (void*)&strlen);
    loader.registerSymbol("strcmp", (void*)&strcmp);
    loader.registerSymbol("strcpy", (void*)&strcpy);
    loader.registerSymbol("strncpy", (void*)&strncpy);
    
    // C++ new/delete operators (mangled names)
    loader.registerSymbol("_Znwj", (void*)&__wrap_new);              // operator new(unsigned int)
    loader.registerSymbol("_ZdlPv", (void*)&__wrap_delete_void);     // operator delete(void*)
    loader.registerSymbol("_ZdlPvj", (void*)&__wrap_delete);         // operator delete(void*, unsigned int)
}

} // namespace CRTOS
