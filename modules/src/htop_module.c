/**
 * @file htop_module.c
 * @brief RTOS Monitor Module - Similar to Linux htop
 *
 * This module monitors and displays real-time RTOS statistics:
 * - Task list with CPU usage, state, priority
 * - Memory usage (heap)
 * - Stack usage per task
 * - System load
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
#define MONITOR_INTERVAL_MS 500u // Update every 500ms
#define MAX_TASKS 10             // Maximum tasks to monitor (reduced for stack)
#define HEADER_REFRESH_COUNT 10  // Refresh header every N updates

/* =============================================================================
 * Commands via Shared Memory
 * ===========================================================================*/
#define CMD_OFFSET 100
#define CMD_IDLE 0x00
#define CMD_GET_STATS 0x01
#define CMD_GET_TASK_LIST 0x02
#define CMD_SET_REFRESH_RATE 0x03

#define RESP_READY 0x00
#define RESP_DATA_AVAILABLE 0x01

/* Shared memory layout (starting at data[100]):
 * data[100]: command
 * data[101]: response
 * data[102-105]: update_count (32-bit LE)
 * data[106-109]: total_tasks (32-bit LE)
 * data[110-113]: heap_free (32-bit LE)
 * data[114-117]: heap_used (32-bit LE)
 */

/* =============================================================================
 * Task State Strings
 * ===========================================================================*/
static const char *get_task_state_string(uint8_t state)
{
    switch (state)
    {
    case 0:
        return "READY  ";
    case 1:
        return "RUNNING";
    case 2:
        return "BLOCKED";
    case 3:
        return "SUSPEND";
    case 4:
        return "DELETED";
    default:
        return "UNKNOWN";
    }
}

/* =============================================================================
 * Helper Functions
 * ===========================================================================*/

static uint32_t read_u32(const volatile uint8_t *data, uint32_t offset)
{
    return ((uint32_t)data[offset]) |
           ((uint32_t)data[offset + 1] << 8) |
           ((uint32_t)data[offset + 2] << 16) |
           ((uint32_t)data[offset + 3] << 24);
}

