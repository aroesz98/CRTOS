/*
 * CRTOS
 * Author: Arkadiusz Szlanta
 * Date: 17 Dec 2024
 *
 * License:
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 */

#include "CRTOS.hpp"
#include "CRTOS_Internal.hpp"

#include <HeapAllocator.hpp>

#include "ELFParser.hpp"
#include "stdio.h"
#include <cstring>

__attribute__((used)) volatile TaskControlBlock *sCurrentTCB = nullptr;
CRTOS::Task::TaskHandle idleTaskHandle = nullptr;

// Module tracking
LoadedModuleInfo *loadedModules = nullptr;
uint32_t loadedModulesCount = 0;
uint32_t MAX_LOADED_MODULES = 16;

// Constants and hardware register definitions moved to CRTOS_Internal.hpp

extern "C" void SVC_Handler(void) __attribute__((naked));
extern "C" void PendSV_Handler(void) __attribute__((naked));
extern "C" void SysTick_Handler(void);

extern "C" void RestoreCtxOfTheFirstTask(void) __attribute__((naked));
extern "C" void startFirstTask(void) __attribute__((naked));

// __DSB and __ISB are in CRTOS_Internal.hpp

extern "C" void memcpy_optimized(void *d, void *s, uint32_t len);
extern "C" void memset_optimized(void *d, uint32_t val, uint32_t len);

// Interrupt mask functions with C linkage for use in both CRTOS.cpp and Task.cpp
extern "C" uint32_t getInterruptMask(void);
extern "C" void setInterruptMask(uint32_t mask);

volatile uint32_t tickCount = 0u;

uint32_t MAX_TASK_PRIORITY = 10u;
static uint32_t sTickRate = 1000u;
static uint32_t sCoreClock = 150000000u;

// Time slicing configuration
static uint32_t sTimeSliceQuantum = 10u;  // Default: 10 ticks per time slice
static uint32_t sTimeSliceRemaining = 10u; // Remaining ticks for current task
static bool sTimeSliceExpired = false;     // Flag: was context switch triggered by time slice expiration?

uint32_t MODULE_MAGIC = 0x4D4F4455u; // 'MODU'
uint32_t DEFAULT_MODULE_LEN = 4096u;
uint32_t DEFAULT_STACK_SIZE = 1024u;

HeapAllocator mem;

static bool isPendingTask(void);

// Internal function to trigger round-robin on explicit yield
extern "C" void setTimeSliceExpired(void)
{
    sTimeSliceExpired = true;
}

volatile uint32_t switchTime = 0u;
volatile uint32_t switchStartTime = 0u;

static void switchedIn(void)
{
    uint32_t currentTime = DWT->CYCCNT;
    if (switchStartTime != 0u)
    {
        switchTime = currentTime - switchStartTime;
        switchStartTime = 0u;
    }
}

static void switchedOut(void)
{
    switchStartTime = DWT->CYCCNT;
}

#define TASK_SWITCHED_IN() switchedIn()
#define TASK_SWITCHED_OUT() switchedOut()

uint32_t GetSystemTime(void)
{
    return tickCount;
}

uint32_t GetIdleTaskTime(void)
{
    uint32_t totalTime = 0u;

    TaskControlBlock *idle = (TaskControlBlock *)idleTaskHandle;
    totalTime = idle->executionTime;

    return totalTime;
}

// Explicit template instantiations for static tail members
template <>
Node<TaskControlBlock> *Node<TaskControlBlock>::tail = nullptr;
template <>
Node<CRTOS::Timer::SoftwareTimer> *Node<CRTOS::Timer::SoftwareTimer>::tail = nullptr;
template <>
Node<unsigned long *> *Node<unsigned long *>::tail = nullptr;

Node<TaskControlBlock> *readyTaskList = nullptr;
Node<CRTOS::Timer::SoftwareTimer> *sTimerList = nullptr;

CRTOS::Task::StackOverflowHook sStackOverflowHook = nullptr;

uint32_t pStringLength(const char *buffer)
{
    const char *tmp = buffer;

    while (*tmp != 0)
    {
        tmp++;
    }

    return (tmp - buffer) + 1;
}

CRTOS::Result CRTOS::Config::InitMem(void *pool, uint32_t size)
{
    if (pool == nullptr || size == 0u)
    {
        return CRTOS::Result::RESULT_NO_MEMORY;
    }

    mem.init(pool, size);

    return CRTOS::Result::RESULT_SUCCESS;
}

void *CRTOS::Config::Allocate(uint32_t size)
{
    return mem.allocate(size);
}

