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

#include <HeapAllocator.hpp>

#include "ELFParser.hpp"
#include "kernel.h"
#include "stdio.h"
#include <cstring>

typedef void (*TaskFunction)(void *);

enum class TaskState : uint32_t
{
    TASK_RUNNING,
    TASK_READY,
    TASK_DELAYED,
    TASK_PAUSED,
    TASK_BLOCKED_BY_SEMAPHORE,
    TASK_BLOCKED_BY_QUEUE,
    TASK_BLOCKED_BY_CIRC_BUFFER
};

struct TaskControlBlock
{
    volatile uint32_t *stackTop;
    volatile uint32_t *stack;
    TaskFunction function;
    void *function_args;
    uint32_t vtor_addr;
    uint32_t priority;
    TaskState state;
    uint32_t timeout;
    uint32_t delayUpTo;
    uint32_t stackSize;
    uint32_t enterCycles;
    uint32_t exitCycles;
    uint64_t executionTime;
    char name[20u];
};

typedef struct TaskControlBlock TaskControlBlock;

// Must match module ProgramInfo
typedef struct ProgramInfoBin
{
    uint32_t stackPointer;
    uint32_t entryPoint; // offset from image base; code is Thumb PIE
    uint32_t vectors[74];
    uint32_t section_data_start_addr; // offset in image of .data load
    uint32_t section_data_dest_addr;
    uint32_t section_data_size;
    uint32_t section_bss_start_addr;
    uint32_t section_bss_size;
    uint32_t reserved[22];
    uint32_t vtor_offset;
    uint32_t msp_limit;
} ProgramInfoBin;

// Optional descriptor directly following ProgramInfo in our module format
typedef struct __attribute__((packed)) ModuleDescriptorBin
{
    uint32_t magic; // 'MODU' 0x4D4F4455
    uint16_t desc_version;
    uint16_t _r0;
    uint32_t api_version;
    uint8_t name[32];
    uint8_t semver_major;
    uint8_t semver_minor;
    uint16_t semver_patch;
    uint32_t build_timestamp;
    uint32_t image_size; // total size of BIN
    uint32_t entry;      // address; ignore for BIN loader
    uint32_t reserved[6];
} ModuleDescriptorBin;

// Module tracking structure
struct LoadedModuleInfo
{
    char name[20];
    uint32_t baseAddress;
    uint32_t entryPoint;
    uint32_t textAddr;
    uint32_t textSize;
    uint32_t dataAddr;
    uint32_t dataSize;
    uint32_t bssAddr;
    uint32_t bssSize;
    uint32_t stackAddr;
    uint32_t stackSize;
    uint32_t totalSize;
    TaskControlBlock *tcb;
    CRTOS::ModuleState state;
    uint32_t loadTime;
    ModuleSharedMemory *sharedMemory; // Shared memory for data exchange
};

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

// Module tracking
static LoadedModuleInfo *loadedModules = nullptr;
static uint32_t loadedModulesCount = 0;
static const uint32_t MAX_LOADED_MODULES = 16;

constexpr uint32_t NVIC_MIN_PRIO = 0xFFul;
constexpr uint32_t NVIC_PENDSV_PRIO = NVIC_MIN_PRIO << 16u;
constexpr uint32_t NVIC_SYSTICK_PRIO = NVIC_MIN_PRIO << 24u;
constexpr uint32_t MAX_SYSCALL_IRQ_PRIO = 1ul << 5u;
constexpr uint32_t NVIC_PENDSV_BIT = 1ul << 28u;

#define DWT_REG ((volatile uint32_t *)0xE0001000ul)
#define ICSR_REG ((volatile uint32_t *)0xE000ED04ul)
#define SYSTICK_REG ((volatile uint32_t *)0xE000E010ul)
#define NVIC_SHPR3_REG ((volatile uint32_t *)0xE000ED20ul)

#define DWT ((DWT_Type *)DWT_REG)
#define SysTick ((SysTick_Type *)SYSTICK_REG)

#define SysTick_CTRL_CLKSOURCE (1ul << 2u)
#define SysTick_CTRL_TICKINT (1ul << 1u)
#define SysTick_CTRL_ENABLE (1ul)

