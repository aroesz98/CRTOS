/*
 * boot/main.c - the boot loader of CRTOS on the STM32H7S78-DK.
 *
 * The STM32H7S7 has 64 KB of internal flash: room for this boot loader, not for the kernel.
 * The kernel runs in place from the external NOR flash, which the processor cannot read
 * before somebody sets it up. So this program, in the internal flash:
 *   1. sets the power supply (SMPS) and the clock tree (core 600 MHz, buses 300/150 MHz,
 *      PLL2 200 MHz for the XSPI memories, PLL3 for the display and the audio clocks),
 *   2. puts the NOR flash (XSPI2, Macronix MX66UW1G45G, octal DTR) and the PSRAM (XSPI1,
 *      APS256XXN, x16 DTR) into memory-mapped mode at 200 MHz,
 *   3. checks the kernel image at 0x70000000 (header, CRC-32) and jumps to it.
 * The kernel keeps PLL1, PLL2 and the XSPI timing as they are. At its start it switches the
 * PSRAM to variable latency (board/platform.cpp: XSPI1 in indirect mode for a moment, MR0
 * rewritten, back to this fixed latency if its test fails), and the NOR flash driver switches
 * XSPI2 to indirect mode for a while to write the flash.
 *
 * Messages go to the console UART (UART4, the ST-LINK's virtual COM port, 1 Mbaud 8N1).
 */
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include "boot.h"
#include "image.h"

#define CON_BAUD 1000000u

uint32_t SystemCoreClock = 64000000u;   /* HSI at reset; the HAL updates it */

/* ---- the HAL's time base: the cycle counter, no interrupt ------------------------------------ */

HAL_StatusTypeDef HAL_InitTick(uint32_t prio)
{
    (void)prio;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    return HAL_OK;
}

uint32_t HAL_GetTick(void)
{
    static uint32_t last, ms, frac;
    uint32_t now = DWT->CYCCNT, per = SystemCoreClock / 1000u;
    frac += now - last;
    last = now;
    ms += frac / per;
    frac %= per;
    return ms;
}

void HAL_Delay(uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < ms + 1u) {
    }
}

/* ---- console ---------------------------------------------------------------------------------- */

static void con_flush(void);

static void con_init(void)
{
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();
    GPIO_InitTypeDef io = { 0 };
    io.Pin = GPIO_PIN_0 | GPIO_PIN_1;   /* PD0 RX, PD1 TX */
    io.Mode = GPIO_MODE_AF_PP;
    io.Pull = GPIO_PULLUP;
    io.Speed = GPIO_SPEED_FREQ_LOW;
    io.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(GPIOD, &io);
    con_flush();        /* what is still in the FIFO leaves at the old setting first */
    UART4->CR1 = 0;
    UART4->PRESC = 0;
    UART4->BRR = (HAL_RCC_GetPCLK1Freq() + CON_BAUD / 2u) / CON_BAUD;   /* kernel clock: PCLK1 (HSI or PLL1) */
    UART4->CR1 = USART_CR1_FIFOEN | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void con_putc(char c)
{
    if (!(UART4->CR1 & USART_CR1_UE))
        return;
    while (!(UART4->ISR & USART_ISR_TXE_TXFNF)) {
    }
    UART4->TDR = (uint8_t)c;
}

static void con_flush(void)
{
    if (UART4->CR1 & USART_CR1_UE)
        while (!(UART4->ISR & USART_ISR_TC)) {
        }
}

/* a small printf: %s %c %d %u %x %p with width and '0', 'l' accepted */
static void con_vprintf(const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            if (*fmt == '\n')
                con_putc('\r');
            con_putc(*fmt);
            continue;
        }
        char pad = ' ';
        int width = 0;
        fmt++;
        if (*fmt == '0')
            pad = '0';
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l')
            fmt++;
        char buf[12];
        int n = 0;
        unsigned long v;
        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            for (int len = (int)strlen(s); len < width; len++)
                con_putc(' ');
            while (*s)
                con_putc(*s++);
            continue;
        }
        case 'c':
            con_putc((char)va_arg(ap, int));
            continue;
        case 'd': {
            long d = va_arg(ap, long);
            if (d < 0) {
                con_putc('-');
                d = -d;
            }
            v = (unsigned long)d;
            do
                buf[n++] = (char)('0' + v % 10u);
            while (v /= 10u);
            break;
        }
        case 'u':
            v = va_arg(ap, unsigned long);
            do
                buf[n++] = (char)('0' + v % 10u);
            while (v /= 10u);
            break;
        case 'p':
        case 'x':
            v = va_arg(ap, unsigned long);
            do
                buf[n++] = "0123456789abcdef"[v & 15u];
            while (v >>= 4);
            break;
        default:
            con_putc(*fmt);
            continue;
        }
        while (n < width)
            buf[n++] = pad;
        while (n)
            con_putc(buf[--n]);
    }
}

void boot_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    con_vprintf(fmt, ap);
    va_end(ap);
}

