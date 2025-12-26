/**
 * @file dpc_test_module.c
 * @brief DPC (Deferred Procedure Call) Dispatcher Test Module
 *
 * Tests and monitors the DPC interrupt dispatcher:
 * - Registers for DPC interrupt notifications
 * - Tracks interrupt statistics
 * - Measures DPC latency and performance
 * - Tests ISR callbacks and semaphore notifications
 */

#include "module_descriptor.h"
#include "program_info.h"
#include <stdint.h>
#include <kernel.h>
#include <stdio.h>
#include <string.h>

/* =============================================================================
 * Configuration
 * ===========================================================================*/
#define DPC_TEST_IRQ 52  // Reserved68_IRQn (actual IRQ number)
#define STATS_INTERVAL_MS 2000u // Print stats every 2 seconds

#ifndef MODULE_NAME
#define MODULE_NAME "DPC Module Test"
#endif

#ifndef MODULE_VER_MAJOR
#define MODULE_VER_MAJOR 1
#endif

#ifndef MODULE_VER_MINOR
#define MODULE_VER_MINOR 0
#endif

#ifndef MODULE_VER_PATCH
#define MODULE_VER_PATCH 0
#endif

#ifndef API_VERSION
#define API_VERSION 0x0001
#endif

/* =============================================================================
 * Module State
 * ===========================================================================*/
typedef struct {
    uint32_t test_iterations;
    uint32_t irq_received;
    uint32_t sem_wait_success;
    uint32_t sem_wait_timeout;
    uint32_t errors;
    uint32_t last_irq_time;
    uint32_t min_latency;
    uint32_t max_latency;
    uint64_t total_latency;
    uint32_t running;
    uint32_t verbose;
    uint32_t stats_print_counter;
} DPCTestState;

static DPCTestState state = {0};
static SemaphoreHandle dpc_semaphore = NULL;
static uint32_t last_irq_timestamp = 0;

/* =============================================================================
 * DPC Callback - Runs in ISR context
 * ===========================================================================*/

/**
 * DPC callback - called from ISR when interrupt fires
 * Keep this minimal - just capture timestamp
 */
static void dpc_isr_callback(void *context)
{
    (void)context;
    // This runs in ISR context - keep it fast!
    // Just capture a timestamp or set a flag
    last_irq_timestamp++;
}

/* =============================================================================
 * Helper Functions
 * ===========================================================================*/

/**
 * Print DPC statistics
 */
static void print_stats(void)
{
    uint32_t avg_latency = 0;
    if (state.irq_received > 0)
    {
        avg_latency = (uint32_t)(state.total_latency / state.irq_received);
    }
    
    printf("\r\n");
    printf("================================================================\r\n");
    printf("           DPC Test Module Statistics\r\n");
    printf("================================================================\r\n");
    printf("  Test Iterations:       %lu\r\n", state.test_iterations);
    printf("  IRQs Received:         %lu\r\n", state.irq_received);
    printf("  Semaphore Waits OK:    %lu\r\n", state.sem_wait_success);
    printf("  Semaphore Timeouts:    %lu\r\n", state.sem_wait_timeout);
    printf("  Errors:                %lu\r\n", state.errors);
    printf("  Latency (min/avg/max): %lu / %lu / %lu cycles\r\n",
           state.min_latency, avg_latency, state.max_latency);
    printf("  Status:                %s\r\n", state.running ? "RUNNING" : "STOPPED");
    printf("================================================================\r\n");
}

/**
 * Reset statistics
 */
static void reset_stats(void)
{
    state.test_iterations = 0;
    state.irq_received = 0;
    state.sem_wait_success = 0;
    state.sem_wait_timeout = 0;
    state.errors = 0;
    state.last_irq_time = 0;
    state.min_latency = 0xFFFFFFFF;
    state.max_latency = 0;
    state.total_latency = 0;
}

/* =============================================================================
 * Main Module Entry Point
 * ===========================================================================*/