extern "C" void SVC_Handler(void) __attribute__((naked));
extern "C" void PendSV_Handler(void) __attribute__((naked));
extern "C" void SysTick_Handler(void);

extern "C" void RestoreCtxOfTheFirstTask(void) __attribute__((naked));
extern "C" uint32_t getInterruptMask(void);
extern "C" void setInterruptMask(uint32_t mask = 0u);
extern "C" void startFirstTask(void) __attribute__((naked));

static inline void __DSB(void);
static inline void __ISB(void);

extern "C" void memcpy_optimized(void *d, void *s, uint32_t len);
extern "C" void memset_optimized(void *d, uint32_t val, uint32_t len);

static volatile uint32_t tickCount = 0u;

static uint32_t MAX_TASK_PRIORITY = 10u;
static uint32_t sTickRate = 1000u;
static uint32_t sCoreClock = 150000000u;

static constexpr uint32_t MODULE_MAGIC = 0x4D4F4455u; // 'MODU'
static constexpr uint32_t DEFAULT_MODULE_LEN = 4096u;
static constexpr uint32_t DEFAULT_STACK_SIZE = 1024u;

static HeapAllocator mem;

static bool isPendingTask(void);

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

template <typename T>
class Node
{
public:
    T *data;
    Node *next;
    Node *prev;

    // Static tail pointer for O(1) insertions at end
    static Node<T> *tail;

    // Optimized constructor with inline hint
    inline Node(T *data) : data(data), next(nullptr), prev(nullptr) {}
    inline Node() : data(nullptr), next(nullptr), prev(nullptr) {}
    inline void init(T *d)
    {
        data = d;
        next = nullptr;
        prev = nullptr;
    }
};

// Fast insert at beginning - O(1)
template <typename T>
inline void ListInsertAtBeginning(Node<T> *&head, T *data)
{
    Node<T> *newNode = reinterpret_cast<Node<T> *>(mem.allocate(sizeof(Node<T>)));
    if (__builtin_expect(newNode == nullptr, 0))
        return;

    // Initialize node inline for performance
    newNode->data = data;
    newNode->next = head;
    newNode->prev = nullptr;

    if (__builtin_expect(head == nullptr, 0))
    {
        head = newNode;
        Node<T>::tail = newNode;
        return;
    }

    head->prev = newNode;
    head = newNode;
}

// Fast insert at end using tail pointer - O(1)
template <typename T>
inline void ListInsertAtEnd(Node<T> *&head, T *data)
{
    Node<T> *newNode = reinterpret_cast<Node<T> *>(mem.allocate(sizeof(Node<T>)));
    if (__builtin_expect(newNode == nullptr, 0))
        return;

    // Initialize node inline for performance
    newNode->data = data;
    newNode->next = nullptr;
    newNode->prev = nullptr;

    if (__builtin_expect(head == nullptr, 0))
    {
        head = newNode;
        Node<T>::tail = newNode;
        return;
    }

    if (__builtin_expect(Node<T>::tail != nullptr, 1))
    {
        // Fast O(1) insertion at the end
        Node<T>::tail->next = newNode;
        newNode->prev = Node<T>::tail;
        Node<T>::tail = newNode;
        return;
    }

    Node<T>::tail = head;
    while (Node<T>::tail->next != nullptr)
    {
        Node<T>::tail = Node<T>::tail->next;
    }

    Node<T>::tail->next = newNode;
    newNode->prev = Node<T>::tail;
    Node<T>::tail = newNode;
}

// Optimized insert at position
template <typename T>
inline void ListInsertAtPosition(Node<T> *&head, T *data, uint32_t position)
{
    if (__builtin_expect(position == 0, 0))
    {
        ListInsertAtBeginning(head, data);
        return;
    }

    Node<T> *newNode = reinterpret_cast<Node<T> *>(mem.allocate(sizeof(Node<T>)));
    if (__builtin_expect(newNode == nullptr, 0))
        return;

    newNode->data = data;
    newNode->next = nullptr;
    newNode->prev = nullptr;

    Node<T> *current = head;
    for (uint32_t i = 0; current != nullptr && i < position - 1; i++)
    {
        current = current->next;
    }

    if (__builtin_expect(current == nullptr, 0))
    {
        mem.deallocate(newNode);
        return;
    }

    if (__builtin_expect(current->next == nullptr, 0))
    {
        Node<T>::tail = current;
        Node<T>::tail->next = newNode;
        newNode->prev = Node<T>::tail;
        Node<T>::tail = newNode;
        return;
    }

    newNode->next = current->next;
    newNode->prev = current;
    current->next->prev = newNode;
    current->next = newNode;
}

