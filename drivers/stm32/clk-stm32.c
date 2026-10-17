/*
 * clk-stm32.ko - clock provider of the STM32H7S7's RCC.
 *
 * A clock is one cell (dt-bindings/clock/stm32h7rs-clock.h):
 *   STM32_CLK(register, bit)       a peripheral's bus clock gate; its rate is the bus clock
 *                                  (HCLK for AHB, PCLKx for APBx), which is also what the
 *                                  UARTs and I2C buses take as kernel clock by default
 *   STM32_CLK(STM32_KERNEL, n)     PLL3 R (the LCD pixel clock), PLL3 Q (SPI6's kernel clock: the
 *                                  I2S to the audio codec), the HSI48 and LSE oscillators
 * The boot loader set up PLL1 (the core and buses) and PLL2 (XSPI, SD card) for good; PLL3
 * is shared by the display (R) and the sound (Q). An audio rate that the VCO reaches by its
 * fraction alone does not stop the PLL; another one takes the Q that keeps the VCO nearest
 * 614.4 MHz and restarts PLL3 (the display misses a frame), R following the pixel clock.
 * Gates of devices the kernel itself relies on (console, SD card, both XSPI, GPIO) are never
 * switched off.
 */
#include <stdbool.h>
#include <crtos/arch.h>
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/module.h>
#include <crtos/printk.h>
#include "stm32h7rsxx.h"
#include <dt-bindings/clock/stm32h7rs-clock.h>

#define HSE_HZ 24000000u
#define VCO_TARGET 614400000u
#define VCO_MIN 400000000u              /* the wide VCO range */
#define VCO_MAX 1600000000u

static const uint32_t s_critical[] = {
    STM32_CLK(STM32_APB1ENR1, STM32_APB1_UART4),
    STM32_CLK(STM32_AHB5ENR, STM32_AHB5_SDMMC1),
    STM32_CLK(STM32_AHB5ENR, STM32_AHB5_XSPI1),
    STM32_CLK(STM32_AHB5ENR, STM32_AHB5_XSPI2),
    STM32_CLK(STM32_AHB5ENR, STM32_AHB5_XSPIM),
    STM32_CLK(STM32_APB4ENR, STM32_APB4_SBS),
};

static volatile uint32_t *reg(uint32_t off)
{
    return (volatile uint32_t *)(RCC_BASE + off);
}

/* ---- bus clocks ------------------------------------------------------------------------------ */

static uint32_t hclk(void)
{
    uint32_t bm = RCC->BMCFGR & 0xFu;
    return bm < 8u ? SystemCoreClock : SystemCoreClock >> (bm - 7u);  /* 1000: /2, 1001: /4 ... */
}

static uint32_t pclk(unsigned shift)
{
    uint32_t p = (RCC->APBCFGR >> shift) & 7u;
    return p < 4u ? hclk() : hclk() >> (p - 3u);                    /* 100: /2, 101: /4 ... */
}

static uint32_t bus_rate(uint32_t off)
{
    switch (off) {
    case STM32_APB1ENR1:
    case STM32_APB1ENR2:
        return pclk(RCC_APBCFGR_PPRE1_Pos);
    case STM32_APB2ENR:
        return pclk(RCC_APBCFGR_PPRE2_Pos);
    case STM32_APB4ENR:
        return pclk(RCC_APBCFGR_PPRE4_Pos);
    case STM32_APB5ENR:
        return pclk(RCC_APBCFGR_PPRE5_Pos);
    default:
        return hclk();
    }
}

/* ---- PLL3 ----------------------------------------------------------------------------------- */

struct pll3 {
    uint32_t m, n, frac, p, q, r;
};

static void pll3_read(struct pll3 *c)
{
    c->m = (RCC->PLLCKSELR & RCC_PLLCKSELR_DIVM3) >> RCC_PLLCKSELR_DIVM3_Pos;
    uint32_t d = RCC->PLL3DIVR1;
    c->n = (d & RCC_PLL3DIVR1_DIVN) + 1u;
    c->p = ((d & RCC_PLL3DIVR1_DIVP) >> RCC_PLL3DIVR1_DIVP_Pos) + 1u;
    c->q = ((d & RCC_PLL3DIVR1_DIVQ) >> RCC_PLL3DIVR1_DIVQ_Pos) + 1u;
    c->r = ((d & RCC_PLL3DIVR1_DIVR) >> RCC_PLL3DIVR1_DIVR_Pos) + 1u;
    c->frac = (RCC->PLLCFGR & RCC_PLLCFGR_PLL3FRACEN) ? (RCC->PLL3FRACR & RCC_PLL3FRACR_FRACN) >> RCC_PLL3FRACR_FRACN_Pos : 0;
}