static void write_u32(volatile uint8_t *data, uint32_t offset, uint32_t value)
{
    data[offset] = (uint8_t)(value & 0xFF);
    data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    data[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    data[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

/**
 * Draw header - simple scrolling format without screen clearing
 */
static void draw_header(uint32_t heap_total, uint32_t heap_free, uint32_t heap_used,
                        uint32_t task_count)
{
    // Clear screen and move cursor to home position (VT100)
    printf("\033[2J\033[H");
    printf("========== CRTOS Monitor | Tasks: %-3lu ==========\r\n", task_count);

    // Heap usage bar
    uint32_t heap_percent = (heap_used * 100) / heap_total;
    printf("Heap [");
    for (uint32_t i = 0; i < 40; i++)
    {
        if (i < (heap_percent * 40 / 100))
        {
            printf("#");
        }
        else
        {
            printf("-");
        }
    }
    printf("] %3lu%%\r\n", heap_percent);
    printf("Total:%-6lu | Used:%-6lu | Free:%-6lu bytes\r\n", heap_total, heap_used, heap_free);
    printf("%-4s %-16s %-3s %-8s %-6s %-5s %-5s %-4s %-4s\r\n", "PID", "NAME", "PRI", "STATE", "STACK", "USED", "FREE", "STK%", "CPU%");
    printf("------------------------------------------------------------------\r\n");
}

/**
 * Display task information in table format
 */
static void display_task_info(uint32_t pid, const char *name, uint32_t priority,
                              uint8_t state, uint32_t stack_size, uint32_t stack_used,
                              uint32_t stack_free, uint32_t stack_percent,
                              uint32_t cpu_percent)
{
    printf("%-4lu %-16s %-3lu %-8s %-6lu %-5lu %-5lu %3lu%% %3lu%%\r\n",
           pid, name, priority, get_task_state_string(state),
           stack_size, stack_used, stack_free, stack_percent, cpu_percent);
}

/**
 * Draw footer
 */
static void draw_footer(void)
{
    printf("------------------------------------------------------------------\r\n");
    printf("htop_module monitoring | Refresh: 500ms\r\n\r\n");
}

/* =============================================================================
 * Monitoring Logic
 * ===========================================================================*/

/**
 * Structure to store task snapshot for CPU calculation
 */
typedef struct
{
    char name[24];
    uint32_t cycles;
    uint32_t cpu_percent; // Calculated CPU percentage
} TaskSnapshot;

static TaskSnapshot prev_snapshots[MAX_TASKS];
static uint32_t prev_task_count = 0;

// Move all large arrays/structures to static to avoid stack overflow
static ModuleTaskInfo task_info_buffer[MAX_TASKS];
static ModuleHeapInfo heap_info_buffer;

// Smoothing factor for CPU average (0-100): higher = more smoothing
#define CPU_SMOOTHING_FACTOR 70

/**
 * Calculate total CPU cycles delta (sum of all task deltas)
 */
static uint64_t calculate_total_delta(uint32_t current_task_count)
{
    uint64_t total_delta = 0;

    // For each current task, find its previous snapshot by name and calculate delta
    for (uint32_t i = 0; i < current_task_count; i++)
    {
        for (uint32_t j = 0; j < prev_task_count; j++)
        {
            if (strcmp(task_info_buffer[i].name, prev_snapshots[j].name) == 0)
            {
                if (task_info_buffer[i].runtimeCycles >= prev_snapshots[j].cycles)
                {
                    uint64_t delta = (uint64_t)(task_info_buffer[i].runtimeCycles - prev_snapshots[j].cycles);
                    total_delta += delta;
                }
                break;
            }
        }
    }

    return total_delta;
}

/**
 * Calculate CPU usage for a specific task
 */
static uint32_t calculate_cpu_usage(const char *task_name, uint32_t current_cycles, uint64_t total_delta)
{
    if (total_delta == 0)
    {
        return 0;
    }

    // Find previous snapshot for this task by name
    for (uint32_t i = 0; i < prev_task_count; i++)
    {
        if (strcmp(prev_snapshots[i].name, task_name) == 0)
        {
            if (current_cycles >= prev_snapshots[i].cycles)
            {
                uint64_t task_delta = (uint64_t)(current_cycles - prev_snapshots[i].cycles);

                // Calculate CPU% with high precision to avoid rounding errors
                // Scale by 10000 to get 2 decimal places, then divide by 100
                uint64_t cpu_scaled = (task_delta * 10000ULL) / total_delta;
                uint32_t cpu_percent = (uint32_t)(cpu_scaled / 100ULL);
                
                // Clamp to 100%
                if (cpu_percent > 100)
                    cpu_percent = 100;

                return cpu_percent;
            }
            return 0;
        }
    }

    // New task - no previous data
    return 0;
}

/**
 * Update task snapshots for next CPU calculation
 */
static void update_snapshots(uint32_t task_count, ModuleTaskInfo *tasks, uint32_t *cpu_percents)
{
    // For each current task, find or create snapshot
    for (uint32_t i = 0; i < task_count && i < MAX_TASKS; i++)
    {
        // Try to find existing snapshot for this task
        uint32_t snapshot_idx = MAX_TASKS;
        for (uint32_t j = 0; j < prev_task_count && j < MAX_TASKS; j++)
        {
            if (strcmp(prev_snapshots[j].name, tasks[i].name) == 0)
            {
                snapshot_idx = j;
                break;
            }
        }

        // If not found, use a new slot
        if (snapshot_idx >= MAX_TASKS)
        {
            snapshot_idx = i;
            prev_snapshots[snapshot_idx].cpu_percent = 0;
        }

        // Update snapshot
        strncpy(prev_snapshots[snapshot_idx].name, tasks[i].name, 23);
        prev_snapshots[snapshot_idx].name[23] = '\0';
        prev_snapshots[snapshot_idx].cycles = tasks[i].runtimeCycles;
        prev_snapshots[snapshot_idx].cpu_percent = cpu_percents[i];
    }

    prev_task_count = task_count;
}

/* =============================================================================
 * Main Module Entry Point
 * ===========================================================================*/

__attribute__((section(".entry"))) int module_entry(uint32_t reason, void *ctx)
{
    (void)reason;
    (void)ctx;

    ModuleSharedMemory *shared = module_get_shared_memory();

    printf("\r\n");
    printf("================================================================================\r\n");
    printf("                     CRTOS System Monitor Module Started                       \r\n");
    printf("================================================================================\r\n");
    printf("Version: 1.0\r\n");
    printf("Features:\r\n");
    printf("  - Real-time task monitoring (htop-style)\r\n");
    printf("  - Heap usage tracking\r\n");
    printf("  - Stack usage per task\r\n");
    printf("  - CPU usage estimation\r\n");
    printf("  - Configurable refresh rate\r\n");
    printf("\r\n");

    if (!shared)
    {
        printf("[HTOP] ERROR: No shared memory available!\r\n");
        return -1;
    }

    // Initialize shared memory
    shared->data[CMD_OFFSET] = CMD_IDLE;
    shared->data[CMD_OFFSET + 1] = RESP_READY;
    shared->flags = MODULE_FLAG_DATA_READY;

    uint32_t refresh_interval = MONITOR_INTERVAL_MS;
    uint32_t header_counter = 0;

    printf("[HTOP] Starting monitoring loop (refresh: %lu ms)...\r\n\r\n", refresh_interval);
    delay(1000); // Initial delay to let system stabilize

    while (1)
    {
        // Check for commands
        if (shared->data[CMD_OFFSET] == CMD_SET_REFRESH_RATE)
        {
            refresh_interval = read_u32(shared->data, CMD_OFFSET + 10);
            if (refresh_interval < 100)
                refresh_interval = 100; // Minimum 100ms
            printf("[HTOP] Refresh rate changed to %lu ms\r\n", refresh_interval);
            shared->data[CMD_OFFSET] = CMD_IDLE;
        }

        // Get real task information from kernel (use static buffer to avoid stack overflow)
        uint32_t task_count = module_get_task_count();

        uint32_t tasks_retrieved = 0;
        for (uint32_t i = 0; i < task_count && i < MAX_TASKS; i++)
        {
            if (module_get_task_info(i, &task_info_buffer[i]) == 0)
            {
                tasks_retrieved++;
            }
        }

        // Get heap information from kernel (use static buffer to avoid stack allocation)
        module_get_heap_info(&heap_info_buffer);

        // Calculate total CPU delta for all tasks
        uint64_t total_delta = calculate_total_delta(tasks_retrieved);

        // Calculate individual CPU percentages and store them
        static uint32_t cpu_percents[MAX_TASKS];
        for (uint32_t i = 0; i < tasks_retrieved; i++)
        {
            cpu_percents[i] = calculate_cpu_usage(task_info_buffer[i].name,
                                                   task_info_buffer[i].runtimeCycles,
                                                   total_delta);
        }

        // Normalize percentages to ensure they sum to exactly 100%
        if (total_delta > 0 && tasks_retrieved > 0)
        {
            uint32_t sum = 0;
            uint32_t max_idx = 0;
            uint32_t max_val = 0;
            
            // Calculate sum and find task with highest CPU
            for (uint32_t i = 0; i < tasks_retrieved; i++)
            {
                sum += cpu_percents[i];
                if (cpu_percents[i] > max_val)
                {
                    max_val = cpu_percents[i];
                    max_idx = i;
                }
            }
            
            // Adjust the highest CPU task to make total exactly 100%
            if (sum > 0 && sum != 100)
            {
                int32_t adjustment = 100 - (int32_t)sum;
                int32_t new_val = (int32_t)cpu_percents[max_idx] + adjustment;
                
                // Ensure we don't go negative or over 100
                if (new_val < 0)
                    new_val = 0;
                if (new_val > 100)
                    new_val = 100;
                    
                cpu_percents[max_idx] = (uint32_t)new_val;
            }
        }

        // Always redraw the entire display
        draw_header(heap_info_buffer.totalSize, heap_info_buffer.freeMemory,
                    heap_info_buffer.allocatedMemory, tasks_retrieved);

        // Display all tasks
        for (uint32_t i = 0; i < tasks_retrieved; i++)
        {
            uint32_t stack_percent = (task_info_buffer[i].stackUsed * 100) / task_info_buffer[i].stackSize;

            display_task_info(i, task_info_buffer[i].name, task_info_buffer[i].priority,
                              (uint8_t)task_info_buffer[i].state, task_info_buffer[i].stackSize,
                              task_info_buffer[i].stackUsed, task_info_buffer[i].stackFree,
                              stack_percent, cpu_percents[i]);
        }

        draw_footer();

        // Update snapshots for next CPU calculation
        update_snapshots(tasks_retrieved, task_info_buffer, cpu_percents);

        header_counter++;

        // Write statistics to shared memory (for host queries)
        write_u32(shared->data, CMD_OFFSET + 6, tasks_retrieved);
        write_u32(shared->data, CMD_OFFSET + 10, heap_info_buffer.freeMemory);
        write_u32(shared->data, CMD_OFFSET + 14, heap_info_buffer.allocatedMemory);
        shared->data[CMD_OFFSET + 1] = RESP_DATA_AVAILABLE;
        shared->flags |= MODULE_FLAG_DATA_READY;

        // Wait for next update
        delay(refresh_interval);
    }

    printf("\n[HTOP] Module stopped\n");
    return 0;
}

/* Build-time provided values via -DMODULE_NAME, -DMODULE_VERSION_*, -DAPI_VERSION */
#ifndef API_VERSION
#define API_VERSION 1u
#endif

#ifndef MODULE_NAME
#define MODULE_NAME "HTOP Monitor Module"
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
