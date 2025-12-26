/*
 * CRTOS_Internal.hpp - Internal definitions shared between CRTOS modules
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

#ifndef CRTOS_INTERNAL_HPP
#define CRTOS_INTERNAL_HPP

#include <cstdint>
#include "CRTOS.hpp"
#include "kernel.h"
#include "HeapAllocator.hpp"

// Forward declarations
template <typename T> class Node;

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
    uint32_t heapAllocated;      // Total heap memory allocated by this task
    uint32_t registeredIRQ;      // IRQ number this task is registered for (0xFFFFFFFF if none)
    char name[20u];
};

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

// Node template for linked lists
// Note: Implementation is in CRTOS.cpp
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

// External reference to mem allocator (defined in CRTOS.cpp)
extern HeapAllocator mem;

// List function implementations - inline to avoid linking issues
template <typename T>
inline void ListInsertAtBeginning(Node<T> *&head, T *data)
{
    Node<T> *newNode = reinterpret_cast<Node<T> *>(mem.allocate(sizeof(Node<T>)));
    if (__builtin_expect(newNode == nullptr, 0))
        return;

    newNode->data = data;
    newNode->next = head;
    newNode->prev = nullptr;

    if (__builtin_expect(head != nullptr, 1))
    {
        head->prev = newNode;
    }
    else
    {
        Node<T>::tail = newNode;
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

// O(1) delete at beginning
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

    Node<T> *nodeToDelete = Node<T>::tail;

    if (nodeToDelete == nullptr)
    {
        nodeToDelete = head;
        while (nodeToDelete->next != nullptr)
        {
            nodeToDelete = nodeToDelete->next;
        }
    }

    Node<T>::tail = nodeToDelete->prev;

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

// Hardware register definitions
#define DWT_REG ((volatile uint32_t *)0xE0001000ul)
#define ICSR_REG ((volatile uint32_t *)0xE000ED04ul)
#define SYSTICK_REG ((volatile uint32_t *)0xE000E010ul)
#define NVIC_SHPR3_REG ((volatile uint32_t *)0xE000ED20ul)

#define DWT ((DWT_Type *)DWT_REG)
#define SysTick ((SysTick_Type *)SYSTICK_REG)

#define SysTick_CTRL_CLKSOURCE (1ul << 2u)
#define SysTick_CTRL_TICKINT (1ul << 1u)
#define SysTick_CTRL_ENABLE (1ul)

// Constants
constexpr uint32_t NVIC_MIN_PRIO = 0xFFul;
constexpr uint32_t NVIC_PENDSV_PRIO = NVIC_MIN_PRIO << 16u;
constexpr uint32_t NVIC_SYSTICK_PRIO = NVIC_MIN_PRIO << 24u;
constexpr uint32_t MAX_SYSCALL_IRQ_PRIO = 1ul << 5u;
constexpr uint32_t NVIC_PENDSV_BIT = 1ul << 28u;

// Inline barrier functions
static inline void __DSB(void)
{
    __asm volatile("dsb 0xF" ::: "memory");
}

static inline void __ISB(void)
{
    __asm volatile("isb 0xF" ::: "memory");
}

#endif /* CRTOS_INTERNAL_HPP */
