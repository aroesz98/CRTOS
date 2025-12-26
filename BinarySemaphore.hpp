/*
 * BinarySemaphore.hpp - CRTOS Binary Semaphore Class
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

#ifndef BINARY_SEMAPHORE_HPP
#define BINARY_SEMAPHORE_HPP

#include <cstdint>

// Forward declarations
template <typename T> class Node;

namespace CRTOS
{
    enum class Result : uint8_t;

    class BinarySemaphore
    {
        public:
            BinarySemaphore() : listOfTasksWaitingToRecv(nullptr), _val(0) {}
            ~BinarySemaphore() = default;

            Result wait(uint32_t ticks);
            Result signal();

        private:
            Node<uint32_t*> *listOfTasksWaitingToRecv;
            uint32_t _val;
    };
}

#endif /* BINARY_SEMAPHORE_HPP */
