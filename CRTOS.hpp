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
        RESULT_SEMAPHORE_BUSY,
        RESULT_SEMAPHORE_TIMEOUT,
        RESULT_SEMAPHORE_NO_OWNER,
        RESULT_TIMER_ALREADY_ACTIVE,
        RESULT_TIMER_ALREADY_STOPPED,
        RESULT_QUEUE_TIMEOUT,
        RESULT_CIRCULAR_BUFFER_TIMEOUT,
        RESULT_TASK_NOT_FOUND,
        RESULT_IPC_TIMEOUT,
        RESULT_CRC_NOT_INITIALIZED,
        RESULT_CRC_ALREADY_INITIALIZED
    };

    namespace Config
    {
        void SetCoreClock(uint32_t ClockInMHz);
        void SetTickRate(uint32_t TicksPerSecond);
        Result InitMem(void *pool, uint32_t size);
        uint32_t GetFreeMemory(void);
        uint32_t GetAllocatedMemory(void);
    }

    class Semaphore
    {
        public:
            Semaphore(uint32_t initialValue);
            ~Semaphore(void) {};

            Result Wait(uint32_t timeoutTicks);
            void Signal(void);

            Result GetOwner(void *&owner);
            Result GetTimeout(uint32_t *&timeout);

        private:
            std::atomic<uint32_t> value;
            uint32_t mTimeout;
            void *mOwner;
    };

    class Mutex
    {
        public:
            Mutex(void);
            ~Mutex(void);
            void Lock(void);
            void Unlock(void);

        private:
            std::atomic_flag flag;
    };

    namespace Task
    {
        typedef void* TaskHandle;
        void SetMaximumTaskPrio(uint32_t maxPriority);
        Result Create(void (*function)(void *),  const char * const name, uint32_t stackDepth, void *args, uint32_t prio, TaskHandle *handle);
        Result Delete(void);
        Result Delete(TaskHandle *handle);

        CRTOS::Result Delay(uint32_t ticks);
        CRTOS::Result Pause(TaskHandle *handle);
        CRTOS::Result Resume(TaskHandle *handle);

        uint32_t GetTaskCycles(void);
        uint32_t GetFreeStack(void);
        void GetCoreLoad(uint32_t &load, uint32_t &mantissa);

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

    class Queue
    {
        private:
            uint8_t *mQueue;
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

            Result Init(void);

            Result Send(const uint8_t* data, uint32_t size, uint32_t timeout_ms);
            Result Receive(uint8_t* data, uint32_t size, uint32_t timeout_ms);
    };

    namespace IPC
    {
        typedef struct IPCMessage
        {
            CRTOS::Task::TaskHandle sender;
            CRTOS::Task::TaskHandle receiver;
            uint32_t messageId;
            void* data;
            uint32_t dataSize;
            IPCMessage* next;
        } IPCMessage;

        typedef struct TaskMessageQueue
        {
            CRTOS::Task::TaskHandle task;
            IPCMessage* messageHead;
            TaskMessageQueue* next;
        } TaskMessageQueue;

        CRTOS::Result SendMessage(CRTOS::Task::TaskHandle *sender, CRTOS::Task::TaskHandle *receiver, uint32_t messageId, void* data, uint32_t dataSize);
        CRTOS::Result ReceiveMessage(CRTOS::Task::TaskHandle *receiver, IPCMessage*& outMessage, uint32_t timeoutTicks);
        void ReleaseMessage(IPCMessage* message);
    }

    namespace CRC32
    {
        namespace
        {
            static const uint32_t sCrcTableSize = 256u;
            __attribute__((used)) static uint32_t *sCrcTable = nullptr;
            static const uint32_t sPolynomial = 0xEDB88320u;
        }

        CRTOS::Result Init(void);
        CRTOS::Result Calculate(const uint8_t* data, uint32_t length, uint32_t &output, uint32_t previousCrc = 0xFFFFFFFFu);
        CRTOS::Result Denit(void);
    }
};

#endif /* RTOS_HPP */
