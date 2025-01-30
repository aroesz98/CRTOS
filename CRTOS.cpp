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

#include <CRTOS.hpp>
#include <HeapAllocator.hpp>

typedef void (*TaskFunction)(void *);

enum class TaskState : uint32_t
{
    TASK_RUNNING,
    TASK_READY,
    TASK_DELAYED,
    TASK_BLOCKED
};

struct TaskControlBlock
{
    volatile uint32_t *stackTop;
    volatile uint32_t *stack;
    TaskFunction function;
    void *function_args;
    uint32_t priority;
    TaskState state;
    uint32_t delayUpTo;
    uint32_t stackWatermark;
    uint32_t stackSize;
    uint32_t enterCycles;
    uint32_t exitCycles;
    uint32_t executionTime;
    char name[20u];
};

typedef struct TaskControlBlock TaskControlBlock;

typedef struct
{
    volatile uint32_t CTRL;
    volatile uint32_t LOAD;
    volatile uint32_t VAL;
} SysTick_Type;

typedef struct
{
  volatile uint32_t CTRL;
  volatile uint32_t CYCCNT;
} DWT_Type;

__attribute__((used)) volatile TaskControlBlock *sCurrentTCB = nullptr;
CRTOS::Task::TaskHandle idleTaskHandle = nullptr;

constexpr uint32_t NVIC_MIN_PRIO = 0xFFul;
constexpr uint32_t NVIC_PENDSV_PRIO = NVIC_MIN_PRIO << 16u;
constexpr uint32_t NVIC_SYSTICK_PRIO = NVIC_MIN_PRIO << 24u;
constexpr uint32_t MAX_SYSCALL_INTERRUPT_PRIORITY = 2ul << 5u;
constexpr uint32_t NVIC_PENDSV_BIT = 1ul << 28u;

#define NVIC_SHPR3_REG                  ((volatile uint32_t *)0xE000ED20ul)
#define ICSR_REG                        ((volatile uint32_t *)0xE000ED04ul)
#define SYSTICK_REG                     ((volatile uint32_t *)0xE000E010ul)
#define DWT_REG                         ((volatile uint32_t *)0xE0001000ul)

#define SysTick                         ((SysTick_Type *)SYSTICK_REG)
#define DWT                             ((DWT_Type*)DWT_REG)

#define SysTick_CTRL_CLKSOURCE_Msk      (1ul << 2u)
#define SysTick_CTRL_TICKINT_Msk        (1ul << 1u)
#define SysTick_CTRL_ENABLE_Msk         (1ul)

extern "C" void SVC_Handler(void) __attribute__((naked));
extern "C" void PendSV_Handler(void) __attribute__((naked));
extern "C" void SysTick_Handler(void);

extern "C" void RestoreCtxOfTheFirstTask(void) __attribute__((naked));
extern "C" uint32_t getInterruptMask(void) __attribute__((naked));
extern "C" void setInterruptMask(uint32_t ulMask = 0u) __attribute__((naked));
extern "C" void startFirstTask(void) __attribute__((naked));

extern "C" void memcpy_optimized(void *d, void *s, uint32_t len);
extern "C" void memset_optimized(void *d, uint32_t val, uint32_t len);

static volatile uint32_t tickCount = 0u;

static uint32_t MAX_TASK_PRIORITY = 10u;
static uint32_t sTickRate = 1000u;
static uint32_t sCoreClock = 150000000u;

static uint32_t sLastTimestamp = 0;
static uint64_t sSystemTotalTime = 0u;

static HeapAllocator mem;

uint32_t GetTotalTime(void)
{
    return sSystemTotalTime;
}

uint32_t GetIdleTaskTime(void)
{
    uint32_t totalTime = 0u;

    TaskControlBlock *idle = (TaskControlBlock*)idleTaskHandle;
    totalTime = idle->executionTime;

    return totalTime;
}

template <typename T>
class Node
{
public:
    T *data;
    Node *next;
    Node *prev;

    Node(T *data)
    {
        this->data = data;
        this->next = nullptr;
        this->prev = nullptr;
    }
};

