#include "module_descriptor.h"
#include "program_info.h"
#include <stdint.h>
#include <kernel.h>

// Test counters
volatile uint32_t cnt = 0;
volatile uint32_t task1_counter = 0;
volatile uint32_t task2_counter = 0;
volatile uint32_t task3_counter = 0;
volatile uint32_t timer_fired = 0;
volatile uint32_t semaphore_signals = 0;
volatile uint32_t queue_messages = 0;

// Test flags
volatile uint32_t test_status = 0;
#define TEST_TASK1_OK       (1 << 0)
#define TEST_TASK2_OK       (1 << 1)
#define TEST_TASK3_OK       (1 << 2)
#define TEST_TIMER_OK       (1 << 3)
#define TEST_SEMAPHORE_OK   (1 << 4)
#define TEST_QUEUE_OK       (1 << 5)
#define TEST_DELAY_OK       (1 << 6)
#define TEST_ALL_OK         0x7F

// Timer storage (must persist - don't use local variable)
static ModuleSoftwareTimer logTimer;

// Timer callback that sends periodic logs
void periodic_log_callback(void* arg)
{
    static uint32_t count = 0;
    char buffer[64];
    
    // Simple string formatting
    const char* prefix = "[Module Timer] Log #";
    char* ptr = buffer;
    while (*prefix) *ptr++ = *prefix++;
    
    // Convert count to string (simple itoa)
    uint32_t num = count++;
    char digits[10];
    int i = 0;
    do {
        digits[i++] = '0' + (num % 10);
        num /= 10;
    } while (num > 0);
    while (i > 0) *ptr++ = digits[--i];
    *ptr++ = '\r';
    *ptr++ = '\n';
    *ptr = '\0';
    
    module_log(buffer);
}

/* Main module entry point with comprehensive CRTOS testing */
__attribute__((weak, section(".entry"))) int module_entry(uint32_t reason, void* ctx) {
    (void)reason; (void)ctx;
    
    // Initialize test status
    test_status = 0;
    
    // Try to get shared memory for communication with host
    ModuleSharedMemory* shared = module_get_shared_memory();

    // Initialize timer: 1000 ticks (1 second), auto-reload enabled
    module_timer_init(&logTimer, 1000, periodic_log_callback, 0, 1);
    
    // Start the timer
    module_timer_start(&logTimer);
    
    // Main loop - increment counter and monitor test status
    while(1) {
        cnt++;
        
        // Send counter value to host every 100 iterations (only if shared memory available)
        if (shared && (cnt % 100 == 0)) {
            module_send_to_host(cnt);
        }
        
        // Check if host sent us data (only if shared memory available)
        if (shared && module_host_has_data()) {
            uint32_t hostValue = module_read_from_host();
            // Echo back to host with modified value
            module_send_to_host(hostValue + 1000);
        }
        
        // Every 1000 iterations, check test status
        if (cnt % 1000 == 0) {
            // If all tests passed, you could signal success
            if (test_status == TEST_ALL_OK) {
                // All tests passed!
                cnt |= 0x80000000;  // Set MSB to indicate success
                
                // Send success notification to host
                if (shared) {
                    shared->data[0] = 0xFF;  // Success marker
                    shared->flags |= MODULE_FLAG_DATA_READY;
                }
            }
        }
        
        delay(1000); // 1 second delay
    }
    
    return 0; /* success */
}

/* Build-time provided values via -DMODULE_NAME, -DMODULE_VERSION_*, -DAPI_VERSION */
#ifndef API_VERSION
#define API_VERSION 1u
#endif

#ifndef MODULE_NAME
#define MODULE_NAME "example"
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
extern unsigned long _sidata; /* start of .data load in FLASH (if AT>FLASH) */
extern unsigned long _sdata;  /* start of .data in RAM */
extern unsigned long _edata;  /* end of .data in RAM */
extern unsigned long _sbss;   /* start of .bss in RAM */
extern unsigned long _ebss;   /* end of .bss in RAM */
extern unsigned long __data_size__;
extern unsigned long __bss_size__;
extern unsigned long __StackTop;
extern unsigned long __MSPLIM;
extern unsigned long __entry_offset__;

/* Place descriptor at a dedicated section which the linker maps to image origin */
__attribute__((used, section(".module_header")))
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
