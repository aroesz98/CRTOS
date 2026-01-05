/*
 * HeapAllocator
 * Author: Arkadiusz Szlanta
 * Date: 17 Dec 2024
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 */

#include <HeapAllocator.hpp>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cstddef>
#include "fsl_cache.h"  // For cache maintenance on SDRAM heap metadata

// Enable heap tracking for memory leak detection
#define HEAP_TRACKING_ENABLED
#include "HeapTracker.hpp"

// Forward declare TaskControlBlock structure to access heapAllocated field
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
};

extern "C" {
    extern volatile TaskControlBlock *sCurrentTCB;
}

static constexpr uint32_t MARKER = 0xDEADBEEFul;

HeapAllocator::HeapAllocator() : head(nullptr), tail(nullptr),
                                 mPool(nullptr), mPoolSize(0u)
{
}

void HeapAllocator::init(void *memoryPool, uint32_t totalSize)
{
    head = (Block *)memoryPool;
    head->size = totalSize - sizeof(Block) - 2 * sizeof(uint32_t);
    head->free = true;
    head->prev = nullptr;
    head->next = nullptr;
    head->startMarker = MARKER;
    head->endMarker = MARKER;

    mPool = memoryPool;
    mPoolSize = totalSize;
    tail = head;
    
    // Ensure Block header is written to memory (important for cached SDRAM)
    DCACHE_CleanByRange((uint32_t)head, sizeof(Block));
}

void* HeapAllocator::allocate(uint32_t size)
{
    if (size == 0)
    {
        return nullptr;
    }

    size = align8(size);

    Block* forward = head;
    Block* backward = tail;

    while (forward || backward)
    {
        if (forward)
        {
            if (forward->free && forward->size >= size)
            {
                if (forward->size >= size + sizeof(Block) + 2 * sizeof(uint32_t))
                {
                    split(forward, size);
                }
                forward->free = false;
                
                // Track allocation for current task
                if (sCurrentTCB != nullptr)
                {
                    TaskControlBlock *tcb = (TaskControlBlock *)sCurrentTCB;
                    tcb->heapAllocated += forward->size;
                    forward->ownerTCB = tcb;  // Store owner
                }
                else
                {
                    forward->ownerTCB = nullptr;
                }
                
                // Clean modified Block header
                DCACHE_CleanByRange((uint32_t)forward, sizeof(Block));
                
                return (void*)((char*)forward + sizeof(Block) + sizeof(uint32_t));
            }
            forward = forward->next;
        }

        if (backward && backward != forward)
        {
            if (backward->free && backward->size >= size)
            {
                if (backward->size >= size + sizeof(Block) + 2 * sizeof(uint32_t))
                {
                    split(backward, size);
                }
                backward->free = false;
                
                // Track allocation for current task
                if (sCurrentTCB != nullptr)
                {
                    TaskControlBlock *tcb = (TaskControlBlock *)sCurrentTCB;
                    tcb->heapAllocated += backward->size;
                    backward->ownerTCB = tcb;  // Store owner
                }
                else
                {
                    backward->ownerTCB = nullptr;
                }
                
                // Clean modified Block header
                DCACHE_CleanByRange((uint32_t)backward, sizeof(Block));
                
                return (void*)((char*)backward + sizeof(Block) + sizeof(uint32_t));
            }
            backward = backward->prev;
        }
    }

    return nullptr;
}