template <typename T>
void ListInsertAtBeginning(Node<T> *&head, T *data)
{
    Node<T> *newNode = reinterpret_cast<Node<T> *>(mem.allocate(sizeof(Node<T>)));
    memset_optimized(newNode, 0, sizeof(Node<T>));
    newNode->data = data;

    if (head == nullptr)
    {
        head = newNode;
        return;
    }

    newNode->next = head;
    head->prev = newNode;
    head = newNode;
}

template <typename T>
void ListInsertAtEnd(Node<T> *&head, T *data)
{
    Node<T> *newNode = reinterpret_cast<Node<T> *>(mem.allocate(sizeof(Node<T>)));
    memset_optimized(newNode, 0, sizeof(Node<T>));
    newNode->data = data;

    if (head == nullptr)
    {
        head = newNode;
        return;
    }

    Node<T> *temp = head;
    while (temp->next != nullptr)
    {
        temp = temp->next;
    }

    temp->next = newNode;
    newNode->prev = temp;
}

template <typename T>
void ListInsertAtPosition(Node<T> *&head, T *data, uint32_t position)
{
    if (position < 0)
    {
        // Position can't be less than 0
        return;
    }

    if (position == 0)
    {
        insertAtBeginning(head, data);
        return;
    }

    Node<T> *newNode = reinterpret_cast<Node<T> *>(mem.allocate(sizeof(Node<T>)));
    memset_optimized(newNode, 0, sizeof(Node<T>));
    newNode->data = data;

    Node<T> *temp = head;

    for (int i = 0; temp != nullptr && i < position; i++)
    {
        temp = temp->next;
    }

    if (temp == nullptr)
    {
        // Wrong position number
        return;
    }

    newNode->next = temp->next;
    newNode->prev = temp;
    if (temp->next != nullptr)
    {
        temp->next->prev = newNode;
    }
    temp->next = newNode;
}

template <typename T>
void ListDeleteAtBeginning(Node<T> *&head)
{
    if (head == nullptr)
    {
        // Empty list
        return;
    }

    Node<T> *temp = head;
    head = head->next;
    if (head != nullptr)
    {
        head->prev = nullptr;
    }
    mem.deallocate(temp);
}

template <typename T>
void ListDeleteAtEnd(Node<T> *&head)
{
    if (head == nullptr)
    {
        // List empty
        return;
    }

    Node<T> *temp = head;
    if (temp->next == nullptr)
    {
        head = nullptr;
        mem.deallocate(temp);
        return;
    }

    while (temp->next != nullptr)
    {
        temp = temp->next;
    }

    temp->prev->next = nullptr;
    mem.deallocate(temp);
}

template <typename T>
void ListDeleteAtPosition(Node<T> *&head, uint32_t position)
{
    if (head == nullptr)
    {
        // List empty
        return;
    }

    if (position == 0)
    {
        ListDeleteAtBeginning(head);
        return;
    }

    Node<T> *temp = head;
    for (uint32_t i = 0; temp != nullptr && i < position; i++)
    {
        temp = temp->next;
    }

    if (temp == nullptr)
    {
        // Wrong position
        return;
    }

    if (temp->next != nullptr)
    {
        temp->next->prev = temp->prev;
    }
    if (temp->prev != nullptr)
    {
        temp->prev->next = temp->next;
    }
    mem.deallocate(temp);
}

template <typename T>
void SortListByPriority(Node<T> *&head)
{
    if (head == nullptr)
        return;

    bool swapped;
    Node<T> *ptr1;
    Node<T> *lptr = nullptr;

    // Bubble sort
    do
    {
        swapped = false;
        ptr1 = head;

        while (ptr1->next != lptr)
        {
            if (ptr1->data->priority < ptr1->next->data->priority)
            {
                TaskControlBlock *temp = ptr1->data;
                ptr1->data = ptr1->next->data;
                ptr1->next->data = temp;
                swapped = true;
            }
            ptr1 = ptr1->next;
        }
        lptr = ptr1;
    } while (swapped);
}

static Node<TaskControlBlock> *readyTaskList = nullptr;
static Node<CRTOS::Timer::SoftwareTimer> *sTimerList = nullptr;
static Node<CRTOS::Semaphore> *waitingSemaphoresList = nullptr;
static CRTOS::IPC::TaskMessageQueue* sTaskMessageQueues = nullptr;

