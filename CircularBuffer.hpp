/*
 * CircularBuffer.hpp - CRTOS Circular Buffer Class
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

#ifndef CIRCULAR_BUFFER_HPP
#define CIRCULAR_BUFFER_HPP

#include <cstdint>

// Forward declarations
template <typename T> class Node;

namespace CRTOS
{
    enum class Result : uint8_t;

    class CircularBuffer
    {
        private:
            uint8_t* mBuffer;
            uint32_t mHead;
            uint32_t mTail;
            uint32_t mCurrentSize;
            uint32_t mBufferSize;
            Node<uint32_t*> *listOfTasksWaitingToRecv = nullptr;

        public:
            CircularBuffer(uint32_t mBuffer_size);
            CircularBuffer(const CircularBuffer& old);
            ~CircularBuffer(void);

            Result Init(void);

            Result Send(const uint8_t* data, uint32_t size);
            Result Receive(uint8_t* data, uint32_t size, uint32_t timeout_ms = 0u);
    };
}

#endif /* CIRCULAR_BUFFER_HPP */
