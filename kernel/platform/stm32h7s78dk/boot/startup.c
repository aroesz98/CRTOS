/*
 * boot/startup.c - vector table and reset entry of the boot loader.
 *
 * The boot loader runs with interrupts off; a fault is reported on the console and the
 * board stops there (the debugger can still connect).
 */
#include <stdint.h>
#include "boot.h"

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
int main(void);

void Reset_Handler(void)
{
    /* the FPU: the HAL is built for the hard-float ABI */
    SCB->CPACR |= (3UL << 20) | (3UL << 22);
    __DSB();
    __ISB();
    for (uint32_t *s = _sidata, *d = _sdata; d < _edata;)
        *d++ = *s++;
    for (uint32_t *d = _sbss; d < _ebss;)
        *d++ = 0;
    main();
    for (;;) {
    }
}

static void fault(void)
{
    boot_panic("fault (CFSR %08lx HFSR %08lx MMFAR %08lx BFAR %08lx)", (unsigned long)SCB->CFSR,
               (unsigned long)SCB->HFSR, (unsigned long)SCB->MMFAR, (unsigned long)SCB->BFAR);
}

__attribute__((used, section(".isr_vector")))
static void (*const vectors[16])(void) = {
    (void (*)(void))_estack, Reset_Handler, fault, fault, fault, fault, fault,
    0, 0, 0, 0, fault, fault, 0, fault, fault,
};