uint32_t pStringLength(const char *buffer)
{
    const char *tmp = buffer;

    while (*tmp != 0)
    {
        tmp++;
    }

    return (tmp - buffer) + 1;
}

CRTOS::Mutex::Mutex(void) : flag(ATOMIC_FLAG_INIT)
{
}

CRTOS::Mutex::~Mutex(void)
{
}

void CRTOS::Mutex::Lock(void)
{
    getInterruptMask();

    while (flag.test_and_set(std::memory_order_acquire));
}

void CRTOS::Mutex::Unlock(void)
{
    flag.clear(std::memory_order_release);

    setInterruptMask();
}

CRTOS::Semaphore::Semaphore(uint32_t initialValue) :
    value(initialValue)
{
}

CRTOS::Result CRTOS::Semaphore::Wait(uint32_t timeoutTicks)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    uint32_t startTick = tickCount;

    if (timeoutTicks == 0)
    {
        result = CRTOS::Result::RESULT_BAD_PARAMETER;
        return result;
    }

    mTimeout = startTick + timeoutTicks;
    mOwner = (void*)sCurrentTCB;
    ListInsertAtEnd(waitingSemaphoresList, this);

    sCurrentTCB->state = TaskState::TASK_BLOCKED;

    while (true)
    {
        uint32_t oldValue = value.load();
        if (oldValue > 0u && value.compare_exchange_strong(oldValue, oldValue - 1u))
        {
            mTimeout = 0;
            mOwner = nullptr;
            sCurrentTCB->state = TaskState::TASK_RUNNING;

            Node<CRTOS::Semaphore>* node = waitingSemaphoresList;
            uint32_t pos = 0;
            while (node != nullptr)
            {
                if (node->data == this)
                {
                    ListDeleteAtPosition(waitingSemaphoresList, pos);
                    break;
                }
                node = node->next;
                pos++;
            }
            break;
        }

        uint32_t currentTick = tickCount;
        if (currentTick >= mTimeout)
        {
            sCurrentTCB->state = TaskState::TASK_READY;
            mTimeout = 0;

            Node<CRTOS::Semaphore>* node = waitingSemaphoresList;
            uint32_t pos = 0;
            while (node != nullptr)
            {
                if (node->data == this)
                {
                    ListDeleteAtPosition(waitingSemaphoresList, pos);
                    break;
                }
                node = node->next;
                pos++;
            }
            return CRTOS::Result::RESULT_SEMAPHORE_TIMEOUT;
        }
        else
        {
            uint32_t prevMask = getInterruptMask();
            *ICSR_REG = NVIC_PENDSV_BIT;
            setInterruptMask(prevMask);
        }
    }

    return result;
}

void CRTOS::Semaphore::Signal(void)
{
    value++;

    if (mOwner != nullptr)
    {
        ((TaskControlBlock*)mOwner)->state = TaskState::TASK_READY;
        mOwner = nullptr;
    }
}

CRTOS::Result CRTOS::Semaphore::GetOwner(void *&owner)
{
    if (mOwner == nullptr)
    {
        return CRTOS::Result::RESULT_SEMAPHORE_NO_OWNER;
    }

    owner = mOwner;

    return CRTOS::Result::RESULT_SUCCESS;
}

CRTOS::Result CRTOS::Semaphore::GetTimeout(uint32_t *&timeout)
{
    timeout = &mTimeout;
    return CRTOS::Result::RESULT_SUCCESS;
}

CRTOS::Result CRTOS::Config::InitMem(void *pool, uint32_t size)
{
    if (pool == nullptr || size == 0u)
    {
        return CRTOS::Result::RESULT_NO_MEMORY;
    }

    mem.init(pool, size);
    sTaskMessageQueues = nullptr;

    return CRTOS::Result::RESULT_SUCCESS;
}

uint32_t CRTOS::Config::GetAllocatedMemory(void)
{
    return mem.getAllocatedMemory();
}

uint32_t CRTOS::Config::GetFreeMemory(void)
{
    return mem.getFreeMemory();
}