// Fast delete at beginning - O(1)
template <typename T>
inline void ListDeleteAtBeginning(Node<T> *&head)
{
    if (__builtin_expect(head == nullptr, 0))
        return;

    Node<T> *nodeToDelete = head;
    head = head->next;

    if (__builtin_expect(head != nullptr, 1))
    {
        head->prev = nullptr;
    }
    else
    {
        Node<T>::tail = nullptr;
    }

    if (__builtin_expect(nodeToDelete == Node<T>::tail, 0))
    {
        Node<T>::tail = head;
    }

    mem.deallocate(nodeToDelete);
}

// O(1) delete at end using tail pointer
template <typename T>
inline void ListDeleteAtEnd(Node<T> *&head)
{
    if (head == nullptr)
    {
        return;
    }

    if (head->next == nullptr)
    {
        mem.deallocate(head);
        head = nullptr;
        Node<T>::tail = nullptr;
        return;
    }

    // If tail is not set, find it once
    if (Node<T>::tail == nullptr)
    {
        Node<T>::tail = head;
        while (Node<T>::tail->next != nullptr)
        {
            Node<T>::tail = Node<T>::tail->next;
        }
    }

    Node<T> *nodeToDelete = Node<T>::tail;
    if (Node<T>::tail != nullptr)
    {
        Node<T>::tail = Node<T>::tail->prev;
    }
    if (Node<T>::tail != nullptr)
    {
        Node<T>::tail->next = nullptr;
    }

    mem.deallocate(nodeToDelete);
}

// Optimized delete at position
template <typename T>
inline void ListDeleteAtPosition(Node<T> *&head, uint32_t position)
{
    if (head == nullptr)
    {
        return;
    }

    if (position == 0)
    {
        ListDeleteAtBeginning(head);
        return;
    }

    Node<T> *current = head;
    for (uint32_t i = 0; current != nullptr && i < position; i++)
    {
        current = current->next;
    }

    if (current == nullptr)
    {
        return;
    }

    // Special case: deleting tail
    if (current == Node<T>::tail)
    {
        ListDeleteAtEnd(head);
        return;
    }

    current->prev->next = current->next;
    current->next->prev = current->prev;
    mem.deallocate(current);
}

// Fast search function - no allocation needed
template <typename T>
inline Node<T> *ListSearchByData(Node<T> *head, T *target)
{
    Node<T> *current = head;
    while (current != nullptr)
    {
        if (current->data == target)
        {
            return current;
        }
        current = current->next;
    }
    return nullptr;
}

// Explicit template instantiations for static tail members
template <>
Node<TaskControlBlock> *Node<TaskControlBlock>::tail = nullptr;
template <>
Node<CRTOS::Timer::SoftwareTimer> *Node<CRTOS::Timer::SoftwareTimer>::tail = nullptr;
template <>
Node<unsigned long *> *Node<unsigned long *>::tail = nullptr;

static Node<TaskControlBlock> *readyTaskList = nullptr;
static Node<CRTOS::Timer::SoftwareTimer> *sTimerList = nullptr;

static CRTOS::Task::StackOverflowHook sStackOverflowHook = nullptr;

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
    irqMask = getInterruptMask();

    while (flag.test_and_set(std::memory_order_acquire))
        ;
}

