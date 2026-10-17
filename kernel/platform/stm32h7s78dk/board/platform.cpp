/*
 * platform.cpp - the STM32H7S78-DK as the generic kernel sees it (kernel.h, platform_*): memory
 * map, reset cause, the safe mode button, the C library's console hooks.
 *
 * The boot loader in the internal flash (boot/main.c) has set up the power supply, the clocks
 * (core 600 MHz, buses 300/150 MHz, PLL2 200 MHz, PLL3) and both XSPI memories before the kernel
 * starts. The kernel keeps PLL1, PLL2 and the XSPI timing; at its start it changes one thing:
 * the PSRAM goes to variable latency (psram_variable_latency below: XSPI1 in indirect mode for
 * a moment, the memory's MR0 rewritten, back to the boot loader's fixed latency if the test
 * fails). Later only the NOR flash driver switches XSPI2 to indirect mode, to write the flash.
 */
#include "kernel.h"
#include "platform.h"
#include "psram.h"
#include <crtos/device.h>
#include <errno.h>
#include <string.h>

uint32_t SystemCoreClock = 600000000u;  /* PLL1 P, set by the boot loader */

const char platform_cpu_name[] = "STM32H7S7";
const char platform_sd_compatible[] = "st,stm32-sdmmc";
const uintptr_t platform_sd_base = SDMMC1_BASE;
const char platform_console_compatible[] = "st,stm32-uart";
const char platform_safe_mode_button[] = "USER";

const struct mpu_map_entry platform_mpu_map[] = {
    { 0x00000000u, 18, MPU_KIND_TCM_CODE },         /* ITCM 64-192K (option bytes) */
    { 0x20000000u, 16, MPU_KIND_TCM_DATA },         /* DTCM 64K */
    { 0x24000000u, 19, MPU_KIND_RAM },              /* AXI SRAM (SRAM2-4 from 0x24020000: 328K) */
    { 0x90000000u, 25, MPU_KIND_RAM },              /* PSRAM 32M (XSPI1) */
    { 0x91E00000u, 21, MPU_KIND_RAM_NOCACHE },      /* its last 2M, not cached */
    /* NOR flash 128M (XSPI2): the kernel runs from it, programs from the file system /flash0
     * on it (their code in place; the kernel's image is readable but holds nothing secret) */
    { 0x70000000u, 27, MPU_KIND_FLASH },
    { 0x40000000u, 29, MPU_KIND_DEVICE },           /* peripherals 0x40000000-0x5FFFFFFF */
    { 0, 0, 0 },
};

extern "C" uint32_t g_chip_uid[3];
uint32_t g_chip_uid[3];                 /* copied by the start-up code (startup.cpp) */

size_t board_unique_id(void *buf, size_t len)
{
    memcpy(buf, g_chip_uid, len < sizeof(g_chip_uid) ? len : sizeof(g_chip_uid));
    return sizeof(g_chip_uid);
}

/* ---- PSRAM latency ------------------------------------------------------------------------------
 *
 * The boot loader leaves the PSRAM (APS256XXN on XSPI1, memory-mapped) in fixed latency: 14
 * clocks before the data of every read. Variable latency - ST's BSP default - waits 7 clocks
 * unless a refresh of the memory collides (the chip says so on RWDS, the XSPI follows DQS),
 * and the XSPI's read dummy cycles (6) are already set for it. Every access the processor,
 * the display, the GPU and the DMA make to the PSRAM pays that wait, and with code, windows
 * and frame buffers there it is the system's bottleneck: so the kernel switches the memory to
 * variable latency at the very start - before any bus master but the core runs - checks it
 * with a pattern in the non-cacheable part and goes back if anything differs.
 * (Measured: ending reads early with the XSPI's timeout counter, LPTR, gains nothing.) */

/* the pattern: the non-cacheable part above its static content, which the kernel heap takes
 * only later (mm_init; linker/crtos.ld checks that it fits) */
extern "C" uint8_t __end_noinit_NCACHE_REGION[];
#define RAM_TEST         ((volatile uint32_t *)__end_noinit_NCACHE_REGION)
#define RAM_TEST_WORDS   4096u

static const char *s_psram = "fixed latency (as the boot loader left it)";

static bool xspi_wait(volatile uint32_t *reg, uint32_t mask, uint32_t want)
{
    for (uint32_t i = 0; i < 1000000u; i++)
        if ((*reg & mask) == want)
            return true;
    return false;
}

