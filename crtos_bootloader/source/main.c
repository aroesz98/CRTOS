/*
 * Copyright  2017-2019 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_common.h"
#include "app.h"
#include "board.h"
#include "fsl_elcdif.h"
#include "fsl_debug_console.h"
#include "fsl_cache.h"
#include "fsl_clock.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* ANSI Color Codes */
#define ANSI_COLOR_RESET   "\033[0m"
#define ANSI_COLOR_RED     "\033[31m"
#define ANSI_COLOR_GREEN   "\033[32m"
#define ANSI_COLOR_YELLOW  "\033[33m"
#define ANSI_COLOR_BLUE    "\033[34m"
#define ANSI_COLOR_MAGENTA "\033[35m"
#define ANSI_COLOR_CYAN    "\033[36m"
#define ANSI_COLOR_WHITE   "\033[37m"
#define ANSI_COLOR_BOLD    "\033[1m"

/* Bootloader version */
#define BOOTLOADER_VERSION_MAJOR  1
#define BOOTLOADER_VERSION_MINOR  0
#define BOOTLOADER_VERSION_PATCH  0

/* Application start address in flash memory */
#define APP_START_ADDRESS       (0x60040000U)

/* Pointer to the application vector table */
#define APP_VECTOR_TABLE        ((uint32_t *)APP_START_ADDRESS)

/* Application stack pointer (first entry in vector table) */
#define APP_STACK_POINTER       (APP_VECTOR_TABLE[0])

/* Application reset handler (second entry in vector table) */
#define APP_RESET_HANDLER       (APP_VECTOR_TABLE[1])

/* Typedef for the application entry point function */
typedef void (*app_entry_t)(void);

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void JumpToApplication(uint32_t appStackPointer, uint32_t appResetHandler);
static bool ValidateApplication(uint32_t appAddress);
static void DeinitPeripherals(void);
static void PrintBootloaderBanner(void);
static void PrintSystemInfo(uint32_t appSP, uint32_t appEntry);

/*******************************************************************************
 * Code
 ******************************************************************************/

/*!
 * @brief Print CRTOS ASCII logo and bootloader banner
 */
static void PrintBootloaderBanner(void)
{
    /* Clear screen */
    PRINTF("\033[2J\033[H");
    
    /* CRTOS ASCII Art Logo */
    PRINTF(ANSI_COLOR_CYAN ANSI_COLOR_BOLD);
    PRINTF("\r\n");
    PRINTF("   ██████╗██████╗ ████████╗ ██████╗ ███████╗\r\n");
    PRINTF("  ██╔════╝██╔══██╗╚══██╔══╝██╔═══██╗██╔════╝\r\n");
    PRINTF("  ██║     ██████╔╝   ██║   ██║   ██║███████╗\r\n");
    PRINTF("  ██║     ██╔══██╗   ██║   ██║   ██║╚════██║\r\n");
    PRINTF("  ╚██████╗██║  ██║   ██║   ╚██████╔╝███████║\r\n");
    PRINTF("   ╚═════╝╚═╝  ╚═╝   ╚═╝    ╚═════╝ ╚══════╝\r\n");
    PRINTF(ANSI_COLOR_RESET);
    PRINTF("\r\n");
    
    /* Bootloader info */
    PRINTF(ANSI_COLOR_WHITE ANSI_COLOR_BOLD);
    PRINTF("  ╔═══════════════════════════════════════════╗\r\n");
    PRINTF("  ║     " ANSI_COLOR_YELLOW "CRTOS Bootloader v%d.%d.%d" ANSI_COLOR_WHITE "               ║\r\n", 
           BOOTLOADER_VERSION_MAJOR, BOOTLOADER_VERSION_MINOR, BOOTLOADER_VERSION_PATCH);
    PRINTF("  ║     " ANSI_COLOR_CYAN "Cortex Real-Time Operating System" ANSI_COLOR_WHITE "     ║\r\n");
    PRINTF("  ╚═══════════════════════════════════════════╝\r\n");
    PRINTF(ANSI_COLOR_RESET);
    PRINTF("\r\n");
}

/*!
 * @brief Print system information before jumping to application
 */
static void PrintSystemInfo(uint32_t appSP, uint32_t appEntry)
{
    uint32_t cpuFreqHz = CLOCK_GetFreq(kCLOCK_CpuClk);
    uint32_t cpuFreqMHz = cpuFreqHz / 1000000U;
    
    PRINTF(ANSI_COLOR_GREEN "  [INFO]" ANSI_COLOR_RESET " System Information:\r\n");
    PRINTF("  ├─────────────────────────────────────────\r\n");
    PRINTF("  │ " ANSI_COLOR_YELLOW "CPU Frequency:" ANSI_COLOR_RESET "    %u MHz\r\n", cpuFreqMHz);
    PRINTF("  │ " ANSI_COLOR_YELLOW "App Address:" ANSI_COLOR_RESET "      0x%08X\r\n", (uint32_t)APP_START_ADDRESS);
    PRINTF("  │ " ANSI_COLOR_YELLOW "Stack Pointer:" ANSI_COLOR_RESET "    0x%08X\r\n", appSP);
    PRINTF("  │ " ANSI_COLOR_YELLOW "Reset Handler:" ANSI_COLOR_RESET "    0x%08X\r\n", appEntry);
    PRINTF("  │ " ANSI_COLOR_YELLOW "Vector Table:" ANSI_COLOR_RESET "     0x%08X\r\n", (uint32_t)APP_START_ADDRESS);
    PRINTF("  └─────────────────────────────────────────\r\n");
    PRINTF("\r\n");
    
    PRINTF(ANSI_COLOR_GREEN "  [BOOT]" ANSI_COLOR_RESET " Jumping to application...\r\n");
    PRINTF("\r\n");
    
    // /* Small delay to ensure UART transmission completes */
    // SDK_DelayAtLeastUs(50000, CLOCK_GetFreq(kCLOCK_CpuClk));
}

