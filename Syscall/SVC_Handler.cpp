/*
 * SVC_Handler.cpp - CRTOS SVC (Supervisor Call) Exception Handler
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
 * SVC (Supervisor Call) exception handler for system calls.
 * 
 * ARM Cortex-M SVC calling convention:
 * - User code executes: SVC #n
 * - CPU automatically saves context (r0-r3, r12, LR, PC, xPSR) to stack
 * - SVC_Handler is called
 * - We extract syscall number from SVC instruction
 * - Arguments are in r0-r3
 * - Return value goes to r0
 * - On return, CPU restores context
 *
 * Stack frame after SVC exception:
 * SP+0:  r0  <- arg0
 * SP+4:  r1  <- arg1
 * SP+8:  r2  <- arg2
 * SP+12: r3  <- arg3
 * SP+16: r12
 * SP+20: LR
 * SP+24: PC (return address)
 * SP+28: xPSR
 */

#include "SystemCall.hpp"
#include <cstdint>
#include "../kernel.h"  // For SVC_Commands enum

// External assembly function that handles the low-level SVC entry
// Defined at the bottom of this file
extern "C" void SVC_Handler(void);

// External CRTOS command handler (for backward compatibility)
extern "C" void SVC_Handle_Subprocess(uint32_t *command);

namespace CRTOS
{
namespace Syscall
{
    // Forward declaration - implemented in Syscall_Dispatcher.cpp
    int32_t SVC_Dispatch(uint32_t syscallNum, uint32_t arg0, uint32_t arg1, 
                         uint32_t arg2, uint32_t arg3);
}
}

// Command numbers from kernel.h
#define COMMAND_START_SCHEDULER 0u
#define COMMAND_MODULE_ALLOC_SHARED 28u  // Last old-style command number

//
// C++ callable handler - receives extracted parameters
//
extern "C" int32_t SVC_Handler_Main(uint32_t* svc_args)
{
    // svc_args points to the stack frame:
    // svc_args[0] = r0 (arg0)
    // svc_args[1] = r1 (arg1)
    // svc_args[2] = r2 (arg2)
    // svc_args[3] = r3 (arg3)
    // svc_args[6] = PC (return address, points to instruction after SVC)
    
    // Extract syscall/command number from SVC instruction
    // The SVC instruction is at PC-2 (Thumb mode, 16-bit instruction)
    uint8_t* svc_instruction = (uint8_t*)(svc_args[6] - 2);
    uint32_t svc_number = svc_instruction[0]; // Lower byte contains immediate value
    
    // Determine if this is an old CRTOS command or new syscall
    // Old CRTOS commands: 0-28 (defined in kernel.h)
    // New syscalls: 100-200 (defined in SystemCall.hpp)
    // For backward compatibility:
    // - Numbers 0-28: Old CRTOS commands → SVC_Handle_Subprocess
    // - Numbers >= 100: New syscalls → SVC_Dispatch
    // - Numbers 29-99: Reserved/Invalid
    
    if (svc_number <= COMMAND_MODULE_ALLOC_SHARED)
    {
        // Old CRTOS command - use legacy handler
        // This handler expects the full stack frame pointer
        SVC_Handle_Subprocess(svc_args);
        return 0; // Result already modified by SVC_Handle_Subprocess
    }
    else if (svc_number >= 100)
    {
        // New syscall system
        // Extract arguments from registers (already on stack)
        uint32_t arg0 = svc_args[0];
        uint32_t arg1 = svc_args[1];
        uint32_t arg2 = svc_args[2];
        uint32_t arg3 = svc_args[3];
        
        // Dispatch to the appropriate syscall handler
        int32_t result = CRTOS::Syscall::SVC_Dispatch(svc_number, arg0, arg1, arg2, arg3);
        
        // Store result back to r0 on the stack
        // This will be restored when exception returns
        svc_args[0] = (uint32_t)result;
        
        return result;
    }
    else
    {
        // Invalid syscall number (24-99 reserved range)
        svc_args[0] = (uint32_t)CRTOS::Syscall::SyscallError::ERR_NOSYS;
        return -1;
    }
}

//
// Low-level SVC exception handler (assembly)
// This extracts the stack pointer and calls the C++ handler
//
asm(
    ".syntax unified\n"
    ".cpu cortex-m7\n"
    ".thumb\n"
    ".global SVC_Handler\n"
    ".type SVC_Handler, %function\n"
    "SVC_Handler:\n"
    "    /* Determine which stack pointer was in use (MSP or PSP) */\n"
    "    tst lr, #4              @ Test bit 2 of LR (EXC_RETURN)\n"
    "    ite eq                  @ If-Then-Else\n"
    "    mrseq r0, msp           @ If bit 2 = 0, use MSP (kernel mode)\n"
    "    mrsne r0, psp           @ If bit 2 = 1, use PSP (user mode)\n"
    "    \n"
    "    /* r0 now points to the stack frame with r0-r3, r12, lr, pc, xpsr */\n"
    "    /* Call the C handler */\n"
    "    push {lr}               @ Save LR (EXC_RETURN value)\n"
    "    bl SVC_Handler_Main     @ Call C++ handler (arg in r0)\n"
    "    pop {pc}                @ Return from exception (restores LR to PC)\n"
    "    \n"
    ".size SVC_Handler, .-SVC_Handler\n"
);
