/*
 * MemoryAllocator.hpp
 *
 *  Created on: 15 gru 2024
 *      Author: Administrator
 */

#ifndef HEAPALLOCATOR_HPP
#define HEAPALLOCATOR_HPP

#include <cstdint>

class HeapAllocator
{
    public:
        HeapAllocator();

        void init(void *memoryPool, uint32_t totalSize);
        void* allocate(uint32_t size);
        void deallocate(void *ptr);
        void getMemoryPool(void **memoryPool, uint32_t &totalSize);

        uint32_t getFreeMemory() const;
        uint32_t getAllocatedMemory() const;

    private:
        struct Block
        {
            uint32_t size;
            bool free;
            Block *prev;
            Block *next;
            uint32_t startMarker;
            uint32_t endMarker;
        };

        Block *head;
        void *mPool;
        uint32_t mPoolSize;

        uint32_t align8(uint32_t size);
        void split(Block *block, uint32_t size);
        void join(Block *block);
};

#endif /* HEAPALLOCATOR_HPP */