/* the VCO in Hz (the reference is HSE / M) */
static uint64_t pll3_vco(const struct pll3 *c)
{
    if (!c->m)
        return 0;
    return ((uint64_t)HSE_HZ * (c->n * 8192u + c->frac)) / (c->m * 8192u);
}

static int pll3_write(const struct pll3 *c, bool restart)
{
    if (restart) {
        RCC->CR &= ~RCC_CR_PLL3ON;
        uint32_t t0 = cpu_cycles();
        while (RCC->CR & RCC_CR_PLL3RDY)
            if (cpu_cycles() - t0 > SystemCoreClock / 100u)
                return -ETIMEDOUT;
        RCC->PLL3DIVR1 = (c->n - 1u) | ((c->p - 1u) << RCC_PLL3DIVR1_DIVP_Pos) |
                         ((c->q - 1u) << RCC_PLL3DIVR1_DIVQ_Pos) | ((c->r - 1u) << RCC_PLL3DIVR1_DIVR_Pos);
    }
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLL3FRACEN;
    RCC->PLL3FRACR = c->frac << RCC_PLL3FRACR_FRACN_Pos;
    RCC->PLLCFGR |= RCC_PLLCFGR_PLL3FRACEN;
    if (restart) {
        RCC->CR |= RCC_CR_PLL3ON;
        uint32_t t0 = cpu_cycles();
        while (!(RCC->CR & RCC_CR_PLL3RDY))
            if (cpu_cycles() - t0 > SystemCoreClock / 100u)
                return -ETIMEDOUT;
    }
    return 0;
}

static uint32_t s_pixel_rate;       /* the pixel clock asked for: R follows a VCO change */

static uint32_t divider(uint64_t vco, uint32_t rate)
{
    uint32_t d = (uint32_t)((vco + rate / 2u) / rate);
    return d < 1u ? 1u : d > 128u ? 128u : d;
}

/* the pixel clock: the R divider closest to @rate */
static int set_pixel_clock(uint32_t rate)
{
    struct pll3 c;
    pll3_read(&c);
    s_pixel_rate = rate;
    uint32_t r = divider(pll3_vco(&c), rate);
    if (r == c.r)
        return 0;
    c.r = r;
    return pll3_write(&c, true);
}

/* N * 8192 + FRACN for a VCO of @rate * @q (the reference is HSE / M) */
static uint64_t vco_steps(const struct pll3 *c, uint32_t rate, uint32_t q)
{
    return ((uint64_t)rate * q * c->m * 8192u + HSE_HZ / 2u) / HSE_HZ;
}

/* SPI6's kernel clock (Q) at @rate: by the fraction when the VCO stays within its N, else
 * with the Q that keeps the VCO nearest VCO_TARGET */
static int set_audio_clock(uint32_t rate)
{
    struct pll3 c;
    pll3_read(&c);
    if (!c.m)
        return -EIO;
    uint64_t steps = vco_steps(&c, rate, c.q);
    if (steps / 8192u == c.n) {
        c.frac = (uint32_t)(steps % 8192u);
        return pll3_write(&c, false);
    }
    uint32_t best = 0;
    uint64_t best_d = ~0ull;
    for (uint32_t q = 1; q <= 128u; q++) {
        uint64_t vco = (uint64_t)rate * q;
        if (vco < VCO_MIN || vco > VCO_MAX)
            continue;
        uint64_t d = vco > VCO_TARGET ? vco - VCO_TARGET : VCO_TARGET - vco;
        if (d < best_d) {
            best_d = d;
            best = q;
        }
    }
    if (!best)
        return -ERANGE;
    steps = vco_steps(&c, rate, best);
    uint32_t n = (uint32_t)(steps / 8192u);
    if (n < 8u || n > 420u)
        return -ERANGE;
    c.n = n;
    c.frac = (uint32_t)(steps % 8192u);
    c.q = best;
    if (s_pixel_rate)
        c.r = divider(pll3_vco(&c), s_pixel_rate);
    return pll3_write(&c, true);
}

/* ---- the provider ---------------------------------------------------------------------------- */

static int osc_on(volatile uint32_t *r, uint32_t on, uint32_t rdy)
{
    *r |= on;
    uint32_t t0 = cpu_cycles();
    while (!(*r & rdy))
        if (cpu_cycles() - t0 > SystemCoreClock / 2u)   /* the LSE crystal takes its time */
            return -ETIMEDOUT;
    return 0;
}

