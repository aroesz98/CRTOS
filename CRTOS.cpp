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

#include <cstdint>
#include <cstring>

#include <CRTOS.hpp>
#include <HeapAllocator.hpp>

typedef void (*TaskFunction)(void *);

struct TaskControlBlock
{
    volatile uint32_t *stackTop;
    volatile uint32_t *stack;
    TaskFunction function;
    void *function_args;
    char name[20u];
    int priority;
};

typedef struct TaskControlBlock TaskControlBlock;

typedef struct
{
    volatile uint32_t CTRL;
    volatile uint32_t LOAD;
    volatile uint32_t VAL;
} SysTick_Type;

__attribute__((used)) volatile TaskControlBlock *sCurrentTCB = NULL;

#define NVIC_MIN_PRIO                   (0xFFul)
#define NVIC_PENDSV_PRIO                (NVIC_MIN_PRIO << 16u)
#define NVIC_SYSTICK_PRIO               (NVIC_MIN_PRIO << 24u)
#define MAX_SYSCALL_INTERRUPT_PRIORITY  (2ul << 5u)
#define NVIC_PENDSV_BIT                 (1ul << 28u)

#define NVIC_SHPR3_REG  ((volatile uint32_t *)0xE000ED20ul)
#define ICSR_REG        ((volatile uint32_t *)0xE000ED04ul)
#define SYSTICK_REG     ((volatile uint32_t *)0xE000E010ul)
#define SysTick         ((SysTick_Type *)SYSTICK_REG)

#define SysTick_CTRL_CLKSOURCE_Msk  (1ul << 2u)
#define SysTick_CTRL_TICKINT_Msk    (1ul << 1u)
#define SysTick_CTRL_ENABLE_Msk     (1ul)

extern "C" void SVC_Handler(void) __attribute__((naked));
extern "C" void PendSV_Handler(void) __attribute__((naked));
extern "C" void SysTick_Handler(void);

extern "C" void RestoreCtxOfTheFirstTask(void) __attribute__((naked));
extern "C" uint32_t getInterruptMask(void) __attribute__((naked));
extern "C" void setInterruptMask(uint32_t ulMask) __attribute__((naked));
extern "C" void startFirstTask(void) __attribute__((naked));

static volatile uint32_t tickCount = 0u;

static uint32_t MAX_TASK_PRIORITY = 10u;
static uint32_t sTickRate = 1000u;
static uint32_t sCoreClock = 150000000u;

