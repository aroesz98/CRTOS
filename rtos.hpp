/*
 * rtos.hpp
 *
 *  Created on: 12 gru 2024
 *      Author: Administrator
 */

#ifndef RTOS_HPP_
#define RTOS_HPP_

#include <cstdint>
#include <atomic>

namespace RTOS
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
        RESULT_QUEUE_TIMEOUT
    };

    namespace Config
    {
        void SetCoreClock(uint32_t ClockInMHz);
        void SetTickRate(uint32_t TicksPerSecond);
        Result initMem(void *pool, uint32_t size);
        size_t getFreeMemory();
        size_t getAllocatedMemory();
    }

    class Semaphore
    {
        public:
            Semaphore(uint32_t initialValue);
            ~Semaphore() {};

            Result wait(uint32_t timeoutTicks);
            void signal();

        private:
            std::atomic<uint32_t> value;
    };

    class Mutex
    {
        public:
            Mutex();
            ~Mutex();
            void lock();
            void unlock();

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
            ~Queue();

            Result queueSend(void* item, uint32_t timeout);
            Result queueReceive(void* item, uint32_t timeout);
    };

    namespace Task
    {
        typedef void *TaskHandle;
        void SetMaximumTaskPrio(uint32_t maxPriority);
        Result Create(void (*function)(void *),  const char * const name, uint32_t stackDepth, void *args, uint32_t prio, TaskHandle *handle);
        void Delete(void);
        void Delete(TaskHandle *handle);

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

#endif /* RTOS_HPP_ */
