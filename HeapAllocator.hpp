/*
 * HeapAllocator
 * Author: Arkadiusz Szlanta
 * Date: 17 Dec 2024
 *
 * Unified Memory Allocator with support for multiple memory regions
 * with different properties (DMA capable, cached, non-cached, etc.)
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 */

#ifndef HEAPALLOCATOR_HPP
#define HEAPALLOCATOR_HPP

#include <cstdint>

namespace Memory
{

/**
 * @brief Memory region property flags (can be combined with bitwise OR)
 */
enum MemoryFlags : uint32_t
{
    MEM_NONE            = 0x00000000,   ///< No special properties
    MEM_CACHED          = 0x00000001,   ///< Memory is cacheable
    MEM_DMA_CAPABLE     = 0x00000002,   ///< Memory can be used for DMA transfers
    MEM_EXECUTABLE      = 0x00000004,   ///< Memory can be used for code execution
    MEM_FAST            = 0x00000008,   ///< Fast access memory (TCM, OCRAM)
    MEM_LARGE           = 0x00000010,   ///< Large memory pool (external RAM)
    MEM_NON_CACHED      = 0x00000020,   ///< Non-cacheable memory
    MEM_WRITE_THROUGH   = 0x00000040,   ///< Write-through cache policy
    MEM_WRITE_BACK      = 0x00000080,   ///< Write-back cache policy
    MEM_DEVICE          = 0x00000100,   ///< Device memory (for MMIO)
    MEM_SECURE          = 0x00000200,   ///< Secure memory region
    
    // Convenience combinations
    MEM_DEFAULT         = MEM_CACHED | MEM_FAST,
    MEM_DMA             = MEM_DMA_CAPABLE | MEM_NON_CACHED,
    MEM_FRAMEBUFFER     = MEM_DMA_CAPABLE | MEM_LARGE | MEM_NON_CACHED,
};

/**
 * @brief Memory region descriptor
 */
struct MemoryRegion
{
    const char* name;           ///< Human-readable name
    void* baseAddress;          ///< Start address of the region
    uint32_t size;              ///< Size in bytes
    uint32_t flags;             ///< MemoryFlags properties
    bool initialized;           ///< Has been initialized
};

/**
 * @brief Allocation hints for the allocator
 */
struct AllocHints
{
    uint32_t requiredFlags;     ///< Flags that MUST be present
    uint32_t preferredFlags;    ///< Flags that are preferred but not required
    uint32_t excludeFlags;      ///< Flags that must NOT be present
    uint32_t alignment;         ///< Alignment requirement (power of 2)
    
    // Default constructor - no special requirements
    AllocHints() : requiredFlags(MEM_NONE), preferredFlags(MEM_DEFAULT), 
                   excludeFlags(MEM_NONE), alignment(8) {}
    
    // Convenience constructors
    static AllocHints Default() { return AllocHints(); }
    static AllocHints DMA() { 
        AllocHints h; 
        h.requiredFlags = MEM_DMA_CAPABLE; 
        h.excludeFlags = MEM_CACHED;
        return h; 
    }
    static AllocHints Fast() { 
        AllocHints h; 
        h.preferredFlags = MEM_FAST; 
        return h; 
    }
    static AllocHints Large() { 
        AllocHints h; 
        h.requiredFlags = MEM_LARGE;  // Require large memory (SDRAM)
        return h; 
    }
    static AllocHints Aligned(uint32_t align) { 
        AllocHints h; 
        h.alignment = align; 
        return h; 
    }
};

} // namespace Memory

/**
 * @brief Unified Heap Allocator
 * 
 * Manages multiple memory regions with different properties.
 * Automatically selects the best region based on allocation hints.
 */
class HeapAllocator
{
public:
    /// Maximum number of memory regions that can be registered
    static constexpr uint32_t MAX_REGIONS = 8;

    HeapAllocator();

    // ========================================================================
    // Instance Methods (for managing a single memory pool)
    // ========================================================================
    
    /**
     * @brief Initialize allocator with a memory pool
     */
    void init(void* memoryPool, uint32_t totalSize);
    
    /**
     * @brief Allocate memory from this pool
     */
    void* allocate(uint32_t size);
    
    /**
     * @brief Free memory back to this pool
     */
    void deallocate(void* ptr);
    
    /**
     * @brief Check if pointer belongs to this pool
     */
    bool contains(void* ptr) const;
    
