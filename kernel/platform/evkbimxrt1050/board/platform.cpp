/*
 * platform.cpp - the MIMXRT1050-EVKB as the generic kernel sees it (kernel.h, platform_*):
 * memory map, start-up of pins and clocks, reset cause, the safe mode button.
 */
#include "kernel.h"
#include "platform.h"
#include <crtos/device.h>
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_clock.h"
#include "fsl_gpio.h"
#include "fsl_iomuxc.h"
#include <string.h>

const char platform_cpu_name[] = "i.MX RT1052";
const char platform_sd_compatible[] = "fsl,imxrt1050-usdhc";
const uintptr_t platform_sd_base = (uintptr_t)USDHC1_BASE;
const char platform_console_compatible[] = "fsl,imxrt1050-lpuart";
const char platform_safe_mode_button[] = "SW8";

const struct mpu_map_entry platform_mpu_map[] = {
    { 0x00000000u, 17, MPU_KIND_TCM_CODE },         /* ITCM 128K */
    { 0x20000000u, 17, MPU_KIND_TCM_DATA },         /* DTCM 128K */
    { 0x20200000u, 18, MPU_KIND_RAM },              /* OCRAM 256K */
    { 0x80000000u, 25, MPU_KIND_RAM },              /* SDRAM 32M */
    { 0x81E00000u, 21, MPU_KIND_RAM_NOCACHE },      /* its last 2M, not cached */
    /* XIP flash 64M: the kernel runs from it, programs from the file system /flash0 on it
     * (their code in place; the kernel's image is readable but holds nothing secret) */
    { 0x60000000u, 26, MPU_KIND_FLASH },
    { 0x40000000u, 22, MPU_KIND_DEVICE },           /* peripherals 4M */
    { 0, 0, 0 },
};

extern "C" uint8_t __top_SRAM_ITC[];

uintptr_t platform_itcm_top(void)
{
    return (uintptr_t)__top_SRAM_ITC; /* the FlexRAM split is fixed (start-up code) */
}

void platform_early_init(void)
{
    BOARD_InitBootPins();   /* console pins */
    BOARD_InitBootClocks(); /* 600 MHz core and peripheral clock roots */
}

void platform_init(void)
{
    /* The idle task sleeps with WFI. CCM_CLPCR resets to WAIT mode, where WFI gates the core
     * clock: SysTick would stop and the debugger could no longer halt the core. Stay in RUN. */
    CLOCK_SetMode(kCLOCK_ModeRun);
}

void platform_reset_cause(char *buf, size_t size)
{
    static const char *const names[] = { "power-on/pin", "lockup/sysreset", "CSU", "user", "watchdog", "JTAG",
                                         "JTAG sw", "watchdog3", "temperature" };
    uint32_t srsr = SRC->SRSR;
    int n = 0;
    for (unsigned i = 0; i < ARRAY_SIZE(names); i++)
        if (srsr & (1u << i))
            n += ksnprintf(buf + n, size - (size_t)n, "%s%s", n ? ", " : "", names[i]);
    ksnprintf(buf + n, size - (size_t)n, "%s(SRSR=%03lx)", n ? " " : "unknown ", (unsigned long)srsr);
    SRC->SRSR = srsr; /* write one to clear */
}

size_t board_unique_id(void *buf, size_t len)
{
    CLOCK_EnableClock(kCLOCK_Ocotp);
    uint32_t id[2] = { OCOTP->CFG0, OCOTP->CFG1 };
    memcpy(buf, id, len < sizeof(id) ? len : sizeof(id));
    return sizeof(id);
}

bool platform_safe_mode(void)
{
    /* SW8 on the WAKEUP pad (GPIO5_IO00), active low */
    IOMUXC_SetPinMux(IOMUXC_SNVS_WAKEUP_GPIO5_IO00, 0U);
    IOMUXC_SetPinConfig(IOMUXC_SNVS_WAKEUP_GPIO5_IO00, 0x01B0A0U); /* pull-up, hysteresis */
    gpio_pin_config_t in = { kGPIO_DigitalInput, 0U, kGPIO_NoIntmode };
    GPIO_PinInit(GPIO5, 0U, &in);
    task_sleep_ms(2);
    return GPIO_PinRead(GPIO5, 0U) == 0U;
}