__attribute__((section(".entry"))) int module_entry(uint32_t reason, void *ctx)
{
    (void)reason;
    (void)ctx;

    printf("\r\n");
    printf("================================================================\r\n");
    printf("         DPC Interrupt Dispatcher Test Module\r\n");
    printf("================================================================\r\n");
    printf("This module tests the DPC dispatcher functionality\r\n");
    printf("\r\n");
    printf("Features:\r\n");
    printf("  - Creates a semaphore for DPC notifications\r\n");
    printf("  - Registers with DPC dispatcher for IRQ %u\r\n", DPC_TEST_IRQ);
    printf("  - Waits on semaphore for interrupt notifications\r\n");
    printf("  - Executes callback when interrupt fires\r\n");
    printf("  - Tracks interrupt delivery statistics\r\n");
    printf("================================================================\r\n");
    
    // Initialize state
    state.running = 1;
    state.verbose = 1;  // Disable verbose logging
    reset_stats();
    state.min_latency = 0xFFFFFFFF;
    
    printf("[DPC-Test] Module initialized\r\n");
    
    // Create semaphore for DPC notifications
    dpc_semaphore = module_semaphore_create();
    if (dpc_semaphore == NULL)
    {
        printf("[DPC-Test] ERROR: Failed to create semaphore!\r\n");
        return -1;
    }
    printf("[DPC-Test] Semaphore created at 0x%08lX\r\n", (uint32_t)dpc_semaphore);
    
    // Register with DPC dispatcher
    int32_t result = module_dpc_register_handler(DPC_TEST_IRQ, dpc_semaphore, dpc_isr_callback, NULL);
    if (result != 0)
    {
        printf("[DPC-Test] ERROR: Failed to register DPC handler!\r\n");
        module_semaphore_delete(dpc_semaphore);
        return -1;
    }
    printf("[DPC-Test] Registered with DPC dispatcher for IRQ %u\r\n", DPC_TEST_IRQ);
    
    // Register task for IRQ (makes it visible in htop)
    RegisterCurrentTaskIRQ(DPC_TEST_IRQ);
    printf("[DPC-Test] Task registered for IRQ %u (visible in htop)\r\n", DPC_TEST_IRQ);
    
    printf("[DPC-Test] Entering main loop - waiting for interrupts...\r\n\r\n");
    
    // Module main loop - wait for DPC notifications
    while (state.running)
    {
        // Wait for semaphore (signaled by DPC dispatcher when IRQ fires)
        int32_t wait_result = module_semaphore_wait(dpc_semaphore, 500); // 500ms timeout for status updates
        
        if (wait_result == 0)
        {
            // Semaphore was signaled - interrupt occurred!
            state.irq_received++;
            state.sem_wait_success++;
            
            // Record timestamp
            uint32_t current_time = last_irq_timestamp;
            
            // Calculate latency (cycles from interrupt to task wakeup)
            uint32_t latency = current_time - state.last_irq_time;
            
            if (state.irq_received == 1)
            {
                // First interrupt - no valid latency yet
                latency = 0;
            }
            else
            {
                if (latency < state.min_latency)
                    state.min_latency = latency;
                if (latency > state.max_latency)
                    state.max_latency = latency;
                
                state.total_latency += latency;
            }
            
            state.last_irq_time = current_time;
            
            // Execute DPC callback in task context
            // This is where you'd do actual work (not in ISR)
            // For this test, we just track statistics
        }
        else
        {
            // Timeout - no interrupt received
            state.sem_wait_timeout++;
        }
        
        state.test_iterations++;
        
        // Periodically print statistics
        state.stats_print_counter++;
        if (state.stats_print_counter >= 200)  // Every ~5seconds (200 iterations with 10ms IRQs)
        {
            state.stats_print_counter = 0;
            
            if (state.verbose)
            {
                printf("[DPC-Test] IRQs: %lu, Callbacks: %lu, Timeouts: %lu, Errors: %lu\r\n", 
                       state.irq_received, state.sem_wait_success, state.sem_wait_timeout, state.errors);
                
                if (state.irq_received > 1)
                {
                    uint32_t avg = (uint32_t)(state.total_latency / (state.irq_received - 1));
                    printf("  Latency: min=%lu, avg=%lu, max=%lu cycles\r\n",
                           state.min_latency, avg, state.max_latency);
                }
            }
            
            // Print summary stats periodically
            print_stats();
        }
    }
    
    printf("[DPC-Test] Module stopping\r\n");
    print_stats();
    
    // Cleanup
    module_dpc_unregister_handler(DPC_TEST_IRQ, dpc_semaphore);
    UnregisterCurrentTaskIRQ();
    module_semaphore_delete(dpc_semaphore);
    
    return 0;
}

/* =============================================================================
 * Module Descriptor (linker symbols and metadata)
 * ===========================================================================*/

#ifndef BUILD_UNIX_TIME
#define BUILD_UNIX_TIME 0u
#endif

/* Symbols provided by the linker script and linker for sections */
extern unsigned long __image_size__;
extern unsigned long __flash_end__;
extern unsigned long _sidata;  /* start of .data load in FLASH (if AT>FLASH) */
extern unsigned long _sdata;   /* start of .data in RAM */
extern unsigned long _edata;   /* end of .data in RAM */
extern unsigned long _sbss;    /* start of .bss in RAM */
extern unsigned long _ebss;    /* end of .bss in RAM */
extern unsigned long __data_size__;
extern unsigned long __bss_size__;
extern unsigned long __StackTop;
extern unsigned long __MSPLIM;
extern unsigned long __entry_offset__;

/* Place descriptor at a dedicated section which the linker maps to image origin */
__attribute__((used, section(".module_header"), aligned(8)))
const ModuleDescriptor __module_desc__ = {
    .magic = MODULE_MAGIC,
    .desc_version = MODULE_DESC_VERSION,
    .reserved0 = 0,
    .api_version = API_VERSION,
    .name = MODULE_NAME,
    .semver_major = (uint8_t)MODULE_VER_MAJOR,
    .semver_minor = (uint8_t)MODULE_VER_MINOR,
    .semver_patch = (uint16_t)MODULE_VER_PATCH,
    .build_timestamp = BUILD_UNIX_TIME,
    .image_size = (uint32_t)(uintptr_t)&__image_size__,
    .entry = (uint32_t)(uintptr_t)&module_entry,
    .reserved = {0}
};

/* Place ProgramInfo at the very start of the PT_LOAD segment as expected by CRTOS. */
__attribute__((used, section(".program_info")))
const ProgramInfo __program_info__ = {
    /* stackPointer */      (uint32_t)(uintptr_t)(&__StackTop),
    /* entryPoint */        (uint32_t)(uintptr_t)(&__entry_offset__),
    /* vectors[74] */       {0},
    /* section_data_start_addr */ (uint32_t)(uintptr_t)(&_sidata),
    /* section_data_dest_addr */  (uint32_t)(uintptr_t)(&_sdata),
    /* section_data_size */       (uint32_t)(uintptr_t)(&__data_size__),
    /* section_bss_start_addr */  (uint32_t)(uintptr_t)(&_sbss),
    /* section_bss_size */        (uint32_t)(uintptr_t)(&__bss_size__),
    /* reserved[22] */            {0},
    /* vtor_offset */             0,
    /* msp_limit */               (uint32_t)(uintptr_t)(&__MSPLIM)
};

/* Optionally export a small symbol so linker doesn't drop this TU. */
__attribute__((used)) static const char* __mod_name__ = (const char*)__module_desc__.name;