CRTOS::Result CRTOS::Timer::Init(SoftwareTimer *timer, uint32_t timeoutTicks, void (*callback)(void *), void *callbackArgs, bool autoReload)
{
    if (timer == nullptr || callback == nullptr)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    timer->timeoutTicks = timeoutTicks;
    timer->elapsedTicks = 0u;
    timer->isActive = false;
    timer->callback = callback;
    timer->callbackArgs = callbackArgs;
    timer->autoReload = autoReload;

    ListInsertAtBeginning(sTimerList, timer);

    return CRTOS::Result::RESULT_SUCCESS;
}

CRTOS::Result CRTOS::Timer::Start(SoftwareTimer *timer)
{
    if (timer == nullptr)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }
    if (timer->isActive == true)
    {
        return CRTOS::Result::RESULT_TIMER_ALREADY_ACTIVE;
    }

    timer->elapsedTicks = 0u;
    timer->isActive = true;

    return CRTOS::Result::RESULT_SUCCESS;
}

CRTOS::Result CRTOS::Timer::Stop(SoftwareTimer *timer)
{
    if (timer == nullptr)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }
    if (timer->isActive == true)
    {
        return CRTOS::Result::RESULT_TIMER_ALREADY_STOPPED;
    }

    timer->isActive = false;
    timer->elapsedTicks = 0u;

    return CRTOS::Result::RESULT_SUCCESS;
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

uint32_t getInterruptMask(void)
{
    __asm volatile(
        ".syntax unified \n"
        "mrs r0, basepri \n"
        "mov r1, %0      \n"
        "msr basepri, r1 \n"
        "dsb             \n"
        "isb             \n"
        "bx lr           \n"
        ".align 4        \n" ::"i"(MAX_SYSCALL_INTERRUPT_PRIORITY) : "memory");
}

void setInterruptMask(uint32_t mask)
{
    __asm volatile(
        ".syntax unified \n"
        "msr basepri, r0 \n"
        "dsb             \n"
        "isb             \n"
        "bx lr           \n"
        ".align 4        \n" ::: "memory");
}

void CRTOS::Task::EnterCriticalSection(void)
{
    getInterruptMask();

    __asm volatile (
        "cpsid i \n"
        "cpsid f \n");

    __asm volatile ( "dsb" ::: "memory" );
    __asm volatile ( "isb" );
}

void CRTOS::Task::ExitCriticalSection(void)
{
    __asm volatile (
        "cpsie i \n"
        "cpsie f \n");

    __asm volatile ( "dsb" ::: "memory" );
    __asm volatile ( "isb" );

    setInterruptMask();
}

void RestoreCtxOfTheFirstTask(void)
{
    sCurrentTCB->enterCycles = DWT->CYCCNT;
    sLastTimestamp = DWT->CYCCNT;
    __asm volatile (
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
        "ldm   r0!, {r4-r11}                   \n"
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
    __asm volatile (
        ".syntax unified                              \n"
        "tst lr, #4                                   \n"
        "ite eq                                       \n"
        "mrseq r0, msp                                \n"
        "mrsne r0, psp                                \n"
        "ldr r1, SVC_ISR_ADDR                         \n"
        "bx r1                                        \n"
        ".align 4                                     \n"
        "SVC_ISR_ADDR:                                \n"
        "\t.word RestoreCtxOfTheFirstTask             \n"
    );
}

extern "C" void PendSV_Handler(void)
{
    __asm volatile (
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
        "ldmia r0!, {r2-r11} \n"
        // Restore PSPLIM and set new PSP
        "msr psplim, r2      \n"
        "msr psp, r0         \n"
        // Leave interrupt
        "bx r3               \n"
        ".align 4            \n"
        "currentTCB: .word sCurrentTCB \n" ::"i"(MAX_SYSCALL_INTERRUPT_PRIORITY)
    );
}

void startFirstTask(void)
{
    __asm volatile(
        ".syntax unified \n"
        "cpsie i         \n"
        "cpsie f         \n"
        "dsb             \n"
        "isb             \n"
        "svc 7           \n"
        "nop             \n"
        ".align 4        \n"
    );
}

__attribute__((always_inline)) static inline void __ISB(void)
{
    __asm volatile("isb 0xF" ::: "memory");
}

__attribute__((always_inline)) static inline void __DSB(void)
{
    __asm volatile("dsb 0xF" ::: "memory");
}

extern "C" char *currentTaskName(void)
{
    return (char*)(&(sCurrentTCB->name[0]));
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

        sCurrentTCB->stackWatermark = usedStack;

        if (sCurrentTCB->state == TaskState::TASK_RUNNING)
        {
            sCurrentTCB->state = TaskState::TASK_READY;
        }
    }

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
            if (highestPriorityTask == nullptr || temp->data->priority > highestPriorityTask->data->priority)
            {
                highestPriorityTask = temp;
            }
        }

        temp = temp->next;
    }

    if (highestPriorityTask != nullptr)
    {
        sCurrentTCB = highestPriorityTask->data;
        sCurrentTCB->state = TaskState::TASK_RUNNING;
    }
    else
    {
        sCurrentTCB = (TaskControlBlock *)(idleTaskHandle);
        sCurrentTCB->state = TaskState::TASK_RUNNING;
    }

    updateEnterCycles();
}