void HeapAllocator::deallocate(void *ptr)
{
    if (!ptr)
    {
        return;
    }

    Block *block = (Block *)((char *)ptr - sizeof(Block) - sizeof(uint32_t));
    if (block->startMarker != MARKER || block->endMarker != MARKER)
    {
        assert(false && "Memory corruption detected\r\n");
        return;
    }
    
    // Decrement heap counter for owner task
    if (block->ownerTCB != nullptr)
    {
        TaskControlBlock *tcb = (TaskControlBlock *)block->ownerTCB;
        if (tcb->heapAllocated >= block->size)
        {
            tcb->heapAllocated -= block->size;
        }
        block->ownerTCB = nullptr;
    }
    
    block->free = true;

    if (block->prev && block->prev->free)
    {
        block->prev->size += block->size + sizeof(Block) + 2 * sizeof(uint32_t);
        block->prev->next = block->next;
        if (block->next)
        {
            block->next->prev = block->prev;
            DCACHE_CleanByRange((uint32_t)block->next, sizeof(Block));
        }
        block->prev->endMarker = MARKER;
        DCACHE_CleanByRange((uint32_t)block->prev, sizeof(Block));
        block = block->prev;
    }
    if (block->next && block->next->free)
    {
        block->size += block->next->size + sizeof(Block) + 2 * sizeof(uint32_t);
        block->next = block->next->next;
        if (block->next)
        {
            block->next->prev = block;
            DCACHE_CleanByRange((uint32_t)block->next, sizeof(Block));
        }
        block->endMarker = MARKER;
    }

    if (block->next == nullptr)
    {
        tail = block;
    }

    // Clean the block header after all modifications
    DCACHE_CleanByRange((uint32_t)block, sizeof(Block));

    join(block);

    ptr = nullptr;
}

void HeapAllocator::getMemoryPool(void **memoryPool, uint32_t &totalSize)
{
    *memoryPool = mPool;
    totalSize = mPoolSize;
}

uint32_t HeapAllocator::getFreeMemory() const
{
    uint32_t freeMemory = 0;
    Block *current = head;

    while (current)
    {
        if (current->free)
        {
            freeMemory += current->size;
        }
        current = current->next;
    }
    return freeMemory;
}

uint32_t HeapAllocator::getAllocatedMemory() const
{
    uint32_t allocatedMemory = 0;
    Block *current = head;
    while (current)
    {
        if (!current->free)
        {
            allocatedMemory += current->size;
        }
        current = current->next;
    }
    return allocatedMemory;
}

uint32_t HeapAllocator::align8(uint32_t size)
{
    return (size + 7) & ~7;
}

void HeapAllocator::split(Block *block, uint32_t size)
{
    Block *newBlock = (Block *)((char *)block + sizeof(Block) + size + 2 * sizeof(uint32_t));
    newBlock->size = block->size - size - sizeof(Block) - 2 * sizeof(uint32_t);
    newBlock->free = true;
    newBlock->prev = block;
    newBlock->next = block->next;
    newBlock->startMarker = MARKER;
    newBlock->endMarker = MARKER;
    if (block->next)
    {
        block->next->prev = newBlock;
        // Clean the modified prev pointer
        DCACHE_CleanByRange((uint32_t)block->next, sizeof(Block));
    }
    block->next = newBlock;
    block->size = size;
    block->endMarker = MARKER;

    if (newBlock->next == nullptr)
    {
        tail = newBlock;
    }
    
    // Clean both modified Block headers to ensure they're written to memory
    DCACHE_CleanByRange((uint32_t)block, sizeof(Block));
    DCACHE_CleanByRange((uint32_t)newBlock, sizeof(Block));
}

void HeapAllocator::join(Block *block)
{
    if (block->prev && block->prev->free)
    {
        block->prev->size += block->size + sizeof(Block) + 2 * sizeof(uint32_t);
        block->prev->next = block->next;

        if (block->next)
        {
            block->next->prev = block->prev;
            DCACHE_CleanByRange((uint32_t)block->next, sizeof(Block));
        }

        block->prev->endMarker = MARKER;
        DCACHE_CleanByRange((uint32_t)block->prev, sizeof(Block));
        block = block->prev;
    }

    if (block->next && block->next->free)
    {
        block->size += block->next->size + sizeof(Block) + 2 * sizeof(uint32_t);
        block->next = block->next->next;

        if (block->next)
        {
            block->next->prev = block;
            DCACHE_CleanByRange((uint32_t)block->next, sizeof(Block));
        }

        block->endMarker = MARKER;
    }

    if (block->next == nullptr)
    {
        tail = block;
    }
    
    // Clean the final block state
    DCACHE_CleanByRange((uint32_t)block, sizeof(Block));
}

