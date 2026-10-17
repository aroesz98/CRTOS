/*
 * startup.cpp - the kernel image's first words and its reset entry on the STM32H7S78-DK.
 *
 * At 0x70000000 (the NOR flash) the image starts with the vector table: the boot loader takes
 * the stack pointer and the entry from it, and the kernel's interrupt code copies the 16
 * system vectors into the table it builds in RAM (arch/irq.cpp; every device interrupt goes
 * through irq_dispatch there). At 0x400 follows the image header the boot loader checks
 * (board/image.h; tools/imagehdr.py writes its size and CRC after the link).
 *
 * ResetISR copies the initialised data of every RAM (DTCM, ITCM code, AXI SRAM, PSRAM) and
 * clears the zero-initialised sections, as the linker's section table lists them.
 */
#include <stdint.h>
#include <crtos/config.h>
#include "platform.h"
#include "image.h"

extern "C" {
void ResetISR(void);
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);
void IntDefaultHandler(void);
int main(void);
void __libc_init_array(void);

extern uint32_t _vStackTop[];
extern uint32_t __data_section_table[], __data_section_table_end[];
extern uint32_t __bss_section_table[], __bss_section_table_end[];
extern uint32_t g_chip_uid[3]; /* (platform.cpp) */

__attribute__((weak, section(".after_vectors"))) void NMI_Handler(void)
{
    for (;;) {
    }
}

__attribute__((weak, section(".after_vectors"))) void DebugMon_Handler(void)
{
    for (;;) {
    }
}

/* a device interrupt before the kernel's vector table is in place */
__attribute__((section(".after_vectors"))) void IntDefaultHandler(void)
{
    for (;;) {
    }
}

/* a device vector for each NVIC line of crtos/config.h */
static_assert(CONFIG_NUM_IRQS == 156, "g_pfnVectors below lists 156 device vectors");

typedef void (*vector_t)(void);

/* (const objects of C++ have internal linkage unless declared extern: irq.cpp reads this one) */
extern void (*const g_pfnVectors[16 + CONFIG_NUM_IRQS])(void);
extern const struct crtos_image_header g_image_header;

__attribute__((used, section(".isr_vector"))) void (*const g_pfnVectors[16 + CONFIG_NUM_IRQS])(void) = {
    (vector_t)_vStackTop, ResetISR, NMI_Handler, HardFault_Handler, MemManage_Handler, BusFault_Handler,
    UsageFault_Handler, 0, 0, 0, 0, SVC_Handler, DebugMon_Handler, 0, PendSV_Handler, SysTick_Handler,
#define D8 IntDefaultHandler, IntDefaultHandler, IntDefaultHandler, IntDefaultHandler, \
           IntDefaultHandler, IntDefaultHandler, IntDefaultHandler, IntDefaultHandler,
    D8 D8 D8 D8 D8 D8 D8 D8 D8 D8 D8 D8 D8 D8 D8 D8 D8 D8 D8
    IntDefaultHandler, IntDefaultHandler, IntDefaultHandler, IntDefaultHandler,
#undef D8
};

/* filled in after the link by tools/imagehdr.py (size, CRC-32) */
__attribute__((used, section(".crtos_image_header"))) const struct crtos_image_header g_image_header = {
    CRTOS_IMAGE_MAGIC, sizeof(struct crtos_image_header), 0, 0, "built " __DATE__ " " __TIME__,
};

__attribute__((section(".after_vectors.init"), noreturn)) static void reset_c(void)
{
    /* the FPU (the kernel and its modules use the hard-float ABI) */
    SCB->CPACR |= (3UL << 20) | (3UL << 22);
    __DSB();
    __ISB();

    uint32_t *t = __data_section_table;
    while (t < __data_section_table_end) {
        const uint32_t *src = (const uint32_t *)t[0];
        uint32_t *dst = (uint32_t *)t[1];
        for (uint32_t n = t[2] / 4u; n; n--)
            *dst++ = *src++;
        t += 3;
    }
    t = __bss_section_table;
    while (t < __bss_section_table_end) {
        uint32_t *dst = (uint32_t *)t[0];
        for (uint32_t n = t[1] / 4u; n; n--)
            *dst++ = 0;
        t += 2;
    }
    /* the unique ID, while the system flash can be read (the kernel's MPU map has no region
     * for it) */
    for (int i = 0; i < 3; i++)
        g_chip_uid[i] = ((const volatile uint32_t *)UID_BASE)[i];
    SCB->VTOR = (uint32_t)g_pfnVectors;
    __DSB();
    __ISB();
    __libc_init_array();
    __asm volatile("cpsie i");
    main();
    for (;;) {
    }
}

__attribute__((naked, section(".after_vectors.reset"))) void ResetISR(void)
{
    __asm volatile("cpsid i\n"
                   "ldr r0, =_vStackTop\n"
                   "msr msp, r0\n"
                   "b %c0\n" ::"i"(reset_c));
}
}
