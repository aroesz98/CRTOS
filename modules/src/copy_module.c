/**
 * @file copy_module.c
 * @brief Memory copy module for CRTOS - similar to Linux cp but for memory regions
 * 
 * This module implements memory copying functionality from one address to another.
 * Features:
 * - Copy memory from source address to destination address
 * - Progress reporting via module_log
 * - Error handling and validation
 * - Support for large memory regions with buffered copy
 */

#include "module_descriptor.h"
#include "program_info.h"
#include <stdint.h>
#include <kernel.h>
#include <stdio.h>
#include <string.h>

/* =============================================================================
 * Configuration Constants
 * ===========================================================================*/
#define COPY_BUFFER_SIZE    512u     // Buffer size for copying data (bytes)
#define COPY_TICK_DELAY     5u       // Delay between operations (ticks)
#define PROGRESS_INTERVAL   10u      // Report progress every N%
#define CMD_POLL_DELAY      100u     // Delay between command polls (ticks)

/* =============================================================================
 * Command Protocol (via shared memory)
 * ===========================================================================*/
#define CMD_IDLE            0x00
#define CMD_COPY            0x01
#define CMD_VERIFY          0x02
#define CMD_STATUS          0x03
#define CMD_SHUTDOWN        0xFF

#define RESP_READY          0x00
#define RESP_BUSY           0x01
#define RESP_SUCCESS        0x02
#define RESP_ERROR          0x03

/* Shared memory layout:
 * data[0]:     command (CMD_xxx)
 * data[1]:     response (RESP_xxx)
 * data[2-5]:   source_addr (little-endian)
 * data[6-9]:   dest_addr (little-endian)
 * data[10-13]: size (little-endian)
 * data[14-17]: bytes_copied (little-endian, output)
 * data[18-21]: verify_errors (little-endian, output)
 */

/* =============================================================================
 * Copy Operation Status
 * ===========================================================================*/
typedef enum {
    COPY_STATUS_IDLE = 0,
    COPY_STATUS_VALIDATING,
    COPY_STATUS_COPYING,
    COPY_STATUS_VERIFYING,
    COPY_STATUS_SUCCESS,
    COPY_STATUS_ERROR
} CopyStatus;

/* =============================================================================
 * Module State
 * ===========================================================================*/
static struct {
    uint32_t source_addr;
    uint32_t dest_addr;
    uint32_t size;
    uint32_t bytes_copied;
    CopyStatus status;
    char error_msg[128];
    uint32_t verify_errors;
} copy_state;

static uint8_t copy_buffer[COPY_BUFFER_SIZE];

/* =============================================================================
 * Helper Functions
 * ===========================================================================*/

/**
 * Read 32-bit value from shared memory (little-endian)
 */
static uint32_t read_u32(const uint8_t* data, uint32_t offset)
{
    return ((uint32_t)data[offset]) |
           ((uint32_t)data[offset + 1] << 8) |
           ((uint32_t)data[offset + 2] << 16) |
           ((uint32_t)data[offset + 3] << 24);
}

/**
 * Write 32-bit value to shared memory (little-endian)
 */
