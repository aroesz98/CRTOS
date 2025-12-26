/*
 * Queue.hpp - CRTOS Queue Class
 * Author: Arkadiusz Szlanta
 * Date: 25 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 */

#ifndef QUEUE_HPP
#define QUEUE_HPP

#include <cstdint>

// Forward declarations
template <typename T> class Node;

namespace CRTOS
{
    enum class Result : uint8_t;

    class Queue
    {
        private:
            uint8_t *mQueue;
            uint32_t mFront;
            uint32_t mRear;
            uint32_t mSize;
            uint32_t mMaxSize;
            uint32_t mElementSize;
            Node<uint32_t*> *listOfTasksWaitingToRecv = nullptr;

        public:
            Queue(uint32_t maxsize, uint32_t element_size);
            ~Queue(void);

            Result Send(void* item);
            Result Receive(void* item, uint32_t timeout = 0u);
    };
}

#endif /* QUEUE_HPP */
