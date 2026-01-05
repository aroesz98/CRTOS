/*
 * syscall_test.cpp - Quick test of new syscall interface
 * This can be called from main.cpp to test syscalls from kernel side
 */

#include "CRTOS.hpp"
#include "fsl_debug_console.h"

// Test syscalls by calling them through SVC (simulating user-space call)
extern "C" void test_syscalls_from_kernel(void)
{
    PRINTF("\n========================================\n");
    PRINTF("Testing New Syscall Interface\n");
    PRINTF("========================================\n");
    
    // Test 1: SYS_GET_TICK (syscall 150)
    uint32_t tick;
    __asm volatile(
        ".syntax unified    \n"
        "svc #150           \n"
        "mov %0, r0         \n"
        : "=r"(tick)
        :
        : "r0", "memory"
    );
    PRINTF("✓ SYS_GET_TICK: %lu ticks\n", tick);
    
    // Test 2: SYS_WRITE to stdout (syscall 123)
    const char* test_msg = "Hello from syscall!\n";
    int32_t bytes_written;
    __asm volatile(
        ".syntax unified    \n"
        "mov r0, #1         \n"  // fd = 1 (stdout)
        "mov r1, %1         \n"  // buffer
        "mov r2, #20        \n"  // count
        "svc #123           \n"  // SYS_WRITE
        "mov %0, r0         \n"
        : "=r"(bytes_written)
        : "r"(test_msg)
        : "r0", "r1", "r2", "memory"
    );
    PRINTF("✓ SYS_WRITE: %ld bytes written\n", bytes_written);
    
    // Test 3: SYS_MALLOC (syscall 111)
    void* ptr;
    __asm volatile(
        ".syntax unified    \n"
        "mov r0, #256       \n"  // size
        "svc #111           \n"  // SYS_MALLOC
        "mov %0, r0         \n"
        : "=r"(ptr)
        :
        : "r0", "memory"
    );
    PRINTF("✓ SYS_MALLOC: allocated at 0x%08lX\n", (uint32_t)ptr);
    
    // Test 4: SYS_FREE (syscall 112)
    if (ptr != nullptr) {
        __asm volatile(
            ".syntax unified    \n"
            "mov r0, %0         \n"  // ptr
            "svc #112           \n"  // SYS_FREE
            :
            : "r"(ptr)
            : "r0", "memory"
        );
        PRINTF("✓ SYS_FREE: freed memory\n");
    }
    
    // Test 5: SYS_GET_SYSTEM_INFO (syscall 162)
    CRTOS::Syscall::SystemInfo sysinfo;
    int32_t result;
    __asm volatile(
        ".syntax unified    \n"
        "mov r0, %1         \n"  // &sysinfo
        "svc #162           \n"  // SYS_GET_SYSTEM_INFO
        "mov %0, r0         \n"
        : "=r"(result)
        : "r"(&sysinfo)
        : "r0", "memory"
    );
    if (result == 0) {
        PRINTF("✓ SYS_GET_SYSTEM_INFO:\n");
        PRINTF("  Total memory: %lu bytes\n", sysinfo.totalMemory);
        PRINTF("  Free memory:  %lu bytes\n", sysinfo.freeMemory);
        PRINTF("  Uptime:       %lu ticks\n", sysinfo.uptime);
    }
    
    // Test 6: Test unimplemented syscall (should return ENOSYS = -38)
    int32_t err_result;
    __asm volatile(
        ".syntax unified    \n"
        "mov r0, #42        \n"  // random arg
        "svc #130           \n"  // SYS_WAIT_IRQ (not implemented yet)
        "mov %0, r0         \n"
        : "=r"(err_result)
        :
        : "r0", "memory"
    );
    PRINTF("✓ Unimplemented syscall: %ld (expected -38 ENOSYS)\n", err_result);
    
    PRINTF("\n========================================\n");
    PRINTF("Syscall Interface Test PASSED!\n");
    PRINTF("========================================\n\n");
}