void SysTick_Handler(void)
{
    uint32_t prevMask = getInterruptMask();
    uint32_t now = DWT->CYCCNT;
    uint32_t diff;

    if (sLastTimestamp > now)
    {
        sSystemTotalTime = 0;
        sLastTimestamp = 0;

        Node<TaskControlBlock> *temp = readyTaskList;
        while (temp != nullptr)
        {
            temp->data->executionTime = 0;
            temp->data->enterCycles = 0;
            temp->data->exitCycles = 0;
            temp = temp->next;
        }
    }
    else
    {
        diff = now - sLastTimestamp;
        sSystemTotalTime += diff;
        sLastTimestamp = now;
    }

    tickCount++;

    Node<CRTOS::Semaphore>* node = waitingSemaphoresList;
    uint32_t nodePos = 0u;
    while (node != nullptr)
    {
        uint32_t *smphrTimeout = nullptr;
        void *smphrOwner = nullptr;
        node->data->GetTimeout(smphrTimeout);
        node->data->GetOwner(smphrOwner);

        if (tickCount >= (*smphrTimeout))
        {
            ((TaskControlBlock*)smphrOwner)->state = TaskState::TASK_READY;
            *smphrTimeout = 0u;
            ListDeleteAtPosition(waitingSemaphoresList, nodePos);
        }
        node = node->next;
        nodePos++;
    }

    *ICSR_REG = NVIC_PENDSV_BIT;
    setInterruptMask(prevMask);
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
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R11
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R10
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R09
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R08
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R07
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R06
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R05
    *(--stackTop) = (uint32_t)0xFEEDC0DEul; // R04
    *(--stackTop) = (uint32_t)0xFFFFFFFDul; // EXC_RETURN
    *(--stackTop) = (uint32_t)stackEnd;     // PSPLIM

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
    for(;;)
    {
        __asm volatile("wfi");
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

        result = CRTOS::Task::Create(TimerISR, "TimerSVC", 256, nullptr, MAX_TASK_PRIORITY - 2u, nullptr);
        if (result != CRTOS::Result::RESULT_SUCCESS)
        {
            continue;
        }

        result = CRTOS::Task::Create(idleTask, "IDLE", 128, nullptr, 0u, &idleTaskHandle);
        if (result != CRTOS::Result::RESULT_SUCCESS)
        {
            continue;
        }

        SortListByPriority(readyTaskList);

        sCurrentTCB = readyTaskList->data;

        SysTick->LOAD = (sCoreClock / sTickRate) - 1ul;
        SysTick->VAL = 0u;
        SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;

        startFirstTask();
    } while (0);

    return result;
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
        TaskControlBlock *tmpTCB = reinterpret_cast<TaskControlBlock*>(mem.allocate(sizeof(TaskControlBlock)));
        if (tmpTCB == nullptr)
        {
            result = CRTOS::Result::RESULT_NO_MEMORY;
            continue;
        }

        uint32_t *tmpStack = reinterpret_cast<uint32_t*>(mem.allocate(stackDepth * sizeof(uint32_t)));
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
        memset_optimized(&(tmpTCB->name[0u]), 0u, 20u);

        tmpTCB->stack = &tmpStack[0u];
        tmpTCB->stackSize = stackDepth;
        tmpTCB->function = function;
        tmpTCB->function_args = args;
        tmpTCB->enterCycles = 0u;
        tmpTCB->exitCycles = 0u;

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
        memcpy_optimized(&tmpTCB->name[0], (char*)&name[0u], nameLength < 20u ? nameLength : 20u);

        volatile uint32_t *stackTop = &(tmpTCB->stack[stackDepth - 1u]);
        stackTop = (uint32_t *)(((uint32_t)stackTop) & ~7u);

        tmpTCB->stackTop = initStack(stackTop, tmpTCB->stack, function, args);
        tmpTCB->stackWatermark = stackDepth;

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

        mem.deallocate((void*)sCurrentTCB->stack);
        mem.deallocate((void*)sCurrentTCB);
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

        mem.deallocate((void*)tmpHandle->stack);
        mem.deallocate((void*)tmpHandle);
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

    *ICSR_REG = NVIC_PENDSV_BIT;

    setInterruptMask(prevMask);

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

    task->state = TaskState::TASK_BLOCKED;

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

    if (task->state == TaskState::TASK_BLOCKED)
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

uint32_t CRTOS::Task::GetTaskCycles(void)
{
    return sCurrentTCB->executionTime;
}

uint32_t CRTOS::Task::GetFreeStack(void)
{
    TaskControlBlock *tcb = (TaskControlBlock *)sCurrentTCB;
    return (tcb->stackSize - tcb->stackWatermark);
}

void CRTOS::Task::GetCoreLoad(uint32_t &load, uint32_t &mantissa)
{
    uint64_t idle = GetIdleTaskTime();
    uint64_t total = GetTotalTime() / 100u;
    uint32_t cload = idle / total;
    uint32_t mod = total / 1000;
    mod = (idle / mod);
    mod -= (cload * 1000);

    load = 100u - cload;
    mantissa = mod;
}

CRTOS::Queue::Queue(uint32_t maxsize, uint32_t element_size)
    : mFront(0u), mRear(0u), mSize(0u), mMaxSize(maxsize), mElementSize(element_size)
{
    mQueue = reinterpret_cast<uint8_t*>(mem.allocate(maxsize * element_size));
}

CRTOS::Queue::~Queue(void)
{
    mem.deallocate(mQueue);
    mQueue = nullptr;
}

CRTOS::Result CRTOS::Queue::Send(void *item, uint32_t timeout)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    uint32_t start_time = tickCount;

    if (mQueue == nullptr)
    {
        result = CRTOS::Result::RESULT_NO_MEMORY;
        return result;
    }

    if (item == nullptr)
    {
        result = CRTOS::Result::RESULT_BAD_PARAMETER;
        return result;
    }

    while (mSize == mMaxSize)
    {
        if (tickCount - start_time >= timeout)
        {
            result = CRTOS::Result::RESULT_QUEUE_TIMEOUT;
            return result;
        }
        else
        {
            uint32_t prevMask = getInterruptMask();
            *ICSR_REG = NVIC_PENDSV_BIT;
            setInterruptMask(prevMask);
        }
    }

    memcpy_optimized(mQueue + (mRear * mElementSize), item, mElementSize);
    mRear = (mRear + 1) % mMaxSize;
    mSize++;

    return result;
}