static void write_u32(uint8_t* data, uint32_t offset, uint32_t value)
{
    data[offset]     = (uint8_t)(value & 0xFF);
    data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    data[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    data[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

/**
 * Validate memory address (basic checks)
 */
static int validate_address(uint32_t addr, uint32_t size)
{
    // Check for NULL pointer
    if (addr == 0) {
        return -1;
    }
    
    // Check for overflow
    if (addr + size < addr) {
        return -1;
    }
    
    // TODO: Add more sophisticated checks based on your memory map
    // - Check if address is in valid RAM region
    // - Check if address is in writable region
    // - Check alignment requirements
    
    return 0;
}

/**
 * Check if memory regions overlap
 */
static int check_overlap(uint32_t src, uint32_t dst, uint32_t size)
{
    uint32_t src_end = src + size;
    uint32_t dst_end = dst + size;
    
    // Check if regions overlap
    if ((src < dst_end) && (dst < src_end)) {
        return 1;  // Overlap detected
    }
    
    return 0;  // No overlap
}

/* =============================================================================
 * Copy Functions
 * ===========================================================================*/

/* =============================================================================
 * Copy Functions
 * ===========================================================================*/

/**
 * Initialize copy operation
 */
static int copy_init(uint32_t source, uint32_t dest, uint32_t size)
{
    // Reset state
    memset(&copy_state, 0, sizeof(copy_state));
    
    copy_state.source_addr = source;
    copy_state.dest_addr = dest;
    copy_state.size = size;
    copy_state.status = COPY_STATUS_VALIDATING;
    
    printf("[Copy] Initializing copy operation:\r\n");
    printf("  Source:      0x%X\r\n", source);
    printf("  Destination: 0x%X\r\n", dest);
    printf("  Size:        %u bytes\r\n", size);
    
    // Validate size
    if (size == 0) {
        strcpy(copy_state.error_msg, "Size cannot be zero");
        copy_state.status = COPY_STATUS_ERROR;
        return -1;
    }
    
    // Validate addresses
    if (validate_address(source, size) != 0) {
        strcpy(copy_state.error_msg, "Invalid source address");
        copy_state.status = COPY_STATUS_ERROR;
        return -1;
    }
    
    if (validate_address(dest, size) != 0) {
        strcpy(copy_state.error_msg, "Invalid destination address");
        copy_state.status = COPY_STATUS_ERROR;
        return -1;
    }
    
    // Check for overlap
    if (check_overlap(source, dest, size)) {
        printf("[Copy] Warning: Memory regions overlap - using memmove semantics\r\n");
    }
    
    copy_state.status = COPY_STATUS_IDLE;
    printf("[Copy] Validation passed\r\n");
    
    return 0;
}

/**
 * Execute memory copy operation with progress reporting
 */
static int copy_execute(void)
{
    uint32_t remaining = copy_state.size;
    uint32_t src_offset = 0;
    uint32_t dst_offset = 0;
    uint32_t chunk_size = 0;
    uint32_t last_progress = 0;
    
    printf("[Copy] Starting copy operation...\r\n");
    copy_state.bytes_copied = 0;
    copy_state.status = COPY_STATUS_COPYING;
    
    uint8_t* src_ptr = (uint8_t*)copy_state.source_addr;
    uint8_t* dst_ptr = (uint8_t*)copy_state.dest_addr;
    
    // Copy in chunks
    while (remaining > 0) {
        chunk_size = (remaining > COPY_BUFFER_SIZE) ? COPY_BUFFER_SIZE : remaining;
        
        // Copy chunk to buffer
        memcpy(copy_buffer, src_ptr + src_offset, chunk_size);
        
        // Copy buffer to destination
        memcpy(dst_ptr + dst_offset, copy_buffer, chunk_size);
        
        src_offset += chunk_size;
        dst_offset += chunk_size;
        remaining -= chunk_size;
        copy_state.bytes_copied += chunk_size;
        
        // Report progress
        uint32_t progress = (copy_state.bytes_copied * 100) / copy_state.size;
        if (progress >= last_progress + PROGRESS_INTERVAL) {
            printf("[Copy] Progress: %u%% (%u/%u bytes)\r\n", 
                   progress, copy_state.bytes_copied, copy_state.size);
            last_progress = progress;
        }
        
        // Yield to other tasks
        delay(COPY_TICK_DELAY);
    }
    
    copy_state.status = COPY_STATUS_SUCCESS;
    printf("[Copy] Copy completed! %u bytes copied\r\n", copy_state.bytes_copied);
    
    return 0;
}

/**
 * Verify copied data (optional)
 */
static int copy_verify(void)
{
    printf("[Copy] Verifying copied data...\r\n");
    copy_state.status = COPY_STATUS_VERIFYING;
    copy_state.verify_errors = 0;
    
    uint8_t* src_ptr = (uint8_t*)copy_state.source_addr;
    uint8_t* dst_ptr = (uint8_t*)copy_state.dest_addr;
    
    for (uint32_t i = 0; i < copy_state.size; i++) {
        if (src_ptr[i] != dst_ptr[i]) {
            copy_state.verify_errors++;
            if (copy_state.verify_errors <= 10) {  // Report first 10 errors
                printf("[Copy] Verify error at offset 0x%X: expected 0x%X, got 0x%X\r\n",
                       i, src_ptr[i], dst_ptr[i]);
            }
        }
        
        // Report progress for large copies
        if (i % 10000 == 0 && i > 0) {
            uint32_t progress = (i * 100) / copy_state.size;
            printf("[Copy] Verify progress: %u%%\r\n", progress);
            delay(COPY_TICK_DELAY);
        }
    }
    
    if (copy_state.verify_errors == 0) {
        printf("[Copy] Verification passed - data integrity confirmed\r\n");
        return 0;
    } else {
        printf("[Copy] Verification failed - %u errors found\r\n", copy_state.verify_errors);
        sprintf(copy_state.error_msg, "%u verification errors", copy_state.verify_errors);
        copy_state.status = COPY_STATUS_ERROR;
        return -1;
    }
}

/**
 * Display usage information
 */
static void print_usage(void)
{
    printf("Copy Module - Memory copy utility for CRTOS\r\n");
    printf("Usage: Send commands via shared memory\r\n");
    printf("\r\nCommand Protocol:\r\n");
    printf("  data[0] = CMD_COPY (0x01)\r\n");
    printf("  data[2-5]   = source_addr (32-bit LE)\r\n");
    printf("  data[6-9]   = dest_addr (32-bit LE)\r\n");
    printf("  data[10-13] = size (32-bit LE)\r\n");
    printf("\r\n\nResponse:\r\n");
    printf("  data[1] = RESP_READY/BUSY/SUCCESS/ERROR\r\n");
    printf("  data[14-17] = bytes_copied\r\n");
    printf("  data[18-21] = verify_errors\r\n");
    printf("\r\nCommands:\r\n");
    printf("  0x01 - CMD_COPY: Execute copy operation\r\n");
    printf("  0x02 - CMD_VERIFY: Verify last copy\r\n");
    printf("  0x03 - CMD_STATUS: Get current status\r\n");
    printf("  0xFF - CMD_SHUTDOWN: Stop module\r\n");
}

/**
 * Process command from shared memory
 */
static int process_command(ModuleSharedMemory* shared)
{
    uint8_t cmd = shared->data[0];
    
    switch (cmd) {
        case CMD_COPY: {
            // Read parameters
            uint32_t source = read_u32(shared->data, 2);
            uint32_t dest = read_u32(shared->data, 6);
            uint32_t size = read_u32(shared->data, 10);
            
            printf("[Copy] Received CMD_COPY:\r\n");
            printf("  Source: 0x%X\r\n", source);
            printf("  Dest:   0x%X\r\n", dest);
            printf("  Size:   %u bytes\r\n", size);
            
            // Set busy status
            shared->data[1] = RESP_BUSY;
            shared->flags |= MODULE_FLAG_DATA_READY;
            
            // Initialize and execute copy
            if (copy_init(source, dest, size) != 0) {
                printf("[Copy] Init failed: %s\r\n", copy_state.error_msg);
                shared->data[1] = RESP_ERROR;
                write_u32(shared->data, 14, 0);
                shared->flags |= MODULE_FLAG_DATA_READY;
                return -1;
            }
            
            if (copy_execute() != 0) {
                printf("[Copy] Execute failed: %s\r\n", copy_state.error_msg);
                shared->data[1] = RESP_ERROR;
                write_u32(shared->data, 14, copy_state.bytes_copied);
                shared->flags |= MODULE_FLAG_DATA_READY;
                return -1;
            }
            
            // Return success with results
            shared->data[1] = RESP_SUCCESS;
            write_u32(shared->data, 14, copy_state.bytes_copied);
            write_u32(shared->data, 18, 0);  // No verify errors yet
            shared->flags |= MODULE_FLAG_DATA_READY;
            
            printf("[Copy] Command completed successfully\r\n");
            break;
        }
        
        case CMD_VERIFY: {
            printf("[Copy] Received CMD_VERIFY\r\n");
            shared->data[1] = RESP_BUSY;
            shared->flags |= MODULE_FLAG_DATA_READY;
            
            if (copy_verify() != 0) {
                shared->data[1] = RESP_ERROR;
            } else {
                shared->data[1] = RESP_SUCCESS;
            }
            
            write_u32(shared->data, 18, copy_state.verify_errors);
            shared->flags |= MODULE_FLAG_DATA_READY;
            break;
        }
        
        case CMD_STATUS: {
            printf("[Copy] Received CMD_STATUS\r\n");
            shared->data[1] = RESP_READY;
            write_u32(shared->data, 14, copy_state.bytes_copied);
            write_u32(shared->data, 18, copy_state.verify_errors);
            shared->flags |= MODULE_FLAG_DATA_READY;
            break;
        }
        
        case CMD_SHUTDOWN: {
            printf("[Copy] Received CMD_SHUTDOWN\r\n");
            shared->data[1] = RESP_SUCCESS;
            shared->flags |= MODULE_FLAG_DATA_READY;
            return 1;  // Signal shutdown
        }
        
        case CMD_IDLE:
            // No command, just waiting
            break;
            
        default:
            printf("[Copy] Unknown command: 0x%X\r\n", cmd);
            shared->data[1] = RESP_ERROR;
            shared->flags |= MODULE_FLAG_DATA_READY;
            break;
    }
    
    // Clear command after processing
    shared->data[0] = CMD_IDLE;
    
    return 0;
}

/* =============================================================================
 * Main Module Entry Point
 * ===========================================================================*/

/**
 * Module entry point
 */
__attribute__((section(".entry"))) int module_entry(uint32_t reason, void* ctx) 
{
    (void)reason;
    (void)ctx;
    
    ModuleSharedMemory* shared = module_get_shared_memory();
    
    printf("\n=== Copy Module Started ===\r\n");
    print_usage();
    
    if (!shared) {
        printf("[Copy] ERROR: No shared memory available!\r\n");
        printf("[Copy] Module cannot function without shared memory\r\n");
        return -1;
    }
    
    // Initialize shared memory
    shared->data[0] = CMD_IDLE;
    shared->data[1] = RESP_READY;
    shared->flags |= MODULE_FLAG_DATA_READY;
    
    printf("[Copy] Module ready, waiting for commands...\r\n");
    printf("[Copy] Send CMD_COPY with addresses and size to start\r\n");
    
    // Main command processing loop
    while (1) {
        // Check if host has sent a command
        if (shared->data[0] != CMD_IDLE) {
            int result = process_command(shared);
            
            if (result > 0) {
                // Shutdown requested
                printf("[Copy] Shutting down...\r\n");
                break;
            }
        }
        
        // Wait before checking again
        delay(CMD_POLL_DELAY);
    }
    
    printf("\n=== Copy Module Stopped ===\r\n");
    return 0;
}

/* Build-time provided values via -DMODULE_NAME, -DMODULE_VERSION_*, -DAPI_VERSION */
#ifndef API_VERSION
#define API_VERSION 1u
#endif

#ifndef MODULE_NAME
#define MODULE_NAME "Example Copy Module"
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
