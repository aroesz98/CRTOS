/*
 * RingBuffer.hpp - Template Ring Buffer for Drivers
 * Author: Arkadiusz Szlanta
 * Date: 26 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * Interrupt-safe ring buffer template class for use in device drivers.
 * Designed for single producer, single consumer (SPSC) scenarios.
 */

#ifndef DRIVER_RING_BUFFER_HPP
#define DRIVER_RING_BUFFER_HPP

#include <cstdint>
#include <cstddef>
#include <atomic>

namespace CRTOS
{
namespace Drivers
{

/**
 * @brief Lock-free ring buffer for driver use
 *
 * This ring buffer is designed for ISR-to-task communication where
 * one side produces and one side consumes. It's lock-free for SPSC use case.
 *
 * @tparam T        Type of elements in the buffer
 * @tparam Size     Maximum number of elements in the buffer
 */
template <typename T, size_t Size>
class RingBuffer
{
public:
    RingBuffer() : _head(0), _tail(0) {}

    /**
     * @brief Reset the buffer to empty state
     */
    void reset()
    {
        _head.store(0, std::memory_order_relaxed);
        _tail.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Check if buffer is empty
     *
     * @return true if empty, false otherwise
     */
    bool isEmpty() const
    {
        return _head.load(std::memory_order_acquire) == _tail.load(std::memory_order_acquire);
    }

    /**
     * @brief Check if buffer is full
     *
     * @return true if full, false otherwise
     */
    bool isFull() const
    {
        size_t nextHead = (_head.load(std::memory_order_relaxed) + 1) % Size;
        return nextHead == _tail.load(std::memory_order_acquire);
    }

    /**
     * @brief Get current number of elements in buffer
     *
     * @return Number of elements
     */
    size_t count() const
    {
        size_t head = _head.load(std::memory_order_acquire);
        size_t tail = _tail.load(std::memory_order_acquire);
        if (head >= tail)
        {
            return head - tail;
        }
        return Size - tail + head;
    }

    /**
     * @brief Get available space in buffer
     *
     * @return Number of elements that can be pushed
     */
    size_t available() const
    {
        return Size - 1 - count();
    }

    /**
     * @brief Push a single element to the buffer
     *
     * Safe to call from ISR (producer side).
     *
     * @param item  Item to push
     * @return true if successful, false if buffer is full
     */
    bool push(const T& item)
    {
        size_t head = _head.load(std::memory_order_relaxed);
        size_t nextHead = (head + 1) % Size;

        if (nextHead == _tail.load(std::memory_order_acquire))
        {
            return false; // Buffer full
        }

        _buffer[head] = item;
        _head.store(nextHead, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pop a single element from the buffer
     *
     * Safe to call from task context (consumer side).
     *
     * @param item  Reference to store popped item
     * @return true if successful, false if buffer is empty
     */
    bool pop(T& item)
    {
        size_t tail = _tail.load(std::memory_order_relaxed);
        
        if (tail == _head.load(std::memory_order_acquire))
        {
            return false; // Buffer empty
        }

        item = _buffer[tail];
        _tail.store((tail + 1) % Size, std::memory_order_release);
        return true;
    }

    /**
     * @brief Peek at the front element without removing it
     *
     * @param item  Reference to store peeked item
     * @return true if successful, false if buffer is empty
     */
    bool peek(T& item) const
    {
        size_t tail = _tail.load(std::memory_order_relaxed);
        
        if (tail == _head.load(std::memory_order_acquire))
        {
            return false;
        }

        item = _buffer[tail];
        return true;
    }

    /**
     * @brief Write multiple elements to the buffer
     *
     * @param data      Pointer to data array
     * @param count     Number of elements to write
     * @return Number of elements actually written
     */
    size_t write(const T* data, size_t count)
    {
        size_t written = 0;
        for (size_t i = 0; i < count; i++)
        {
            if (!push(data[i]))
            {
                break;
            }
            written++;
        }
        return written;
    }

    /**
     * @brief Read multiple elements from the buffer
     *
     * @param data      Pointer to output array
     * @param maxCount  Maximum number of elements to read
     * @return Number of elements actually read
     */
    size_t read(T* data, size_t maxCount)
    {
        size_t readCount = 0;
        for (size_t i = 0; i < maxCount; i++)
        {
            if (!pop(data[i]))
            {
                break;
            }
            readCount++;
        }
        return readCount;
    }

private:
    T _buffer[Size];
    std::atomic<size_t> _head;
    std::atomic<size_t> _tail;
};

} // namespace Drivers
} // namespace CRTOS

#endif // DRIVER_RING_BUFFER_HPP
