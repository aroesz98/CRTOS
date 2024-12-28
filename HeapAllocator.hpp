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
        Block *tail;
        void *mPool;
        uint32_t mPoolSize;

        uint32_t align8(uint32_t size);
        void split(Block *block, uint32_t size);
        void join(Block *block);
};

#endif /* HEAPALLOCATOR_HPP */
