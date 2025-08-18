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

template <typename T>
class Node;

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
        RESULT_QUEUE_FULL,
        RESULT_QUEUE_EMPTY,
        RESULT_CIRCULAR_BUFFER_TIMEOUT,
        RESULT_CIRCULAR_BUFFER_FULL,
		RESULT_CIRCULAR_BUFFER_EMPTY,
        RESULT_TASK_NOT_FOUND,
        RESULT_IPC_TIMEOUT,
        RESULT_IPC_EMPTY,
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

    class Mutex
    {
        public:
            Mutex(void);
            ~Mutex(void);
            void Lock(void);
            void Unlock(void);

        private:
            std::atomic_flag flag;
            uint32_t irqMask;
    };

    class BinarySemaphore
    {
		public:
    		BinarySemaphore() = default;
			~BinarySemaphore() = default;

			Result wait(uint32_t ticks);
			Result signal();

		private:
			Node<uint32_t*> *listOfTasksWaitingToRecv = nullptr;
			uint32_t _val;
    };

    namespace Task
    {
        typedef void (*TaskFunction)(void *);
        typedef void* TaskHandle;

        Result Create(void (*function)(void *),  const char * const name, uint32_t stackDepth, void *args, uint32_t prio, TaskHandle *handle);
        Result Delete(void);
        Result Delete(TaskHandle *handle);

        CRTOS::Result Delay(uint32_t ticks);
        CRTOS::Result Pause(TaskHandle *handle);
        CRTOS::Result Resume(TaskHandle *handle);
        void Yield(void);

        uint32_t GetTaskCycles(void);
        uint32_t GetFreeStack(void);
        void GetCoreLoad(uint32_t &load, uint32_t &mantissa);
        uint32_t GetLastTaskSwitchTime(void);  // Get the last task switch latency in cycles

        uint32_t EnterCriticalSection(void);
        void ExitCriticalSection(uint32_t mask);

        char* GetCurrentTaskName(void);
        char* GetTaskName(TaskHandle *handle);
        TaskHandle GetCurrentTaskHandle(void);

        namespace LPC55S69_Features
        {
            Result CreateTaskForExecutable(const uint8_t *elf_file, const char *const name, void *args, uint32_t prio, TaskHandle *handle);
            // Create task from a raw BIN module produced by this module template
			// The BIN layout begins with ProgramInfo followed by code/rodata.
			Result CreateTaskForBinModule(uint8_t *bin, const char *const name, void *args, uint32_t prio, TaskHandle *handle);
        };
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
            Node<uint32_t*> *listOfTasksWaitingToRecv = nullptr;

        public:
            Queue(uint32_t maxsize, uint32_t element_size);
            ~Queue(void);

            Result Send(void* item);
            Result Receive(void* item, uint32_t timeout = 0u);
    };

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
        CRTOS::Result Deinit(void);
    }
};

#endif /* RTOS_HPP */