void HeapAllocator::defragment()
{
    uint32_t blocksBefore = 0;
    uint32_t fragmentsBefore = 0;
    bool lastWasFree = false;
    
    Block *current = head;
    while (current != nullptr)
    {
        blocksBefore++;
        if (current->free)
        {
            if (!lastWasFree && blocksBefore > 1)
            {
                fragmentsBefore++;
            }
            lastWasFree = true;
        }
        else
        {
            lastWasFree = false;
        }
        current = current->next;
    }
    
    // Perform defragmentation - coalesce all adjacent free blocks
    uint32_t mergeCount = 0;
    current = head;
    
    while (current != nullptr)
    {
        if (current->free && current->next != nullptr && current->next->free)
        {
            // Merge current with next free block
            current->size += current->next->size + sizeof(Block) + 2 * sizeof(uint32_t);
            current->next = current->next->next;
            
            if (current->next != nullptr)
            {
                current->next->prev = current;
            }
            else
            {
                tail = current;
            }
            
            current->endMarker = MARKER;
            mergeCount++;
            
            // Don't advance - check if we can merge with the new next block
            continue;
        }
        
        current = current->next;
    }
    
    // Gather statistics after defragmentation
    uint32_t blocksAfter = 0;
    uint32_t fragmentsAfter = 0;
    lastWasFree = false;
    
    current = head;
    while (current != nullptr)
    {
        blocksAfter++;
        if (current->free)
        {
            if (!lastWasFree && blocksAfter > 1)
            {
                fragmentsAfter++;
            }
            lastWasFree = true;
        }
        else
        {
            lastWasFree = false;
        }
        current = current->next;
    }
}

bool HeapAllocator::contains(void* ptr) const
{
    if (mPool == nullptr || ptr == nullptr)
        return false;
    
    uint32_t addr = (uint32_t)ptr;
    uint32_t poolStart = (uint32_t)mPool;
    uint32_t poolEnd = poolStart + mPoolSize;
    
    return (addr >= poolStart && addr < poolEnd);
}

// ============================================================================
// Static Unified Memory Management Implementation
// ============================================================================

// Static member initialization
Memory::MemoryRegion HeapAllocator::s_regions[MAX_REGIONS] = {};
HeapAllocator HeapAllocator::s_allocators[MAX_REGIONS];
uint32_t HeapAllocator::s_regionCount = 0;

bool HeapAllocator::RegisterRegion(const char* name, void* pool, uint32_t size, 
                                   uint32_t flags)
{
    if (s_regionCount >= MAX_REGIONS || pool == nullptr || size == 0)
    {
        return false;
    }
    
    // Add region at the end (first registered = first checked)
    uint32_t index = s_regionCount;
    
    // Initialize the new region
    s_regions[index].name = name;
    s_regions[index].baseAddress = pool;
    s_regions[index].size = size;
    s_regions[index].flags = flags;
    s_regions[index].initialized = true;
    
    s_allocators[index].init(pool, size);
    
    s_regionCount++;
    
    return true;
}

int32_t HeapAllocator::FindBestRegion(uint32_t size, const Memory::AllocHints& hints)
{
    int32_t bestMatch = -1;
    int32_t bestScore = -1;
    
    for (uint32_t i = 0; i < s_regionCount; i++)
    {
        const Memory::MemoryRegion& region = s_regions[i];
        
        if (!region.initialized)
            continue;
        
        // Check required flags - must all be present
        if (hints.requiredFlags != Memory::MEM_NONE)
        {
            if ((region.flags & hints.requiredFlags) != hints.requiredFlags)
                continue;
        }
        
        // Check excluded flags - must not be present
        if (hints.excludeFlags != Memory::MEM_NONE)
        {
            if ((region.flags & hints.excludeFlags) != 0)
                continue;
        }
        
        // Check if region has enough free memory
        if (s_allocators[i].getFreeMemory() < size)
            continue;
        
        // Calculate match score - registration order is the PRIMARY factor
        // First registered region gets highest score
        int32_t score = (MAX_REGIONS - i) * 1000;
        
        // Count matching preferred flags as secondary factor
        if (hints.preferredFlags != Memory::MEM_NONE)
        {
            uint32_t matched = region.flags & hints.preferredFlags;
            while (matched)
            {
                score += 10;
                matched &= (matched - 1);
            }
        }
        
        if (score > bestScore)
        {
            bestScore = score;
            bestMatch = (int32_t)i;
        }
    }
    
    return bestMatch;
}

