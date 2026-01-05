// ****************************************************************************
// semihost_hardfault.c
//                - Provides hard fault handler to allow semihosting code not
//                  to hang application when debugger not connected.
//
// ****************************************************************************
// Copyright 2017-2025 NXP
// All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause
// ****************************************************************************
//
//                       ===== DESCRIPTION =====
//
// One of the issues with applications that make use of semihosting operations
// (such as printf calls) is that the code will not execute correctly when the
// debugger is not connected. Generally this will show up with the application
// appearing to just hang. This may include the application running from reset
// or powering up the board (with the application already in FLASH), and also
// as the application failing to continue to execute after a debug session is
// terminated.
//
// The problem here is that the "bottom layer" of the semihosted variants of
// the C library, semihosting is implemented by a "BKPT 0xAB" instruction.
// When the debug tools are not connected, this instruction triggers a hard
// fault - and the default hard fault handler within an application will
// typically just contains an infinite loop - causing the application to
// appear to have hang when no debugger is connected.
//
// The below code provides an example hard fault handler which instead looks
// to see what the instruction that caused the hard fault was - and if it
// was a "BKPT 0xAB", then it instead returns back to the user application.
//
// In most cases this will allow applications containing semihosting
// operations to execute (to some degree) when the debugger is not connected.
//
// == NOTE ==
//
// Correct execution of the application containing semihosted operations
// which are vectored onto this hard fault handler cannot be guaranteed. This
// is because the handler may not return data or return codes that the higher
// level C library code or application code expects. This hard fault handler
// is meant as a development aid, and it is not recommended to leave
// semihosted code in a production build of your application!
//
// ****************************************************************************

#include <stdio.h>
#include <stdint.h>

// Allow handler to be removed by setting a define (via command line)
#if !defined (__SEMIHOST_HARDFAULT_DISABLE)

// Helper function to print hard fault information
void HardFault_PrintInfo(uint32_t *stack_frame, uint32_t lr_value)
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
    
    volatile uint32_t *HFSR = (uint32_t *)0xE000ED2C;  // HardFault Status Register
    volatile uint32_t *CFSR = (uint32_t *)0xE000ED28;  // Configurable Fault Status Register
    volatile uint32_t *MMFAR = (uint32_t *)0xE000ED34; // MemManage Fault Address Register
    volatile uint32_t *BFAR = (uint32_t *)0xE000ED38;  // BusFault Address Register
    
    uint32_t hfsr = *HFSR;
    uint32_t cfsr = *CFSR;
    uint32_t mmfar = *MMFAR;
    uint32_t bfar = *BFAR;
    
    printf("\r\n\r\n=== HARDFAULT ===\r\n");
    printf("HFSR: 0x%08X\r\n", (unsigned int)hfsr);
    printf("CFSR: 0x%08X\r\n", (unsigned int)cfsr);
    
    // Decode HFSR
    if (hfsr & (1 << 30)) printf("  FORCED - Escalated fault\r\n");
    if (hfsr & (1 << 1)) printf("  VECTTBL - Vector table read fault\r\n");
    
    // Decode CFSR sub-registers
    uint8_t ufsr = (cfsr >> 16) & 0xFF;  // UsageFault
    uint8_t bfsr = (cfsr >> 8) & 0xFF;   // BusFault
    uint8_t mmfsr = cfsr & 0xFF;          // MemManage
    
    if (ufsr) {
        printf("UsageFault (UFSR=0x%02X):\r\n", (unsigned int)ufsr);
        if (ufsr & (1 << 9)) printf("  DIVBYZERO\r\n");
        if (ufsr & (1 << 8)) printf("  UNALIGNED\r\n");
        if (ufsr & (1 << 3)) printf("  NOCP - No coprocessor\r\n");
        if (ufsr & (1 << 2)) printf("  INVPC - Invalid PC load\r\n");
        if (ufsr & (1 << 1)) printf("  INVSTATE - Invalid state\r\n");
        if (ufsr & (1 << 0)) printf("  UNDEFINSTR - Undefined instruction\r\n");
    }
    
    if (bfsr) {
        printf("BusFault (BFSR=0x%02X):\r\n", (unsigned int)bfsr);
        if (bfsr & (1 << 7)) printf("  BFARVALID - BFAR valid\r\n");
        if (bfsr & (1 << 4)) printf("  STKERR - Stacking error\r\n");
        if (bfsr & (1 << 3)) printf("  UNSTKERR - Unstacking error\r\n");
        if (bfsr & (1 << 1)) printf("  DACCVIOL - Data access violation\r\n");
        if (bfsr & (1 << 0)) printf("  IBUSERR - Instruction bus error\r\n");
        if (bfsr & (1 << 7)) printf("  BFAR: 0x%08X\r\n", (unsigned int)bfar);
    }
    
    if (mmfsr) {
        printf("MemManage (MMFSR=0x%02X):\r\n", (unsigned int)mmfsr);
        if (mmfsr & (1 << 7)) printf("  MMARVALID - MMFAR valid\r\n");
        if (mmfsr & (1 << 4)) printf("  MSTKERR - Stacking error\r\n");
        if (mmfsr & (1 << 3)) printf("  MUNSTKERR - Unstacking error\r\n");
        if (mmfsr & (1 << 1)) printf("  DACCVIOL - Data access violation\r\n");
        if (mmfsr & (1 << 0)) printf("  IACCVIOL - Instruction access violation\r\n");
        if (mmfsr & (1 << 7)) printf("  MMFAR: 0x%08X\r\n", (unsigned int)mmfar);
    }
    
    printf("\r\nStack Frame:\r\n");
    printf("  R0:  0x%08X\r\n", (unsigned int)r0);
    printf("  R1:  0x%08X\r\n", (unsigned int)r1);
    printf("  R2:  0x%08X\r\n", (unsigned int)r2);
    printf("  R3:  0x%08X\r\n", (unsigned int)r3);
    printf("  R12: 0x%08X\r\n", (unsigned int)r12);
    printf("  LR:  0x%08X (from stack)\r\n", (unsigned int)lr);
    printf("  PC:  0x%08X (Fault occurred here)\r\n", (unsigned int)pc);
    printf("  PSR: 0x%08X\r\n", (unsigned int)psr);
    printf("  EXC_LR: 0x%08X\r\n", (unsigned int)lr_value);
    printf("\r\n=================\r\n\r\n");
}

