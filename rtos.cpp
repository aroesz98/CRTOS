/*
 * rtos.cpp
 *
 *  Created on: 12 gru 2024
 *      Author: Administrator
 */
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <list>
#include <algorithm>

#include <rtos.hpp>

typedef void (*TaskFunction)(void*);

typedef struct TaskControlBlock
{
    volatile uint32_t *stackTop;
    volatile uint32_t *stack;
    TaskFunction function;
    void *function_args;
    char name[20u];
    int priority;
} TaskControlBlock;

typedef struct
{
    volatile uint32_t CTRL;
    volatile uint32_t LOAD;
    volatile uint32_t VAL;
} SysTick_Type;

extern uint32_t SystemCoreClock;

__attribute__((used )) volatile TaskControlBlock *sCurrentTCB = NULL;

#define NVIC_MIN_PRIO                     (0xFFul)
#define NVIC_PENDSV_PRIO                  (NVIC_MIN_PRIO << 16u)
#define NVIC_SYSTICK_PRIO                 (NVIC_MIN_PRIO << 24u)
#define MAX_SYSCALL_INTERRUPT_PRIORITY    (2ul << 5u)
#define NVIC_PENDSV_BIT                   (1ul << 28u)

#define NVIC_SHPR3_REG  ((volatile uint32_t *)0xE000ED20ul)
#define ICSR_REG        ((volatile uint32_t *)0xE000ED04ul)
#define SYSTICK_REG     ((volatile uint32_t *)0xE000E010ul)
#define SysTick         ((SysTick_Type*)SYSTICK_REG)

#define SysTick_CTRL_CLKSOURCE_Msk  (1ul << 2u)
#define SysTick_CTRL_TICKINT_Msk    (1ul << 1u)
#define SysTick_CTRL_ENABLE_Msk     (1ul)

extern "C" void SVC_Handler(void) __attribute__ (( naked ));
extern "C" void PendSV_Handler(void) __attribute__ (( naked ));
extern "C" void SysTick_Handler(void);

extern "C" void RestoreCtxOfTheFirstTask(void) __attribute__((naked ));
extern "C" uint32_t getInterruptMask(void) __attribute__((naked ));
extern "C" void setInterruptMask(uint32_t ulMask) __attribute__((naked ));
extern "C" void startFirstTask(void) __attribute__((naked ));

static std::atomic<uint32_t> tickCount(0u);
static std::list<volatile TaskControlBlock*> tcbList;

static uint32_t MAX_TASK_PRIORITY = 10u;

//
RTOS::Semaphore TmrSvc_Smphr(0u);
static std::list<RTOS::Timer::SoftwareTimer*> sTimerList;

RTOS::Semaphore::Semaphore(uint32_t initialValue) : value(initialValue) {}

void RTOS::Semaphore::wait(uint32_t timeoutTicks)
{
    uint32_t startTick = tickCount.load();

    while (true)
    {
        uint32_t oldValue = value.load();
        if (oldValue > 0u && value.compare_exchange_strong(oldValue, oldValue - 1u))
        {
            return;
        }

        uint32_t currentTick = tickCount.load();
        if ((currentTick - startTick) >= timeoutTicks)
        {
            return;
        }
    }
}

void RTOS::Semaphore::signal()
{
    value++;
}

void RTOS::Semaphore::destroy()
{
    value.store(0u);
}

void RTOS::Timer::Init(SoftwareTimer *timer, uint32_t timeoutTicks, void (*callback)(void*), void *callbackArgs, bool autoReload)
{
    timer->timeoutTicks = timeoutTicks;
    timer->elapsedTicks = 0u;
    timer->isActive = false;
    timer->callback = callback;
    timer->callbackArgs = callbackArgs;
    timer->autoReload = autoReload;
    sTimerList.push_back(timer);
}

void RTOS::Timer::Start(SoftwareTimer *timer)
{
    timer->elapsedTicks = 0u;
    timer->isActive = true;
}