void CRTOS::Config::Deallocate(void *ptr)
{
    mem.deallocate(ptr);
}

uint32_t CRTOS::Config::GetAllocatedMemory(void)
{
    return mem.getAllocatedMemory();
}

uint32_t CRTOS::Config::GetFreeMemory(void)
{
    return mem.getFreeMemory();
}

uint32_t CRTOS::Config::GetTotalHeapSize(void)
{
    void *pool = nullptr;
    uint32_t size = 0;
    mem.getMemoryPool(&pool, size);
    return size;
}

void CRTOS::Config::GetHeapInfo(CRTOS::HeapInfo &info)
{
    void *pool = nullptr;
    uint32_t totalSize = 0;

    mem.getMemoryPool(&pool, totalSize);

    info.totalSize = totalSize;
    info.freeMemory = mem.getFreeMemory();
    // Calculate allocated as total minus free (this accounts for all metadata automatically)
    info.allocatedMemory = (totalSize > info.freeMemory) ? (totalSize - info.freeMemory) : 0;

    // Calculate utilization percentage
    if (totalSize > 0)
    {
        info.utilizationPercent = (info.allocatedMemory * 100) / totalSize;
    }
    else
    {
        info.utilizationPercent = 0;
    }
}

void CRTOS::Config::DefragmentHeap(void)
{
    mem.defragment();
}

void CRTOS::Config::SetCoreClock(uint32_t clock)
{
    if (clock > 1000000u)
    {
        sCoreClock = clock;
    }
}

void CRTOS::Config::SetTickRate(uint32_t ticks)
{
    if (ticks < 1000000)
    {
        sTickRate = ticks;
    }
}

void CRTOS::Config::SetTimeSlice(uint32_t ticks)
{
    if (ticks > 0 && ticks < 1000)
    {
        sTimeSliceQuantum = ticks;
        sTimeSliceRemaining = ticks;
    }
}

// These functions need C linkage for use in Task.cpp
extern "C" uint32_t getInterruptMask(void)
{
    uint32_t basepri, newBasepri;

    __asm volatile(
        "mrs %0, basepri    \n"
        "mov %1, %2         \n"
        "msr basepri, %1    \n"
        "isb                \n"
        "dsb                \n"
        : "=r"(basepri), "=r"(newBasepri) : "i"(MAX_SYSCALL_IRQ_PRIO) : "memory");

    return basepri;
}

extern "C" void setInterruptMask(uint32_t mask)
{
    __asm__ volatile("msr basepri, %0" ::"r"(mask) : "memory");
}

void CRTOS::RegisterCurrentTaskIRQ(uint32_t irqNumber)
{
    uint32_t mask = getInterruptMask();
    if (sCurrentTCB != nullptr)
    {
        const_cast<TaskControlBlock*>(sCurrentTCB)->registeredIRQ = irqNumber;
    }
    setInterruptMask(mask);
}

void CRTOS::UnregisterCurrentTaskIRQ(void)
{
    uint32_t mask = getInterruptMask();
    if (sCurrentTCB != nullptr)
    {
        const_cast<TaskControlBlock*>(sCurrentTCB)->registeredIRQ = 0xFFFFFFFFu;
    }
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

bool isHigherPrioTaskPending(void)
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
        if ((temp->data->state == TaskState::TASK_READY) && (sCurrentTCB->priority < temp->data->priority))
        {
            return true;
        }

        temp = temp->next;
    }

    return false;
}

void RestoreCtxOfTheFirstTask(void)
{
    sCurrentTCB->enterCycles = DWT->CYCCNT;
    __asm volatile(
        ".syntax unified                       \n"
        // Read top of the stack
        "ldr  r2, currentCtxTCB                \n"
        "ldr  r1, [r2]                         \n"
        "ldr  r0, [r1]                         \n"
        // R2 = EXC_RETURN
        "ldm  r0!, {r4-r11, lr}                \n"
        // Update current PSP
        "msr  psp, r0                          \n"
        "isb                                   \n"
        "mov  r0, #0                           \n"
        // Enable interrupts and exit
        "msr  basepri, r0                      \n"
        "bx   lr                               \n"
        ".align 4                              \n"
        "currentCtxTCB: .word sCurrentTCB      \n");
}

