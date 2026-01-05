/*
 * KernelAPI.cpp - Kernel API Implementation
 * Author: Arkadiusz Szlanta
 * Date: 04 Jan 2026
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 */

#include "KernelAPI.hpp"
#include "CRTOS.hpp"
#include "Task.hpp"
#include "BinarySemaphore.hpp"
#include "DPCWorker.hpp"
#include "InterruptDPC.hpp"
#include "HeapAllocator.hpp"
#include "fsl_debug_console.h"
#include "../device/fsl_device_registers.h"
#include "ff.h"  // FatFS
#include <cstring>

// Get system time from CRTOS.cpp
extern uint32_t GetSystemTime(void);

namespace CRTOS
{

// ============================================================================
// Static Wrapper Functions
// ============================================================================

static void api_task_delay(uint32_t ms)
{
    Task::Delay(ms);
}

static void api_task_yield(void)
{
    Task::Yield();
}

static uint32_t api_get_tick_count(void)
{
    return GetSystemTime();
}

static void* api_semaphore_create(void)
{
    return new BinarySemaphore();
}

static void api_semaphore_delete(void* sem)
{
    delete static_cast<BinarySemaphore*>(sem);
}

static int api_semaphore_wait(void* sem, uint32_t timeout)
{
    if (sem == nullptr) return 0;
    return static_cast<BinarySemaphore*>(sem)->wait(timeout) == Result::RESULT_SUCCESS ? 1 : 0;
}

static void api_semaphore_signal(void* sem)
{
    if (sem != nullptr)
    {
        static_cast<BinarySemaphore*>(sem)->signal();
    }
}

static int api_dpc_register_irq(uint32_t irq)
{
    return GlobalDPCDispatcher.RegisterInterruptSource(static_cast<IRQn_Type>(irq)) == Result::RESULT_SUCCESS ? 1 : 0;
}

static int api_dpc_unregister_irq(uint32_t irq)
{
    GlobalDPCDispatcher.UnregisterInterruptSource(static_cast<IRQn_Type>(irq));
    return 1;
}

static int api_dpc_register_handler(uint32_t irq, void* sem, 
                                     void (*callback)(void*), void* ctx,
                                     uint32_t gotBase)
{
    return GlobalDPCDispatcher.RegisterHandler(
        static_cast<IRQn_Type>(irq),
        static_cast<BinarySemaphore*>(sem),
        callback, ctx, gotBase) == Result::RESULT_SUCCESS ? 1 : 0;
}

static int api_dpc_unregister_handler(uint32_t irq, void* sem)
{
    GlobalDPCDispatcher.UnregisterHandler(
        static_cast<IRQn_Type>(irq),
        static_cast<BinarySemaphore*>(sem));
    return 1;
}

static void api_nvic_enable_irq(uint32_t irq)
{
    NVIC_EnableIRQ(static_cast<IRQn_Type>(irq));
}

static void api_nvic_disable_irq(uint32_t irq)
{
    NVIC_DisableIRQ(static_cast<IRQn_Type>(irq));
}

static void api_nvic_set_priority(uint32_t irq, uint32_t prio)
{
    NVIC_SetPriority(static_cast<IRQn_Type>(irq), prio);
}

static void* api_mem_alloc(uint32_t size)
{
    return HeapAllocator::Allocate(size);
}

static void api_mem_free(void* ptr)
{
    HeapAllocator::Free(ptr);
}

// ============================================================================
// Filesystem API Wrappers
// ============================================================================

static void* api_file_open(const char* path, uint8_t mode)
{
    FIL* file = static_cast<FIL*>(HeapAllocator::Allocate(sizeof(FIL)));
    if (file == nullptr) return nullptr;
    
    FRESULT res = f_open(file, path, mode);
    if (res != FR_OK)
    {
        HeapAllocator::Free(file);
        return nullptr;
    }
    return file;
}

static int api_file_close(void* file)
{
    if (file == nullptr) return -1;
    FRESULT res = f_close(static_cast<FIL*>(file));
    HeapAllocator::Free(file);
    return (res == FR_OK) ? 0 : -1;
}

static int api_file_read(void* file, void* buffer, uint32_t size, uint32_t* bytesRead)
{
    if (file == nullptr || buffer == nullptr) return -1;
    UINT br = 0;
    FRESULT res = f_read(static_cast<FIL*>(file), buffer, size, &br);
    if (bytesRead) *bytesRead = br;
    
    // SD card uses DMA which writes directly to memory bypassing cache.
    // Invalidate D-cache for the buffer to ensure CPU reads fresh data.
    if (res == FR_OK && br > 0)
    {
        // Align address down and size up to cache line boundary (32 bytes)
        uint32_t alignedAddr = reinterpret_cast<uint32_t>(buffer) & ~31UL;
        uint32_t endAddr = reinterpret_cast<uint32_t>(buffer) + br;
        uint32_t alignedSize = ((endAddr + 31) & ~31UL) - alignedAddr;
        SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(alignedAddr), static_cast<int32_t>(alignedSize));
    }
    
    return (res == FR_OK) ? 0 : -1;
}

static int api_file_size(void* file)
{
    if (file == nullptr) return -1;
    return static_cast<int>(f_size(static_cast<FIL*>(file)));
}

// ============================================================================
// Kernel API Singleton
// ============================================================================

static KernelAPI s_kernelAPI;
static bool s_apiInitialized = false;

void InitKernelAPI(void)
{
    if (s_apiInitialized) return;
    
    s_kernelAPI.version = KERNEL_API_VERSION;
    
    // Task functions
    s_kernelAPI.task_delay      = api_task_delay;
    s_kernelAPI.task_yield      = api_task_yield;
    s_kernelAPI.get_tick_count  = api_get_tick_count;
    
    // Semaphore functions
    s_kernelAPI.semaphore_create = api_semaphore_create;
    s_kernelAPI.semaphore_delete = api_semaphore_delete;
    s_kernelAPI.semaphore_wait   = api_semaphore_wait;
    s_kernelAPI.semaphore_signal = api_semaphore_signal;
    
    // DPC functions
    s_kernelAPI.dpc_register_irq       = api_dpc_register_irq;
    s_kernelAPI.dpc_unregister_irq     = api_dpc_unregister_irq;
    s_kernelAPI.dpc_register_handler   = api_dpc_register_handler;
    s_kernelAPI.dpc_unregister_handler = api_dpc_unregister_handler;
    
    // NVIC functions
    s_kernelAPI.nvic_enable_irq   = api_nvic_enable_irq;
    s_kernelAPI.nvic_disable_irq  = api_nvic_disable_irq;
    s_kernelAPI.nvic_set_priority = api_nvic_set_priority;
    
    // Memory functions
    s_kernelAPI.mem_alloc = api_mem_alloc;
    s_kernelAPI.mem_free  = api_mem_free;
    s_kernelAPI.memset    = ::memset;
    s_kernelAPI.memcpy    = ::memcpy;
    
    // Debug functions
    s_kernelAPI.printf = PRINTF;
    
    // Filesystem functions
    s_kernelAPI.file_open  = api_file_open;
    s_kernelAPI.file_close = api_file_close;
    s_kernelAPI.file_read  = api_file_read;
    s_kernelAPI.file_size  = api_file_size;
    
    s_apiInitialized = true;
}

extern "C" const KernelAPI* crtos_get_kernel_api(void)
{
    return &s_kernelAPI;
}

} // namespace CRTOS
