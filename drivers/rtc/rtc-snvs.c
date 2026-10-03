/*
 * rtc-snvs.ko - the secure real-time counter (SRTC) of the i.MX RT SNVS low-power domain as
 * the system's real-time clock (crtos/rtc.h).
 *
 * The SRTC is a 47-bit counter of the 32.768 kHz oscillator (LPSRTCMR:LPSRTCLR) in the
 * SNVS_LP domain, which a reset does not touch (nor power-off, with a coin cell on
 * VDD_SNVS). It counts seconds since 1970 here. Writing it needs the counter stopped
 * (LPCR.SRTC_ENV); the change takes a few cycles of the 32 kHz clock to settle.
 */
#include <stdbool.h>
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/module.h>
#include <crtos/printk.h>
#include <crtos/rtc.h>
#include "fsl_device_registers.h"

struct srtc
{
    struct device *dev;
    SNVS_Type *base;
};

/* The counter, read until two reads agree (it may change between the two halves) */
static uint64_t counter(SNVS_Type *b)
{
    uint64_t prev = ~0ull;
    for (int i = 0; i < 8; i++)
    {
        uint64_t v = ((uint64_t)(b->LPSRTCMR & 0x7FFFu) << 32) | b->LPSRTCLR;
        if (v == prev)
            return v;
        prev = v;
    }
    return prev;
}

static int wait_enable(SNVS_Type *b, bool on)
{
    for (int i = 0; i < 200000; i++) /* a few 32 kHz cycles; no timer needed */
        if (((b->LPCR & SNVS_LPCR_SRTC_ENV_MASK) != 0) == on)
            return 0;
    return -ETIMEDOUT;
}

static int srtc_read(void *ctx, int64_t *us)
{
    struct srtc *s = ctx;
    if (!(s->base->LPCR & SNVS_LPCR_SRTC_ENV_MASK))
        return -EAGAIN; /* never started: no date */
    uint64_t c = counter(s->base);
    *us = (int64_t)((c >> 15) * 1000000ull + (((c & 0x7FFFu) * 1000000ull) >> 15));
    return 0;
}

static int srtc_set(void *ctx, int64_t us)
{
    struct srtc *s = ctx;
    SNVS_Type *b = s->base;
    if (us < 0)
        return -EINVAL;
    uint64_t sec = (uint64_t)us / 1000000u, frac = (uint64_t)us % 1000000u;
    uint64_t c = (sec << 15) | ((frac << 15) / 1000000u);
    b->LPCR &= ~SNVS_LPCR_SRTC_ENV_MASK;
    int r = wait_enable(b, false);
    if (r)
        return r;
    b->LPSRTCMR = (uint32_t)(c >> 32) & 0x7FFFu;
    b->LPSRTCLR = (uint32_t)c;
    b->LPCR |= SNVS_LPCR_SRTC_ENV_MASK;
    return wait_enable(b, true);
}

static const struct rtc_ops srtc_ops = {srtc_read, srtc_set};

static int srtc_probe(struct device *dev)
{
    struct srtc *s = devm_kzalloc(dev, sizeof(*s), 0);
    if (!s)
        return -ENOMEM;
    s->dev = dev;
    s->base = device_map(dev, 0);
    if (!s->base)
        return -ENODEV;
    static const char *const names[] = {"hp", "lp"};
    for (int i = 0; i < 2; i++)
    {
        struct clk *clk;
        int r = devm_clk_get_enabled(dev, names[i], &clk);
        if (r)
            return r;
    }
    dev_set_drvdata(dev, s);
    return rtc_register(&srtc_ops, s, dev->name);
}

static void srtc_remove(struct device *dev)
{
    rtc_unregister(dev_get_drvdata(dev));
}

static const struct of_device_id srtc_ids[] =
    {
        {"fsl,imxrt1050-snvs-rtc", NULL},
        {NULL, NULL},
};

static struct driver srtc_driver = {
    .name = "rtc-snvs",
    .of_match_table = srtc_ids,
    .probe = srtc_probe,
    .remove = srtc_remove,
};

static int init(void)
{
    return driver_register(&srtc_driver);
}

static void fini(void)
{
    driver_unregister(&srtc_driver);
}

MODULE("rtc-snvs", "i.MX RT SNVS real-time counter", init, fini);
