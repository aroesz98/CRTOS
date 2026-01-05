/*
 * HeapTracker
 * Author: Arkadiusz Szlanta
 * Date: 05 Jan 2026
 *
 * Memory leak detection and heap tracking for CRTOS.
 * Tracks all allocations with caller information to help identify leaks.
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 */

#ifndef HEAP_TRACKER_HPP
#define HEAP_TRACKER_HPP

#include <cstdint>

namespace CRTOS
{

/**
 * @brief Configuration for HeapTracker
 */
struct HeapTrackerConfig
{
    static constexpr uint32_t MAX_TRACKED_ALLOCATIONS = 256;  ///< Maximum concurrent allocations to track
    static constexpr bool TRACK_CALL_STACK = true;            ///< Track caller address
    static constexpr bool TRACK_TASK_NAME = true;             ///< Track owning task name
    static constexpr uint32_t TASK_NAME_LEN = 16;             ///< Max task name length to store
};

/**
 * @brief Record of a single allocation
 */
struct AllocationRecord
{
    void*    ptr;                                       ///< Allocated pointer
    uint32_t size;                                      ///< Requested size
    uint32_t actualSize;                                ///< Actual allocated size (aligned)
    void*    callerAddr;                                ///< Return address of caller (LR)
    uint32_t tickCount;                                 ///< Tick when allocated
    uint32_t regionIndex;                               ///< Memory region index
    char     taskName[HeapTrackerConfig::TASK_NAME_LEN];///< Name of allocating task
    bool     inUse;                                     ///< Slot is in use
};

/**
 * @brief Heap statistics
 */
struct HeapStats
{
    uint32_t totalAllocations;      ///< Total number of allocations
    uint32_t totalDeallocations;    ///< Total number of deallocations
    uint32_t currentAllocations;    ///< Current active allocations
    uint32_t peakAllocations;       ///< Peak number of concurrent allocations
    uint32_t totalBytesAllocated;   ///< Total bytes ever allocated
    uint32_t totalBytesFreed;       ///< Total bytes ever freed
    uint32_t currentBytesAllocated; ///< Current bytes in use
    uint32_t peakBytesAllocated;    ///< Peak bytes in use
    uint32_t failedAllocations;     ///< Number of failed allocations
    uint32_t doubleFrees;           ///< Number of double-free attempts
    uint32_t invalidFrees;          ///< Number of invalid pointer frees
};

/**
 * @brief Leak report entry
 */
struct LeakReport
{
    const AllocationRecord* record; ///< Pointer to allocation record
    uint32_t ageInTicks;            ///< How old is this allocation
};

/**
 * @brief Heap Tracker - Memory leak detection system
 */
class HeapTracker
{
public:
    /**
     * @brief Initialize the heap tracker
     */
    static void Init();
    
    /**
     * @brief Record a new allocation
     * 
     * @param ptr Allocated pointer
     * @param requestedSize Size requested by caller
     * @param actualSize Actual size allocated (after alignment)
     * @param regionIndex Memory region index (from HeapAllocator)
     */
    static void RecordAllocation(void* ptr, uint32_t requestedSize, 
                                  uint32_t actualSize, uint32_t regionIndex);
    
    /**
     * @brief Record a deallocation
     * 
     * @param ptr Pointer being freed
     * @return true if valid deallocation, false if error (double-free, invalid ptr)
     */
    static bool RecordDeallocation(void* ptr);
    
    /**
     * @brief Record a failed allocation attempt
     * 
     * @param requestedSize Size that failed to allocate
     */
    static void RecordAllocationFailure(uint32_t requestedSize);
    
    /**
     * @brief Get current heap statistics
     */
    static const HeapStats& GetStats();
    
    /**
     * @brief Reset all statistics (does not affect tracking)
     */
    static void ResetStats();
    
    /**
     * @brief Check for potential memory leaks
     * 
     * Returns allocations that are older than the specified age.
     * 
     * @param minAgeTicks Minimum age in ticks to consider as potential leak
     * @param reports Array to fill with leak reports
     * @param maxReports Maximum number of reports to return
     * @return Number of potential leaks found
     */
    static uint32_t CheckForLeaks(uint32_t minAgeTicks, LeakReport* reports, 
                                   uint32_t maxReports);
    
    /**
     * @brief Print all current allocations to debug console
     */
    static void PrintAllocations();
    
    /**
     * @brief Print heap statistics to debug console
     */
    static void PrintStats();
    
    /**
     * @brief Print potential memory leaks
     * 
     * @param minAgeTicks Minimum age in ticks to consider as potential leak
     */
    static void PrintLeaks(uint32_t minAgeTicks = 10000);
    
    /**
     * @brief Get allocation record for a pointer
     * 
     * @param ptr Pointer to look up
     * @return Pointer to record or nullptr if not found
     */
    static const AllocationRecord* GetAllocationRecord(void* ptr);
    
    /**
     * @brief Check if tracking is enabled
     */
    static bool IsEnabled();
    
    /**
     * @brief Enable/disable tracking
     */
    static void SetEnabled(bool enabled);
    
    /**
     * @brief Get allocations for a specific task
     * 
     * @param taskName Name of the task
     * @param records Array to fill with matching records
     * @param maxRecords Maximum records to return
     * @return Number of records found
     */
    static uint32_t GetTaskAllocations(const char* taskName, 
                                        const AllocationRecord** records,
                                        uint32_t maxRecords);
    
    /**
     * @brief Get total bytes allocated by a specific task
     */
    static uint32_t GetTaskBytesAllocated(const char* taskName);

private:
    static AllocationRecord s_records[HeapTrackerConfig::MAX_TRACKED_ALLOCATIONS];
    static HeapStats s_stats;
    static bool s_enabled;
    static bool s_initialized;
    
    // Find a free slot in records array
    static int32_t FindFreeSlot();
    
    // Find record for a pointer
    static int32_t FindRecord(void* ptr);
    
    // Get caller address (LR register)
    static void* GetCallerAddress();
    
    // Get current task name
    static const char* GetCurrentTaskName();
    
    // Get current tick count
    static uint32_t GetTickCount();
};

} // namespace CRTOS

// ============================================================================
// Macros for easy integration
// ============================================================================

#ifdef HEAP_TRACKING_ENABLED

#define HEAP_TRACK_ALLOC(ptr, reqSize, actSize, region) \
    CRTOS::HeapTracker::RecordAllocation(ptr, reqSize, actSize, region)

#define HEAP_TRACK_FREE(ptr) \
    CRTOS::HeapTracker::RecordDeallocation(ptr)

#define HEAP_TRACK_FAIL(size) \
    CRTOS::HeapTracker::RecordAllocationFailure(size)

#else

#define HEAP_TRACK_ALLOC(ptr, reqSize, actSize, region) ((void)0)
#define HEAP_TRACK_FREE(ptr) ((void)0)
#define HEAP_TRACK_FAIL(size) ((void)0)

#endif

#endif /* HEAP_TRACKER_HPP */
