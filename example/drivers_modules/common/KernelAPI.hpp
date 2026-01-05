/*
 * KernelAPI.hpp - Kernel API Structure for Dynamic Modules
 * Author: Arkadiusz Szlanta
 * Date: 04 Jan 2026
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * Defines the kernel API structure that is passed to dynamically loaded
 * modules. This provides a clean interface without requiring weak symbols
 * or excessive extern declarations.
 */

#ifndef KERNEL_API_HPP
#define KERNEL_API_HPP

#include <cstdint>
#include <cstddef>

namespace CRTOS
{

/**
 * @brief Kernel API version
 * Increment when API changes to detect incompatibility
 */
constexpr uint32_t KERNEL_API_VERSION = 1;

/**
 * @brief Kernel API structure
 * 
 * Contains pointers to all kernel functions available to modules.
 * Modules receive this structure during initialization.
 */
struct KernelAPI
{
    // API version for compatibility checking
    uint32_t version;
    
    // ========================================================================
    // Task Functions
    // ========================================================================
    void     (*task_delay)(uint32_t ms);
    void     (*task_yield)(void);
    uint32_t (*get_tick_count)(void);
    
    // ========================================================================
    // Semaphore Functions
    // ========================================================================
    void* (*semaphore_create)(void);
    void  (*semaphore_delete)(void* sem);
    int   (*semaphore_wait)(void* sem, uint32_t timeout);
    void  (*semaphore_signal)(void* sem);
    
    // ========================================================================
    // DPC Functions
    // ========================================================================
    int (*dpc_register_irq)(uint32_t irq);
    int (*dpc_unregister_irq)(uint32_t irq);
    int (*dpc_register_handler)(uint32_t irq, void* sem, 
                                void (*callback)(void*), void* ctx,
                                uint32_t gotBase);
    int (*dpc_unregister_handler)(uint32_t irq, void* sem);
    
    // ========================================================================
    // NVIC Functions
    // ========================================================================
    void (*nvic_enable_irq)(uint32_t irq);
    void (*nvic_disable_irq)(uint32_t irq);
    void (*nvic_set_priority)(uint32_t irq, uint32_t prio);
    
    // ========================================================================
    // Memory Functions
    // ========================================================================
    void* (*mem_alloc)(uint32_t size);
    void  (*mem_free)(void* ptr);
    void* (*memset)(void* s, int c, size_t n);
    void* (*memcpy)(void* dest, const void* src, size_t n);
    
    // ========================================================================
    // Debug Functions (optional)
    // ========================================================================
    int (*printf)(const char* fmt, ...);
    
    // ========================================================================
    // Filesystem Functions (for configuration loading)
    // ========================================================================
    void* (*file_open)(const char* path, uint8_t mode);  // Returns FIL* or nullptr
    int   (*file_close)(void* file);
    int   (*file_read)(void* file, void* buffer, uint32_t size, uint32_t* bytesRead);
    int   (*file_size)(void* file);
};

/**
 * @brief Get the kernel API
 * 
 * Modules call this to get a pointer to the kernel API structure.
 * This function is always exported as a symbol.
 * 
 * @return Pointer to the kernel API structure (never null)
 */
extern "C" const KernelAPI* crtos_get_kernel_api(void);

} // namespace CRTOS

#endif // KERNEL_API_HPP