CRTOS::Result CRTOS::Queue::Receive(void *item, uint32_t timeout)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    uint32_t start_time = tickCount;

    if (mQueue == nullptr)
    {
        result = CRTOS::Result::RESULT_NO_MEMORY;
        return result;
    }

    if (item == nullptr)
    {
        result = CRTOS::Result::RESULT_BAD_PARAMETER;
        return result;
    }

    while (mSize == 0)
    {
        if (tickCount - start_time >= timeout)
        {
            result = CRTOS::Result::RESULT_QUEUE_TIMEOUT;
            return result;
        }
        else
        {
            uint32_t prevMask = getInterruptMask();
            *ICSR_REG = NVIC_PENDSV_BIT;
            setInterruptMask(prevMask);
        }
    }

    memcpy_optimized(item, mQueue + (mFront * mElementSize), mElementSize);
    mFront = (mFront + 1) % mMaxSize;
    mSize--;

    return result;
}

CRTOS::CircularBuffer::CircularBuffer(uint32_t mBuffer_size)
    : mHead(0u), mTail(0u), mCurrentSize(0u), mBufferSize(mBuffer_size)
{
}

CRTOS::CircularBuffer::CircularBuffer(const CircularBuffer &old)
{
    mHead = old.mHead;
    mTail = old.mTail;
    mCurrentSize = old.mCurrentSize;
    mBufferSize = old.mBufferSize;
    mBuffer = reinterpret_cast<uint8_t*>(mem.allocate(mBufferSize));
    memcpy_optimized(&mBuffer[0], &(old.mBuffer[0]), mBufferSize);
}