extern "C" void SVC_Handle_Subprocess(uint32_t *command)
{
    uint32_t command_id = (uint32_t)(((uint8_t *)command[6u])[-2u]);
    uint32_t *callerStack = command;

    switch (command_id)
    {
    case SVC_Commands::COMMAND_TASK_DELAY:
        CRTOS::Task::Delay(callerStack[0u]);
        break;
    case SVC_Commands::COMMAND_START_SCHEDULER:
        RestoreCtxOfTheFirstTask();
        break;
    case SVC_Commands::COMMAND_MODULE_GET_SHARED_MEM:
    {
        // Find the module associated with the current task
        ModuleSharedMemory *sharedMem = nullptr;
        if (loadedModules != nullptr && sCurrentTCB != nullptr && loadedModulesCount > 0)
        {
            for (uint32_t i = 0; i < loadedModulesCount; i++)
            {
                if (loadedModules[i].tcb == sCurrentTCB && loadedModules[i].sharedMemory != nullptr)
                {
                    sharedMem = loadedModules[i].sharedMemory;
                    printf("DEBUG SVC: Found shared memory for task at 0x%08lX\n", (uint32_t)sharedMem);
                    break;
                }
            }
            if (sharedMem == nullptr)
            {
                printf("DEBUG SVC: Shared memory NOT found. Current TCB=0x%08lX, Modules=%lu\n",
                       (uint32_t)sCurrentTCB, loadedModulesCount);
            }
        }
        else
        {
            printf("DEBUG SVC: Invalid state - modules=0x%08lX, TCB=0x%08lX, count=%lu\n",
                   (uint32_t)loadedModules, (uint32_t)sCurrentTCB, loadedModulesCount);
        }
        // Return pointer in R0 (will be null if not found or invalid)
        callerStack[0u] = (uint32_t)sharedMem;
        break;
    }
    case SVC_Commands::COMMAND_MODULE_LOG:
    {
        // Print log message from module directly to console
        const char *message = (const char *)callerStack[0u];
        if (message != nullptr)
        {
            printf("%s", message);
        }
        break;
    }
    case SVC_Commands::COMMAND_TIMER_INIT:
    {
        // Timer init: R0 = timer ptr, R1 = timeout, R2 = callback, R3 = args, [SP+0] = autoReload
        CRTOS::Timer::SoftwareTimer *timer = (CRTOS::Timer::SoftwareTimer *)callerStack[0u];
        uint32_t timeout = callerStack[1u];
        void (*callback)(void *) = (void (*)(void *))callerStack[2u];
        void *args = (void *)callerStack[3u];
        uint32_t autoReload = callerStack[4u]; // From stack frame

        CRTOS::Result result = CRTOS::Timer::Init(timer, timeout, callback, args, autoReload != 0);
        callerStack[0u] = (uint32_t)result;
        break;
    }
    case SVC_Commands::COMMAND_TIMER_START:
    {
        // Timer start: R0 = timer ptr
        CRTOS::Timer::SoftwareTimer *timer = (CRTOS::Timer::SoftwareTimer *)callerStack[0u];
        CRTOS::Result result = CRTOS::Timer::Start(timer);
        callerStack[0u] = (uint32_t)result;
        break;
    }
    case SVC_Commands::COMMAND_TIMER_STOP:
    {
        // Timer stop: R0 = timer ptr
        CRTOS::Timer::SoftwareTimer *timer = (CRTOS::Timer::SoftwareTimer *)callerStack[0u];
        CRTOS::Result result = CRTOS::Timer::Stop(timer);
        callerStack[0u] = (uint32_t)result;
        break;
    }
    case SVC_Commands::COMMAND_GET_TASK_COUNT:
    {
        // Get total number of tasks in the system
        CRTOS::TaskStackInfo stackInfo[20];
        uint32_t count = CRTOS::Task::GetAllTasksStackInfo(stackInfo, 20);
        callerStack[0u] = count;
        break;
    }
    case SVC_Commands::COMMAND_GET_TASK_INFO:
    {
        // Get task info by index: R0 = task_index, R1 = ModuleTaskInfo* output
        uint32_t task_index = callerStack[0u];
        ModuleTaskInfo *info = (ModuleTaskInfo *)callerStack[1u];
        
        if (info != nullptr)
        {
            CRTOS::TaskStackInfo stackInfo[20];
            uint32_t count = CRTOS::Task::GetAllTasksStackInfo(stackInfo, 20);
            
            if (task_index < count)
            {
                // Copy task information
                strncpy(info->name, stackInfo[task_index].name, 23);
                info->name[23] = '\0';
                info->stackSize = stackInfo[task_index].stackSize;
                info->stackUsed = stackInfo[task_index].stackUsed;
                info->stackFree = stackInfo[task_index].stackFree;
                info->stackPercent = stackInfo[task_index].utilizationPercent;
                info->heapAllocated = stackInfo[task_index].heapAllocated;
                
                // Get priority, state, and runtime cycles from TCB
                TaskControlBlock *tcb = (TaskControlBlock *)stackInfo[task_index].taskHandle;
                if (tcb != nullptr)
                {
                    info->priority = tcb->priority;
                    info->state = (uint32_t)tcb->state;
                    info->runtimeCycles = tcb->executionTime;
                    info->registeredIRQ = tcb->registeredIRQ;
                }
                else
                {
                    info->priority = 0;
                    info->state = 0;
                    info->runtimeCycles = 0;
                    info->registeredIRQ = 0xFFFFFFFFu;
                }
                
                callerStack[0u] = 0;  // Success
            }
            else
            {
                callerStack[0u] = (uint32_t)-1;  // Error: index out of range
            }
        }
        else
        {
            callerStack[0u] = (uint32_t)-1;  // Error: null pointer
        }
        break;
    }
    case SVC_Commands::COMMAND_GET_HEAP_INFO:
    {
        // Get heap info: R0 = ModuleHeapInfo* output
        ModuleHeapInfo *info = (ModuleHeapInfo *)callerStack[0u];
        
        if (info != nullptr)
        {
            CRTOS::HeapInfo heapInfo;
            CRTOS::Config::GetHeapInfo(heapInfo);
            
            info->totalSize = heapInfo.totalSize;
            info->freeMemory = heapInfo.freeMemory;
            info->allocatedMemory = heapInfo.allocatedMemory;
            info->utilizationPercent = heapInfo.utilizationPercent;
        }
        break;
    }
    case SVC_Commands::COMMAND_MODULE_MALLOC:
    {
        // Module memory allocation: R0 = size, returns pointer in R0
        uint32_t size = callerStack[0u];
        void *ptr = mem.allocate(size);
        callerStack[0u] = (uint32_t)ptr;
        break;
    }
    case SVC_Commands::COMMAND_MODULE_FREE:
    {
        // Module memory deallocation: R0 = pointer
        void *ptr = (void *)callerStack[0u];
        if (ptr != nullptr)
        {
            mem.deallocate(ptr);
        }
        break;
    }
    default:
        break;
    }
}