/* a mode register of the PSRAM, written (@read: read) in indirect mode; memory-mapped again
 * afterwards with the read configuration it had */
static bool psram_reg(XSPI_TypeDef *x, uint32_t addr, uint8_t *value, bool read)
{
    uint32_t cr = x->CR & ~XSPI_CR_ABORT, ccr = x->CCR, tcr = x->TCR, ir = x->IR;
    x->CR = cr | XSPI_CR_ABORT;
    if (!xspi_wait(&x->CR, XSPI_CR_ABORT, 0) || !xspi_wait(&x->SR, XSPI_SR_BUSY, 0))
        return false;
    x->FCR = XSPI_FCR_CTCF | XSPI_FCR_CTEF;
    x->CR = (cr & ~XSPI_CR_FMODE) | ((read ? 1u : 0u) << XSPI_CR_FMODE_Pos);
    x->DLR = 1;                     /* two bytes (DTR) */
    x->TCR = (tcr & ~XSPI_TCR_DCYC) | ((read ? 7u : 0u) << XSPI_TCR_DCYC_Pos);
    x->CCR = (4u << XSPI_CCR_IMODE_Pos) | (4u << XSPI_CCR_ADMODE_Pos) | (3u << XSPI_CCR_ADSIZE_Pos) | XSPI_CCR_ADDTR |
             (4u << XSPI_CCR_DMODE_Pos) | XSPI_CCR_DDTR | (read ? XSPI_CCR_DQSE : 0u);
    x->IR = read ? PSRAM_READ_REG : PSRAM_WRITE_REG;
    x->AR = addr;                   /* (a read starts here) */
    bool ok = true;
    if (read) {
        ok = xspi_wait(&x->SR, XSPI_SR_TCF, XSPI_SR_TCF);
        *value = *(volatile uint8_t *)&x->DR;
        (void)*(volatile uint8_t *)&x->DR;
    } else {
        *(volatile uint8_t *)&x->DR = *value;
        *(volatile uint8_t *)&x->DR = PSRAM_MR1;
        ok = xspi_wait(&x->SR, XSPI_SR_TCF, XSPI_SR_TCF);
    }
    x->FCR = XSPI_FCR_CTCF;
    ok = ok && xspi_wait(&x->SR, XSPI_SR_BUSY, 0);
    x->CCR = ccr;
    x->TCR = tcr;
    x->IR = ir;
    x->CR = (cr & ~XSPI_CR_FMODE) | (3u << XSPI_CR_FMODE_Pos);
    return ok;
}

static bool psram_test(void)
{
    volatile uint32_t *m = RAM_TEST;
    for (uint32_t i = 0; i < RAM_TEST_WORDS; i++)
        m[i] = i * 0x9E3779B9u ^ 0xA5A5A5A5u;
    for (uint32_t i = 0; i < RAM_TEST_WORDS; i++)
        if (m[i] != (i * 0x9E3779B9u ^ 0xA5A5A5A5u))
            return false;
    for (uint32_t i = 0; i < RAM_TEST_WORDS; i++)
        m[i] = ~i;
    for (uint32_t i = RAM_TEST_WORDS; i-- > 0;)
        if (m[i] != ~i)
            return false;
    return true;
}

static void psram_variable_latency(void)
{
    XSPI_TypeDef *x = XSPI1;
    uint8_t mr0 = 0;
    if (!psram_reg(x, 0, &mr0, true) || mr0 != PSRAM_MR0_FIXED) {
        s_psram = "left alone (not the mode the boot loader sets)";
        return;
    }
    uint8_t v = PSRAM_MR0_VARIABLE;
    if (psram_reg(x, 0, &v, false) && psram_reg(x, 0, &mr0, true) && mr0 == PSRAM_MR0_VARIABLE && psram_test()) {
        s_psram = "variable latency";
        return;
    }
    v = PSRAM_MR0_FIXED;            /* back to what worked */
    psram_reg(x, 0, &v, false);
    s_psram = psram_test() ? "fixed latency (variable latency failed its test)" : "FAILED its test";
}