void RTOS::Timer::Stop(SoftwareTimer *timer)
{
    timer->isActive = false;
    timer->elapsedTicks = 0u;
}

void RTOS::Timer::Reset(SoftwareTimer *timer)
{
    timer->elapsedTicks = 0u;
}

uint32_t getInterruptMask(void)
{
    __asm volatile
    (
        ".syntax unified \n"
        "mrs r0, basepri \n"
        "mov r1, %0      \n"
        "msr basepri, r1 \n"
        "dsb             \n"
        "isb             \n"
        "bx lr           \n"
        ".align 4        \n"
        ::"i" ( MAX_SYSCALL_INTERRUPT_PRIORITY ) : "memory"
    );
}

void setInterruptMask(uint32_t mask)
{
    __asm volatile
    (
        ".syntax unified \n"
        "msr basepri, r0 \n"
        "dsb             \n"
        "isb             \n"
        "bx lr           \n"
        ".align 4        \n"
        ::: "memory"
    );
}

void RestoreCtxOfTheFirstTask(void)
{
    __asm volatile
    (
        ".syntax unified                       \n"
        // Read top of the stack
        "ldr  r2, currentCtxTCB                \n"
        "ldr  r1, [r2]                         \n"
        "ldr  r0, [r1]                         \n"
        // R1 = PSPLIM || R2 = EXC_RETURN
        "ldm  r0!, {r1-r2}                     \n"
        // Set current task PSPLIM
        "msr  psplim, r1                       \n"
        // Switch to PSP - thread mode
        "movs r1, #2                           \n"
        "msr  CONTROL, r1                      \n"
        // Discard R4-R11
        "adds r0, #32                          \n"
        // Update current PSP
        "msr  psp, r0                          \n"
        "isb                                   \n"
        "mov  r0, #0                           \n"
        // Enable interrupts and exit
        "msr  basepri, r0                      \n"
        "bx   r2                               \n"
        ".align 4                              \n"
        "currentCtxTCB: .word sCurrentTCB      \n"
    );
}

void SVC_Handler(void)
{
    __asm volatile
    (
        ".syntax unified                              \n"
        "tst lr, #4                                   \n"
        "ite eq                                       \n"
        "mrseq r0, msp                                \n"
        "mrsne r0, psp                                \n"
        "ldr r1, SVC_ISR_ADDR                         \n"
        "bx r1                                        \n"
        ".align 4                                     \n"
        "SVC_ISR_ADDR: .word RestoreCtxOfTheFirstTask \n"
    );
}

void PendSV_Handler(void)
{
    __asm volatile
    (
        ".syntax unified     \n"
        // Load PSP to R0, PSPLIM to R2, LR to r3
        "mrs r0, psp         \n"
        "mrs r2, psplim      \n"
        "mov r3, lr          \n"
        // Save r2-r11 under PSP location
        "stmdb r0!, {r2-r11} \n"
        // Save new PSP
        "ldr r2, currentTCB  \n"
        "ldr r1, [r2]        \n"
        "str r0, [r1]        \n"
        "mov r0, %0          \n"
        "msr basepri, r0     \n"
        "dsb                 \n"
        "isb                 \n"
        // Switch to next TCN
        "bl switchCtx        \n"
        "mov r0, #0          \n"
        "msr basepri, r0     \n"
        // Load PSP address of next task to R0
        "ldr r2, currentTCB  \n"
        "ldr r1, [r2]        \n"
        "ldr r0, [r1]        \n"
        // Restore context of next task
        "ldmia r0!, {r2-r11} \n"
        // Restore PSPLIM and set new PSP
        "msr psplim, r2      \n"
        "msr psp, r0         \n"
        // Leave interrupt
        "bx r3               \n"
        ".align 4            \n"
        "currentTCB: .word sCurrentTCB \n"
        ::"i" ( MAX_SYSCALL_INTERRUPT_PRIORITY )
    );
}

