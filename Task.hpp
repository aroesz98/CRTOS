/*
 * Task.hpp - CRTOS Task Management
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

#ifndef CRTOS_TASK_HPP
#define CRTOS_TASK_HPP

#include <cstdint>

namespace CRTOS
{
    // Forward declarations
    enum class Result : uint8_t;
    enum class ModuleState : uint8_t;
    
    struct TaskStackInfo;
    struct ModuleInfo;
    
    namespace Task
    {
        typedef void (*TaskFunction)(void *);
        typedef void* TaskHandle;
        typedef void (*StackOverflowHook)(const char *taskName, void *taskHandle);

        Result Create(void (*function)(void *),  const char * const name, uint32_t stackDepth, void *args, uint32_t prio, TaskHandle *handle);
        Result Delete(void);
        Result Delete(TaskHandle *handle);

        Result Delay(uint32_t ticks);
        Result Pause(TaskHandle *handle);
        Result Resume(TaskHandle *handle);
        void Yield(void);
        
        void SetStackOverflowHook(StackOverflowHook hook);

        uint32_t GetTaskCycles(void);
        uint32_t GetFreeStack(void);
        uint32_t GetFreeStack(TaskHandle *handle);
        uint32_t GetAllTasksStackInfo(TaskStackInfo *infoArray, uint32_t maxTasks);
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
            // Task name is taken from the module descriptor in the binary file.
            Result CreateTaskForBinModule(uint8_t *bin, void *args, uint32_t prio, TaskHandle *handle);
            
            // Module management functions
            uint32_t GetLoadedModulesCount(void);
            uint32_t GetAllModulesInfo(ModuleInfo *infoArray, uint32_t maxModules);
            Result GetModuleInfo(TaskHandle *handle, ModuleInfo &info);
            Result SetModuleState(TaskHandle *handle, ModuleState newState);
            
            // Module data exchange functions
            Result WriteToModule(TaskHandle *handle, uint32_t value);
            Result ReadFromModule(TaskHandle *handle, uint32_t &value);
            Result GetModuleSharedMemory(TaskHandle *handle, void **sharedMemPtr);
        };
    };
};

#endif /* CRTOS_TASK_HPP */
