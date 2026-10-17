/*
 * kernel/main.cpp - board bring-up, then hand the machine over to the kernel.
 *
 * Everything else (drivers, services, applications) is loaded by the kernel from the SD card
 * according to the device tree.
 */
extern "C" void mpu_init(void);
extern "C" void platform_early_init(void);
extern "C" void kernel_main(void) __attribute__((noreturn));

int main(void)
{
    mpu_init();             /* memory map (the board's: platform_mpu_map) + caches */
    platform_early_init();  /* what the board needs before the kernel starts: pins, clocks */
    kernel_main();
}