    /**
     * @brief Get free memory in this pool
     */
    uint32_t getFreeMemory() const;
    
    /**
     * @brief Get allocated memory in this pool
     */
    uint32_t getAllocatedMemory() const;
    
    /**
     * @brief Get pool info
     */
    void getMemoryPool(void** memoryPool, uint32_t& totalSize);
    
    /**
     * @brief Print memory map for debugging
     */
    void printMemoryGraph() const;
    
    /**
     * @brief Defragment memory pool
     */
    void defragment();

    // ========================================================================
    // Static Methods (Global Unified Allocator API)
    // ========================================================================
    
    /**
     * @brief Register a memory region with the allocator
     * 
     * @param name Human-readable name for the region
     * @param pool Base address of the memory region
     * @param size Size in bytes
     * @param flags Memory properties (MemoryFlags)
     * @return true if registered successfully
     */
    static bool RegisterRegion(const char* name, void* pool, uint32_t size, 
                               uint32_t flags);
    
    /**
     * @brief Allocate memory with optional hints
     * 
     * @param size Size in bytes
     * @param hints Allocation hints (optional, defaults to standard allocation)
     * @return Pointer to allocated memory or nullptr on failure
     */
    static void* Allocate(uint32_t size, const Memory::AllocHints& hints = Memory::AllocHints());
    
    /**
     * @brief Free previously allocated memory
     * 
     * Automatically determines which region the memory belongs to.
     * 
     * @param ptr Pointer to free
     */
    static void Free(void* ptr);
    
    /**
     * @brief Get total free memory across all regions
     * 
     * @param flags If non-zero, only count regions with these flags
     * @return Total free bytes
     */
    static uint32_t GetFreeMemory(uint32_t flags = Memory::MEM_NONE);
    
    /**
     * @brief Get total memory across all regions
     * 
     * @param flags If non-zero, only count regions with these flags
     * @return Total bytes
     */
    static uint32_t GetTotalMemory(uint32_t flags = Memory::MEM_NONE);
    
    /**
     * @brief Get number of registered regions
     */
    static uint32_t GetRegionCount();
    
    /**
     * @brief Get region info by index
     */
    static const Memory::MemoryRegion* GetRegion(uint32_t index);
    
    /**
     * @brief Get free memory for a specific region by index
     */
    static uint32_t GetRegionFreeMemory(uint32_t index);
    
    /**
     * @brief Get allocated memory for a specific region by index
     */
    static uint32_t GetRegionAllocatedMemory(uint32_t index);
    
    /**
     * @brief Print all registered regions and their status
     */
    static void PrintRegions();

private:
    // Block header for tracking allocations
    struct Block
    {
        uint32_t startMarker;
        uint32_t size;
        bool free;
        Block* prev;
        Block* next;
        void* ownerTCB;
        uint32_t endMarker;
    };

    Block* head;
    Block* tail;
    void* mPool;
    uint32_t mPoolSize;

    uint32_t align8(uint32_t size);
    void split(Block* block, uint32_t size);
    void join(Block* block);

    // Static region management
    static Memory::MemoryRegion s_regions[MAX_REGIONS];
    static HeapAllocator s_allocators[MAX_REGIONS];
    static uint32_t s_regionCount;
    
    // Find best matching region for allocation
    static int32_t FindBestRegion(uint32_t size, const Memory::AllocHints& hints);
    
    // Find which region contains a pointer
    static int32_t FindRegionForPointer(void* ptr);
};

// ============================================================================
// Convenience macros for common allocation patterns
// ============================================================================

// Standard allocation (uses default hints)
#define MEM_ALLOC(size)             HeapAllocator::Allocate(size)

// DMA-capable allocation
#define MEM_ALLOC_DMA(size)         HeapAllocator::Allocate(size, Memory::AllocHints::DMA())

// Fast memory allocation
#define MEM_ALLOC_FAST(size)        HeapAllocator::Allocate(size, Memory::AllocHints::Fast())

// Large allocation (prefers external memory)
#define MEM_ALLOC_LARGE(size)       HeapAllocator::Allocate(size, Memory::AllocHints::Large())

// Aligned allocation
#define MEM_ALLOC_ALIGNED(size, a)  HeapAllocator::Allocate(size, Memory::AllocHints::Aligned(a))

// Free memory
#define MEM_FREE(ptr)               HeapAllocator::Free(ptr)

#endif /* HEAPALLOCATOR_HPP */
