/*
 * AppAPI.hpp - CRTOS Application API Header
 * Author: Arkadiusz Szlanta
 * Date: 05 Jan 2026
 *
 * Description:
 * Header file for user-space applications.
 * Include this to access CRTOS services from your application.
 */

#ifndef CRTOS_APP_API_HPP
#define CRTOS_APP_API_HPP

#include <cstdint>
#include <cstddef>

namespace CRTOS
{

/**
 * @brief Application API structure
 *
 * Provides system services to user applications.
 */
struct AppAPI
{
    // Version
    uint32_t version;
    
    // Task control
    void (*yield)(void);
    void (*delay)(uint32_t ms);
    uint32_t (*get_tick_count)(void);
    void (*exit)(int exitCode);
    
    // Console I/O
    int (*printf)(const char* fmt, ...);
    int (*getchar)(void);
    int (*putchar)(int c);
    
    // Memory
    void* (*malloc)(size_t size);
    void (*free)(void* ptr);
    
    // Driver access
    void* (*get_driver)(const char* name);
};

} // namespace CRTOS

// Get the application API - resolved by AppLoader
extern "C" const CRTOS::AppAPI* crtos_get_app_api(void) __attribute__((weak));

// Convenience macro to get API
#define APP_API() crtos_get_app_api()

// Helper macros for common operations
#define app_printf(...)     APP_API()->printf(__VA_ARGS__)
#define app_delay(ms)       APP_API()->delay(ms)
#define app_yield()         APP_API()->yield()
#define app_exit(code)      APP_API()->exit(code)
#define app_malloc(size)    APP_API()->malloc(size)
#define app_free(ptr)       APP_API()->free(ptr)

#endif // CRTOS_APP_API_HPP
