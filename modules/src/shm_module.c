/**
 * @file module.c
 * @brief Module application logic and entry point
 * 
 * This file contains the main module functionality including task management,
 * host communication, and periodic logging. Module metadata is defined in module_config.c.
 */

#include "module_descriptor.h"
#include "program_info.h"
#include <stdint.h>
#include <kernel.h>
#include <stdio.h>

/* =============================================================================
 * Configuration Constants
 * ===========================================================================*/
#define TIMER_PERIOD_TICKS      1000u    // Timer fires every 1000 ticks (1 second)
#define HOST_UPDATE_INTERVAL    100u     // Send counter to host every N iterations
#define STATUS_CHECK_INTERVAL   1000u    // Check test status every N iterations
#define HOST_VALUE_OFFSET       1000u    // Value added when echoing host data

/* =============================================================================
 * Test Status Flags
 * ===========================================================================*/
#define TEST_TASK1_OK       (1 << 0)
#define TEST_TASK2_OK       (1 << 1)
#define TEST_TASK3_OK       (1 << 2)
#define TEST_TIMER_OK       (1 << 3)
#define TEST_SEMAPHORE_OK   (1 << 4)
#define TEST_QUEUE_OK       (1 << 5)
#define TEST_DELAY_OK       (1 << 6)
#define TEST_ALL_OK         0x7F
#define TEST_SUCCESS_FLAG   0x80000000u
#define SUCCESS_MARKER      0xFFu

/* =============================================================================
 * Module State Variables
 * ===========================================================================*/
// Test counters
volatile uint32_t cnt = 0;
volatile uint32_t task1_counter = 0;
volatile uint32_t task2_counter = 0;
volatile uint32_t task3_counter = 0;
volatile uint32_t timer_fired = 0;
volatile uint32_t semaphore_signals = 0;
volatile uint32_t queue_messages = 0;
volatile uint32_t test_status = 0;

// Timer storage (must persist - don't use local variable)
static ModuleSoftwareTimer logTimer;

/* =============================================================================
 * Helper Functions
 * ===========================================================================*/

/**
 * Convert unsigned integer to string (simple itoa implementation)
 */
static void uint_to_string(uint32_t num, char* buffer)
{
    char digits[10];
    int i = 0;
    
    do {
        digits[i++] = '0' + (num % 10);
        num /= 10;
    } while (num > 0);
    
    // Reverse digits into buffer
    while (i > 0) {
        *buffer++ = digits[--i];
    }
    *buffer = '\0';
}

/**
 * Copy string from source to destination
 */
static char* string_copy(char* dest, const char* src)
{
    while (*src) {
        *dest++ = *src++;
    }
    return dest;
}

/**
 * Convert unsigned integer to hex string
 */
static void uint_to_hex(uint32_t num, char* buffer)
{
    const char hex_chars[] = "0123456789ABCDEF";
    buffer[0] = '0';
    buffer[1] = 'x';
    
    for (int i = 7; i >= 0; i--) {
        buffer[2 + (7 - i)] = hex_chars[(num >> (i * 4)) & 0xF];
    }
    buffer[10] = '\0';
}

/**
 * Simple printf-like function for modules (limited format support)
 * Supports: %u (unsigned), %d (signed), %X (hex), %s (string)
 */
static void module_printf(const char* format, ...)
{
    char buffer[256];
    char* ptr = buffer;
    const char* fmt = format;
    
    // Simple varargs handling
    uint32_t* args = (uint32_t*)((char*)&format + sizeof(format));
    int arg_index = 0;
    
    while (*fmt && (ptr - buffer) < 250) {
        if (*fmt == '%' && *(fmt + 1)) {
            fmt++;
            switch (*fmt) {
                case 'u': {
                    char num_buf[12];
                    uint_to_string(args[arg_index++], num_buf);
                    ptr = string_copy(ptr, num_buf);
                    break;
                }
                case 'd': {
                    int32_t val = (int32_t)args[arg_index++];
                    if (val < 0) {
                        *ptr++ = '-';
                        val = -val;
                    }
                    char num_buf[12];
                    uint_to_string((uint32_t)val, num_buf);
                    ptr = string_copy(ptr, num_buf);
                    break;
                }
                case 'X': {
                    char hex_buf[11];
                    uint_to_hex(args[arg_index++], hex_buf);
                    ptr = string_copy(ptr, hex_buf);
                    break;
                }
                case 's': {
                    const char* str = (const char*)args[arg_index++];
                    if (str) {
                        ptr = string_copy(ptr, str);
                    }
                    break;
                }
                default:
                    *ptr++ = '%';
                    *ptr++ = *fmt;
                    break;
            }
        } else {
            *ptr++ = *fmt;
        }
        fmt++;
    }
    *ptr = '\0';
    module_log(buffer);
}