int32_t HeapAllocator::FindRegionForPointer(void* ptr)
{
    if (ptr == nullptr)
        return -1;
    
    for (uint32_t i = 0; i < s_regionCount; i++)
    {
        if (s_allocators[i].contains(ptr))
        {
            return (int32_t)i;
        }
    }
    
    return -1;
}

// Marker used to identify aligned allocations
static constexpr uint32_t ALIGNED_MARKER = 0xA116DEAD;

// Structure stored before aligned pointer
struct AlignedHeader
{
    void* rawPtr;
    uint32_t marker;
};

void* HeapAllocator::Allocate(uint32_t size, const Memory::AllocHints& hints)
{
    if (size == 0)
        return nullptr;
    
    // Determine actual allocation size
    uint32_t alignment = hints.alignment > 8 ? hints.alignment : 8;
    uint32_t totalSize = size;
    
    // For alignments > 8, we need to over-allocate
    // We'll store AlignedHeader just before the aligned pointer
    bool needsAlignmentPadding = (alignment > 8);
    if (needsAlignmentPadding)
    {
        // Add extra space for alignment and header storage
        totalSize = size + alignment + sizeof(AlignedHeader);
    }
    
    // Find best matching region
    int32_t regionIndex = FindBestRegion(totalSize, hints);
    
    if (regionIndex >= 0)
    {
        void* rawPtr = s_allocators[regionIndex].allocate(totalSize);
        if (rawPtr != nullptr)
        {
            void* resultPtr = rawPtr;
            
            if (needsAlignmentPadding)
            {
                // Calculate aligned pointer (after leaving room for header)
                uintptr_t rawAddr = (uintptr_t)rawPtr;
                uintptr_t alignedAddr = (rawAddr + sizeof(AlignedHeader) + alignment - 1) & ~(alignment - 1);
                
                // Store header just before aligned address
                AlignedHeader* header = (AlignedHeader*)(alignedAddr - sizeof(AlignedHeader));
                header->rawPtr = rawPtr;
                header->marker = ALIGNED_MARKER;
                
                resultPtr = (void*)alignedAddr;
            }
            
            // Track allocation
            HEAP_TRACK_ALLOC(resultPtr, size, totalSize, regionIndex);
            
            return resultPtr;
        }
    }
    
    // Fallback: try any region that has enough space
    for (uint32_t i = 0; i < s_regionCount; i++)
    {
        if ((int32_t)i == regionIndex)
            continue;  // Already tried
        
        // Skip if required flags not met
        if (hints.requiredFlags != Memory::MEM_NONE)
        {
            if ((s_regions[i].flags & hints.requiredFlags) != hints.requiredFlags)
                continue;
        }
        
        // Skip if excluded flags present
        if (hints.excludeFlags != Memory::MEM_NONE)
        {
            if ((s_regions[i].flags & hints.excludeFlags) != 0)
                continue;
        }
        
        void* rawPtr = s_allocators[i].allocate(totalSize);
        if (rawPtr != nullptr)
        {
            void* resultPtr = rawPtr;
            
            if (needsAlignmentPadding)
            {
                // Calculate aligned pointer (after leaving room for header)
                uintptr_t rawAddr = (uintptr_t)rawPtr;
                uintptr_t alignedAddr = (rawAddr + sizeof(AlignedHeader) + alignment - 1) & ~(alignment - 1);
                
                // Store header just before aligned address
                AlignedHeader* header = (AlignedHeader*)(alignedAddr - sizeof(AlignedHeader));
                header->rawPtr = rawPtr;
                header->marker = ALIGNED_MARKER;
                
                resultPtr = (void*)alignedAddr;
            }
            
            // Track allocation
            HEAP_TRACK_ALLOC(resultPtr, size, totalSize, i);
            
            return resultPtr;
        }
    }
    
    // Track allocation failure
    HEAP_TRACK_FAIL(size);
    
    return nullptr;
}