void boot_panic(const char *fmt, ...)
{
    va_list ap;
    boot_printf("boot: ");
    va_start(ap, fmt);
    con_vprintf(fmt, ap);
    va_end(ap);
    boot_printf("\nboot: stopped (the debugger can connect; 'crtos flash' writes a new kernel)\n");
    con_flush();
    __disable_irq();
    for (;;) {      /* no WFI: the debugger must still reach the core */
    }
}

/* ---- power and clocks --------------------------------------------------------------------------- */

static void clocks_init(void)
{
    /* The STM32H7S78-DK supplies the core from the SMPS alone. The supply can be set once
     * after power-up: a reset keeps what the previous program chose, so it must be the same. */
    if (HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY) != HAL_OK)
        boot_panic("power supply configured otherwise (PWR_CSR2 %08lx): power the board off and on",
                   (unsigned long)PWR->CSR2);
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE0) != HAL_OK)
        boot_panic("voltage scaling");

    RCC_OscInitTypeDef osc = { 0 };
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_BYPASS;              /* X3: a 24 MHz oscillator on PH0 */
    /* PLL1: 24 MHz / 12 * 300 = 600 MHz VCO: P 600 MHz (the core), Q/R/S/T 300 MHz */
    osc.PLL1.PLLState = RCC_PLL_ON;
    osc.PLL1.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL1.PLLM = 12;
    osc.PLL1.PLLN = 300;
    osc.PLL1.PLLP = 1;
    osc.PLL1.PLLQ = 2;
    osc.PLL1.PLLR = 2;
    osc.PLL1.PLLS = 2;
    osc.PLL1.PLLT = 2;
    /* PLL2: 24 MHz / 6 * 100 = 400 MHz VCO, all outputs 200 MHz: S feeds both XSPI and the SD card */
    osc.PLL2.PLLState = RCC_PLL_ON;
    osc.PLL2.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL2.PLLM = 6;
    osc.PLL2.PLLN = 100;
    osc.PLL2.PLLP = 2;
    osc.PLL2.PLLQ = 2;
    osc.PLL2.PLLR = 2;
    osc.PLL2.PLLS = 2;
    osc.PLL2.PLLT = 2;
    /* PLL3: 24 MHz / 12 * 307.2 = 614.4 MHz VCO (fractional): P 12.288 MHz for the audio
     * (256 x 48 kHz; the sound driver moves the fraction for 44.1 kHz), R 25.6 MHz the LCD
     * pixel clock (800x480 at 61.6 Hz), Q 24.576 MHz */
    osc.PLL3.PLLState = RCC_PLL_ON;
    osc.PLL3.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL3.PLLM = 12;
    osc.PLL3.PLLN = 307;
    osc.PLL3.PLLFractional = 1638;
    osc.PLL3.PLLP = 50;
    osc.PLL3.PLLQ = 25;
    osc.PLL3.PLLR = 24;
    osc.PLL3.PLLS = 2;
    osc.PLL3.PLLT = 2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        boot_panic("oscillators / PLLs");

    RCC_ClkInitTypeDef clk = { 0 };
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                    RCC_CLOCKTYPE_PCLK4 | RCC_CLOCKTYPE_PCLK5;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.SYSCLKDivider = RCC_SYSCLK_DIV1;        /* core 600 MHz */
    clk.AHBCLKDivider = RCC_HCLK_DIV2;          /* AXI/AHB 300 MHz */
    clk.APB1CLKDivider = RCC_APB1_DIV2;         /* APB 150 MHz */
    clk.APB2CLKDivider = RCC_APB2_DIV2;
    clk.APB4CLKDivider = RCC_APB4_DIV2;
    clk.APB5CLKDivider = RCC_APB5_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_7) != HAL_OK)
        boot_panic("bus clocks");

    /* HAL_RCC_OscConfig() enables only PLL1 P (the core): the other outputs feed the kernel
     * clocks of the peripherals (XSPI, SD card, LCD, audio, USB...) */
    __HAL_RCC_PLL1CLKOUT_ENABLE(RCC_PLL_QCLK | RCC_PLL_RCLK | RCC_PLL_SCLK | RCC_PLL_TCLK);
    __HAL_RCC_PLL2CLKOUT_ENABLE(RCC_PLL_PCLK | RCC_PLL_QCLK | RCC_PLL_RCLK | RCC_PLL_SCLK | RCC_PLL_TCLK);
    __HAL_RCC_PLL3CLKOUT_ENABLE(RCC_PLL_PCLK | RCC_PLL_QCLK | RCC_PLL_RCLK | RCC_PLL_SCLK);
    __HAL_RCC_XSPI1_CONFIG(RCC_XSPI1CLKSOURCE_PLL2S);
    __HAL_RCC_XSPI2_CONFIG(RCC_XSPI2CLKSOURCE_PLL2S);
    __HAL_RCC_SDMMC12_CONFIG(RCC_SDMMC12CLKSOURCE_PLL2S);
}

