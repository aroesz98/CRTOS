/*
 * crtos_userspace.h - CRTOS User Space API
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
 * User space API for CRTOS applications/modules.
 * This header provides C-style wrappers for system calls.
 * Include this in your user space applications (modules).
 *
 * Usage in modules:
 *   #include <crtos_userspace.h>
 *
 *   int main(void) {
 *       int fd = open("/dev/uart0", O_RDWR);
 *       write(fd, "Hello!\n", 7);
 *       close(fd);
 *       return 0;
 *   }
 */

#ifndef CRTOS_USERSPACE_H
#define CRTOS_USERSPACE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ===== Type Definitions =====

typedef int32_t pid_t;

// File flags (for open)
#define O_RDONLY    0x0001
#define O_WRONLY    0x0002
#define O_RDWR      0x0003
#define O_NONBLOCK  0x0004
#define O_APPEND    0x0008
#define O_CREAT     0x0010
#define O_TRUNC     0x0020

// Error codes (same as in SystemCall.hpp)
#define EPERM       1
#define ENOENT      2
#define ESRCH       3
#define EINTR       4
#define EIO         5
#define ENXIO       6
#define E2BIG       7
#define EBADF       9
#define ECHILD      10
#define EAGAIN      11
#define ENOMEM      12
#define EACCES      13
#define EFAULT      14
#define EBUSY       16
#define EEXIST      17
#define ENODEV      19
#define EINVAL      22
#define EMFILE      24
#define ENOSYS      38
#define ETIMEDOUT   110

// Timeout constants
#define TIMEOUT_INFINITE    0xFFFFFFFF
#define TIMEOUT_IMMEDIATE   0

// IRQ Event structure
struct irq_event {
    uint32_t irq_number;
    uint32_t timestamp;
    uint32_t data;
    void* context;
};

// Process info structure
struct process_info {
    uint32_t pid;
    char name[32];
    uint32_t heap_used;
    uint32_t heap_size;
    uint32_t stack_used;
    uint32_t stack_size;
    uint32_t cpu_time;
    uint32_t state;
};

// System info structure
struct system_info {
    uint32_t total_memory;
    uint32_t free_memory;
    uint32_t process_count;
    uint32_t uptime;
    uint32_t cpu_freq;
    uint32_t tick_rate;
};

// ===== System Call Wrappers =====

//
// Invoke a system call with 0-4 arguments
// This uses ARM Cortex-M SVC (Supervisor Call) instruction
//
static inline int32_t syscall0(uint32_t num)
{
    register int32_t r0 __asm__("r0");
    __asm__ volatile("svc %1" : "=r"(r0) : "i"(num) : "memory");
    return r0;
}

static inline int32_t syscall1(uint32_t num, uint32_t arg0)
{
    register int32_t r0 __asm__("r0") = arg0;
    __asm__ volatile("svc %1" : "+r"(r0) : "i"(num) : "memory");
    return r0;
}

static inline int32_t syscall2(uint32_t num, uint32_t arg0, uint32_t arg1)
{
    register int32_t r0 __asm__("r0") = arg0;
    register int32_t r1 __asm__("r1") = arg1;
    __asm__ volatile("svc %2" : "+r"(r0) : "r"(r1), "i"(num) : "memory");
    return r0;
}

static inline int32_t syscall3(uint32_t num, uint32_t arg0, uint32_t arg1, uint32_t arg2)
{
    register int32_t r0 __asm__("r0") = arg0;
    register int32_t r1 __asm__("r1") = arg1;
    register int32_t r2 __asm__("r2") = arg2;
    __asm__ volatile("svc %3" : "+r"(r0) : "r"(r1), "r"(r2), "i"(num) : "memory");
    return r0;
}

static inline int32_t syscall4(uint32_t num, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    register int32_t r0 __asm__("r0") = arg0;
    register int32_t r1 __asm__("r1") = arg1;
    register int32_t r2 __asm__("r2") = arg2;
    register int32_t r3 __asm__("r3") = arg3;
    __asm__ volatile("svc %4" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "i"(num) : "memory");
    return r0;
}

// ===== POSIX-like API =====

// Process management
static inline void exit(int status)
{
    syscall1(100, status); // SYS_EXIT
    while(1); // Should never return
}

static inline pid_t getpid(void)
{
    return syscall0(101); // SYS_GETPID
}

static inline int yield(void)
{
    return syscall0(102); // SYS_YIELD
}

static inline int sleep(uint32_t ticks)
{
    return syscall1(103, ticks); // SYS_SLEEP
}