void CRTOS::Mutex::Unlock(void)
{
    flag.clear(std::memory_order_release);

    setInterruptMask(irqMask);
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
    info.allocatedMemory = mem.getAllocatedMemory();

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

inline __attribute__((always_inline)) uint32_t getInterruptMask(void)
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

inline __attribute__((always_inline)) void setInterruptMask(uint32_t mask)
{
    __asm__ volatile("msr basepri, %0" ::"r"(mask) : "memory");
}

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

CRTOS::Result CRTOS::BinarySemaphore::signal(void)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;

    uint32_t mask = getInterruptMask();

    if (_val > 0)
    {
        result = CRTOS::Result::RESULT_SEMAPHORE_BUSY;
    }
    else
    {
        if (listOfTasksWaitingToRecv != nullptr)
        {
            TaskControlBlock *tmp = *(TaskControlBlock **)(listOfTasksWaitingToRecv->data);
            if (tmp->state == TaskState::TASK_BLOCKED_BY_SEMAPHORE)
            {
                tmp->state = TaskState::TASK_READY;
            }
            ListDeleteAtBeginning(listOfTasksWaitingToRecv);
        }

        _val = 1;
    }

    setInterruptMask(mask);

    return result;
}

CRTOS::Result CRTOS::BinarySemaphore::wait(uint32_t ticks)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    uint32_t time = GetSystemTime();
    uint32_t timeout = time + ticks;
    bool isBlocked = false;

    for (;;)
    {
        uint32_t mask = getInterruptMask();

        time = GetSystemTime();

        if (_val > 0u)
        {
            _val = 0u;
            setInterruptMask(mask);
            return result;
        }
        else
        {
            if (ticks == 0u)
            {
                setInterruptMask(mask);
                result = CRTOS::Result::RESULT_SEMAPHORE_TIMEOUT;
                return result;
            }

            if (isBlocked == false)
            {
                sCurrentTCB->timeout = timeout;
                sCurrentTCB->state = TaskState::TASK_BLOCKED_BY_SEMAPHORE;
                ListInsertAtEnd(listOfTasksWaitingToRecv, (uint32_t **)&sCurrentTCB);
                isBlocked = true;
            }
        }

        setInterruptMask(mask);

        if (time < timeout)
        {
            if (_val > 0u)
            {
                mask = getInterruptMask();
                if (listOfTasksWaitingToRecv != nullptr)
                {
                    TaskControlBlock *tmp = *(TaskControlBlock **)(listOfTasksWaitingToRecv->data);
                    if (tmp->state == TaskState::TASK_BLOCKED_BY_SEMAPHORE)
                    {
                        tmp->state = TaskState::TASK_READY;
                    }
                    ListDeleteAtBeginning(listOfTasksWaitingToRecv);
                }
                setInterruptMask(mask);

                if (isHigherPrioTaskPending() == true)
                {
                    *ICSR_REG = NVIC_PENDSV_BIT;

                    __DSB();
                    __ISB();
                }
            }
        }
        else
        {
            // Timeout occurred - remove ourselves from waiting list
            mask = getInterruptMask();

            // Find and remove current task from waiting list
            Node<uint32_t *> *temp = listOfTasksWaitingToRecv;
            if (temp != nullptr)
            {
                ListDeleteAtBeginning(listOfTasksWaitingToRecv);
            }

            sCurrentTCB->state = TaskState::TASK_READY;
            setInterruptMask(mask);

            result = CRTOS::Result::RESULT_SEMAPHORE_TIMEOUT;
            return result;
        }
    }
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
                
                // Get priority, state, and runtime cycles from TCB
                TaskControlBlock *tcb = (TaskControlBlock *)stackInfo[task_index].taskHandle;
                if (tcb != nullptr)
                {
                    info->priority = tcb->priority;
                    info->state = (uint32_t)tcb->state;
                    info->runtimeCycles = tcb->executionTime;
                }
                else
                {
                    info->priority = 0;
                    info->state = 0;
                    info->runtimeCycles = 0;
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
        sCurrentTCB = highestPriorityTask->data;
        sCurrentTCB->state = TaskState::TASK_RUNNING;
    }
    else
    {
        sCurrentTCB = (TaskControlBlock *)(idleTaskHandle);
        sCurrentTCB->state = TaskState::TASK_RUNNING;
    }

    updateEnterCycles();
    TASK_SWITCHED_IN();
}

