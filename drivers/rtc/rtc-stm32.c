/*
 * rtc-stm32.ko - the STM32 RTC as the system's real-time clock (crtos/rtc.h).
 *
 * The RTC counts a BCD calendar (date, time and a sub-second down-counter) from the 32.768 kHz
 * LSE crystal in the backup domain, which a reset does not touch (nor power-off, with VBAT).
 * It keeps UTC here. The calendar counts from 2000 to 2099; a calendar that was never set
 * (ICSR.INITS clear) gives no date. Setting it: the write protection unlocked, the
 * initialisation mode entered, the prescalers (127, 255: 1 Hz) and the new date and time
 * written, and left.
 *
 * The RTC's clock is chosen once per backup domain reset (RCC BDCR.RTCSEL): the driver picks
 * the LSE when nothing is chosen yet and refuses another choice rather than reset the domain.
 */
#include <stdbool.h>
#include <crtos/arch.h>
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/module.h>
#include <crtos/printk.h>
#include <crtos/rtc.h>
#include "stm32h7rsxx.h"

#define PREDIV_A 127u
#define PREDIV_S 255u

struct rtc
{
    struct device *dev;
    RTC_TypeDef *r;
};

static uint32_t bcd(uint32_t v)
{
    return (v >> 4) * 10u + (v & 0xfu);
}

static uint32_t to_bcd(uint32_t v)
{
    return ((v / 10u) << 4) | (v % 10u);
}

/* days since 1970-01-01 of a date (proleptic Gregorian) */
static int64_t days_from_civil(int64_t y, uint32_t m, uint32_t d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t)(y - era * 400);
    uint32_t doy = (153u * (m > 2 ? m - 3 : m + 9) + 2u) / 5u + d - 1u;
    uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, uint32_t *y, uint32_t *m, uint32_t *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp = (5u * doy + 2u) / 153u;
    *d = doy - (153u * mp + 2u) / 5u + 1u;
    *m = mp < 10u ? mp + 3u : mp - 9u;
    *y = (uint32_t)(yoe + era * 400) + (*m <= 2u);
}

static int rtc_read(void *ctx, int64_t *us)
{
    struct rtc *c = ctx;
    RTC_TypeDef *r = c->r;
    if (!(r->ICSR & RTC_ICSR_INITS))
        return -EAGAIN; /* never set: no date */
    /* the shadow registers: reading SSR freezes TR and DR until DR is read, so the three
     * belong together */
    uint32_t ssr = r->SSR, tr = r->TR, dr = r->DR;
    uint32_t year = 2000u + bcd(dr >> 16 & 0xffu), month = bcd(dr >> 8 & 0x1fu), day = bcd(dr & 0x3fu);
    uint32_t hour = bcd(tr >> 16 & 0x3fu), min = bcd(tr >> 8 & 0x7fu), sec = bcd(tr & 0x7fu);
    int64_t s = days_from_civil(year, month, day) * 86400 + hour * 3600u + min * 60u + sec;
    uint32_t frac = PREDIV_S - (ssr & 0xffffu); /* SSR counts down from PREDIV_S */
    *us = s * 1000000 + (int64_t)frac * 1000000 / (PREDIV_S + 1u);
    return 0;
}

static int wait_bit(volatile uint32_t *reg, uint32_t bit, bool set)
{
    uint32_t t0 = cpu_cycles();
    while (((*reg & bit) != 0) != set)
        if (cpu_cycles() - t0 > SystemCoreClock / 10u) /* a few 32 kHz cycles normally */
            return -ETIMEDOUT;
    return 0;
}

static int rtc_set(void *ctx, int64_t us)
{
    struct rtc *c = ctx;
    RTC_TypeDef *r = c->r;
    if (us < 0)
        return -EINVAL;
    int64_t s = us / 1000000;
    int64_t days = s / 86400;
    uint32_t t = (uint32_t)(s % 86400);
    uint32_t y, m, d;
    civil_from_days(days, &y, &m, &d);
    if (y < 2000u || y > 2099u)
        return -ERANGE;
    uint32_t wday = (uint32_t)((days + 4) % 7); /* 1970-01-01 was a Thursday; RTC: 1 Monday .. 7 Sunday */
    wday = wday ? wday : 7u;
    uint32_t tr = to_bcd(t / 3600u) << 16 | to_bcd(t / 60u % 60u) << 8 | to_bcd(t % 60u);
    uint32_t dr = to_bcd(y - 2000u) << 16 | wday << 13 | to_bcd(m) << 8 | to_bcd(d);

    PWR->CR1 |= PWR_CR1_DBP;
    r->WPR = 0xca;
    r->WPR = 0x53;
    r->ICSR |= RTC_ICSR_INIT;
    int ret = wait_bit(&r->ICSR, RTC_ICSR_INITF, true);
    if (!ret)
    {
        r->PRER = PREDIV_A << 16 | PREDIV_S;
        r->TR = tr;
        r->DR = dr;
        r->CR &= ~RTC_CR_FMT; /* 24 hours */
        r->ICSR &= ~(RTC_ICSR_INIT | RTC_ICSR_RSF);
        ret = wait_bit(&r->ICSR, RTC_ICSR_RSF, true);
    }
    else
    {
        r->ICSR &= ~RTC_ICSR_INIT;
    }
    r->WPR = 0xff;
    return ret;
}

static const struct rtc_ops stm32_rtc_ops = {rtc_read, rtc_set};

static int rtc_probe(struct device *dev)
{
    struct rtc *c = devm_kzalloc(dev, sizeof(*c), 0);
    if (!c)
        return -ENOMEM;
    c->dev = dev;
    c->r = device_map(dev, 0);
    if (!c->r)
        return -ENODEV;
    struct clk *bus, *lse;
    int r = devm_clk_get_enabled(dev, "bus", &bus);
    if (!r)
        r = devm_clk_get_enabled(dev, "lse", &lse); /* (also unlocks the backup domain) */
    if (r)
        return r;
    uint32_t sel = (RCC->BDCR & RCC_BDCR_RTCSEL) >> RCC_BDCR_RTCSEL_Pos;
    if (sel == 0)
    {
        RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL) | (1u << RCC_BDCR_RTCSEL_Pos); /* LSE */
        sel = 1;
    }
    if (sel != 1)
    {
        dev_err(dev, "the RTC runs from another clock (RTCSEL %lu), not the LSE\n", (unsigned long)sel);
        return -EBUSY;
    }
    RCC->BDCR |= RCC_BDCR_RTCEN;
    wait_bit(&c->r->ICSR, RTC_ICSR_RSF, true); /* the shadow registers synchronised after the reset */
    dev_set_drvdata(dev, c);
    r = rtc_register(&stm32_rtc_ops, c, dev->name);
    if (!r)
        dev_info(dev, "calendar from the LSE%s\n", (c->r->ICSR & RTC_ICSR_INITS) ? "" : ", not set yet");
    return r;
}

static void rtc_remove(struct device *dev)
{
    rtc_unregister(dev_get_drvdata(dev));
}

static const struct of_device_id rtc_ids[] = {
    {"st,stm32-rtc", NULL},
    {NULL, NULL},
};

static struct driver rtc_driver = {
    .name = "rtc-stm32",
    .of_match_table = rtc_ids,
    .probe = rtc_probe,
    .remove = rtc_remove,
};

static int init(void)
{
    return driver_register(&rtc_driver);
}

static void fini(void)
{
    driver_unregister(&rtc_driver);
}

MODULE("rtc-stm32", "STM32 real-time clock", init, fini);