__attribute__((naked))
void HardFault_Handler(void){
    __asm(  ".syntax unified\n"
        // Check which stack is in use
            "MOVS   R0, #4           \n"
            "MOV    R1, LR           \n"
            "TST    R0, R1           \n"
            "BEQ    _MSP             \n"
            "MRS    R0, PSP          \n"
            "B  _process             \n"
            "_MSP:                   \n"
            "MRS    R0, MSP          \n"
        // Load the instruction that triggered hard fault
        "_process:                   \n"
            "LDR    R1,[R0,#24]      \n"
            "LDRH   R2,[r1]          \n"
        // Semihosting instruction is "BKPT 0xAB" (0xBEAB)
            "LDR    R3,=0xBEAB       \n"
            "CMP    R2,R3            \n"
            "BEQ    _semihost_return \n"
        // Wasn't semihosting instruction - print info and hang
            "MOV    R1, LR           \n"  // Save LR as second parameter
            "PUSH   {R0, R1, LR}     \n"  // Save registers
            "BL     HardFault_PrintInfo\n"
            "POP    {R0, R1, LR}     \n"
            "B .                     \n"
        // Was semihosting instruction, so adjust location to
        // return to by 1 instruction (2 bytes), then exit function
            "_semihost_return:       \n"
            "ADDS   R1,#2            \n"
            "STR    R1,[R0,#24]      \n"
        // Set a return value from semihosting operation.
        // 0 is slightly arbitrary, but appears to allow most
        // C Library IO functions sitting on top of semihosting to
        // continue to operate to some degree
        // Return a positive value (32) for SYS_OPEN only
            "LDR    R1,[ R0,#0 ]     \n"  // R0 is at location 0 on stack
            "CMP    R1, #1           \n"  // 0x01=SYS_OPEN
            "BEQ    _non_zero_ret    \n"
            "MOVS   R1,#0            \n"
            "B      _sys_ret         \n"
            "_non_zero_ret:          \n"
            "MOVS   R1,#32           \n"
            "_sys_ret:               \n"
            "STR    R1,[ R0,#0 ]     \n" // R0 is at location 0 on stack
        // Return from hard fault handler to application
            "BX     LR               \n"
        ".syntax divided\n") ;
}

#endif