/**
 * @brief Drop privileges to user mode
 * 
 * This is a one-way operation - once privileges are dropped,
 * they cannot be regained. Used for security sandboxing.
 * 
 * @return 0 on success
 */
static inline int drop_privileges(void)
{
    return syscall0(104); // SYS_DROP_PRIVILEGES
}

/**
 * @brief Check if running in privileged mode
 * 
 * @return 1 if privileged, 0 if user mode
 */
static inline int is_privileged(void)
{
    return syscall0(105); // SYS_GET_PRIVILEGE
}

// Memory management
static inline void* sbrk(int32_t increment)
{
    return (void*)syscall1(110, increment); // SYS_SBRK
}

static inline void* malloc(size_t size)
{
    return (void*)syscall1(111, size); // SYS_MALLOC
}

static inline int free(void* ptr)
{
    return syscall1(112, (uint32_t)ptr); // SYS_FREE
}

// I/O operations
static inline int open(const char* path, uint32_t flags)
{
    return syscall2(120, (uint32_t)path, flags); // SYS_OPEN
}

static inline int close(int fd)
{
    return syscall1(121, fd); // SYS_CLOSE
}

static inline int read(int fd, void* buffer, size_t count)
{
    return syscall3(122, fd, (uint32_t)buffer, count); // SYS_READ
}

static inline int write(int fd, const void* buffer, size_t count)
{
    return syscall3(123, fd, (uint32_t)buffer, count); // SYS_WRITE
}

static inline int ioctl(int fd, uint32_t cmd, void* arg)
{
    return syscall3(124, fd, cmd, (uint32_t)arg); // SYS_IOCTL
}

// Interrupt/Event handling
static inline int wait_irq(uint32_t irq_number, struct irq_event* event, uint32_t timeout)
{
    return syscall3(130, irq_number, (uint32_t)event, timeout); // SYS_WAIT_IRQ
}

static inline int register_irq(uint32_t irq_number, uint8_t priority)
{
    return syscall2(131, irq_number, priority); // SYS_REGISTER_IRQ
}

static inline int unregister_irq(uint32_t irq_number)
{
    return syscall1(132, irq_number); // SYS_UNREGISTER_IRQ
}

static inline int poll_irq(uint32_t irq_number, struct irq_event* event)
{
    return syscall2(133, irq_number, (uint32_t)event); // SYS_POLL_IRQ
}

// Time
static inline uint32_t get_tick(void)
{
    return syscall0(150); // SYS_GET_TICK
}

static inline uint32_t get_time(void)
{
    return syscall0(151); // SYS_GET_TIME
}

// Debug/Info
static inline int debug_print(const char* message, size_t length)
{
    return syscall2(160, (uint32_t)message, length); // SYS_DEBUG_PRINT
}

static inline int get_process_info(struct process_info* info)
{
    return syscall1(161, (uint32_t)info); // SYS_GET_PROCESS_INFO
}

static inline int get_system_info(struct system_info* info)
{
    return syscall1(162, (uint32_t)info); // SYS_GET_SYSTEM_INFO
}

// ===== Futex (Fast Userspace Mutex) Operations =====

/**
 * @brief Wait on futex if value matches
 * 
 * If *uaddr == expected_val, the calling task is suspended until:
 * - Another task calls futex_wake() on the same address
 * - The timeout expires
 * - A spurious wakeup occurs (caller should recheck condition)
 * 
 * @param uaddr     Pointer to the futex variable (must be word-aligned)
 * @param expected_val Expected value - only wait if *uaddr == expected_val
 * @param timeout   Timeout in system ticks (TIMEOUT_INFINITE for infinite wait)
 * @return 0 on success, -ETIMEDOUT on timeout, -EAGAIN if value mismatch
 */
static inline int futex_wait(volatile uint32_t* uaddr, uint32_t expected_val, uint32_t timeout)
{
    return syscall3(180, (uint32_t)uaddr, expected_val, timeout); // SYS_FUTEX_WAIT
}

/**
 * @brief Wake tasks waiting on a futex
 * 
 * Wakes up to num_wake tasks that are waiting on the futex at uaddr.
 * 
 * @param uaddr     Pointer to the futex variable
 * @param num_wake  Maximum number of waiters to wake (1 for mutex, INT32_MAX for broadcast)
 * @return Number of tasks actually woken (>= 0)
 */
static inline int futex_wake(volatile uint32_t* uaddr, uint32_t num_wake)
{
    return syscall2(181, (uint32_t)uaddr, num_wake); // SYS_FUTEX_WAKE
}