void startFirstTask(void)
{
    __asm volatile
    (
        ".syntax unified \n"
        "cpsie i         \n"
        "cpsie f         \n"
        "dsb             \n"
        "isb             \n"
        "svc 7           \n"
        "nop             \n"
        ".align 4        \n"
        "xVTORConst: .word 0xe000ed08 \n"
    );
}

__attribute__((always_inline)) static inline void __ISB(void)
{
    __asm volatile ("isb 0xF":::"memory");
}

__attribute__((always_inline)) static inline void __DSB(void)
{
    __asm volatile ("dsb 0xF":::"memory");
}

extern "C" void SVC_ISR(uint32_t *svcArgs)
{
    // https://developer.arm.com/documentation/ka004005/latest/
    unsigned int svc_number;
    svc_number = ((char*)svcArgs[6])[-2];

    switch (svc_number)
    {
        case 7:
            RestoreCtxOfTheFirstTask();
            break;

        default:
            assert(true);
    }
}

extern "C" void switchCtx()
{
    if (tcbList.empty())
    {
        return;
    }

    auto it = std::find(tcbList.begin(), tcbList.end(), sCurrentTCB);
    if (it != tcbList.end() && ++it != tcbList.end())
    {
        sCurrentTCB = *it;
    }
    else
    {
        sCurrentTCB = tcbList.front();
    }
}

void SysTick_Handler(void)
{
    uint32_t prevMask = getInterruptMask();

    tickCount++;

    TmrSvc_Smphr.signal();

    *ICSR_REG = NVIC_PENDSV_BIT;

    setInterruptMask(prevMask);
}

static void dummyTask(void)
{
    volatile uint32_t ulDummy = 0UL;

    getInterruptMask();

    while (ulDummy == 0)
    {
        ;
    }
}

uint32_t* initStack(volatile uint32_t *stackTop, volatile uint32_t *stackEnd, TaskFunction code, void *args)
{
    *(--stackTop) = (uint32_t)0x01000000lu; // xPSR
    *(--stackTop) = (uint32_t)code;         // PC
    *(--stackTop) = (uint32_t)dummyTask;    // LR
    *(--stackTop) = (uint32_t)0xDEADC0DEul; // R12
    *(--stackTop) = (uint32_t)0xFACEFEEDul; // R3
    *(--stackTop) = (uint32_t)0xF00DBABEul; // R2
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R1
    *(--stackTop) = (uint32_t)args;         // R0
    *(--stackTop) = (uint32_t)0xCAFEBEEFul; // R11
    *(--stackTop) = (uint32_t)0xDEADFEEDul; // R10
    *(--stackTop) = (uint32_t)0xBADDCAFEul; // R09
    *(--stackTop) = (uint32_t)0xCAFEBABEul; // R08
    *(--stackTop) = (uint32_t)0xBAAAAAADul; // R07
    *(--stackTop) = (uint32_t)0x00FACADEul; // R06
    *(--stackTop) = (uint32_t)0xBEEFCACEul; // R05
    *(--stackTop) = (uint32_t)0x00DEC0DEul; // R04
    *(--stackTop) = (uint32_t)0xFFFFFFFDul; // EXC_RETURN
    *(--stackTop) = (uint32_t)stackEnd;     // PSPLIM

    return ((uint32_t*)stackTop);
}

void TimerISR(void *)
{
    while(1)
    {
        for (auto timer : sTimerList)
        {
            if (timer->isActive)
            {
                timer->elapsedTicks++;
                if (timer->elapsedTicks >= timer->timeoutTicks)
                {
                    timer->callback(timer->callbackArgs);
                    if (timer->autoReload == true)
                    {
                        timer->elapsedTicks = 0u;
                    }
                    else
                    {
                        timer->elapsedTicks = 0u;
                        timer->isActive = false;
                    }
                }
            }
        }
        TmrSvc_Smphr.wait(10u);
    }
}