extern "C" void MemManage_Handler(void)
{
    // Determine which stack pointer to use and call the print info function
    __asm volatile(
        "MOV R1, LR\n" // Save exception return LR value
        "TST R1, #4\n" // Test bit 2 to determine stack
        "ITE EQ\n"
        "MRSEQ R0, MSP\n"          // If using MSP
        "MRSNE R0, PSP\n"          // If using PSP
        "PUSH {R1, LR}\n"          // Save exception LR and current LR
        "BL MemManage_PrintInfo\n" // Call function (it may return or hang)
        "POP {R1, LR}\n"           // Restore exception LR
        "BX R1\n"                  // Exception return using saved EXC_RETURN value
    );
}

extern "C" void MemManage_PrintInfo(uint32_t *stack_frame)
{
    // Stack frame: R0, R1, R2, R3, R12, LR, PC, xPSR
    uint32_t r0 = stack_frame[0];
    uint32_t r1 = stack_frame[1];
    uint32_t r2 = stack_frame[2];
    uint32_t r3 = stack_frame[3];
    uint32_t r12 = stack_frame[4];
    uint32_t lr = stack_frame[5];
    uint32_t pc = stack_frame[6];
    uint32_t psr = stack_frame[7];

    volatile uint32_t *CFSR = (uint32_t *)0xE000ED28;
    volatile uint32_t *MMFAR = (uint32_t *)0xE000ED34;
    uint32_t mmfsr = (*CFSR) & 0xFF;
    uint32_t mmfar = *MMFAR;

    // Print fault information
    printf("\r\n\r\n=== MEMMANAGE FAULT ===\r\n");
    printf("MMFSR: 0x%02X\r\n", (unsigned int)mmfsr);

    // Decode fault type
    printf("Fault Type: ");
    if (mmfsr & 0x01)
        printf("IACCVIOL (Instruction access violation) ");
    if (mmfsr & 0x02)
        printf("DACCVIOL (Data access violation) ");
    if (mmfsr & 0x08)
        printf("MUNSTKERR (Unstacking error) ");
    if (mmfsr & 0x10)
        printf("MSTKERR (Stacking error) ");
    if (mmfsr & 0x20)
        printf("MLSPERR (FP lazy state error) ");
    printf("\r\n");

    // Print MMFAR only if valid
    if (mmfsr & 0x80)
    {
        printf("MMFAR (Fault Address): 0x%08X (VALID)\r\n", (unsigned int)mmfar);
    }
    else
    {
        printf("MMFAR (Fault Address): 0x%08X (INVALID - not set for this fault type)\r\n", (unsigned int)mmfar);
        if (mmfsr & 0x01)
        {
            printf("  Note: For IACCVIOL, check PC or register values for the target address\r\n");
        }
    }

    printf("\r\nStack Frame:\r\n");
    printf("  R0:  0x%08X\r\n", (unsigned int)r0);
    printf("  R1:  0x%08X\r\n", (unsigned int)r1);
    printf("  R2:  0x%08X\r\n", (unsigned int)r2);
    printf("  R3:  0x%08X\r\n", (unsigned int)r3);
    printf("  R12: 0x%08X (Jump to this address caused the issue)\r\n", (unsigned int)r12);
    printf("  LR:  0x%08X\r\n", (unsigned int)lr);
    printf("  PC:  0x%08X (Fault occurred here)\r\n", (unsigned int)pc);
    printf("  PSR: 0x%08X\r\n", (unsigned int)psr);

    // Attempt recovery
    printf("\r\nAttempting to recover from fault...\r\n");

    // Check stack boundaries first - MIMXRT1052 RAM typically 0x20000000-0x20020000
    uint32_t stack_ptr = (uint32_t)stack_frame;
    uint32_t stack_after_return = stack_ptr + 32; // Exception frame is 32 bytes

    // If stack would be at/beyond RAM limit after return, cannot safely recover
    if (stack_after_return >= 0x20020000)
    {
        printf("ERROR: Stack at 0x%08X, would overflow after exception return!\r\n", (unsigned int)stack_ptr);
        printf("Stack overflow detected - cannot recover.\r\n");
        printf("===================\r\n\r\n");
        while (1)
        {
        }
    }

    // Clear the MemManage fault status bits by writing 1s to them
    *CFSR = mmfsr;

    // Get current task information
    CRTOS::Task::TaskHandle currentTask = CRTOS::Task::GetCurrentTaskHandle();
    char *taskName = CRTOS::Task::GetCurrentTaskName();

    printf("Faulting Task: %s (handle: 0x%08X)\r\n",
           taskName ? taskName : "Unknown", (unsigned int)currentTask);

    // Kill the faulting task
    printf("Terminating faulting task...\r\n");
    CRTOS::Result result = CRTOS::Task::Delete(&currentTask);

    if (result == CRTOS::Result::RESULT_SUCCESS)
    {
        printf("Task terminated successfully. System will continue with other tasks.\r\n");
        printf("===================\r\n\r\n");

        // Clear fault flags again
        *CFSR = mmfsr;

        // Force a context switch to another task
        // Trigger PendSV to schedule next task
        volatile uint32_t *ICSR = (uint32_t *)0xE000ED04;
        *ICSR = (1 << 28); // Set PENDSVSET bit

        // The exception return will now switch to a different task
        return;
    }
    else
    {
        printf("ERROR: Failed to terminate task (error %d)\r\n", (int)result);
        printf("System cannot recover.\r\n");
        printf("===================\r\n\r\n");
        while (1)
        {
        }
    }
}