void SysTick_Handler(void)
{
    uint32_t mask = getInterruptMask();

    tickCount++;

    if (isPendingTask() == true)
    {
        CRTOS::Task::ExitCriticalSection();
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

        result = CRTOS::Task::Create(TimerISR, "TimerSVC", 512, nullptr, MAX_TASK_PRIORITY - 1u, nullptr);
        if (result != CRTOS::Result::RESULT_SUCCESS)
        {
            continue;
        }

        result = CRTOS::Task::Create(idleTask, "IDLE", 128, nullptr, 0u, &idleTaskHandle);
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

void CRTOS::Task::Yield(void)
{
    if (isHigherPrioTaskPending() == true)
    {
        *ICSR_REG = NVIC_PENDSV_BIT;
        __DSB();
        __ISB();
    }
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
        memset_optimized(&(tmpTCB->name[0u]), 0u, 20u);

        tmpTCB->stack = &tmpStack[0u];
        tmpTCB->stackSize = stackDepth;
        tmpTCB->function = function;
        tmpTCB->function_args = args;
        tmpTCB->enterCycles = 0u;
        tmpTCB->exitCycles = 0u;
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
        memcpy_optimized(&tmpTCB->name[0], (char *)&name[0u], nameLength < 20u ? nameLength : 20u);

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

        memset_optimized(&(tmpTCB->name[0u]), 0u, 20u);

        tmpTCB->stackSize = 0u;
        tmpTCB->function_args = args;
        tmpTCB->enterCycles = 0u;
        tmpTCB->exitCycles = 0u;

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
        memcpy_optimized(&tmpTCB->name[0], (char *)&name[0u], nameLength < 20u ? nameLength : 20u);

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
CRTOS::Result CRTOS::Task::LPC55S69_Features::CreateTaskForBinModule(uint8_t *bin, const char *const name, void *args, uint32_t prio, TaskHandle *handle)
{
    if (bin == nullptr || name == nullptr)
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

        memset_optimized(&(tmpTCB->name[0u]), 0u, 20u);
        tmpTCB->function_args = args;
        tmpTCB->enterCycles = 0u;
        tmpTCB->exitCycles = 0u;

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

        // Task name
        uint32_t nameLength = pStringLength(name);
        memcpy_optimized(&tmpTCB->name[0], (char *)&name[0u], nameLength < 20u ? nameLength : 20u);

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
            memcpy_optimized(modInfo->name, tmpTCB->name, 20);
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
        memcpy_optimized(&infoArray[taskCount].name[0], &tcb->name[0], 20u);
        infoArray[taskCount].stackSize = totalStackBytes;
        infoArray[taskCount].stackUsed = usedStackBytes;
        infoArray[taskCount].stackFree = freeStackBytes;
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

CRTOS::Queue::Queue(uint32_t maxsize, uint32_t element_size)
    : mFront(0u), mRear(0u), mSize(0u), mMaxSize(maxsize), mElementSize(element_size)
{
    mQueue = reinterpret_cast<uint8_t *>(mem.allocate(maxsize * element_size));
}

CRTOS::Queue::~Queue(void)
{
    mem.deallocate(mQueue);
}

CRTOS::Result CRTOS::Queue::Send(void *item)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;

    do
    {
        if (item == nullptr)
        {
            result = CRTOS::Result::RESULT_BAD_PARAMETER;
            continue;
        }

        if (mQueue == nullptr)
        {
            result = CRTOS::Result::RESULT_NO_MEMORY;
            continue;
        }

        if (mSize == mMaxSize)
        {
            result = CRTOS::Result::RESULT_QUEUE_FULL;
            continue;
        }

        uint32_t mask = getInterruptMask();

        if (listOfTasksWaitingToRecv != nullptr)
        {
            TaskControlBlock *tmp = *(TaskControlBlock **)(listOfTasksWaitingToRecv->data);
            if (tmp->state == TaskState::TASK_BLOCKED_BY_QUEUE)
            {
                tmp->state = TaskState::TASK_READY;
            }
            ListDeleteAtBeginning(listOfTasksWaitingToRecv);
        }

        memcpy_optimized(mQueue + (mRear * mElementSize), item, mElementSize);
        mRear = (mRear + 1) % mMaxSize;
        mSize++;

        setInterruptMask(mask);
    } while (0u);

    return result;
}

CRTOS::Result CRTOS::Queue::Receive(void *item, uint32_t timeout)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;
    uint32_t time = GetSystemTime();
    uint32_t stimeout = time + timeout;
    bool isBlocked = false;

    uint32_t mask = getInterruptMask();

    for (;;)
    {
        time = GetSystemTime();

        if (item == nullptr)
        {
            setInterruptMask(mask);
            result = CRTOS::Result::RESULT_BAD_PARAMETER;
            return result;
        }

        if (mQueue == nullptr)
        {
            setInterruptMask(mask);
            result = CRTOS::Result::RESULT_NO_MEMORY;
            return result;
        }

        if (mSize > 0u)
        {
            memcpy_optimized(item, mQueue + (mFront * mElementSize), mElementSize);
            mFront = (mFront + 1) % mMaxSize;
            mSize--;

            setInterruptMask(mask);

            result = CRTOS::Result::RESULT_SUCCESS;
            return result;
        }
        else
        {
            if (timeout == 0u)
            {
                setInterruptMask(mask);

                result = CRTOS::Result::RESULT_QUEUE_TIMEOUT;
                return result;
            }
            if (isBlocked == false)
            {
                sCurrentTCB->timeout = stimeout;
                sCurrentTCB->state = TaskState::TASK_BLOCKED_BY_QUEUE;
                ListInsertAtEnd(listOfTasksWaitingToRecv, (uint32_t **)&sCurrentTCB);
                isBlocked = true;
            }
        }

        setInterruptMask(mask);

        if (time < stimeout)
        {
            if (mSize > 0u)
            {
                mask = getInterruptMask();
                if (listOfTasksWaitingToRecv != nullptr)
                {
                    TaskControlBlock *tmp = *(TaskControlBlock **)(listOfTasksWaitingToRecv->data);
                    if (tmp->state == TaskState::TASK_BLOCKED_BY_QUEUE)
                    {
                        tmp->state = TaskState::TASK_READY;
                    }
                    ListDeleteAtBeginning(listOfTasksWaitingToRecv);
                }
                setInterruptMask(mask);

                if (isHigherPrioTaskPending() == true)
                {
                    *ICSR_REG = NVIC_PENDSV_BIT;

                    __DSB();
                    __ISB();
                }
            }
        }
        else
        {
            // Timeout occurred - remove ourselves from waiting list
            mask = getInterruptMask();

            // Find and remove current task from waiting list
            Node<uint32_t *> *temp = listOfTasksWaitingToRecv;
            if (temp != nullptr)
            {
                ListDeleteAtBeginning(listOfTasksWaitingToRecv);
            }

            sCurrentTCB->state = TaskState::TASK_READY;
            setInterruptMask(mask);

            result = CRTOS::Result::RESULT_QUEUE_TIMEOUT;
            return result;
        }
    }
}

CRTOS::CircularBuffer::CircularBuffer(uint32_t mBuffer_size)
    : mBuffer(nullptr),
      mHead(0u),
      mTail(0u),
      mCurrentSize(0u),
      mBufferSize(mBuffer_size)
{
}

CRTOS::CircularBuffer::CircularBuffer(const CircularBuffer &old)
{
    uint32_t mask = getInterruptMask();

    mHead = old.mHead;
    mTail = old.mTail;
    mCurrentSize = old.mCurrentSize;
    mBufferSize = old.mBufferSize;
    mBuffer = reinterpret_cast<uint8_t *>(mem.allocate(mBufferSize));
    memcpy_optimized(&mBuffer[0], &(old.mBuffer[0]), mBufferSize);

    setInterruptMask(mask);
}

CRTOS::CircularBuffer::~CircularBuffer(void)
{
    mem.deallocate(mBuffer);
}

CRTOS::Result CRTOS::CircularBuffer::Init(void)
{
    CRTOS::Result result = CRTOS::Result::RESULT_SUCCESS;

    uint32_t mask = getInterruptMask();

    do
    {
        if (mBufferSize == 0)
        {
            setInterruptMask(mask);

            result = CRTOS::Result::RESULT_BAD_PARAMETER;
            continue;
        }

        mBuffer = reinterpret_cast<uint8_t *>(mem.allocate(mBufferSize));

        if (mBuffer == nullptr)
        {
            setInterruptMask(mask);

            result = CRTOS::Result::RESULT_NO_MEMORY;
            continue;
        }

        setInterruptMask(mask);
    } while (0u);

    return result;
}

CRTOS::Result CRTOS::CircularBuffer::Send(const uint8_t *data, uint32_t size)
{
    if ((data == nullptr) || (size == 0u))
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    if (mBuffer == nullptr)
    {
        return CRTOS::Result::RESULT_NO_MEMORY;
    }

    uint32_t mask = getInterruptMask();

    if (mCurrentSize + size > mBufferSize)
    {
        setInterruptMask(mask);
        return CRTOS::Result::RESULT_CIRCULAR_BUFFER_FULL;
    }

    // Copy data to buffer
    if (mHead + size <= mBufferSize)
    {
        memcpy_optimized(&mBuffer[mHead], (void *)data, size);
    }
    else
    {
        uint32_t firstPartSize = mBufferSize - mHead;
        memcpy_optimized(&mBuffer[mHead], (void *)data, firstPartSize);
        memcpy_optimized(mBuffer, (void *)(&data[firstPartSize]), size - firstPartSize);
    }

    mHead = (mHead + size) % mBufferSize;
    mCurrentSize += size;

    // Wake up waiting receiver if any
    bool shouldSwitch = false;
    if (listOfTasksWaitingToRecv != nullptr)
    {
        TaskControlBlock *tmp = *(TaskControlBlock **)(listOfTasksWaitingToRecv->data);
        if (tmp->state == TaskState::TASK_BLOCKED_BY_CIRC_BUFFER)
        {
            tmp->state = TaskState::TASK_READY;
            // Check if woken task has higher priority
            if (tmp->priority > sCurrentTCB->priority)
            {
                shouldSwitch = true;
            }
        }
        ListDeleteAtBeginning(listOfTasksWaitingToRecv);
    }

    setInterruptMask(mask);

    // Trigger context switch if higher priority task was woken
    if (shouldSwitch)
    {
        *ICSR_REG = NVIC_PENDSV_BIT;
        __DSB();
        __ISB();
    }

    return CRTOS::Result::RESULT_SUCCESS;
}

CRTOS::Result CRTOS::CircularBuffer::Receive(uint8_t *data, uint32_t size, uint32_t timeout)
{
    if ((data == nullptr) || (size == 0u))
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    if (mBuffer == nullptr)
    {
        return CRTOS::Result::RESULT_NO_MEMORY;
    }

    uint32_t stimeout = GetSystemTime() + timeout;

    while (1)
    {
        uint32_t mask = getInterruptMask();

        // Check if data is available
        if (mCurrentSize >= size)
        {
            // Data available - copy it out
            if (mTail + size <= mBufferSize)
            {
                memcpy_optimized(&data[0], &mBuffer[mTail], size);
            }
            else
            {
                uint32_t firstPartSize = mBufferSize - mTail;
                memcpy_optimized(&data[0], &mBuffer[mTail], firstPartSize);
                memcpy_optimized(&data[firstPartSize], &mBuffer[0], size - firstPartSize);
            }

            mTail = (mTail + size) % mBufferSize;
            mCurrentSize -= size;

            setInterruptMask(mask);
            return CRTOS::Result::RESULT_SUCCESS;
        }

        // No data available - check timeout first before blocking
        if (timeout == 0u || GetSystemTime() >= stimeout)
        {
            setInterruptMask(mask);
            return CRTOS::Result::RESULT_CIRCULAR_BUFFER_TIMEOUT;
        }

        // Block this task and add to waiting list
        sCurrentTCB->timeout = stimeout;
        sCurrentTCB->state = TaskState::TASK_BLOCKED_BY_CIRC_BUFFER;
        ListInsertAtEnd(listOfTasksWaitingToRecv, (uint32_t **)&sCurrentTCB);

        setInterruptMask(mask);

        // Trigger context switch - when we resume, sender has either:
        // 1. Sent data and woken us up, OR
        // 2. Timeout occurred and scheduler woke us
        *ICSR_REG = NVIC_PENDSV_BIT;
        __DSB();
        __ISB();

        // When we get here, we've been woken up - loop back to check data/timeout
    }
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
        memcpy_optimized(dest->name, src->name, 20);
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

            memcpy_optimized(info.name, src->name, 20);
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
