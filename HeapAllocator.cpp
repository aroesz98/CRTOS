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
#include <cassert>

const uint32_t MARKER = 0xDEADBEEF;

HeapAllocator::HeapAllocator() : head(nullptr),
                                 mPool(nullptr),
                                 mPoolSize(0u)
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
}

void *HeapAllocator::allocate(uint32_t size)
{
    size = align8(size);
    Block *current = head;
    while (current)
    {
        if (current->free && current->size >= size)
        {
            if (current->size >= size + sizeof(Block) + 2 * sizeof(uint32_t))
            {
                split(current, size);
            }
            current->free = false;
            return (void *)((char *)current + sizeof(Block) + sizeof(uint32_t));
        }
        current = current->next;
    }
    return nullptr;
}

void HeapAllocator::deallocate(void *ptr)
{
    if (!ptr)
        return;

    Block *block = (Block *)((char *)ptr - sizeof(Block) - sizeof(uint32_t));
    if (block->startMarker != MARKER || block->endMarker != MARKER)
    {
        assert(false);
        return;
    }
    block->free = true;
    join(block);
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
    }
    block->next = newBlock;
    block->size = size;
    block->endMarker = MARKER;
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
        }
        block->prev->endMarker = MARKER;
        block = block->prev;
    }
    if (block->next && block->next->free)
    {
        block->size += block->next->size + sizeof(Block) + 2 * sizeof(uint32_t);
        block->next = block->next->next;
        if (block->next)
        {
            block->next->prev = block;
        }
        block->endMarker = MARKER;
    }
}