/*!
 * @brief Validate if a valid application exists at the given address
 * @param appAddress The start address of the application
 * @return true if valid application found, false otherwise
 */
static bool ValidateApplication(uint32_t appAddress)
{
    uint32_t *vectorTable = (uint32_t *)appAddress;
    uint32_t stackPointer = vectorTable[0];
    uint32_t resetHandler = vectorTable[1];

    /* Check if stack pointer is within valid RAM range (DTCM, OCRAM, or SDRAM) */
    /* DTCM: 0x20000000 - 0x2007FFFF (512KB) */
    /* OCRAM: 0x20200000 - 0x2027FFFF (512KB) */
    /* SDRAM: 0x80000000 - 0x81FFFFFF (32MB typical) */
    bool validSP = ((stackPointer >= 0x20000000U) && (stackPointer <= 0x2007FFFFU)) ||
                   ((stackPointer >= 0x20200000U) && (stackPointer <= 0x2027FFFFU)) ||
                   ((stackPointer >= 0x80000000U) && (stackPointer <= 0x81FFFFFFU));

    /* Check if reset handler is within valid flash/execution range */
    /* FlexSPI (external flash): 0x60000000 - 0x7FFFFFFF */
    /* SDRAM: 0x80000000 - 0x81FFFFFF */
    bool validRH = ((resetHandler >= 0x60000000U) && (resetHandler <= 0x7FFFFFFFU)) ||
                   ((resetHandler >= 0x80000000U) && (resetHandler <= 0x81FFFFFFU));

    return (validSP && validRH);
}

/*!
 * @brief Deinitialize peripherals before jumping to application
 */
static void DeinitPeripherals(void)
{
    /* Disable all interrupts */
    __disable_irq();

    /* Clear pending interrupts */
    for (int i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    /* Disable SysTick and clear its exception pending bit */
    SysTick->CTRL = 0U;
    SCB->ICSR |= SCB_ICSR_PENDSTCLR_Msk;

//    /* Disable D-Cache first (this also cleans it) */
//    SCB_DisableDCache();
//
//    /* Invalidate I-Cache */
//    SCB_DisableICache();
//
//    /* Disable MPU - application will reconfigure it */
//    ARM_MPU_Disable();
    
    __enable_irq();

    /* Data Synchronization Barrier to ensure all operations complete */
    __DSB();

    /* Instruction Synchronization Barrier */
    __ISB();
}

/*!
 * @brief Jump to the application
 * @param appStackPointer The application's initial stack pointer
 * @param appResetHandler The application's reset handler address
 */
__attribute__((naked)) static void JumpToApplication(uint32_t appStackPointer, uint32_t appResetHandler)
{
    __asm volatile (
        "MSR MSP, %0\n"      /* Set Main Stack Pointer to application's SP */
        "DSB\n"              /* Data Synchronization Barrier */
        "ISB\n"              /* Instruction Synchronization Barrier */
        "BX %1\n"            /* Branch to application reset handler */
        :
        : "r" (appStackPointer), "r" (appResetHandler)
        : "memory"
    );
}

/*!
 * @brief Main function - Bootloader entry point
 */
int main(void)
{
    BOARD_InitHardware();
    BOARD_InitDebugConsole();
    
    /* Print bootloader banner with CRTOS logo */
    PrintBootloaderBanner();

    /* Validate that a proper application exists at the target address */
    if (ValidateApplication(APP_START_ADDRESS))
    {
        /* Get application stack pointer and reset handler */
        uint32_t appSP = APP_STACK_POINTER;
        uint32_t appEntry = APP_RESET_HANDLER;
        
        /* Print system information */
        PrintSystemInfo(appSP, appEntry);

        /* Deinitialize peripherals and prepare for jump */
        DeinitPeripherals();

        /* Set Vector Table Offset Register to application's vector table */
        SCB->VTOR = APP_START_ADDRESS;

        /* Jump to application - this function never returns */
        JumpToApplication(appSP, appEntry);
    }
    else
    {
        /* No valid application found - print error */
        PRINTF(ANSI_COLOR_RED "  [ERROR]" ANSI_COLOR_RESET " No valid application found at 0x%08lX\r\n", 
               (uint32_t)APP_START_ADDRESS);
        PRINTF(ANSI_COLOR_YELLOW "  [WAIT]" ANSI_COLOR_RESET " Staying in bootloader mode...\r\n");
    }

    /* If we reach here, no valid application was found */
    /* Stay in bootloader mode */
    while (1)
    {
        __asm("nop");
    }
}