static CRTOS::Semaphore TmrSvc_Smphr(0u);
static HeapAllocator mem;

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
    Node<T> *newNode = (Node<T> *)(mem.allocate(sizeof(Node<T>)));
    memset(newNode, 0, sizeof(Node<T>));
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
    Node<T> *newNode = (Node<T> *)(mem.allocate(sizeof(Node<T>)));
    memset(newNode, 0, sizeof(Node<T>));
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
void ListInsertAtPosition(Node<T> *&head, T *data, int position)
{
    if (position < 0)
    {
        // Position can't be less than 0
        return;
    }

    if (position == 1)
    {
        insertAtBeginning(head, data);
        return;
    }

    Node<T> *newNode = (Node<T> *)(mem.allocate(sizeof(Node<T>)));
    memset(newNode, 0, sizeof(Node<T>));
    newNode->data = data;

    Node<T> *temp = head;

    for (int i = 0; temp != nullptr && i < position - 1; i++)
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
        delete temp;
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
void ListDeleteAtPosition(Node<T> *&head, int position)
{
    if (head == nullptr)
    {
        // List empty
        return;
    }

    if (position == 1)
    {
        ListDeleteAtBeginning(head);
        return;
    }

    Node<T> *temp = head;
    for (int i = 0; temp != nullptr && i < position; i++)
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

static Node<TaskControlBlock> *tcbList = nullptr;
static Node<CRTOS::Timer::SoftwareTimer> *sTimerList = nullptr;

CRTOS::Semaphore::Semaphore(uint32_t initialValue) : value(initialValue) {}

CRTOS::Result CRTOS::Semaphore::wait(uint32_t timeoutTicks)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    uint32_t startTick = tickCount;

    if (timeoutTicks == 0)
    {
        result = CRTOS::Result::RESULT_BAD_PARAMETER;
        return result;
    }

    while (true)
    {
        uint32_t oldValue = value.load();
        if (oldValue > 0u && value.compare_exchange_strong(oldValue, oldValue - 1u))
        {
            break;
        }

        uint32_t currentTick = tickCount;
        if ((currentTick - startTick) >= timeoutTicks)
        {
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

void CRTOS::Semaphore::signal(void)
{
    value++;
}

CRTOS::Result CRTOS::Config::initMem(void *pool, uint32_t size)
{
    if (pool == nullptr || size == 0u)
    {
        return CRTOS::Result::RESULT_NO_MEMORY;
    }

    mem.init(pool, size);

    return CRTOS::Result::RESULT_SUCCESS;
}

size_t CRTOS::Config::getAllocatedMemory(void)
{
    return mem.getAllocatedMemory();
}

size_t CRTOS::Config::getFreeMemory(void)
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

CRTOS::Mutex::Mutex(void) : flag(ATOMIC_FLAG_INIT)
{
}

CRTOS::Mutex::~Mutex(void)
{
}

void CRTOS::Mutex::lock(void)
{
    while (flag.test_and_set(std::memory_order_acquire))
        ;
}

void CRTOS::Mutex::unlock(void)
{
    flag.clear(std::memory_order_release);
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

void RestoreCtxOfTheFirstTask(void)
{
    __asm volatile(
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
        "currentCtxTCB: .word sCurrentTCB      \n");
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
        "SVC_ISR_ADDR: .word RestoreCtxOfTheFirstTask \n");
}

void PendSV_Handler(void)
{
    __asm volatile(
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
        "currentTCB: .word sCurrentTCB \n" ::"i"(MAX_SYSCALL_INTERRUPT_PRIORITY));
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
        "xVTORConst: .word 0xe000ed08 \n");
}

__attribute__((always_inline)) static inline void __ISB(void)
{
    __asm volatile("isb 0xF" ::: "memory");
}

__attribute__((always_inline)) static inline void __DSB(void)
{
    __asm volatile("dsb 0xF" ::: "memory");
}

extern "C" void switchCtx(void)
{
    // Find the next task with the highest priority
    Node<TaskControlBlock> *temp = tcbList;
    while (temp != nullptr)
    {
        if (temp->data == sCurrentTCB)
        {
            temp = temp->next;
            break;
        }
        temp = temp->next;
    }

    // If we reach the end of the list, loop back to the beginning
    if (temp == nullptr)
    {
        temp = tcbList;
    }

    // Update the current task
    sCurrentTCB = temp->data;
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

uint32_t *initStack(volatile uint32_t *stackTop, volatile uint32_t *stackEnd, TaskFunction code, void *args)
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
        TmrSvc_Smphr.wait(10u);
    }
}

CRTOS::Result CRTOS::Scheduler::Start(void)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    void *pool = nullptr;
    uint32_t poolSize = 0u;

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

    CRTOS::Task::Create(TimerISR, "TimerSVC", 400, NULL, MAX_TASK_PRIORITY - 2u, NULL);

    SortListByPriority(tcbList);

    sCurrentTCB = tcbList->data;

    SysTick->LOAD = (sCoreClock / sTickRate) - 1ul;
    SysTick->VAL = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;

    startFirstTask();

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
        TaskControlBlock *tmpTCB = (TaskControlBlock *)(mem.allocate(sizeof(TaskControlBlock)));
        if (tmpTCB == nullptr)
        {
            mem.deallocate(tmpTCB);
            result = CRTOS::Result::RESULT_NO_MEMORY;
            continue;
        }

        uint32_t *tmpStack = (uint32_t *)(mem.allocate(stackDepth * sizeof(uint32_t)));
        if (tmpStack == nullptr)
        {
            mem.deallocate(tmpStack);
            result = CRTOS::Result::RESULT_NO_MEMORY;
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

        volatile uint32_t *stackTop = &(tmpTCB->stack[stackDepth - (uint32_t)1u]);
        stackTop = (uint32_t *)(((uint32_t)stackTop) & ~7lu);

        tmpTCB->stackTop = initStack(stackTop, tmpTCB->stack, function, args);

        ListInsertAtEnd(tcbList, tmpTCB);
        SortListByPriority(tcbList);

        if (handle != NULL)
        {
            *handle = (TaskHandle)tmpTCB;
        }
    } while (0);

    setInterruptMask(prevMask);

    return result;
}

void CRTOS::Task::Delete(void)
{
    uint32_t prevMask = getInterruptMask();
    __DSB();
    __ISB();

    Node<TaskControlBlock> *tmp = tcbList;
    uint32_t pos = 0;
    while (tmp != nullptr)
    {
        if (sCurrentTCB == tmp->data)
        {
            ListDeleteAtPosition(tcbList, pos);
            break;
        }

        tmp = tmp->next;
        pos++;
    }

    mem.deallocate((void *)(sCurrentTCB->stack));
    mem.deallocate((void *)(sCurrentTCB));

    *ICSR_REG = NVIC_PENDSV_BIT;

    setInterruptMask(prevMask);
}

void CRTOS::Task::Delete(TaskHandle *handle)
{
    uint32_t prevMask = getInterruptMask();
    __DSB();
    __ISB();

    TaskControlBlock *tmpHandle = (TaskControlBlock *)(*handle);

    Node<TaskControlBlock> *tmp = tcbList;
    uint32_t pos = 0;

    while (tmp != nullptr)
    {
        if (tmpHandle == tmp->data)
        {
            ListDeleteAtPosition(tcbList, pos);
            break;
        }

        tmp = tmp->next;
        pos++;
    }

    mem.deallocate((void *)(tmpHandle->stack));
    mem.deallocate((void *)(tmpHandle));

    *ICSR_REG = NVIC_PENDSV_BIT;

    setInterruptMask(prevMask);
}

char *CRTOS::Task::GetCurrentTaskName(void)
{
    return ((char *)(sCurrentTCB->name));
}

CRTOS::Queue::Queue(uint32_t maxsize, uint32_t element_size)
    : mFront(0u), mRear(0u), mSize(0u), mMaxSize(maxsize), mElementSize(element_size)
{
    mQueue = mem.allocate(maxsize * element_size);
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

    memcpy(static_cast<char *>(mQueue) + mRear * mElementSize, item, mElementSize);
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

    memcpy(item, static_cast<char *>(mQueue) + mFront * mElementSize, mElementSize);
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
    mBuffer = (uint8_t *)(mem.allocate(mBufferSize));
    memcpy(&mBuffer[0], &(old.mBuffer[0]), mBufferSize);
}

CRTOS::CircularBuffer::~CircularBuffer(void)
{
    mem.deallocate(mBuffer);
}

CRTOS::Result CRTOS::CircularBuffer::init(void)
{
    if (mBufferSize == 0)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    mBuffer = (uint8_t *)(mem.allocate(mBufferSize));

    if (mBuffer == nullptr)
    {
        return CRTOS::Result::RESULT_NO_MEMORY;
    }

    return CRTOS::Result::RESULT_SUCCESS;
}

CRTOS::Result CRTOS::CircularBuffer::put(const uint8_t *data, uint32_t size, uint32_t timeout_ms)
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
        memcpy(&mBuffer[mHead], data, size);
    }
    else
    {
        uint32_t firstPartSize = mBufferSize - mHead;
        memcpy(&mBuffer[mHead], data, firstPartSize);
        memcpy(mBuffer, &data[firstPartSize], size - firstPartSize);
    }

    mHead = (mHead + size) % mBufferSize;
    mCurrentSize += size;
    return result;
}

CRTOS::Result CRTOS::CircularBuffer::get(uint8_t *data, uint32_t size, uint32_t timeout_ms)
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
        memcpy(data, &mBuffer[mTail], size);
    }
    else
    {
        uint32_t firstPartSize = mBufferSize - mTail;
        memcpy(data, &mBuffer[mTail], firstPartSize);
        memcpy(&data[firstPartSize], mBuffer, size - firstPartSize);
    }

    mTail = (mTail + size) % mBufferSize;
    mCurrentSize -= size;
    return result;
}