CRTOS::CircularBuffer::~CircularBuffer(void)
{
    mem.deallocate(mBuffer);
}

CRTOS::Result CRTOS::CircularBuffer::Init(void)
{
    if (mBufferSize == 0)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    mBuffer = reinterpret_cast<uint8_t*>(mem.allocate(mBufferSize));

    if (mBuffer == nullptr)
    {
        return CRTOS::Result::RESULT_NO_MEMORY;
    }

    return CRTOS::Result::RESULT_SUCCESS;
}

CRTOS::Result CRTOS::CircularBuffer::Send(const uint8_t *data, uint32_t size, uint32_t timeout_ms)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    uint32_t start_time = tickCount;
    while (mCurrentSize + size > mBufferSize)
    {
        if (tickCount - start_time >= timeout_ms)
        {
            result = CRTOS::Result::RESULT_CIRCULAR_BUFFER_TIMEOUT;
            return result;
        }
        else
        {
            uint32_t prevMask = getInterruptMask();
            *ICSR_REG = NVIC_PENDSV_BIT;
            setInterruptMask(prevMask);
        }
    }

    if (mHead + size <= mBufferSize)
    {
        memcpy_optimized(&mBuffer[mHead], (void*)data, size);
    }
    else
    {
        uint32_t firstPartSize = mBufferSize - mHead;
        memcpy_optimized(&mBuffer[mHead], (void*)data, firstPartSize);
        memcpy_optimized(mBuffer, (void*)(&data[firstPartSize]), size - firstPartSize);
    }

    mHead = (mHead + size) % mBufferSize;
    mCurrentSize += size;
    return result;
}

CRTOS::Result CRTOS::CircularBuffer::Receive(uint8_t *data, uint32_t size, uint32_t timeout_ms)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    uint32_t start_time = tickCount;

    while (mCurrentSize < size)
    {
        if (tickCount - start_time >= timeout_ms)
        {
            result = CRTOS::Result::RESULT_CIRCULAR_BUFFER_TIMEOUT;
            return result;
        }
        else
        {
            uint32_t prevMask = getInterruptMask();
            *ICSR_REG = NVIC_PENDSV_BIT;
            setInterruptMask(prevMask);
        }
    }

    if (mTail + size <= mBufferSize)
    {
        memcpy_optimized(data, &mBuffer[mTail], size);
    }
    else
    {
        uint32_t firstPartSize = mBufferSize - mTail;
        memcpy_optimized(data, &mBuffer[mTail], firstPartSize);
        memcpy_optimized(&data[firstPartSize], mBuffer, size - firstPartSize);
    }

    mTail = (mTail + size) % mBufferSize;
    mCurrentSize -= size;
    return result;
}

CRTOS::IPC::TaskMessageQueue* GetMessageQueueForTask(CRTOS::Task::TaskHandle *task)
{
    CRTOS::IPC::TaskMessageQueue* current = sTaskMessageQueues;

    while (current != nullptr)
    {
        if (current->task == *task)
        {
            return current;
        }
        current = current->next;
    }

    CRTOS::IPC::TaskMessageQueue* newQueue = reinterpret_cast<CRTOS::IPC::TaskMessageQueue*>(mem.allocate(sizeof(CRTOS::IPC::TaskMessageQueue)));
    if (newQueue == nullptr)
    {
        return nullptr;
    }

    memset_optimized(newQueue, 0, sizeof(CRTOS::IPC::TaskMessageQueue));

    newQueue->task = *task;
    newQueue->messageHead = nullptr;
    newQueue->next = sTaskMessageQueues;
    sTaskMessageQueues = newQueue;

    return newQueue;
}