/* ---- the kernel ---------------------------------------------------------------------------------- */

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n)
{
    static const uint32_t nib[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
    };
    while (n--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ nib[crc & 15u];
        crc = (crc >> 4) ^ nib[crc & 15u];
    }
    return crc;
}

static void start_kernel(void)
{
    const uint32_t *vt = (const uint32_t *)CRTOS_IMAGE_BASE;
    const struct crtos_image_header *h = (const struct crtos_image_header *)(CRTOS_IMAGE_BASE + CRTOS_IMAGE_HDR_OFF);
    if (h->magic != CRTOS_IMAGE_MAGIC || h->header_size != sizeof(*h))
        boot_panic("no kernel at %08lx (%08lx %08lx): write one with 'crtos flash'", (unsigned long)CRTOS_IMAGE_BASE,
                   (unsigned long)vt[0], (unsigned long)vt[1]);
    if (h->image_size <= CRTOS_IMAGE_HDR_OFF + sizeof(*h) || h->image_size > CRTOS_IMAGE_MAX)
        boot_panic("kernel image size %lu is wrong", (unsigned long)h->image_size);
    /* the CRC of the image with the header's crc32 field read as 0 */
    const uint8_t *img = (const uint8_t *)CRTOS_IMAGE_BASE;
    uint32_t at = CRTOS_IMAGE_HDR_OFF + (uint32_t)offsetof(struct crtos_image_header, crc32);
    static const uint8_t zero[4];
    uint32_t crc = crc32_update(0xFFFFFFFFu, img, at);
    crc = crc32_update(crc, zero, 4);
    crc = crc32_update(crc, img + at + 4, h->image_size - at - 4) ^ 0xFFFFFFFFu;
    if (crc != h->crc32)
        boot_panic("kernel image damaged: CRC %08lx, header says %08lx", (unsigned long)crc, (unsigned long)h->crc32);
    uint32_t sp = vt[0], pc = vt[1];
    if (sp < 0x20000000u || sp > 0x20010000u || (pc & 1u) == 0 || pc < CRTOS_IMAGE_BASE ||
        pc >= CRTOS_IMAGE_BASE + h->image_size)
        boot_panic("kernel vectors %08lx %08lx are wrong", (unsigned long)sp, (unsigned long)pc);
    char build[sizeof(h->build) + 1];
    memcpy(build, h->build, sizeof(h->build));
    build[sizeof(h->build)] = 0;
    boot_printf("boot: kernel %u KB, %s\n", (unsigned)(h->image_size / 1024u), build);
    con_flush();

    /* hand over a quiet machine: no interrupt, caches off (the kernel sets them up) */
    __disable_irq();
    SysTick->CTRL = 0;
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }
    SCB_DisableICache();
    SCB->VTOR = CRTOS_IMAGE_BASE;
    __DSB();
    __ISB();
    __asm volatile("msr msp, %0\n"
                   "bx %1\n" ::"r"(sp),
                   "r"(pc));
    for (;;) {
    }
}

/* a quick look at the PSRAM: patterns in a few places, so a wrong setup is reported here and not
 * as random crashes of the programs that live there */
static int psram_check(void)
{
    static const uint32_t where[] = { 0x00000000u, 0x00800000u, 0x01000000u, 0x01FF0000u };
    int bad = 0;
    for (unsigned w = 0; w < sizeof(where) / sizeof(where[0]); w++) {
        volatile uint32_t *p = (volatile uint32_t *)(0x90000000u + where[w]);
        for (uint32_t i = 0; i < 4096; i++)
            p[i] = (i * 0x9E3779B9u) ^ where[w];
        for (uint32_t i = 0; i < 4096; i++)
            if (p[i] != ((i * 0x9E3779B9u) ^ where[w]))
                bad++;
    }
    return bad;
}

int main(void)
{
    /* the debugger keeps access to the core in its sleep modes (the kernel's idle task) */
    DBGMCU->CR |= DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY;
    HAL_InitTick(0);
    SCB_EnableICache();
    con_init();     /* on the reset clock (HSI 64 MHz) first: a problem with the clocks is reported */
    boot_printf("\nCRTOS boot loader (STM32H7S78-DK)\n");
    con_flush();    /* all of it out before clocks_init() changes PCLK1, the UART's clock */
    clocks_init();
    con_init();
    boot_printf("boot: core %lu MHz\n", (unsigned long)(SystemCoreClock / 1000000u));

    int r = nor_init();
    if (r)
        boot_panic("NOR flash (XSPI2) setup failed at step %d, id %06lx", -r, (unsigned long)nor_id());
    r = psram_init();
    if (r)
        boot_panic("PSRAM (XSPI1) setup failed at step %d", -r);
    int bad = psram_check();
    if (bad)
        boot_panic("PSRAM check: %d words wrong", bad);
    boot_printf("boot: NOR %06lx, PSRAM 32 MB ok\n", (unsigned long)nor_id());

    start_kernel();
}