void SVC_Handler(void)
{
    __asm volatile(
        ".syntax unified                              \n"
        "tst lr, #4                                   \n"
        "ite eq                                       \n"
        "mrseq r0, msp                                \n"
        "mrsne r0, psp                                \n"
        "ldr r1, SVC_ISR_ADDR                         \n"
        "bx r1                                        \n"
        ".align 4                                     \n"
        "SVC_ISR_ADDR:                                \n"
        "\t.word SVC_Handle_Subprocess                \n");
}

extern "C" void PendSV_Handler(void)
{
    __asm volatile(
        ".syntax unified     \n"
        // Load PSP to R0
        "mrs r0, psp         \n"
        // Save FPU context if needed
        "tst r14, #0x10          \n"
        "it eq                   \n"
        "vstmdbeq r0!, {s16-s31} \n"
        // Save r4-r11 and LR under PSP location
        "stmdb r0!, {r4-r11, lr} \n"
        // NOW check for stack overflow after all registers are pushed
        "ldr r2, currentTCB  \n"
        "ldr r1, [r2]        \n"
        "ldr r3, [r1, #4]    \n" // Load stack bottom address (TCB->stack)
        "cmp r0, r3          \n" // Compare PSP with stack bottom
        "blo stackOverflow   \n" // Branch if PSP < stack bottom (overflow!)
        // Save new PSP
        "str r0, [r1]        \n"
        // Perform context switch
        "mov r0, %0          \n"
        "msr basepri, r0     \n"
        "dsb                 \n"
        "isb                 \n"
        "bl switchCtx        \n"
        "mov r0, #0          \n"
        "msr basepri, r0     \n"
        // Load PSP address of next task to R0
        "ldr r2, currentTCB  \n"
        "ldr r1, [r2]        \n"
        "ldr r0, [r1]        \n"
        // Restore context of next task
        "ldmia r0!, {r4-r11, lr} \n"
        "tst r14, #0x10          \n"
        "it eq                   \n"
        "vldmiaeq r0!, {s16-s31} \n"
        // set new PSP
        "msr psp, r0         \n"
        // Leave interrupt
        "bx lr               \n"
        "stackOverflow:      \n"
        "bl stackOverflowDetected \n"
        "b stackOverflow     \n" // Infinite loop after overflow
        ".align 4            \n"
        "currentTCB: .word sCurrentTCB \n" ::"i"(MAX_SYSCALL_IRQ_PRIO));
}