/**
 * Timer callback that sends periodic log messages
 */
void periodic_log_callback(void* arg)
{
    (void)arg;  // Unused parameter
    static uint32_t count = 0;
    char buffer[64];
    char num_buffer[12];
    
    // Build log message
    char* ptr = buffer;
    ptr = string_copy(ptr, "[Module Timer] Log #");
    uint_to_string(count++, num_buffer);
    ptr = string_copy(ptr, num_buffer);
    ptr = string_copy(ptr, "\r\n");
    
    module_log(buffer);
}

/**
 * Handle communication with host if shared memory is available
 */
static void handle_host_communication(ModuleSharedMemory* shared, uint32_t counter)
{
    if (!shared) {
        return;  // No shared memory available
    }
    
    // Send counter value to host at regular intervals
    if (counter % HOST_UPDATE_INTERVAL == 0) {
        module_send_to_host(counter);
    }
    
    // Check and respond to host messages
    if (module_host_has_data()) {
        uint32_t hostValue = module_read_from_host();
        // Echo back with offset
        module_send_to_host(hostValue + HOST_VALUE_OFFSET);
    }
}

/**
 * Check test status and notify host if all tests passed
 */
static void check_test_status(ModuleSharedMemory* shared, volatile uint32_t* counter)
{
    if (test_status == TEST_ALL_OK) {
        // Mark counter with success flag
        *counter |= TEST_SUCCESS_FLAG;
        
        // Notify host of success
        if (shared) {
            shared->data[0] = SUCCESS_MARKER;
            shared->flags |= MODULE_FLAG_DATA_READY;
        }
    }
}

/**
 * Initialize module timer
 */
static void initialize_timer(void)
{
    module_timer_init(&logTimer, TIMER_PERIOD_TICKS, periodic_log_callback, 0, 1);
    module_timer_start(&logTimer);
}

/* =============================================================================
 * Main Module Entry Point
 * ===========================================================================*/
/**
 * Main module entry point with comprehensive CRTOS testing
 */
__attribute__((section(".entry"))) int module_entry(uint32_t reason, void* ctx) 
{
    (void)reason; 
    (void)ctx;
    
    // Initialize module state
    test_status = 0;
    cnt = 0;
    
    // Get shared memory for host communication
    ModuleSharedMemory* shared = module_get_shared_memory();
    
    // Log startup message
    module_log("[Module] Starting...\r\n");
    
    // Test REAL printf functionality
    printf("Printf test: counter=%lu\n", cnt);
    
    // Setup periodic timer for logging
    // DISABLED: Timer logging causes heap growth due to module malloc/free limitations
    // initialize_timer();
    
    // Main execution loop
    while (1) {
        cnt++;
        
        // Handle host communication
        handle_host_communication(shared, cnt);
        
        // Periodic status check
        if (cnt % STATUS_CHECK_INTERVAL == 0) {
            check_test_status(shared, &cnt);
        }
        
        // Simple periodic log every 5000 iterations
        if (cnt % 5000 == 0) {
            module_log("[Module] Running...\r\n");
            printf("Printf: cnt=%lu, test_status=0x%lX\n", cnt, test_status);
            module_printf("Custom: cnt=%u, status=0x%X\n", cnt, test_status);
        }
        
        // Delay between iterations
        delay(1000);
    }
    
    return 0;  // Success (never reached)
}

/* Build-time provided values via -DMODULE_NAME, -DMODULE_VERSION_*, -DAPI_VERSION */
#ifndef API_VERSION
#define API_VERSION 1u
#endif

#ifndef MODULE_NAME
#define MODULE_NAME "Shared Memory Module"
#endif

/* Parse MODULE_VERSION string like M.m.p into components at compile time is tricky.
   Instead expect -DMODULE_VER_MAJOR/MINOR/PATCH optionally; default to 0. */
#ifndef MODULE_VER_MAJOR
#define MODULE_VER_MAJOR 0
#endif
#ifndef MODULE_VER_MINOR
#define MODULE_VER_MINOR 0
#endif
#ifndef MODULE_VER_PATCH
#define MODULE_VER_PATCH 0
#endif

/* Build timestamp: allow override with -DBUILD_UNIX_TIME; else 0 (host may fill). */
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
