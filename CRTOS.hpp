/*
 * CRTOS
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

#ifndef RTOS_HPP
#define RTOS_HPP

#include <cstdint>
#include <atomic>

namespace CRTOS
{
    enum class Result : uint8_t
    {
        RESULT_SUCCESS = 0,
        RESULT_BAD_PARAMETER,
        RESULT_NO_MEMORY,
        RESULT_MEMORY_NOT_INITIALIZED,
        RESULT_SEMAPHORE_TIMEOUT,
        RESULT_TIMER_ALREADY_ACTIVE,
        RESULT_TIMER_ALREADY_STOPPED,
        RESULT_QUEUE_TIMEOUT,
        RESULT_CIRCULAR_BUFFER_TIMEOUT,
        RESULT_TASK_NOT_FOUND
    };

    namespace Config
    {
        void SetCoreClock(uint32_t ClockInMHz);
        void SetTickRate(uint32_t TicksPerSecond);
        Result initMem(void *pool, uint32_t size);
        size_t getFreeMemory(void);
        size_t getAllocatedMemory(void);
    }

    class Semaphore
    {
        public:
            Semaphore(uint32_t initialValue);
            ~Semaphore(void) {};

            Result wait(uint32_t timeoutTicks);
            void signal(void);

        private:
            std::atomic<uint32_t> value;
    };

    class Mutex
    {
        public:
            Mutex(void);
            ~Mutex(void);
            void lock(void);
            void unlock(void);

        private:
            std::atomic_flag flag;
    };

    class Queue
    {
        private:
            void    *mQueue;
            uint32_t mFront;
            uint32_t mRear;
            uint32_t mSize;
            uint32_t mMaxSize;
            uint32_t mElementSize;

        public:
            Queue(uint32_t maxsize, uint32_t element_size);
            ~Queue(void);

            Result Send(void* item, uint32_t timeout);
            Result Receive(void* item, uint32_t timeout);
    };

    class CircularBuffer
    {
        private:
            uint8_t* mBuffer;
            uint32_t mHead;
            uint32_t mTail;
            uint32_t mCurrentSize;
            uint32_t mBufferSize;

        public:
            CircularBuffer(uint32_t mBuffer_size);
            CircularBuffer(const CircularBuffer& old);
            ~CircularBuffer(void);

            Result init(void);

            Result put(const uint8_t* data, uint32_t size, uint32_t timeout_ms);
            Result get(uint8_t* data, uint32_t size, uint32_t timeout_ms);
    };

    namespace Task
    {
        typedef void* TaskHandle;
        void SetMaximumTaskPrio(uint32_t maxPriority);
        Result Create(void (*function)(void *),  const char * const name, uint32_t stackDepth, void *args, uint32_t prio, TaskHandle *handle);
        Result Delete(void);
        Result Delete(TaskHandle *handle);
        void Delay(uint32_t ticks);

        void EnterCriticalSection(void);
        void ExitCriticalSection(void);

        char* GetCurrentTaskName(void);
    };

    namespace Scheduler
    {
        Result Start(void);
    };

    namespace Timer
    {
        typedef struct
        {
            uint32_t timeoutTicks;
            uint32_t elapsedTicks;
            bool isActive;
            void (*callback)(void*);
            void *callbackArgs;
            bool autoReload;
        } SoftwareTimer;

        Result Init(SoftwareTimer *timer, uint32_t timeoutTicks, void (*callback)(void*), void *callbackArgs, bool autoReload);
        Result Start(SoftwareTimer *timer);
        Result Stop(SoftwareTimer *timer);
    };
};

#endif /* RTOS_HPP */