void startFirstTask(void)
{
    __asm volatile(
        ".syntax unified \n"
        "mov r0, #0      \n"
        "msr control, r0 \n"
        "cpsie i         \n"
        "cpsie f         \n"
        "dsb             \n"
        "isb             \n"
        "svc %0          \n"
        "nop             \n"
        ".align 4        \n" ::"i"(SVC_Commands::COMMAND_START_SCHEDULER) : "memory");
}

// __ISB and __DSB moved to CRTOS_Internal.hpp

extern "C" char *currentTaskName(void)
{
    return (char *)(&(sCurrentTCB->name[0]));
}

extern "C" void stackOverflowDetected(void)
{
    (void)getInterruptMask();
    __DSB();
    __ISB();

    if (sStackOverflowHook != nullptr && sCurrentTCB != nullptr)
    {
        sStackOverflowHook((const char *)(&(sCurrentTCB->name[0])), (void *)sCurrentTCB);
    }

    // Infinite loop if no hook or after hook returns
    while (1)
    {
        __asm volatile("nop");
    }
}

void updateExitCycles(void)
{
    sCurrentTCB->exitCycles = DWT->CYCCNT;
}

void updateEnterCycles(void)
{
    sCurrentTCB->enterCycles = DWT->CYCCNT;
}

extern "C" void switchCtx(void)
{
    Node<TaskControlBlock> *temp = readyTaskList;
    Node<TaskControlBlock> *highestPriorityTask = nullptr;

    TASK_SWITCHED_OUT();
    updateExitCycles();

    if (sCurrentTCB != nullptr)
    {
        uint32_t elapsedCycles;
        if (sCurrentTCB->enterCycles > sCurrentTCB->exitCycles)
        {
            elapsedCycles = 0xFFFFFFFFu - sCurrentTCB->exitCycles + sCurrentTCB->enterCycles;
        }
        else
        {
            elapsedCycles = sCurrentTCB->exitCycles - sCurrentTCB->enterCycles;
        }

        sCurrentTCB->executionTime += elapsedCycles;
    }

    while (temp != nullptr)
    {
        switch (temp->data->state)
        {
        case TaskState::TASK_DELAYED:
            if (tickCount >= temp->data->delayUpTo)
            {
                temp->data->state = TaskState::TASK_READY;
            }
            break;
        case TaskState::TASK_BLOCKED_BY_SEMAPHORE:
            if (tickCount >= temp->data->timeout)
            {
                temp->data->state = TaskState::TASK_READY;
            }
            break;
        case TaskState::TASK_BLOCKED_BY_QUEUE:
            if (tickCount >= temp->data->timeout)
            {
                temp->data->state = TaskState::TASK_READY;
            }
            break;
        case TaskState::TASK_BLOCKED_BY_CIRC_BUFFER:
            if (tickCount >= temp->data->timeout)
            {
                temp->data->state = TaskState::TASK_READY;
            }
            break;
        case TaskState::TASK_RUNNING:
            sCurrentTCB->state = TaskState::TASK_READY;
            break;
        default:
            break;
        }

        if (temp->data->state == TaskState::TASK_READY)
        {
            if (highestPriorityTask == nullptr || temp->data->priority > highestPriorityTask->data->priority)
            {
                highestPriorityTask = temp;
            }
        }

        temp = temp->next;
    }

    if (highestPriorityTask != nullptr)
    {
        TaskControlBlock *nextTask = highestPriorityTask->data;
        
        // Round-robin scheduling: Only rotate if time slice expired and
        // current task is still ready at the highest priority level
        if (sTimeSliceExpired && 
            sCurrentTCB != nullptr && 
            sCurrentTCB->state == TaskState::TASK_READY && 
            sCurrentTCB->priority == highestPriorityTask->data->priority)
        {
            // Count tasks at same priority and find next one
            Node<TaskControlBlock> *searchNode = readyTaskList;
            uint32_t tasksAtSamePriority = 0;
            Node<TaskControlBlock> *firstAtPriority = nullptr;
            bool foundCurrent = false;
            
            // First pass: count tasks and find first
            searchNode = readyTaskList;
            while (searchNode != nullptr)
            {
                if (searchNode->data->state == TaskState::TASK_READY && 
                    searchNode->data->priority == sCurrentTCB->priority)
                {
                    tasksAtSamePriority++;
                    if (firstAtPriority == nullptr)
                    {
                        firstAtPriority = searchNode;
                    }
                }
                searchNode = searchNode->next;
            }
            
            // Only do round-robin if there are multiple tasks at this priority
            if (tasksAtSamePriority > 1)
            {
                // Second pass: find next task after current
                searchNode = readyTaskList;
                foundCurrent = false;
                
                while (searchNode != nullptr)
                {
                    if (searchNode->data == sCurrentTCB)
                    {
                        foundCurrent = true;
                    }
                    else if (foundCurrent && 
                             searchNode->data->state == TaskState::TASK_READY && 
                             searchNode->data->priority == sCurrentTCB->priority)
                    {
                        // Found next task at same priority
                        nextTask = searchNode->data;
                        break;
                    }
                    searchNode = searchNode->next;
                }
                
                // If no task found after current, wrap to first at this priority
                if (nextTask == highestPriorityTask->data && firstAtPriority != nullptr)
                {
                    nextTask = firstAtPriority->data;
                }
            }
        }
        
        sCurrentTCB = nextTask;
        sCurrentTCB->state = TaskState::TASK_RUNNING;
        // Reset time slice for the new/continuing task
        sTimeSliceRemaining = sTimeSliceQuantum;
        sTimeSliceExpired = false;  // Clear the flag
    }
    else
    {
        sCurrentTCB = (TaskControlBlock *)(idleTaskHandle);
        sCurrentTCB->state = TaskState::TASK_RUNNING;
        sTimeSliceRemaining = sTimeSliceQuantum;
        sTimeSliceExpired = false;  // Clear the flag
    }

    updateEnterCycles();
    TASK_SWITCHED_IN();
}