void RTOS::Scheduler::Start(void)
{
    *NVIC_SHPR3_REG |= NVIC_PENDSV_PRIO;
    *NVIC_SHPR3_REG |= NVIC_SYSTICK_PRIO;

    getInterruptMask();

    SysTick->CTRL = 0ul;
    SysTick->VAL = 0ul;

    RTOS::Task::Create(TimerISR,  "TimerSVC", 400, NULL, MAX_TASK_PRIORITY - 2u, NULL);

    sCurrentTCB = tcbList.front();

    SysTick->LOAD = 150000 - 1ul;
    SysTick->VAL = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;

    startFirstTask();

    while (1);
}

RTOS::TaskResult RTOS::Task::Create(TaskFunction function,  const char * const name, uint32_t stackDepth, void *args, uint32_t prio, TaskHandle *handle)
{
    RTOS::TaskResult result = RTOS::TaskResult::RESULT_SUCCESS;
    uint32_t prevMask = getInterruptMask();

    __DSB();
    __ISB();

    do
    {
        TaskControlBlock *tmpTCB = new TaskControlBlock;
        if (tmpTCB == nullptr)
        {
            delete tmpTCB;
            result = RTOS::TaskResult::RESULT_NO_MEMORY;
            continue;
        }

        uint32_t *tmpStack = new uint32_t[stackDepth];
        if (tmpStack == nullptr)
        {
            delete []tmpStack;
            result = RTOS::TaskResult::RESULT_NO_MEMORY;
            continue;
        }

        memset(&tmpStack[0u], 0u, sizeof(stackDepth * 4u));
        memset(&(tmpTCB->name[0u]), 0u, 20u);

        tmpTCB->stack = tmpStack;
        tmpTCB->function = function;
        tmpTCB->function_args = args;

        if (prio >= MAX_TASK_PRIORITY)
        {
            tmpTCB->priority = MAX_TASK_PRIORITY - 1u;
        }
        else
        {
            tmpTCB->priority = prio;
        }

        uint32_t nameLength = strlen(name);
        memcpy(tmpTCB->name, &name[0], nameLength < 20u ? nameLength : 20u);

        volatile uint32_t *stackTop = &(tmpTCB->stack[stackDepth - (uint32_t) 1u]);
        stackTop = (uint32_t*)(((uint32_t)stackTop) & ~7lu);

        tmpTCB->stackTop = initStack(stackTop, tmpTCB->stack, function, args);

        tcbList.push_back(tmpTCB);
        tcbList.sort([](volatile TaskControlBlock* a, volatile TaskControlBlock* b) { return a->priority > b->priority; });

        if (handle != NULL)
        {
            *handle = (TaskHandle)tmpTCB;
        }
    } while(0);

    setInterruptMask(prevMask);

    return result;
}

void RTOS::Task::Delete(void)
{
    uint32_t prevMask = getInterruptMask();
    __DSB();
    __ISB();

    auto it = std::find(tcbList.begin(), tcbList.end(), sCurrentTCB);
    tcbList.erase(it);

    delete sCurrentTCB->stack;
    delete sCurrentTCB;

    *ICSR_REG = NVIC_PENDSV_BIT;

    setInterruptMask(prevMask);
}

void RTOS::Task::Delete(TaskHandle *handle)
{
    uint32_t prevMask = getInterruptMask();
    __DSB();
    __ISB();

    TaskControlBlock *tmp = (TaskControlBlock*)(*handle);

    auto it = std::find(tcbList.begin(), tcbList.end(), tmp);
    tcbList.erase(it);

    delete tmp->stack;
    delete tmp;

    *ICSR_REG = NVIC_PENDSV_BIT;

    setInterruptMask(prevMask);
}

char* RTOS::Task::GetCurrentTaskName(void)
{
    return ((char*)(sCurrentTCB->name));
}