static int rcc_enable(void *ctx, uint32_t id)
{
    (void)ctx;
    uint32_t off = id >> 8, bit = id & 0xFFu;
    if (off == STM32_KERNEL) {
        switch (bit) {
        case STM32_CK_HSI48:
            return osc_on(&RCC->CR, RCC_CR_HSI48ON, RCC_CR_HSI48RDY);
        case STM32_CK_LSE:
            PWR->CR1 |= PWR_CR1_DBP;                    /* the backup domain may be written */
            return osc_on(&RCC->BDCR, RCC_BDCR_LSEON, RCC_BDCR_LSERDY);
        case STM32_CK_LTDC:
            RCC->PLLCFGR |= RCC_PLLCFGR_PLL3REN;        /* (the LTDC's only clock) */
            return 0;
        case STM32_CK_AUDIO: {
            uint32_t key = irq_lock();
            RCC->PLLCFGR |= RCC_PLLCFGR_PLL3QEN;
            RCC->CCIPR4 = (RCC->CCIPR4 & ~RCC_CCIPR4_SPI6SEL) | RCC_CCIPR4_SPI6SEL_1;   /* PLL3 Q */
            irq_unlock(key);
            return 0;
        }
        default:
            return -EINVAL;
        }
    }
    if (off < STM32_AHB5ENR || off > STM32_AHB3ENR || bit > 31u)
        return -EINVAL;
    uint32_t key = irq_lock();
    *reg(off) |= 1u << bit;
    (void)*reg(off);
    irq_unlock(key);
    return 0;
}

static void rcc_disable(void *ctx, uint32_t id)
{
    (void)ctx;
    uint32_t off = id >> 8, bit = id & 0xFFu;
    if (off == STM32_KERNEL || off < STM32_AHB5ENR || off > STM32_AHB3ENR || bit > 31u)
        return;
    if (off == STM32_AHB4ENR && bit < 16u)
        return;     /* GPIO ports: shared by everything */
    for (unsigned i = 0; i < sizeof(s_critical) / sizeof(s_critical[0]); i++)
        if (s_critical[i] == id)
            return;
    uint32_t key = irq_lock();
    *reg(off) &= ~(1u << bit);
    irq_unlock(key);
}

static uint32_t rcc_get_rate(void *ctx, uint32_t id)
{
    (void)ctx;
    uint32_t off = id >> 8, bit = id & 0xFFu;
    if (off != STM32_KERNEL)
        return bus_rate(off);
    struct pll3 c;
    switch (bit) {
    case STM32_CK_LTDC:
        pll3_read(&c);
        return (uint32_t)(pll3_vco(&c) / c.r);
    case STM32_CK_AUDIO:
        pll3_read(&c);
        return (uint32_t)(pll3_vco(&c) / c.q);
    case STM32_CK_HSI48:
        return 48000000u;
    case STM32_CK_LSE:
        return 32768u;
    default:
        return 0;
    }
}

static int rcc_set_rate(void *ctx, uint32_t id, uint32_t rate)
{
    (void)ctx;
    if (!rate || id >> 8 != STM32_KERNEL)
        return -EINVAL;
    switch (id & 0xFFu) {
    case STM32_CK_LTDC:
        return set_pixel_clock(rate);
    case STM32_CK_AUDIO:
        return set_audio_clock(rate);
    default:
        return -EINVAL;
    }
}

static const struct clk_ops rcc_ops = { rcc_enable, rcc_disable, rcc_get_rate, rcc_set_rate };

static int rcc_probe(struct device *dev)
{
    int r = clk_provider_register(dev->of_node, &rcc_ops, NULL);
    if (r)
        return r;
    struct pll3 c;
    pll3_read(&c);
    dev_info(dev, "core %lu MHz, AHB %lu MHz, APB %lu MHz, PLL3 %lu MHz (pixel %lu kHz, audio %lu Hz)\n",
             (unsigned long)(SystemCoreClock / 1000000u), (unsigned long)(hclk() / 1000000u),
             (unsigned long)(pclk(RCC_APBCFGR_PPRE1_Pos) / 1000000u), (unsigned long)(pll3_vco(&c) / 1000000u),
             (unsigned long)(pll3_vco(&c) / c.r / 1000u), (unsigned long)(pll3_vco(&c) / c.q));
    return 0;
}

static void rcc_remove(struct device *dev)
{
    clk_provider_unregister(dev->of_node);
}

static const struct of_device_id rcc_ids[] = {
    { "st,stm32h7rs-rcc", NULL },
    { NULL, NULL },
};

static struct driver rcc_driver = {
    .name = "clk-stm32",
    .of_match_table = rcc_ids,
    .probe = rcc_probe,
    .remove = rcc_remove,
};

static int init(void)
{
    return driver_register(&rcc_driver);
}

static void fini(void)
{
    driver_unregister(&rcc_driver);
}

MODULE("clk-stm32", "STM32H7S7 clock controller (RCC)", init, fini);
