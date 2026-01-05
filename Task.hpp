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
        
        /**
         * @brief Task privilege mode for ARM Cortex-M
         * 
         * PRIVILEGED: Task runs with full access to all memory and peripherals.
         *             Can execute privileged instructions (MSR, MRS to special regs).
         *             This is the default mode for kernel tasks.
         * 
         * USER:       Task runs in unprivileged (user) mode.
         *             Cannot access system control registers directly.
         *             Must use syscalls to request kernel services.
         *             This is the recommended mode for application tasks and modules.
         */
        enum class PrivilegeMode : uint8_t
        {
            PRIVILEGED = 0,  // Privileged mode (default for kernel tasks)
            USER = 1         // User mode (unprivileged, recommended for apps/modules)
        };

        /**
         * @brief Create a new task with default privilege mode (PRIVILEGED)
         */
        Result Create(void (*function)(void *), const char * const name, uint32_t stackDepth, void *args, uint32_t prio, TaskHandle *handle);
        
        /**
         * @brief Create a new task with specified privilege mode
         * 
         * @param function    Task entry function
         * @param name        Task name (max 23 chars)
         * @param stackDepth  Stack size in 32-bit words
         * @param args        Arguments passed to task function
         * @param prio        Task priority (0 = lowest)
         * @param handle      Output: handle to created task (can be nullptr)
         * @param privilege   Privilege mode: PRIVILEGED or USER
         * @return Result     RESULT_SUCCESS or error code
         */
        Result Create(void (*function)(void *), const char * const name, uint32_t stackDepth, void *args, uint32_t prio, TaskHandle *handle, PrivilegeMode privilege);
        
        /**
         * @brief Create a new task with specified privilege mode and r9 register value (for PIC code)
         * 
         * @param function    Task entry function
         * @param name        Task name (max 23 chars)
         * @param stackDepth  Stack size in 32-bit words
         * @param args        Arguments passed to task function
         * @param prio        Task priority (0 = lowest)
         * @param handle      Output: handle to created task (can be nullptr)
         * @param privilege   Privilege mode: PRIVILEGED or USER
         * @param r9Value     Initial value for r9 register (GOT base for PIC applications)
         * @return Result     RESULT_SUCCESS or error code
         */
        Result Create(void (*function)(void *), const char * const name, uint32_t stackDepth, void *args, uint32_t prio, TaskHandle *handle, PrivilegeMode privilege, uint32_t r9Value);
        
        /**
         * @brief Check if current task is running in privileged mode
         */
        bool IsCurrentTaskPrivileged(void);
        
        /**
         * @brief Get privilege mode of a task
         */
        bool IsTaskPrivileged(TaskHandle *handle);
        
        /**
         * @brief Elevate current task to privileged mode temporarily
         * Allows USER mode tasks to temporarily access hardware (e.g., UART for debug)
         * Use DropPrivileges() to return to user mode
         * NOTE: This triggers a context switch to apply the change
         */
        void ElevatePrivileges(void);
        
        /**
         * @brief Drop current task back to user mode
         * Use after ElevatePrivileges() to return to unprivileged mode
         * NOTE: This triggers a context switch to apply the change
         */
        void DropPrivileges(void);
        
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
        uint32_t GetTaskInfoByIndex(uint32_t index, TaskStackInfo *info);
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
            
            /**
             * @brief Create task from a raw BIN module (runs in USER mode by default)
             * 
             * The BIN layout begins with ProgramInfo followed by code/rodata.
             * Task name is taken from the module descriptor in the binary file.
             * Modules run in USER (unprivileged) mode by default for security.
             */
            Result RunExecutable(uint8_t *bin, void *args, uint32_t prio, TaskHandle *handle);
            
            /**
             * @brief Create task from a raw BIN module with specified privilege mode
             * 
             * @param bin       Pointer to binary module data
             * @param args      Arguments passed to module entry function
             * @param prio      Task priority
             * @param handle    Output: handle to created task
             * @param privilege Privilege mode: USER (default) or PRIVILEGED
             */
            Result RunExecutable(uint8_t *bin, void *args, uint32_t prio, TaskHandle *handle, PrivilegeMode privilege);
            
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