void HeapAllocator::Free(void* ptr)
{
    if (ptr == nullptr)
        return;
    
    // Track deallocation
    HEAP_TRACK_FREE(ptr);
    
    void* rawPtr = ptr;
    
    // Check if this is an aligned allocation by looking for the marker
    AlignedHeader* header = (AlignedHeader*)((uintptr_t)ptr - sizeof(AlignedHeader));
    
    if (header->marker == ALIGNED_MARKER)
    {
        // This was an aligned allocation, use the stored raw pointer
        rawPtr = header->rawPtr;
        // Clear marker to prevent double-free issues
        header->marker = 0;
    }
    
    int32_t regionIndex = FindRegionForPointer(rawPtr);
    
    if (regionIndex >= 0)
    {
        s_allocators[regionIndex].deallocate(rawPtr);
    }
}

uint32_t HeapAllocator::GetFreeMemory(uint32_t flags)
{
    uint32_t total = 0;
    
    for (uint32_t i = 0; i < s_regionCount; i++)
    {
        if (!s_regions[i].initialized)
            continue;
        
        // If flags specified, only count matching regions
        if (flags != Memory::MEM_NONE)
        {
            if ((s_regions[i].flags & flags) != flags)
                continue;
        }
        
        total += s_allocators[i].getFreeMemory();
    }
    
    return total;
}

uint32_t HeapAllocator::GetTotalMemory(uint32_t flags)
{
    uint32_t total = 0;
    
    for (uint32_t i = 0; i < s_regionCount; i++)
    {
        if (!s_regions[i].initialized)
            continue;
        
        // If flags specified, only count matching regions
        if (flags != Memory::MEM_NONE)
        {
            if ((s_regions[i].flags & flags) != flags)
                continue;
        }
        
        total += s_regions[i].size;
    }
    
    return total;
}

uint32_t HeapAllocator::GetRegionCount()
{
    return s_regionCount;
}

const Memory::MemoryRegion* HeapAllocator::GetRegion(uint32_t index)
{
    if (index >= s_regionCount)
        return nullptr;
    
    return &s_regions[index];
}

void HeapAllocator::PrintRegions()
{
    extern int printf(const char* format, ...);
    
    printf("\r\n=== Memory Regions ===\r\n");
    printf("Total regions: %u\r\n\r\n", s_regionCount);
    
    for (uint32_t i = 0; i < s_regionCount; i++)
    {
        const Memory::MemoryRegion& r = s_regions[i];
        if (!r.initialized)
            continue;
        
        printf("[%u] %s\r\n", i, r.name);
        printf("    Base: 0x%08X, Size: %u bytes\r\n", (uint32_t)r.baseAddress, r.size);
        printf("    Free: %u bytes, Used: %u bytes\r\n", 
               s_allocators[i].getFreeMemory(), s_allocators[i].getAllocatedMemory());
        printf("    Flags: 0x%08X\r\n", r.flags);
        
        // Print flag descriptions
        printf("    Properties: ");
        if (r.flags & Memory::MEM_CACHED) printf("CACHED ");
        if (r.flags & Memory::MEM_DMA_CAPABLE) printf("DMA ");
        if (r.flags & Memory::MEM_EXECUTABLE) printf("EXEC ");
        if (r.flags & Memory::MEM_FAST) printf("FAST ");
        if (r.flags & Memory::MEM_LARGE) printf("LARGE ");
        if (r.flags & Memory::MEM_NON_CACHED) printf("NOCACHE ");
        printf("\r\n\r\n");
    }
}

uint32_t HeapAllocator::GetRegionFreeMemory(uint32_t index)
{
    if (index >= s_regionCount)
        return 0;
    
    return s_allocators[index].getFreeMemory();
}

uint32_t HeapAllocator::GetRegionAllocatedMemory(uint32_t index)
{
    if (index >= s_regionCount)
        return 0;
    
    return s_allocators[index].getAllocatedMemory();
}


