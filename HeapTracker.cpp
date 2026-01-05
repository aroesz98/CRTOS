/*
 * HeapTracker
 * Author: Arkadiusz Szlanta
 * Date: 05 Jan 2026
 *
 * Memory leak detection and heap tracking implementation.
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 */

#include "HeapTracker.hpp"
#include "CRTOS.hpp"
#include "fsl_debug_console.h"
#include <cstring>

namespace CRTOS
{

// Static member definitions
AllocationRecord HeapTracker::s_records[HeapTrackerConfig::MAX_TRACKED_ALLOCATIONS];
HeapStats HeapTracker::s_stats;
bool HeapTracker::s_enabled = false;
bool HeapTracker::s_initialized = false;

// External declarations
extern "C" {
    extern volatile uint32_t tickCount;
    
    // TaskControlBlock forward declaration for task name access
    struct TaskControlBlock
    {
        volatile uint32_t *stackTop;
        volatile uint32_t *stack;
        void (*function)(void *);
        void *function_args;
        uint32_t vtor_addr;
        uint32_t priority;
        uint32_t state;
        uint32_t timeout;
        uint32_t delayUpTo;
        uint32_t stackSize;
        uint32_t enterCycles;
        uint32_t exitCycles;
        uint64_t executionTime;
        uint32_t heapAllocated;
        char name[20u];
        // ... other fields
    };
    
    extern volatile TaskControlBlock* sCurrentTCB;
}

void HeapTracker::Init()
{
    if (s_initialized)
    {
        return;
    }
    
    // Clear all records
    for (uint32_t i = 0; i < HeapTrackerConfig::MAX_TRACKED_ALLOCATIONS; i++)
    {
        s_records[i].inUse = false;
        s_records[i].ptr = nullptr;
        s_records[i].size = 0;
        s_records[i].actualSize = 0;
        s_records[i].callerAddr = nullptr;
        s_records[i].tickCount = 0;
        s_records[i].regionIndex = 0;
        s_records[i].taskName[0] = '\0';
    }
    
    // Reset statistics
    ResetStats();
    
    s_initialized = true;
    s_enabled = true;
}

void HeapTracker::RecordAllocation(void* ptr, uint32_t requestedSize, 
                                    uint32_t actualSize, uint32_t regionIndex)
{
    if (!s_enabled || !s_initialized || ptr == nullptr)
    {
        return;
    }
    
    // Update statistics
    s_stats.totalAllocations++;
    s_stats.currentAllocations++;
    s_stats.totalBytesAllocated += actualSize;
    s_stats.currentBytesAllocated += actualSize;
    
    if (s_stats.currentAllocations > s_stats.peakAllocations)
    {
        s_stats.peakAllocations = s_stats.currentAllocations;
    }
    
    if (s_stats.currentBytesAllocated > s_stats.peakBytesAllocated)
    {
        s_stats.peakBytesAllocated = s_stats.currentBytesAllocated;
    }
    
    // Find a free slot
    int32_t slot = FindFreeSlot();
    if (slot < 0)
    {
        // No free slots - tracking buffer full
        // Could log a warning here
        return;
    }
    
    // Record the allocation
    AllocationRecord& record = s_records[slot];
    record.ptr = ptr;
    record.size = requestedSize;
    record.actualSize = actualSize;
    record.callerAddr = GetCallerAddress();
    record.tickCount = GetTickCount();
    record.regionIndex = regionIndex;
    record.inUse = true;
    
    // Copy task name
    const char* taskName = GetCurrentTaskName();
    if (taskName != nullptr)
    {
        strncpy(record.taskName, taskName, HeapTrackerConfig::TASK_NAME_LEN - 1);
        record.taskName[HeapTrackerConfig::TASK_NAME_LEN - 1] = '\0';
    }
    else
    {
        strcpy(record.taskName, "<unknown>");
    }
}

bool HeapTracker::RecordDeallocation(void* ptr)
{
    if (!s_enabled || !s_initialized || ptr == nullptr)
    {
        return true;  // Don't report error if disabled
    }
    
    // Find the record
    int32_t slot = FindRecord(ptr);
    
    if (slot < 0)
    {
        // Pointer not found - could be:
        // 1. Double free
        // 2. Invalid pointer
        // 3. Allocation happened before tracking was enabled
        s_stats.invalidFrees++;
        return false;
    }
    
    AllocationRecord& record = s_records[slot];
    
    // Update statistics
    s_stats.totalDeallocations++;
    s_stats.currentAllocations--;
    s_stats.totalBytesFreed += record.actualSize;
    s_stats.currentBytesAllocated -= record.actualSize;
    
    // Clear the record
    record.inUse = false;
    record.ptr = nullptr;
    
    return true;
}

void HeapTracker::RecordAllocationFailure(uint32_t requestedSize)
{
    if (!s_enabled || !s_initialized)
    {
        return;
    }
    
    s_stats.failedAllocations++;
    
    // Could log additional info here
    PRINTF("[HeapTracker] Allocation failed: requested %lu bytes\r\n", requestedSize);
}

const HeapStats& HeapTracker::GetStats()
{
    return s_stats;
}

void HeapTracker::ResetStats()
{
    s_stats.totalAllocations = 0;
    s_stats.totalDeallocations = 0;
    s_stats.currentAllocations = 0;
    s_stats.peakAllocations = 0;
    s_stats.totalBytesAllocated = 0;
    s_stats.totalBytesFreed = 0;
    s_stats.currentBytesAllocated = 0;
    s_stats.peakBytesAllocated = 0;
    s_stats.failedAllocations = 0;
    s_stats.doubleFrees = 0;
    s_stats.invalidFrees = 0;
}

uint32_t HeapTracker::CheckForLeaks(uint32_t minAgeTicks, LeakReport* reports, 
                                     uint32_t maxReports)
{
    if (!s_initialized || reports == nullptr || maxReports == 0)
    {
        return 0;
    }
    
    uint32_t currentTick = GetTickCount();
    uint32_t leakCount = 0;
    
    for (uint32_t i = 0; i < HeapTrackerConfig::MAX_TRACKED_ALLOCATIONS && leakCount < maxReports; i++)
    {
        if (s_records[i].inUse)
        {
            uint32_t age = currentTick - s_records[i].tickCount;
            if (age >= minAgeTicks)
            {
                reports[leakCount].record = &s_records[i];
                reports[leakCount].ageInTicks = age;
                leakCount++;
            }
        }
    }
    
    return leakCount;
}

void HeapTracker::PrintAllocations()
{
    if (!s_initialized)
    {
        PRINTF("[HeapTracker] Not initialized\r\n");
        return;
    }
    
    PRINTF("\r\n=== Current Heap Allocations ===\r\n");
    PRINTF("  %-10s %-8s %-8s %-10s %-10s %s\r\n", 
           "Pointer", "Size", "Actual", "Tick", "Caller", "Task");
    PRINTF("  ---------- -------- -------- ---------- ---------- --------\r\n");
    
    uint32_t count = 0;
    for (uint32_t i = 0; i < HeapTrackerConfig::MAX_TRACKED_ALLOCATIONS; i++)
    {
        if (s_records[i].inUse)
        {
            PRINTF("  0x%08lX %8lu %8lu %10lu 0x%08lX %s\r\n",
                   (uint32_t)s_records[i].ptr,
                   s_records[i].size,
                   s_records[i].actualSize,
                   s_records[i].tickCount,
                   (uint32_t)s_records[i].callerAddr,
                   s_records[i].taskName);
            count++;
        }
    }
    
    if (count == 0)
    {
        PRINTF("  (no active allocations)\r\n");
    }
    
    PRINTF("================================\r\n");
    PRINTF("Total active allocations: %lu\r\n\r\n", count);
}

void HeapTracker::PrintStats()
{
    if (!s_initialized)
    {
        PRINTF("[HeapTracker] Not initialized\r\n");
        return;
    }
    
    PRINTF("\r\n=== Heap Statistics ===\r\n");
    PRINTF("  Total allocations:     %lu\r\n", s_stats.totalAllocations);
    PRINTF("  Total deallocations:   %lu\r\n", s_stats.totalDeallocations);
    PRINTF("  Current allocations:   %lu\r\n", s_stats.currentAllocations);
    PRINTF("  Peak allocations:      %lu\r\n", s_stats.peakAllocations);
    PRINTF("  Total bytes allocated: %lu\r\n", s_stats.totalBytesAllocated);
    PRINTF("  Total bytes freed:     %lu\r\n", s_stats.totalBytesFreed);
    PRINTF("  Current bytes in use:  %lu\r\n", s_stats.currentBytesAllocated);
    PRINTF("  Peak bytes in use:     %lu\r\n", s_stats.peakBytesAllocated);
    PRINTF("  Failed allocations:    %lu\r\n", s_stats.failedAllocations);
    PRINTF("  Invalid frees:         %lu\r\n", s_stats.invalidFrees);
    PRINTF("=======================\r\n\r\n");
}

void HeapTracker::PrintLeaks(uint32_t minAgeTicks)
{
    if (!s_initialized)
    {
        PRINTF("[HeapTracker] Not initialized\r\n");
        return;
    }
    
    uint32_t currentTick = GetTickCount();
    
    PRINTF("\r\n=== Potential Memory Leaks (age > %lu ticks) ===\r\n", minAgeTicks);
    PRINTF("  %-10s %-8s %-10s %-10s %s\r\n", 
           "Pointer", "Size", "Age", "Caller", "Task");
    PRINTF("  ---------- -------- ---------- ---------- --------\r\n");
    
    uint32_t count = 0;
    uint32_t totalLeakedBytes = 0;
    
    for (uint32_t i = 0; i < HeapTrackerConfig::MAX_TRACKED_ALLOCATIONS; i++)
    {
        if (s_records[i].inUse)
        {
            uint32_t age = currentTick - s_records[i].tickCount;
            if (age >= minAgeTicks)
            {
                PRINTF("  0x%08lX %8lu %10lu 0x%08lX %s\r\n",
                       (uint32_t)s_records[i].ptr,
                       s_records[i].actualSize,
                       age,
                       (uint32_t)s_records[i].callerAddr,
                       s_records[i].taskName);
                count++;
                totalLeakedBytes += s_records[i].actualSize;
            }
        }
    }
    
    if (count == 0)
    {
        PRINTF("  (no potential leaks detected)\r\n");
    }
    else
    {
        PRINTF("================================================\r\n");
        PRINTF("Potential leaks: %lu allocations, %lu bytes\r\n", count, totalLeakedBytes);
    }
    PRINTF("\r\n");
}

const AllocationRecord* HeapTracker::GetAllocationRecord(void* ptr)
{
    if (!s_initialized || ptr == nullptr)
    {
        return nullptr;
    }
    
    int32_t slot = FindRecord(ptr);
    if (slot < 0)
    {
        return nullptr;
    }
    
    return &s_records[slot];
}

bool HeapTracker::IsEnabled()
{
    return s_enabled && s_initialized;
}

void HeapTracker::SetEnabled(bool enabled)
{
    s_enabled = enabled;
}

uint32_t HeapTracker::GetTaskAllocations(const char* taskName, 
                                          const AllocationRecord** records,
                                          uint32_t maxRecords)
{
    if (!s_initialized || taskName == nullptr || records == nullptr || maxRecords == 0)
    {
        return 0;
    }
    
    uint32_t count = 0;
    
    for (uint32_t i = 0; i < HeapTrackerConfig::MAX_TRACKED_ALLOCATIONS && count < maxRecords; i++)
    {
        if (s_records[i].inUse && strcmp(s_records[i].taskName, taskName) == 0)
        {
            records[count] = &s_records[i];
            count++;
        }
    }
    
    return count;
}

uint32_t HeapTracker::GetTaskBytesAllocated(const char* taskName)
{
    if (!s_initialized || taskName == nullptr)
    {
        return 0;
    }
    
    uint32_t totalBytes = 0;
    
    for (uint32_t i = 0; i < HeapTrackerConfig::MAX_TRACKED_ALLOCATIONS; i++)
    {
        if (s_records[i].inUse && strcmp(s_records[i].taskName, taskName) == 0)
        {
            totalBytes += s_records[i].actualSize;
        }
    }
    
    return totalBytes;
}

// Private methods

int32_t HeapTracker::FindFreeSlot()
{
    for (uint32_t i = 0; i < HeapTrackerConfig::MAX_TRACKED_ALLOCATIONS; i++)
    {
        if (!s_records[i].inUse)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

int32_t HeapTracker::FindRecord(void* ptr)
{
    for (uint32_t i = 0; i < HeapTrackerConfig::MAX_TRACKED_ALLOCATIONS; i++)
    {
        if (s_records[i].inUse && s_records[i].ptr == ptr)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void* HeapTracker::GetCallerAddress()
{
    // Get the return address from the link register
    // This gives us the caller's address for debugging
    void* callerAddr = nullptr;
    
#if defined(__GNUC__)
    callerAddr = __builtin_return_address(2);  // 2 levels up: HeapTracker -> HeapAllocator -> Caller
#else
    callerAddr = nullptr;
#endif
    
    return callerAddr;
}

const char* HeapTracker::GetCurrentTaskName()
{
    if (sCurrentTCB != nullptr)
    {
        return const_cast<const char*>(sCurrentTCB->name);
    }
    return nullptr;
}

uint32_t HeapTracker::GetTickCount()
{
    return tickCount;
}

} // namespace CRTOS