/* ---- ITCM size ----------------------------------------------------------------------------------
 *
 * The option bytes give ITCM 64 KB more for each 64 KB of AXI SRAM1 they take, the lower half
 * first (FLASH_OBW2SR ITCM_AXI_SHARE: 0 and 3 - 64 KB, 1 - 128 KB, 2 - 192 KB). The kernel uses
 * AXI SRAM only from SRAM2 (0x24020000, linker/memory.ld), so it runs with any split; the fast
 * code area of programs (kmalloc.cpp) ends where the ITCM does. 128 KB is recommended for
 * CRTOS: 64 KB of fast code for a program, and SRAM1's upper half (0x24010000) stays AXI SRAM,
 * where the GPU draws 3D primitives (gpu2d-stm32, several times faster than in the PSRAM; 192 KB
 * would take it). 64 KB, the factory default, works too with a smaller fast code area
 * (linker/crtos.ld checks that 8 KB remain), and suits firmware that shares the board and uses
 * SRAM1 from 0x24000000. Set with the ST-LINK: STM32_Programmer_CLI -c port=SWD -ob
 * ITCM_AXI_SHARE=1 (it resets the board). */

uintptr_t platform_itcm_top(void)
{
    uint32_t share = (FLASH->OBW2SR & FLASH_OBW2SR_ITCM_AXI_SHARE_Msk) >> FLASH_OBW2SR_ITCM_AXI_SHARE_Pos;
    return (share == 1 ? 128u : share == 2 ? 192u : 64u) * 1024u;
}

void platform_early_init(void)
{
    /* the console sets up its own pins (console_uart.cpp) */
    psram_variable_latency();
}

void platform_init(void)
{
    printk("psram: %s\n", s_psram);
    if (platform_itcm_top() < 128u * 1024u)
        printk("itcm: 64 KB, the option bytes' default (ITCM_AXI_SHARE=1, 128 KB, gives programs more fast "
               "code)\n");
    else if (platform_itcm_top() > 128u * 1024u)
        printk("itcm: 192 KB (all of AXI SRAM1: no on-chip memory for the GPU's primitives)\n");
    /* The idle task sleeps with WFI, which stops the core's clock: keep the debug port and the
     * bus matrix clocked meanwhile, so the probe can still read memory (the SWD console,
     * screenshots) and halt the core. */
    DBGMCU->CR |= DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY;
}

void platform_reset_cause(char *buf, size_t size)
{
    static const struct {
        uint32_t bit;
        const char *name;
    } causes[] = {
        { RCC_RSR_PORRSTF, "power-on" },   { RCC_RSR_BORRSTF, "brown-out" },  { RCC_RSR_PINRSTF, "pin" },
        { RCC_RSR_SFTRSTF, "sysreset" },   { RCC_RSR_IWDGRSTF, "watchdog" }, { RCC_RSR_WWDGRSTF, "window watchdog" },
        { RCC_RSR_LPWRRSTF, "low-power" }, { RCC_RSR_OBLRSTF, "option bytes" },
    };
    uint32_t rsr = RCC->RSR;
    int n = 0;
    /* a power-on also sets the pin and brown-out flags: name only the first cause then */
    for (unsigned i = 0; i < ARRAY_SIZE(causes); i++)
        if ((rsr & causes[i].bit) && !(i > 0 && i < 3 && (rsr & RCC_RSR_PORRSTF)))
            n += ksnprintf(buf + n, size - (size_t)n, "%s%s", n ? ", " : "", causes[i].name);
    ksnprintf(buf + n, size - (size_t)n, "%s(RSR=%08lx)", n ? " " : "unknown ", (unsigned long)rsr);
    RCC->RSR = RCC_RSR_RMVF;
}

bool platform_safe_mode(void)
{
    /* USER (B2) on PC13: high while pressed */
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOCEN;
    (void)RCC->AHB4ENR;
    GPIOC->PUPDR = (GPIOC->PUPDR & ~(3u << 26)) | (2u << 26);      /* pull-down */
    GPIOC->MODER &= ~(3u << 26);                                   /* input */
    task_sleep_ms(2);
    return (GPIOC->IDR & (1u << 13)) != 0;
}

/* ---- the C library's console: nothing reads it, writes go to the kernel log ---------------- */

extern "C" int _write(int fd, const char *buf, int len)
{
    if (fd != 1 && fd != 2) {
        errno = EBADF;
        return -1;
    }
    log_write(buf, (size_t)len);
    return len;
}

extern "C" int _read(int fd, char *buf, int len)
{
    (void)fd;
    (void)buf;
    (void)len;
    return 0;
}