void SysTick_Handler(void)
{
    uint32_t mask = getInterruptMask();

    tickCount++;

    // Decrement time slice counter
    if (sTimeSliceRemaining > 0u)
    {
        sTimeSliceRemaining--;
    }

    // Force context switch if time slice expired or if there's a pending higher priority task
    bool timeSliceExpired = (sTimeSliceRemaining == 0u);
    bool hasPendingTask = isPendingTask();
    
    if (hasPendingTask || timeSliceExpired)
    {
        // Set flag for switchCtx to know if round-robin should be triggered
        if (timeSliceExpired)
        {
            sTimeSliceExpired = true;
        }
        
        CRTOS::Task::ExitCriticalSection(mask);
        *ICSR_REG = NVIC_PENDSV_BIT;
        __ISB();
    }

    setInterruptMask(mask);
}

static void dummyTask(void)
{
    volatile uint32_t ulDummy = 0UL;
    CRTOS::Task::Delete();

    getInterruptMask();

    while (ulDummy == 0)
    {
        ;
    }
}

uint32_t *initStack(volatile uint32_t *stackTop, volatile uint32_t *stackEnd, TaskFunction code, void *args)
{
    *(--stackTop) = (uint32_t)0x01000000lu; // xPSR
    *(--stackTop) = (uint32_t)code;         // PC
    *(--stackTop) = (uint32_t)dummyTask;    // LR
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R12
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R3
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R2
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R1
    *(--stackTop) = (uint32_t)args;         // R0
    *(--stackTop) = (uint32_t)0xFFFFFFFDul; // EXC_RETURN
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R11
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R10
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R09
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R08
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R07
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R06
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R05
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R04

    return ((uint32_t *)stackTop);
}

