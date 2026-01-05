/*
 * Syscall_Stubs.cpp - CRTOS System Call Stub Implementations
 * Author: Arkadiusz Szlanta
 * Date: 27 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * Stub implementations for system calls that will be implemented later.
 * These return ENOSYS (function not implemented) for now.
 */

#include "SystemCall.hpp"
#include "Syscall_Handlers.hpp"
#include "../CRTOS.hpp"

// tickCount is defined in CRTOS.cpp
extern volatile uint32_t tickCount;

namespace CRTOS
{
    using namespace Syscall; // For SyscallError and other types
    
    // ===== Memory Management =====
    
    void* sys_sbrk(int32_t increment)
    {
        // TODO: Implement heap expansion
        return (void*)static_cast<int32_t>(SyscallError::ERR_NOSYS);
    }
    
    void* sys_malloc(uint32_t size)
    {
        // Use kernel allocator for now
        // TODO: Use per-process heap when implemented
        return Config::Allocate(size);
    }
    
    int32_t sys_free(void* ptr)
    {
        if (ptr == nullptr)
        {
            return 0; // Free(null) is allowed
        }
        
        // Use kernel allocator for now
        Config::Deallocate(ptr);
        return 0;
    }
    
    // ===== Interrupt/Event Handling =====
    // IRQ syscalls are implemented in Syscall_IRQ.cpp
    
    // ===== IPC =====
    
    int32_t sys_ipc_send(int32_t targetPid, const void* message, uint32_t size)
    {
        return static_cast<int32_t>(SyscallError::ERR_NOSYS);
    }
    
    int32_t sys_ipc_recv(void* buffer, uint32_t size, uint32_t timeout)
    {
        return static_cast<int32_t>(SyscallError::ERR_NOSYS);
    }
    
    int32_t sys_shm_create(uint32_t key, uint32_t size)
    {
        return static_cast<int32_t>(SyscallError::ERR_NOSYS);
    }
    
    int32_t sys_shm_attach(uint32_t shmid, void** addr)
    {
        return static_cast<int32_t>(SyscallError::ERR_NOSYS);
    }
    
    int32_t sys_shm_detach(void* addr)
    {
        return static_cast<int32_t>(SyscallError::ERR_NOSYS);
    }
    
    // ===== Time =====
    
    uint32_t sys_get_tick(void)
    {
        return tickCount;
    }
    
    int32_t sys_get_time(uint64_t* time_us)
    {
        if (time_us == nullptr) {
            return static_cast<int32_t>(SyscallError::ERR_FAULT);
        }
        // Convert ticks to microseconds (assuming 1ms ticks)
        *time_us = (uint64_t)tickCount * 1000;
        return 0;
    }
    
    // ===== Debug/Info =====
    
    int32_t sys_debug_print(const char* message)
    {
        // Use sys_write to stdout - calculate length
        size_t length = 0;
        if (message) {
            while (message[length] != '\0') length++;
        }
        return sys_write(1, message, length);
    }
    
    int32_t sys_get_process_info(int32_t pid, ProcessInfo* info)
    {
        if (info == nullptr)
        {
            return static_cast<int32_t>(SyscallError::ERR_FAULT);
        }
        
        // For now, ignore pid and just return current process info
        
        // TODO: Fill with real process information
        return static_cast<int32_t>(SyscallError::ERR_NOSYS);
    }
    
    int32_t sys_get_system_info(SystemInfo* info)
    {
        if (info == nullptr)
        {
            return static_cast<int32_t>(SyscallError::ERR_FAULT);
        }
        
        // Fill with basic system information
        info->totalMemory = Config::GetTotalHeapSize();
        info->freeMemory = Config::GetFreeMemory();
        info->processCount = 0; // TODO: Get from process manager
        info->uptime = tickCount;
        info->cpuFreq = 0; // TODO: Get from system config
        info->tickRate = 0; // TODO: Get from system config
        
        return 0;
    }
    
    // ===== Synchronization =====
    
    int32_t sys_mutex_create(void)
    {
        return static_cast<int32_t>(SyscallError::ERR_NOSYS);
    }
    
    int32_t sys_mutex_lock(int32_t mutexHandle)
    {
        return static_cast<int32_t>(SyscallError::ERR_NOSYS);
    }
    
    int32_t sys_mutex_unlock(int32_t mutexHandle)
    {
        return static_cast<int32_t>(SyscallError::ERR_NOSYS);
    }
    
    int32_t sys_mutex_destroy(int32_t mutexHandle)
    {
        return static_cast<int32_t>(SyscallError::ERR_NOSYS);
    }
    
    int32_t sys_sem_create(int32_t initialValue)
    {
        return static_cast<int32_t>(SyscallError::ERR_NOSYS);
    }
    
    int32_t sys_sem_wait(int32_t semHandle)
    {
        return static_cast<int32_t>(SyscallError::ERR_NOSYS);
    }
    
    int32_t sys_sem_post(int32_t semHandle)
    {
        return static_cast<int32_t>(SyscallError::ERR_NOSYS);
    }
    
    int32_t sys_sem_destroy(int32_t semHandle)
    {
        return static_cast<int32_t>(SyscallError::ERR_NOSYS);
    }

} // namespace CRTOS
