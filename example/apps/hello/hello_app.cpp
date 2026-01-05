/*
 * hello_app.cpp - Simple Hello World Application
 * Author: Arkadiusz Szlanta
 * Date: 05 Jan 2026
 *
 * Description:
 * Example CRTOS user application demonstrating:
 * - Application entry point (app_main)
 * - Using AppAPI for system services
 * - Running as a task in USER mode
 */

#include "../common/AppAPI.hpp"

// Application entry point
// This function is called when the application is started
// It runs in USER (unprivileged) mode
extern "C" int app_main(void* args)
{
    const CRTOS::AppAPI* api = crtos_get_app_api();
    
    if (api == nullptr)
    {
        // No API available - fatal error
        return -1;
    }
    
    // Print hello message
    api->printf("=================================\r\n");
    api->printf("  Hello from CRTOS Application!\r\n");
    api->printf("=================================\r\n");
    api->printf("API version: 0x%08lX\r\n", api->version);
    api->printf("Args pointer: %p\r\n", args);
    
    // Demonstrate task control
    uint32_t startTick = api->get_tick_count();
    api->printf("Start tick: %lu\r\n", startTick);
    
    // Main application loop
    int counter = 0;
    while (counter < 10)
    {
        uint32_t currentTick = api->get_tick_count();
        api->printf("[%lu] Hello loop iteration %d\r\n", currentTick, counter);
        
        // Delay 1 second
        api->delay(1000);
        
        counter++;
    }
    
    // Calculate elapsed time
    uint32_t endTick = api->get_tick_count();
    api->printf("End tick: %lu (elapsed: %lu ms)\r\n", endTick, endTick - startTick);
    
    api->printf("Application finished successfully!\r\n");
    
    // Return success
    return 0;
}