/**
 * @brief Requeue waiters from one futex to another
 * 
 * Atomically wakes num_wake waiters and moves num_requeue waiters
 * to a different futex address. Used for efficient condition variable implementation.
 * 
 * @param uaddr       Source futex address
 * @param uaddr2      Destination futex address
 * @param num_wake    Number of waiters to wake
 * @param num_requeue Number of waiters to move to uaddr2
 * @return Number of affected waiters (woken + requeued)
 */
static inline int futex_requeue(volatile uint32_t* uaddr, volatile uint32_t* uaddr2,
                                 uint32_t num_wake, uint32_t num_requeue)
{
    return syscall4(182, (uint32_t)uaddr, (uint32_t)uaddr2, num_wake, num_requeue); // SYS_FUTEX_REQUEUE
}

// ===== Futex-based Synchronization Primitives =====

// Futex mutex state values
#define FUTEX_UNLOCKED      0
#define FUTEX_LOCKED        1
#define FUTEX_CONTENDED     2

/**
 * @brief Lock a futex-based mutex
 * 
 * Fast path uses atomic compare-exchange in userspace.
 * Slow path falls back to kernel wait.
 * 
 * Usage:
 *   volatile uint32_t my_mutex = FUTEX_UNLOCKED;
 *   futex_lock(&my_mutex);
 *   // critical section
 *   futex_unlock(&my_mutex);
 * 
 * @param mutex Pointer to the mutex variable (initialize to FUTEX_UNLOCKED)
 */
static inline void futex_lock(volatile uint32_t* mutex)
{
    uint32_t expected = FUTEX_UNLOCKED;
    
    // Fast path: try atomic CAS 0 -> 1
    if (__atomic_compare_exchange_n((uint32_t*)mutex, &expected, FUTEX_LOCKED,
                                      0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    {
        return; // Got the lock!
    }
    
    // Slow path: contention detected
    do
    {
        // Mark as contended (has waiters)
        if (expected == FUTEX_LOCKED)
        {
            __atomic_compare_exchange_n((uint32_t*)mutex, &expected, FUTEX_CONTENDED,
                                          0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
        }
        
        // Wait on futex (only if state == FUTEX_CONTENDED)
        if (__atomic_load_n((uint32_t*)mutex, __ATOMIC_RELAXED) == FUTEX_CONTENDED)
        {
            futex_wait(mutex, FUTEX_CONTENDED, TIMEOUT_INFINITE);
        }
        
        // Try to acquire again
        expected = FUTEX_UNLOCKED;
    } while (!__atomic_compare_exchange_n((uint32_t*)mutex, &expected, FUTEX_CONTENDED,
                                            0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED));
}

/**
 * @brief Unlock a futex-based mutex
 * 
 * @param mutex Pointer to the mutex variable
 */
static inline void futex_unlock(volatile uint32_t* mutex)
{
    uint32_t prev = __atomic_exchange_n((uint32_t*)mutex, FUTEX_UNLOCKED, __ATOMIC_RELEASE);
    
    // If there were waiters, wake one
    if (prev == FUTEX_CONTENDED)
    {
        futex_wake(mutex, 1);
    }
}

/**
 * @brief Try to lock a futex-based mutex without blocking
 * 
 * @param mutex Pointer to the mutex variable
 * @return 0 if lock acquired, -1 if already locked
 */
static inline int futex_trylock(volatile uint32_t* mutex)
{
    uint32_t expected = FUTEX_UNLOCKED;
    if (__atomic_compare_exchange_n((uint32_t*)mutex, &expected, FUTEX_LOCKED,
                                      0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    {
        return 0; // Success
    }
    return -1; // Already locked
}

// ===== Convenience Functions =====

// Delay in ticks (alias for sleep)
static inline int delay(uint32_t ticks)
{
    return sleep(ticks);
}

// Print string to stdout (uses write syscall)
static inline int puts(const char* str)
{
    if (!str) return -1;
    
    // Find string length
    size_t len = 0;
    while (str[len] != '\0') len++;
    
    // Write to stdout (fd=1)
    int result = write(1, str, len);
    
    // Add newline
    write(1, "\n", 1);
    
    return result;
}

// Print string without newline
static inline int print(const char* str)
{
    if (!str) return -1;
    
    size_t len = 0;
    while (str[len] != '\0') len++;
    
    return write(1, str, len);
}

#ifdef __cplusplus
}
#endif

#endif /* CRTOS_USERSPACE_H */