CRTOS::Result CRTOS::IPC::SendMessage(CRTOS::Task::TaskHandle *sender, CRTOS::Task::TaskHandle *receiver, uint32_t messageId, void* data, uint32_t dataSize)
{
    CRTOS::IPC::TaskMessageQueue* receiverQueue = GetMessageQueueForTask(receiver);
    if (receiverQueue == nullptr)
    {
        return CRTOS::Result::RESULT_NO_MEMORY;
    }

    IPCMessage* message = reinterpret_cast<IPCMessage*>(mem.allocate(sizeof(IPCMessage)));
    if (message == nullptr)
    {
        return CRTOS::Result::RESULT_NO_MEMORY;
    }
    memset_optimized(message, 0, sizeof(IPCMessage));

    message->sender = *sender;
    message->receiver = *receiver;
    message->messageId = messageId;
    message->dataSize = dataSize;
    message->next = nullptr;

    if (dataSize > 0 && data != nullptr)
    {
        message->data = mem.allocate(dataSize);
        if (message->data == nullptr)
        {
            mem.deallocate(message);
            return CRTOS::Result::RESULT_NO_MEMORY;
        }
        memcpy_optimized(message->data, data, dataSize);
    }
    else
    {
        message->data = nullptr;
    }

    if (receiverQueue->messageHead == nullptr)
    {
        receiverQueue->messageHead = message;
    }
    else
    {
        IPCMessage* temp = receiverQueue->messageHead;
        while (temp->next != nullptr)
        {
            temp = temp->next;
        }
        temp->next = message;
    }

    uint32_t prevMask = getInterruptMask();
    *ICSR_REG = NVIC_PENDSV_BIT;
    setInterruptMask(prevMask);

    return CRTOS::Result::RESULT_SUCCESS;
}

CRTOS::Result CRTOS::IPC::ReceiveMessage(CRTOS::Task::TaskHandle *receiver, IPCMessage*& outMessage, uint32_t timeoutTicks)
{
    uint32_t startTick = tickCount;

    while (true)
    {
        TaskMessageQueue* receiverQueue = GetMessageQueueForTask(receiver);
        if (receiverQueue == nullptr)
        {
            return CRTOS::Result::RESULT_NO_MEMORY;
        }

        if (receiverQueue->messageHead != nullptr)
        {
            outMessage = receiverQueue->messageHead;
            receiverQueue->messageHead = outMessage->next;
            return CRTOS::Result::RESULT_SUCCESS;
        }

        if (tickCount - startTick >= timeoutTicks)
        {
            return CRTOS::Result::RESULT_IPC_TIMEOUT;
        }

        uint32_t prevMask = getInterruptMask();
        *ICSR_REG = NVIC_PENDSV_BIT;
        setInterruptMask(prevMask);
    }
}

void CRTOS::IPC::ReleaseMessage(IPCMessage* message)
{
    if (message != nullptr)
    {
        if (message->data != nullptr)
        {
            mem.deallocate(message->data);
            message->data = nullptr;
            message->dataSize = 0;
            message->messageId = 0;
            message->next = nullptr;
            message->receiver = nullptr;
            message->sender = nullptr;
        }
        mem.deallocate(message);
        message = nullptr;
    }
}

CRTOS::Result CRTOS::CRC32::Init(void)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;

    if (sCrcTable == nullptr)
    {
        sCrcTable = reinterpret_cast<uint32_t*>(mem.allocate(sCrcTableSize * sizeof(uint32_t)));
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

CRTOS::Result CRTOS::CRC32::Calculate(const uint8_t* data, uint32_t length, uint32_t &output, uint32_t crc)
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
        sCrcTable = nullptr;
    }

    return result;
}

