/*
 * Module Debug Helper for Host Application
 * 
 * Add this code to your host application to enable easy module debugging
 * in MCUXpresso IDE.
 * 
 * This provides the debugger with information about where the module
 * was loaded in memory, enabling symbol loading.
 */

#ifndef MODULE_DEBUG_HELPER_H
#define MODULE_DEBUG_HELPER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Module debug information structure
 * This is examined by the GDB script to load symbols at the correct address
 */
typedef struct {
    const char* module_name;      // Name of the loaded module
    uint32_t module_base_addr;    // Base address where module was loaded
    uint32_t module_size;         // Total size of module in bytes
    uint32_t module_entry;        // Entry point address
    bool module_loaded;           // True when module is loaded
    uint32_t load_timestamp;      // Tick count when loaded (for debugging)
} ModuleDebugInfo;

/**
 * Global variable for debugger access
 * The GDB script reads this to get the module load address
 */
volatile ModuleDebugInfo g_module_debug = {
    .module_name = NULL,
    .module_base_addr = 0,
    .module_size = 0,
    .module_entry = 0,
    .module_loaded = false,
    .load_timestamp = 0
};

/**
 * Call this function after loading a module
 * It populates g_module_debug so the debugger can load symbols
 * 
 * @param name Module name (for reference)
 * @param base_addr Address where module code starts
 * @param size Total module size
 * @param entry Entry point address
 */
static inline void module_debug_register(const char* name, 
                                         uint32_t base_addr,
                                         uint32_t size,
                                         uint32_t entry)
{
    g_module_debug.module_name = name;
    g_module_debug.module_base_addr = base_addr;
    g_module_debug.module_size = size;
    g_module_debug.module_entry = entry;
    g_module_debug.module_loaded = true;
    g_module_debug.load_timestamp = 0; // Set to tick count if available
    
    // Optional: Set a breakpoint here in debugger
    // This is a good place to load module symbols
    __asm volatile("nop");
}

/**
 * Breakpoint helper function
 * Set a breakpoint here in your debugger, then when it hits,
 * load the module symbols using the GDB script
 */
static inline void module_debug_breakpoint(void)
{
    // Debugger will break here
    // At this point, g_module_debug is populated
    // Load symbols with: source mcuxpresso_load_module.gdb; load-module
    __asm volatile("bkpt #0");
}

#ifdef __cplusplus
}
#endif

#endif /* MODULE_DEBUG_HELPER_H */

/*
 * USAGE EXAMPLE:
 * 
 * In your host application after loading the module:
 * 
 * #include "module_debug_helper.h"
 * 
 * int main(void) {
 *     // ... CRTOS initialization ...
 *     
 *     // Load module
 *     extern uint8_t module_binary_start[];
 *     extern uint8_t module_binary_end[];
 *     
 *     CRTOS::Task::TaskHandle moduleHandle;
 *     CRTOS::Result result = CRTOS::Task::LPC55S69_Features::CreateTaskForBinModule(
 *         module_binary_start,
 *         "TestModule",
 *         nullptr,
 *         5,
 *         &moduleHandle
 *     );
 *     
 *     if (result == CRTOS::Result::RESULT_SUCCESS) {
 *         // Get the actual load address from CRTOS
 *         // You may need to expose this from CRTOS or calculate it
 *         uint32_t module_addr = (uint32_t)module_binary_start; // Or actual load address
 *         uint32_t module_size = module_binary_end - module_binary_start;
 *         
 *         // Register for debugging
 *         module_debug_register(
 *             "TestModule",
 *             module_addr,
 *             module_size,
 *             module_addr + 0x1F8  // Entry point offset from ProgramInfo
 *         );
 *         
 *         // Optional: Break here to load symbols
 *         module_debug_breakpoint();
 *     }
 *     
 *     // Start scheduler
 *     CRTOS::Scheduler::Start();
 *     
 *     while(1);
 * }
 * 
 * DEBUGGER WORKFLOW:
 * 
 * 1. Start debugging in MCUXpresso
 * 2. Program will hit the breakpoint in module_debug_breakpoint()
 * 3. Open Debugger Console (Window → Show View → Debugger Console)
 * 4. Load the GDB script:
 *    (gdb) source C:/Users/Guitarek/Desktop/modules/mcuxpresso_load_module.gdb
 * 5. Load module symbols:
 *    (gdb) load-module
 * 6. Set breakpoints in module:
 *    (gdb) break test_task1
 * 7. Continue execution:
 *    (gdb) continue
 * 8. Monitor tests:
 *    (gdb) check-tests
 */