void TimerISR(void *)
{
    while (1)
    {
        Node<CRTOS::Timer::SoftwareTimer> *tmp = sTimerList;
        while (tmp != nullptr)
        {
            if (tmp->data->isActive)
            {
                tmp->data->elapsedTicks++;
                if (tmp->data->elapsedTicks >= tmp->data->timeoutTicks)
                {
                    tmp->data->callback(tmp->data->callbackArgs);
                    if (tmp->data->autoReload == true)
                    {
                        tmp->data->elapsedTicks = 0u;
                    }
                    else
                    {
                        tmp->data->elapsedTicks = 0u;
                        tmp->data->isActive = false;
                    }
                }
            }
            tmp = tmp->next;
        }
        CRTOS::Task::Delay(1u);
    }
}

void idleTask(void *)
{
    for (;;)
    {
        if (isPendingTask() == true)
        {
            *ICSR_REG = NVIC_PENDSV_BIT;
            __ISB();
        }
    }
}

CRTOS::Result CRTOS::Scheduler::Start(void)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    void *pool = nullptr;
    uint32_t poolSize = 0u;

    do
    {
        mem.getMemoryPool(&pool, poolSize);

        if ((pool == nullptr) || (poolSize == 0u))
        {
            result = CRTOS::Result::RESULT_MEMORY_NOT_INITIALIZED;
            return result;
        }

        *NVIC_SHPR3_REG |= NVIC_PENDSV_PRIO;
        *NVIC_SHPR3_REG |= NVIC_SYSTICK_PRIO;

        getInterruptMask();

        SysTick->CTRL = 0ul;
        SysTick->VAL = 0ul;

        result = CRTOS::Task::Create(TimerISR, "TimerSVC", 256, nullptr, MAX_TASK_PRIORITY - 1u, nullptr);
        if (result != CRTOS::Result::RESULT_SUCCESS)
        {
            continue;
        }

        result = CRTOS::Task::Create(idleTask, "IDLE", 64, nullptr, 0u, &idleTaskHandle);
        if (result != CRTOS::Result::RESULT_SUCCESS)
        {
            continue;
        }

        sCurrentTCB = readyTaskList->data;

        Node<TaskControlBlock> *temp = readyTaskList;
        while (temp != nullptr)
        {
            temp->data->executionTime = 0;
            temp->data->enterCycles = 0;
            temp->data->exitCycles = 0;
            temp = temp->next;
        }

        SysTick->LOAD = (sCoreClock / sTickRate) - 1ul;
        SysTick->VAL = 0u;
        SysTick->CTRL = SysTick_CTRL_CLKSOURCE | SysTick_CTRL_TICKINT | SysTick_CTRL_ENABLE;

        startFirstTask();
    } while (0);

    return result;
}

CRTOS::Result CRTOS::CRC32::Init(void)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;

    if (sCrcTable == nullptr)
    {
        sCrcTable = reinterpret_cast<uint32_t *>(mem.allocate(sCrcTableSize * sizeof(uint32_t)));
    }
    else
    {
        result = CRTOS::Result::RESULT_CRC_ALREADY_INITIALIZED;
        return result;
    }

    if (sCrcTable == nullptr)
    {
        result = CRTOS::Result::RESULT_NO_MEMORY;
    }
    else
    {
        for (uint32_t i = 0; i < 256; i++)
        {
            uint32_t crc = i;
            for (uint32_t j = 0; j < 8; j++)
            {
                if (crc & 1)
                {
                    crc = (crc >> 1) ^ sPolynomial;
                }
                else
                {
                    crc >>= 1;
                }
            }
            sCrcTable[i] = crc;
        }
    }

    return result;
}

CRTOS::Result CRTOS::CRC32::Calculate(const uint8_t *data, uint32_t length, uint32_t &output, uint32_t crc)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;

    if (data == nullptr)
    {
        result = CRTOS::Result::RESULT_BAD_PARAMETER;
        return result;
    }

    if (sCrcTable == nullptr)
    {
        result = CRTOS::Result::RESULT_CRC_NOT_INITIALIZED;
        return result;
    }

    for (uint32_t i = 0; i < length; i++)
    {
        crc = (crc >> 8) ^ sCrcTable[(crc ^ data[i]) & 0xFF];
    }

    output = crc;
    output = output ^ 0xFFFFFFFF;

    return result;
}

CRTOS::Result CRTOS::CRC32::Deinit(void)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;

    if (sCrcTable == nullptr)
    {
        result = CRTOS::Result::RESULT_CRC_NOT_INITIALIZED;
    }
    else
    {
        mem.deallocate(sCrcTable);
    }

    return result;
}
