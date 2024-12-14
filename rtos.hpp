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
    enum class TaskResult : uint8_t
    {
        RESULT_SUCCESS = 0,
        RESULT_NO_MEMORY,
    };

    class Semaphore
    {
        public:
            Semaphore(uint32_t initialValue);
            ~Semaphore() {};

            void wait(uint32_t timeoutTicks);
            void signal();
            void destroy();

        private:
            std::atomic<uint32_t> value;
    };

    namespace Task
    {
        typedef void *TaskHandle;
        void SetMaximumTaskPrio(uint32_t maxPriority);
        TaskResult Create(void (*function)(void *),  const char * const name, uint32_t stackDepth, void *args, uint32_t prio, TaskHandle *handle);
        void Delete(void);
        void Delete(TaskHandle *handle);

        char* GetCurrentTaskName(void);
    };

    namespace Scheduler
    {
        void Start(void);
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

        void Init(SoftwareTimer *timer, uint32_t timeoutTicks, void (*callback)(void*), void *callbackArgs, bool autoReload);
        void Start(SoftwareTimer *timer);
        void Stop(SoftwareTimer *timer);
        void Reset(SoftwareTimer *timer);
    };
};

#endif /* RTOS_HPP_ */
