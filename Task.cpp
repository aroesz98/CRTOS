/*
 * Task.cpp - CRTOS Task Management Implementation
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

#include "Task.hpp"
#include "CRTOS.hpp"
#include "CRTOS_Internal.hpp"
#include "HeapAllocator.hpp"
#include "ELFParser.hpp"
#include <cstring>
#include <cstdio>

// External declarations from CRTOS.cpp
extern HeapAllocator mem;
extern volatile TaskControlBlock *sCurrentTCB;
extern Node<TaskControlBlock> *readyTaskList;
extern CRTOS::Task::TaskHandle idleTaskHandle;
extern LoadedModuleInfo *loadedModules;
extern uint32_t loadedModulesCount;
extern uint32_t MAX_LOADED_MODULES;
extern uint32_t DEFAULT_STACK_SIZE;
extern uint32_t DEFAULT_MODULE_LEN;
extern uint32_t MODULE_MAGIC;
extern uint32_t MAX_TASK_PRIORITY;
extern volatile uint32_t tickCount;
extern volatile uint32_t switchTime;
extern CRTOS::Task::StackOverflowHook sStackOverflowHook;

// External functions from CRTOS.cpp
extern "C" uint32_t getInterruptMask(void);
extern "C" void setInterruptMask(uint32_t mask);
extern "C" void memcpy_optimized(void *d, void *s, uint32_t len);
extern "C" void memset_optimized(void *d, uint32_t val, uint32_t len);
extern uint32_t pStringLength(const char *buffer);
extern uint32_t *initStack(volatile uint32_t *stackTop, volatile uint32_t *stackStart, void (*function)(void *), void *args);
extern uint32_t GetSystemTime(void);
extern bool isHigherPrioTaskPending(void);
extern uint32_t GetIdleTaskTime(void);

// TODO: Move all Task function implementations from CRTOS.cpp to this file
uint32_t CRTOS::Task::EnterCriticalSection(void)
{
    uint32_t mask = getInterruptMask();
    return mask;
}

void CRTOS::Task::ExitCriticalSection(uint32_t mask = 0u)
{
    setInterruptMask(mask);
}

static bool isPendingTask(void)
{
    Node<TaskControlBlock> *temp = readyTaskList;
    while (temp != nullptr)
    {
        if (temp->data->state == TaskState::TASK_DELAYED)
        {
            if (tickCount >= temp->data->delayUpTo)
            {
                temp->data->state = TaskState::TASK_READY;
            }
        }
        if (temp->data->state == TaskState::TASK_READY)
        {
            return true;
        }

        temp = temp->next;
    }

    return false;
}

extern "C" void setTimeSliceExpired(void);

void CRTOS::Task::Yield(void)
{
    setTimeSliceExpired();
    *ICSR_REG = NVIC_PENDSV_BIT;
    __DSB();
    __ISB();
}

void CRTOS::Task::SetStackOverflowHook(CRTOS::Task::StackOverflowHook hook)
{
    sStackOverflowHook = hook;
}

CRTOS::Result CRTOS::Task::Create(TaskFunction function, const char *const name, uint32_t stackDepth, void *args, uint32_t prio, TaskHandle *handle)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    uint32_t prevMask = getInterruptMask();
    void *pool = nullptr;
    uint32_t poolSize = 0u;

    __DSB();
    __ISB();

    mem.getMemoryPool(&pool, poolSize);

    if ((pool == nullptr) || (poolSize == 0u))
    {
        result = CRTOS::Result::RESULT_MEMORY_NOT_INITIALIZED;
        return result;
    }

    do
    {
        TaskControlBlock *tmpTCB = reinterpret_cast<TaskControlBlock *>(mem.allocate(sizeof(TaskControlBlock)));
        if (tmpTCB == nullptr)
        {
            result = CRTOS::Result::RESULT_NO_MEMORY;
            continue;
        }

        uint32_t *tmpStack = reinterpret_cast<uint32_t *>(mem.allocate(stackDepth * sizeof(uint32_t)));
        if (tmpStack == nullptr)
        {
            mem.deallocate(tmpTCB);
            result = CRTOS::Result::RESULT_NO_MEMORY;
            continue;
        }

        for (uint32_t i = 0; i < stackDepth; i++)
        {
            tmpStack[i] = 0xDEADBEEF;
        }
        memset_optimized(&(tmpTCB->name[0u]), 0u, 24u);

        tmpTCB->stack = &tmpStack[0u];
        tmpTCB->stackSize = stackDepth;
        tmpTCB->function = function;
        tmpTCB->function_args = args;
        tmpTCB->enterCycles = 0u;
        tmpTCB->exitCycles = 0u;
        tmpTCB->heapAllocated = 0u;
        tmpTCB->registeredIRQ = 0xFFFFFFFFu;
        tmpTCB->vtor_addr = 0u;

        if (prio >= MAX_TASK_PRIORITY)
        {
            tmpTCB->priority = MAX_TASK_PRIORITY - 1u;
        }
        else
        {
            tmpTCB->priority = prio;
        }

        // Ustawienie stanu zadania
        tmpTCB->state = TaskState::TASK_READY;

        uint32_t nameLength = pStringLength(name);
        memcpy_optimized(&tmpTCB->name[0], (char *)&name[0u], nameLength < 24u ? nameLength : 24u);

        volatile uint32_t *stackTop = &(tmpTCB->stack[stackDepth - 1u]);
        stackTop = (uint32_t *)(((uint32_t)stackTop) & ~7u);

        tmpTCB->stackTop = initStack(stackTop, tmpTCB->stack, function, args);

        ListInsertAtEnd(readyTaskList, tmpTCB);

        if (handle != nullptr)
        {
            *handle = (TaskHandle)tmpTCB;
        }
    } while (0);

    setInterruptMask(prevMask);

    return result;
}

CRTOS::Result CRTOS::Task::LPC55S69_Features::CreateTaskForExecutable(const uint8_t *elf_file, const char *const name, void *args, uint32_t prio, TaskHandle *handle)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    uint32_t prevMask = getInterruptMask();
    void *pool = nullptr;
    uint32_t poolSize = 0u;

    __DSB();
    __ISB();

    ElfFile elf;

    mem.getMemoryPool(&pool, poolSize);

    if ((pool == nullptr) || (poolSize == 0u))
    {
        result = CRTOS::Result::RESULT_MEMORY_NOT_INITIALIZED;
        return result;
    }

    do
    {
        TaskControlBlock *tmpTCB = reinterpret_cast<TaskControlBlock *>(mem.allocate(sizeof(TaskControlBlock)));
        if (tmpTCB == nullptr)
        {
            result = CRTOS::Result::RESULT_NO_MEMORY;
            continue;
        }

        memset_optimized(&(tmpTCB->name[0u]), 0u, 24u);

        tmpTCB->stackSize = 0u;
        tmpTCB->function_args = args;
        tmpTCB->enterCycles = 0u;
        tmpTCB->exitCycles = 0u;
        tmpTCB->heapAllocated = 0u;
        tmpTCB->registeredIRQ = 0xFFFFFFFFu;

        elf.parse(elf_file, (uint32_t **)(&(tmpTCB->stack)), &(tmpTCB->stackSize), &(tmpTCB->vtor_addr));
        tmpTCB->vtor_addr = 0u;

        if (prio >= MAX_TASK_PRIORITY)
        {
            tmpTCB->priority = MAX_TASK_PRIORITY - 1u;
        }
        else
        {
            tmpTCB->priority = prio;
        }

        tmpTCB->function = elf.entry_point;

        // Ustawienie stanu zadania
        tmpTCB->state = TaskState::TASK_READY;

        uint32_t nameLength = pStringLength(name);
        memcpy_optimized(&tmpTCB->name[0], (char *)&name[0u], nameLength < 24u ? nameLength : 24u);

        volatile uint32_t *stackTop = &(tmpTCB->stack[tmpTCB->stackSize - 1u]);
        stackTop = (uint32_t *)(((uint32_t)stackTop) & ~7u);

        tmpTCB->stackTop = initStack(stackTop, tmpTCB->stack, elf.entry_point, args);

        ListInsertAtEnd(readyTaskList, tmpTCB);

        if (handle != nullptr)
        {
            *handle = (TaskHandle)tmpTCB;
        }
    } while (0);

    setInterruptMask(prevMask);

    return result;
}

// Binary module loader for modules (PIE BIN with ProgramInfo header)
CRTOS::Result CRTOS::Task::LPC55S69_Features::CreateTaskForBinModule(uint8_t *bin, void *args, uint32_t prio, TaskHandle *handle)
{
    if (bin == nullptr)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    uint32_t prevMask = getInterruptMask();
    void *pool = nullptr;
    uint32_t poolSize = 0u;

    __DSB();
    __ISB();

    mem.getMemoryPool(&pool, poolSize);
    if ((pool == nullptr) || (poolSize == 0u))
    {
        return CRTOS::Result::RESULT_MEMORY_NOT_INITIALIZED;
    }

    do
    {
        TaskControlBlock *tmpTCB = reinterpret_cast<TaskControlBlock *>(mem.allocate(sizeof(TaskControlBlock)));
        if (tmpTCB == nullptr)
        {
            result = CRTOS::Result::RESULT_NO_MEMORY;
            continue;
        }

        memset_optimized(&(tmpTCB->name[0u]), 0u, 24u);
        tmpTCB->function_args = args;
        tmpTCB->enterCycles = 0u;
        tmpTCB->exitCycles = 0u;
        tmpTCB->heapAllocated = 0u;
        tmpTCB->registeredIRQ = 0xFFFFFFFFu;

        // Determine image size using descriptor if present; otherwise fallback to data offset + data size
        ProgramInfoBin *pinfo_src = reinterpret_cast<ProgramInfoBin *>(bin);
        uint32_t imgSize = 0u;
        // ModuleDescriptor is at aligned offset after ProgramInfoBin
        // Use 8-byte alignment to match linker script requirement and ensure universal compatibility
        constexpr size_t alignment = 8u;
        constexpr uint32_t MODULE_DESC_OFFSET = ((sizeof(ProgramInfoBin) + alignment - 1u) & ~(alignment - 1u));
        ModuleDescriptorBin *md = reinterpret_cast<ModuleDescriptorBin *>(bin + MODULE_DESC_OFFSET);
        printf("DEBUG ModuleDescriptor: magic=0x%08lX (expected 0x%08lX), image_size=%lu, offset=%lu\r\n", 
                         md->magic, MODULE_MAGIC, md->image_size, MODULE_DESC_OFFSET);
        if (md->magic == MODULE_MAGIC && md->image_size > 0)
        {
            // image_size is the binary file size (without .bss), add .bss size for total runtime memory
            imgSize = md->image_size + pinfo_src->section_bss_size;
            printf("  Using ModuleDescriptor image_size: %lu + bss: %lu = total: %lu\r\n", 
                   md->image_size, pinfo_src->section_bss_size, imgSize);
        }
        else
        {
            // Fallback: include code/rodata/data and add .bss
            // Add validation to detect corrupted headers
            uint32_t data_start = pinfo_src->section_data_start_addr;
            uint32_t data_size = pinfo_src->section_data_size;
            uint32_t bss_size = pinfo_src->section_bss_size;
            
            // Sanity check: these values should be reasonable (< 1MB each for embedded systems)
            if (data_start > 0x100000 || data_size > 0x100000 || bss_size > 0x100000)
            {
                // Header appears corrupted, use default size
                imgSize = 0u;
            }
            else
            {
                // Check for overflow before calculating
                uint64_t total = (uint64_t)data_start + (uint64_t)data_size + (uint64_t)bss_size;
                if (total > 0xFFFFFFFF || total > poolSize)
                {
                    imgSize = 0u;
                }
                else
                {
                    imgSize = (uint32_t)total;
                }
            }
        }
        if (imgSize == 0u || imgSize > poolSize)
        {
            // As a last resort, assume 4KB
            imgSize = DEFAULT_MODULE_LEN;
        }

        // Allocate space for the entire module image (code + data + bss)
        uint8_t *binary = reinterpret_cast<uint8_t *>(mem.allocate(imgSize));
        if (binary == nullptr)
        {
            mem.deallocate(tmpTCB);
            result = CRTOS::Result::RESULT_NO_MEMORY;
            continue;
        }

        // Copy the binary file content (which does NOT include .bss since it's uninitialized)
        // The .bss space is allocated but not in the binary file
        uint32_t binarySizeWithoutBss = imgSize - pinfo_src->section_bss_size;
        printf("  Allocated %lu bytes, copying %lu bytes from binary file\r\n", imgSize, binarySizeWithoutBss);
        memcpy_optimized(binary, bin, binarySizeWithoutBss);

        // Work on the copied image
        ProgramInfoBin *pinfo = reinterpret_cast<ProgramInfoBin *>(binary);

        // DEBUG: Print ProgramInfo values
        printf("DEBUG ProgramInfo:\n");
        printf("  entryPoint:           0x%08lX\r\n", pinfo->entryPoint);
        printf("  section_data_start:   0x%08lX\r\n", pinfo->section_data_start_addr);
        printf("  section_data_size:    %lu\r\n", pinfo->section_data_size);
        printf("  section_bss_start:    0x%08lX\r\n", pinfo->section_bss_start_addr);
        printf("  section_bss_size:     %lu\r\n", pinfo->section_bss_size);
        printf("  binary base:          0x%08lX\r\n", (uint32_t)binary);
        printf("  imgSize:              %lu\r\n", imgSize);

        // Kernel manages stack size - use a reasonable default
        uint32_t stackSize = DEFAULT_STACK_SIZE; // Use kernel's default stack size

        uint8_t *stk = reinterpret_cast<uint8_t *>(mem.allocate(stackSize));
        if (stk == nullptr)
        {
            mem.deallocate(binary);
            mem.deallocate(tmpTCB);
            result = CRTOS::Result::RESULT_NO_MEMORY;
            continue;
        }

        // Initialize stack with watermark pattern for stack usage tracking
        uint32_t *stackPtr = (uint32_t *)stk;
        uint32_t stackWords = stackSize / sizeof(uint32_t);
        for (uint32_t i = 0; i < stackWords; i++)
        {
            stackPtr[i] = 0xDEADBEEF;
        }

        // Calculate addresses within the binary image
        // .data and .bss are now part of the binary image, so they're at their offsets from 'binary'
        // These addresses from ProgramInfo are offsets from image base (ORIGIN(FLASH) = 0)
        uint32_t new_data_addr = (uint32_t)(binary + pinfo->section_data_start_addr);
        uint32_t new_bss_addr = (uint32_t)(binary + pinfo->section_bss_start_addr);
        uint32_t new_msp = (uint32_t)(stk + stackSize);
        uint32_t new_msplim = (uint32_t)stk;

        // CRITICAL: Zero out the .bss section (uninitialized data)
        // The .bss space was allocated but not copied from the binary file
        if (pinfo->section_bss_size > 0)
        {
            uint8_t *bss_location = (uint8_t *)new_bss_addr;
            memset_optimized(bss_location, 0, pinfo->section_bss_size);
        }

        // Relocate entry: binary image base is at 'binary', entry is offset from base; set Thumb bit
        uint32_t new_entry = (uint32_t)(binary + pinfo->entryPoint);
        new_entry |= 1u;

        // Update ProgramInfo inside the copied image
        // .data and .bss are in the binary image, just update their addresses
        pinfo->section_data_dest_addr = new_data_addr;
        pinfo->section_data_start_addr = new_data_addr; // Same location (not copied to RAM)
        pinfo->section_bss_start_addr = new_bss_addr;
        pinfo->stackPointer = new_msp;
        pinfo->msp_limit = new_msplim;
        pinfo->entryPoint = new_entry;
        pinfo->vtor_offset = (uint32_t)(binary + 0); // segment base for this BIN

        // Fill TCB using relocated values
        tmpTCB->stack = (uint32_t *)new_msplim;
        tmpTCB->stackSize = (stackSize / sizeof(uint32_t));
        tmpTCB->function = (void (*)(void *))new_entry;
        tmpTCB->vtor_addr = 0u; // pinfo->vtor_offset;
        tmpTCB->state = TaskState::TASK_READY;
        tmpTCB->timeout = 0u;
        tmpTCB->delayUpTo = 0u;

        if (prio >= MAX_TASK_PRIORITY)
        {
            tmpTCB->priority = MAX_TASK_PRIORITY - 1u;
        }
        else
        {
            tmpTCB->priority = prio;
        }

        // Initialize stack frame
        volatile uint32_t *alignedTop = (uint32_t *)(((uint32_t)new_msp) & ~7u);
        tmpTCB->stackTop = initStack(alignedTop, tmpTCB->stack, tmpTCB->function, args);

        // Task name from module descriptor
        // Ensure the module name from descriptor is null-terminated
        char moduleName[24] = {0};
        if (md->magic == MODULE_MAGIC)
        {
            // Copy name from module descriptor (max 19 chars to leave room for null terminator)
            uint32_t nameLen = 0;
            while (nameLen < 24 && nameLen < 32 && md->name[nameLen] != '\0')
            {
                moduleName[nameLen] = md->name[nameLen];
                nameLen++;
            }
            moduleName[nameLen] = '\0';
        }
        else
        {
            // Fallback name if no valid descriptor
            const char *fallbackName = "unnamed_module";
            memcpy_optimized(moduleName, (void *)fallbackName, 15);
        }
        memcpy_optimized(&tmpTCB->name[0], moduleName, 24);

        // IMPORTANT: Track the loaded module BEFORE adding to ready list
        // This prevents race condition where module starts before tracking is set up
        if (loadedModules == nullptr)
        {
            // First module - allocate tracking array
            loadedModules = reinterpret_cast<LoadedModuleInfo *>(mem.allocate(sizeof(LoadedModuleInfo) * MAX_LOADED_MODULES));
            if (loadedModules != nullptr)
            {
                memset_optimized(loadedModules, 0, sizeof(LoadedModuleInfo) * MAX_LOADED_MODULES);
            }
        }

        if (loadedModules != nullptr && loadedModulesCount < MAX_LOADED_MODULES)
        {
            LoadedModuleInfo *modInfo = &loadedModules[loadedModulesCount];
            memcpy_optimized(modInfo->name, tmpTCB->name, 24);
            modInfo->baseAddress = (uint32_t)binary;
            modInfo->entryPoint = new_entry & ~1u; // Clear Thumb bit for address display

            // .text section starts after ProgramInfo and optional ModuleDescriptor
            uint32_t textOffset = sizeof(ProgramInfoBin);
            ModuleDescriptorBin *md_check = reinterpret_cast<ModuleDescriptorBin *>(binary + sizeof(ProgramInfoBin));
            if (md_check->magic == MODULE_MAGIC)
            {
                textOffset += sizeof(ModuleDescriptorBin);
            }
            modInfo->textAddr = (uint32_t)binary + textOffset;
            modInfo->textSize = imgSize - textOffset;

            // .data section (now in binary image, not separate RAM)
            modInfo->dataAddr = new_data_addr;
            modInfo->dataSize = pinfo->section_data_size;

            // .bss section (also in binary image)
            modInfo->bssAddr = new_bss_addr;
            modInfo->bssSize = pinfo->section_bss_size;

            // Stack section (separate allocation)
            modInfo->stackAddr = new_msplim;
            modInfo->stackSize = stackSize;

            // Allocate shared memory for module-host communication
            modInfo->sharedMemory = reinterpret_cast<ModuleSharedMemory *>(mem.allocate(sizeof(ModuleSharedMemory)));
            if (modInfo->sharedMemory == nullptr)
            {
                // Critical error - shared memory allocation failed
                mem.deallocate(stk);
                mem.deallocate(binary);
                mem.deallocate(tmpTCB);
                result = CRTOS::Result::RESULT_NO_MEMORY;
                setInterruptMask(prevMask);
                return result;
            }

            // Zero the shared memory structure
            memset_optimized(modInfo->sharedMemory, 0, sizeof(ModuleSharedMemory));

            // Clean cache to ensure zeros are written to RAM
            __DSB();
            __ISB();

            modInfo->totalSize = imgSize + stackSize; // Binary image + stack
            modInfo->tcb = tmpTCB;
            modInfo->state = CRTOS::ModuleState::MODULE_RUNNING;
            modInfo->loadTime = GetSystemTime();
            loadedModulesCount++;
            
            // Track all heap allocations made for this module
            // These allocations happened before the task started running, so they weren't
            // automatically tracked by HeapAllocator
            tmpTCB->heapAllocated = sizeof(TaskControlBlock) +  // TCB structure
                                    imgSize +                    // Binary image (code + data + bss)
                                    stackSize +                  // Task stack
                                    sizeof(ModuleSharedMemory);  // Shared memory for communication
        }
        else
        {
            // Failed to allocate module tracking - critical error
            mem.deallocate(stk);
            mem.deallocate(binary);
            mem.deallocate(tmpTCB);
            result = CRTOS::Result::RESULT_NO_MEMORY;
            setInterruptMask(prevMask);
            return result;
        }

        // NOW add to ready list after module tracking is complete
        ListInsertAtEnd(readyTaskList, tmpTCB);
        if (handle != nullptr)
        {
            *handle = (TaskHandle)tmpTCB;
        }
    } while (0);

    setInterruptMask(prevMask);
    return result;
}

CRTOS::Result CRTOS::Task::Delete(void)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    uint32_t prevMask = getInterruptMask();
    Node<TaskControlBlock> *tmp = readyTaskList;
    uint32_t pos = 0;

    __DSB();
    __ISB();

    do
    {
        while (tmp != nullptr)
        {
            if (sCurrentTCB == tmp->data)
            {
                ListDeleteAtPosition(readyTaskList, pos);
                break;
            }

            tmp = tmp->next;
            pos++;
        }

        if (tmp == nullptr)
        {
            result = CRTOS::Result::RESULT_TASK_NOT_FOUND;
            continue;
        }

        mem.deallocate((void *)sCurrentTCB->stack);
        mem.deallocate((void *)sCurrentTCB);
    } while (0);

    *ICSR_REG = NVIC_PENDSV_BIT;

    setInterruptMask(prevMask);

    return result;
}

CRTOS::Result CRTOS::Task::Delete(TaskHandle *handle)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    uint32_t prevMask = getInterruptMask();
    TaskControlBlock *tmpHandle = (TaskControlBlock *)(*handle);
    Node<TaskControlBlock> *tmp = readyTaskList;
    uint32_t pos = 0;

    __DSB();
    __ISB();

    do
    {
        if (tmpHandle == nullptr)
        {
            result = CRTOS::Result::RESULT_BAD_PARAMETER;
            continue;
        }

        if ((tmpHandle->stack) == nullptr)
        {
            result = CRTOS::Result::RESULT_BAD_PARAMETER;
            continue;
        }

        while (tmp != nullptr)
        {
            if (tmpHandle == tmp->data)
            {
                ListDeleteAtPosition(readyTaskList, pos);
                break;
            }

            tmp = tmp->next;
            pos++;
        }

        if (tmp == nullptr)
        {
            result = CRTOS::Result::RESULT_TASK_NOT_FOUND;
            continue;
        }

        mem.deallocate((void *)tmpHandle->stack);
        mem.deallocate((void *)tmpHandle);
    } while (0);

    *ICSR_REG = NVIC_PENDSV_BIT;

    setInterruptMask(prevMask);

    return result;
}

CRTOS::Result CRTOS::Task::Delay(uint32_t ticks)
{
    if (ticks == 0)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    uint32_t prevMask = getInterruptMask();

    sCurrentTCB->state = TaskState::TASK_DELAYED;
    sCurrentTCB->delayUpTo = tickCount + ticks;
    setInterruptMask(prevMask);

    *ICSR_REG = NVIC_PENDSV_BIT;

    return CRTOS::Result::RESULT_SUCCESS;
}

CRTOS::Result CRTOS::Task::Pause(TaskHandle *handle)
{
    if (handle == nullptr || *handle == nullptr)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    uint32_t prevMask = getInterruptMask();
    TaskControlBlock *task = (TaskControlBlock *)(*handle);

    task->state = TaskState::TASK_PAUSED;

    if (task == sCurrentTCB)
    {
        *ICSR_REG = NVIC_PENDSV_BIT;
    }

    setInterruptMask(prevMask);

    return CRTOS::Result::RESULT_SUCCESS;
}

CRTOS::Result CRTOS::Task::Resume(TaskHandle *handle)
{
    if (handle == nullptr || *handle == nullptr)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    uint32_t prevMask = getInterruptMask();
    TaskControlBlock *task = (TaskControlBlock *)(*handle);

    if (task->state == TaskState::TASK_PAUSED)
    {
        task->state = TaskState::TASK_READY;
    }

    setInterruptMask(prevMask);

    return CRTOS::Result::RESULT_SUCCESS;
}

char *CRTOS::Task::GetCurrentTaskName(void)
{
    return ((char *)(&sCurrentTCB->name[0]));
}

char *CRTOS::Task::GetTaskName(CRTOS::Task::TaskHandle *handle)
{
    if (handle == nullptr || *handle == nullptr)
    {
        return nullptr;
    }

    TaskControlBlock *task = (TaskControlBlock *)(*handle);

    return ((char *)(&task->name[0]));
}

CRTOS::Task::TaskHandle CRTOS::Task::GetCurrentTaskHandle(void)
{
    return ((TaskHandle)sCurrentTCB);
}

uint32_t CRTOS::Task::GetTaskCycles(void)
{
    return sCurrentTCB->executionTime;
}

uint32_t CRTOS::Task::GetFreeStack(void)
{
    TaskControlBlock *tcb = (TaskControlBlock *)sCurrentTCB;

    uint32_t *stackStart = (uint32_t *)(sCurrentTCB->stack);
    uint32_t *stackEnd = (uint32_t *)(sCurrentTCB->stack + sCurrentTCB->stackSize);

    uint32_t usedStack = 0u;

    for (uint32_t *ptr = stackStart; ptr < stackEnd; ++ptr)
    {
        if (*ptr != 0xDEADBEEF)
        {
            usedStack = (uint32_t)(stackEnd - ptr);
            break;
        }
    }

    return (tcb->stackSize - usedStack);
}

uint32_t CRTOS::Task::GetFreeStack(TaskHandle *handle)
{
    if (handle == nullptr || *handle == nullptr)
    {
        return 0u;
    }

    TaskControlBlock *tcb = (TaskControlBlock *)(*handle);

    uint32_t *stackStart = (uint32_t *)(tcb->stack);
    uint32_t *stackEnd = (uint32_t *)(tcb->stack + tcb->stackSize);

    uint32_t usedStack = 0u;

    for (uint32_t *ptr = stackStart; ptr < stackEnd; ++ptr)
    {
        if (*ptr != 0xDEADBEEF)
        {
            usedStack = (uint32_t)(stackEnd - ptr);
            break;
        }
    }

    return (tcb->stackSize - usedStack);
}

uint32_t CRTOS::Task::GetAllTasksStackInfo(CRTOS::TaskStackInfo *infoArray, uint32_t maxTasks)
{
    if (infoArray == nullptr || maxTasks == 0u)
    {
        return 0u;
    }

    uint32_t mask = getInterruptMask();
    uint32_t taskCount = 0u;

    Node<TaskControlBlock> *tmp = readyTaskList;
    while (tmp != nullptr && taskCount < maxTasks)
    {
        TaskControlBlock *tcb = tmp->data;

        // Calculate stack usage
        uint32_t *stackStart = (uint32_t *)(tcb->stack);
        uint32_t *stackEnd = (uint32_t *)(tcb->stack + tcb->stackSize);
        uint32_t usedStack = 0u;

        for (uint32_t *ptr = stackStart; ptr < stackEnd; ++ptr)
        {
            if (*ptr != 0xDEADBEEF)
            {
                usedStack = (uint32_t)(stackEnd - ptr);
                break;
            }
        }

        uint32_t freeStack = tcb->stackSize - usedStack;
        uint32_t totalStackBytes = tcb->stackSize * sizeof(uint32_t);
        uint32_t usedStackBytes = usedStack * sizeof(uint32_t);
        uint32_t freeStackBytes = freeStack * sizeof(uint32_t);

        // Fill in the info structure
        memcpy_optimized(&infoArray[taskCount].name[0], &tcb->name[0], 24u);
        infoArray[taskCount].stackSize = totalStackBytes;
        infoArray[taskCount].stackUsed = usedStackBytes;
        infoArray[taskCount].stackFree = freeStackBytes;
        infoArray[taskCount].heapAllocated = tcb->heapAllocated;
        infoArray[taskCount].taskHandle = (void *)tcb;

        // Calculate utilization percentage
        if (totalStackBytes > 0u)
        {
            infoArray[taskCount].utilizationPercent = (usedStackBytes * 100u) / totalStackBytes;
        }
        else
        {
            infoArray[taskCount].utilizationPercent = 0u;
        }

        taskCount++;
        tmp = tmp->next;
    }

    setInterruptMask(mask);

    return taskCount;
}

void CRTOS::Task::GetCoreLoad(uint32_t &load, uint32_t &mantissa)
{
    static uint32_t lastCheckTime = 0u;
    static uint64_t lastIdleTime = 0u;
    static uint64_t lastTotalTime = 0u;
    static uint32_t lastLoad = 0u;
    static uint32_t lastMantissa = 0u;
    static bool firstRun = true;
    static const uint32_t UPDATE_INTERVAL_TICKS = 1000u; // Update every 1 second at 1kHz

    uint32_t currentTime = GetSystemTime();

    // Only calculate load periodically to get meaningful averages
    if (currentTime - lastCheckTime < UPDATE_INTERVAL_TICKS)
    {
        // Return previous calculation
        load = lastLoad;
        mantissa = lastMantissa;
        return;
    }

    // Get current execution times
    uint64_t currentIdle = GetIdleTaskTime();
    uint64_t currentTotal = 0u;

    uint32_t mask = getInterruptMask();
    Node<TaskControlBlock> *tmp = readyTaskList;
    while (tmp != nullptr)
    {
        currentTotal += (tmp->data->executionTime);
        tmp = tmp->next;
    }
    setInterruptMask(mask);

    // For first run, just store values and return 0
    if (firstRun)
    {
        lastIdleTime = currentIdle;
        lastTotalTime = currentTotal;
        lastCheckTime = currentTime;
        firstRun = false;

        load = 0u;
        mantissa = 0u;
        lastLoad = load;
        lastMantissa = mantissa;
        return;
    }

    // Calculate delta since last measurement
    uint64_t deltaIdle = currentIdle - lastIdleTime;
    uint64_t deltaTotal = currentTotal - lastTotalTime;

    // Store current values for next calculation
    lastIdleTime = currentIdle;
    lastTotalTime = currentTotal;
    lastCheckTime = currentTime;

    // Prevent division by zero and handle edge cases
    if (deltaTotal == 0u)
    {
        load = 0u;
        mantissa = 0u;
        lastLoad = load;
        lastMantissa = mantissa;
        return;
    }

    // Calculate CPU load as percentage with better precision
    uint64_t idle_percentage_scaled = (deltaIdle * 10000u) / deltaTotal;

    // Clamp to maximum 100%
    if (idle_percentage_scaled > 10000u)
    {
        idle_percentage_scaled = 10000u;
    }

    // CPU load = 100% - idle%
    uint64_t cpu_load_scaled = 10000u - idle_percentage_scaled;

    load = (uint32_t)(cpu_load_scaled / 100u);
    mantissa = (uint32_t)(cpu_load_scaled % 100u);

    // Store for returning between updates
    lastLoad = load;
    lastMantissa = mantissa;
}

uint32_t CRTOS::Task::GetLastTaskSwitchTime(void)
{
    return switchTime;
}

// ========================================
// Module Management Functions
// ========================================

uint32_t CRTOS::Task::LPC55S69_Features::GetLoadedModulesCount(void)
{
    return loadedModulesCount;
}

uint32_t CRTOS::Task::LPC55S69_Features::GetAllModulesInfo(CRTOS::ModuleInfo *infoArray, uint32_t maxModules)
{
    if (infoArray == nullptr || maxModules == 0 || loadedModules == nullptr)
    {
        return 0;
    }

    uint32_t prevMask = getInterruptMask();
    uint32_t count = 0;

    for (uint32_t i = 0; i < loadedModulesCount && count < maxModules; i++)
    {
        LoadedModuleInfo *src = &loadedModules[i];
        CRTOS::ModuleInfo *dest = &infoArray[count];

        // Copy module information
        memcpy_optimized(dest->name, src->name, 24);
        dest->baseAddress = src->baseAddress;
        dest->entryPoint = src->entryPoint;
        dest->textAddr = src->textAddr;
        dest->textSize = src->textSize;
        dest->dataAddr = src->dataAddr;
        dest->dataSize = src->dataSize;
        dest->bssAddr = src->bssAddr;
        dest->bssSize = src->bssSize;
        dest->stackAddr = src->stackAddr;
        dest->stackSize = src->stackSize;
        dest->totalSize = src->totalSize;
        dest->taskHandle = (void *)src->tcb;
        dest->state = src->state;
        dest->loadTime = src->loadTime;
        dest->sharedMemory = (void *)src->sharedMemory;

        count++;
    }

    setInterruptMask(prevMask);
    return count;
}

CRTOS::Result CRTOS::Task::LPC55S69_Features::GetModuleInfo(TaskHandle *handle, CRTOS::ModuleInfo &info)
{
    if (handle == nullptr || loadedModules == nullptr)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    uint32_t prevMask = getInterruptMask();
    CRTOS::Result result = CRTOS::Result::RESULT_TASK_NOT_FOUND;

    TaskControlBlock *targetTCB = reinterpret_cast<TaskControlBlock *>(*handle);

    for (uint32_t i = 0; i < loadedModulesCount; i++)
    {
        if (loadedModules[i].tcb == targetTCB)
        {
            LoadedModuleInfo *src = &loadedModules[i];

            memcpy_optimized(info.name, src->name, 24);
            info.baseAddress = src->baseAddress;
            info.entryPoint = src->entryPoint;
            info.textAddr = src->textAddr;
            info.textSize = src->textSize;
            info.dataAddr = src->dataAddr;
            info.dataSize = src->dataSize;
            info.bssAddr = src->bssAddr;
            info.bssSize = src->bssSize;
            info.stackAddr = src->stackAddr;
            info.stackSize = src->stackSize;
            info.totalSize = src->totalSize;
            info.taskHandle = (void *)src->tcb;
            info.state = src->state;
            info.loadTime = src->loadTime;
            info.sharedMemory = (void *)src->sharedMemory;

            result = CRTOS::Result::RESULT_SUCCESS;
            break;
        }
    }

    setInterruptMask(prevMask);
    return result;
}

CRTOS::Result CRTOS::Task::LPC55S69_Features::SetModuleState(TaskHandle *handle, CRTOS::ModuleState newState)
{
    if (handle == nullptr || loadedModules == nullptr)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    uint32_t prevMask = getInterruptMask();
    CRTOS::Result result = CRTOS::Result::RESULT_TASK_NOT_FOUND;

    TaskControlBlock *targetTCB = reinterpret_cast<TaskControlBlock *>(*handle);

    for (uint32_t i = 0; i < loadedModulesCount; i++)
    {
        if (loadedModules[i].tcb == targetTCB)
        {
            loadedModules[i].state = newState;

            // Also update task state based on module state
            switch (newState)
            {
            case CRTOS::ModuleState::MODULE_RUNNING:
                targetTCB->state = TaskState::TASK_READY;
                result = CRTOS::Result::RESULT_SUCCESS;
                break;

            case CRTOS::ModuleState::MODULE_PAUSED:
                targetTCB->state = TaskState::TASK_PAUSED;
                result = CRTOS::Result::RESULT_SUCCESS;
                break;

            case CRTOS::ModuleState::MODULE_STOPPED:
            case CRTOS::ModuleState::MODULE_FAILED:
                // Don't change task state for these
                result = CRTOS::Result::RESULT_SUCCESS;
                break;

            default:
                result = CRTOS::Result::RESULT_BAD_PARAMETER;
                break;
            }
            break;
        }
    }

    setInterruptMask(prevMask);
    return result;
}

// Module data exchange functions
CRTOS::Result CRTOS::Task::LPC55S69_Features::WriteToModule(TaskHandle *handle, uint32_t value)
{
    if (handle == nullptr || loadedModules == nullptr)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    uint32_t prevMask = getInterruptMask();
    CRTOS::Result result = CRTOS::Result::RESULT_TASK_NOT_FOUND;

    TaskControlBlock *targetTCB = reinterpret_cast<TaskControlBlock *>(*handle);

    for (uint32_t i = 0; i < loadedModulesCount; i++)
    {
        if (loadedModules[i].tcb == targetTCB && loadedModules[i].sharedMemory != nullptr)
        {
            ModuleSharedMemory *mem = loadedModules[i].sharedMemory;
            mem->hostToModule = value;
            mem->flags |= MODULE_FLAG_HOST_HAS_DATA;
            mem->flags &= ~MODULE_FLAG_MODULE_ACK;

            result = CRTOS::Result::RESULT_SUCCESS;
            break;
        }
    }

    setInterruptMask(prevMask);
    return result;
}

CRTOS::Result CRTOS::Task::LPC55S69_Features::ReadFromModule(TaskHandle *handle, uint32_t &value)
{
    if (handle == nullptr || loadedModules == nullptr)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    uint32_t prevMask = getInterruptMask();
    CRTOS::Result result = CRTOS::Result::RESULT_TASK_NOT_FOUND;

    TaskControlBlock *targetTCB = reinterpret_cast<TaskControlBlock *>(*handle);

    for (uint32_t i = 0; i < loadedModulesCount; i++)
    {
        if (loadedModules[i].tcb == targetTCB && loadedModules[i].sharedMemory != nullptr)
        {
            ModuleSharedMemory *mem = loadedModules[i].sharedMemory;
            value = mem->moduleToHost;
            mem->flags &= ~MODULE_FLAG_DATA_READY;
            mem->flags |= MODULE_FLAG_HOST_ACK;

            result = CRTOS::Result::RESULT_SUCCESS;
            break;
        }
    }

    setInterruptMask(prevMask);
    return result;
}

CRTOS::Result CRTOS::Task::LPC55S69_Features::GetModuleSharedMemory(TaskHandle *handle, void **sharedMemPtr)
{
    if (handle == nullptr || sharedMemPtr == nullptr || loadedModules == nullptr)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    uint32_t prevMask = getInterruptMask();
    CRTOS::Result result = CRTOS::Result::RESULT_TASK_NOT_FOUND;

    TaskControlBlock *targetTCB = reinterpret_cast<TaskControlBlock *>(*handle);

    for (uint32_t i = 0; i < loadedModulesCount; i++)
    {
        if (loadedModules[i].tcb == targetTCB)
        {
            *sharedMemPtr = (void *)loadedModules[i].sharedMemory;
            result = CRTOS::Result::RESULT_SUCCESS;
            break;
        }
    }

    setInterruptMask(prevMask);
    return result;
}
