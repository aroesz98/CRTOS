/*
 * kernel/main.cpp - board bring-up, then hand the machine over to the kernel.
 *
 * Everything else (drivers, services, applications) is loaded by the kernel from the SD
 * card according to the device tree.
 */
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"

extern "C" void mpu_init(void);
extern "C" void kernel_main(void) __attribute__((noreturn));

int main(void)
{
    mpu_init();             /* memory map + caches (replaces BOARD_ConfigMPU) */
    BOARD_InitBootPins();   /* console pins */
    BOARD_InitBootClocks(); /* 600 MHz core and peripheral clock roots */
    kernel_main();
}
