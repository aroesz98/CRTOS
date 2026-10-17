/*
 * rng-stm32.ko - the STM32 true random number generator (RNG) as /dev/random and
 * /dev/urandom: entropy for TLS keys and anything else that must not be predictable.
 *
 * The RNG conditions the noise of free-running oscillators (clocked by HSI48) and hands out
 * 32 bits per read of DR once DRDY is set, a new word every few hundred nanoseconds; the
 * reader polls. A seed or clock error stops it: the conditioning is reset (CONDRST) and the
 * read goes on, up to a few times. Both files give the same hardware entropy, each word
 * handed out once.
 */
#include <stdbool.h>
#include <string.h>
#include <crtos/arch.h>
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/module.h>
#include <crtos/printk.h>
#include <crtos/sched.h>
#include <crtos/sync.h>
#include <crtos/vfs.h>
#include "stm32h7rsxx.h"

#define READ_MAX 4096u /* bytes per read() */
#define ATTEMPTS 4     /* conditioning resets before a read fails */

struct rng
{
    struct device *dev;
    RNG_TypeDef *r;
    struct mutex lock; /* one reader at a time */
    uint32_t errors;
};

/* the conditioning reset (after a seed error, and to start) */
static int reset(struct rng *g)
{
    RNG_TypeDef *r = g->r;
    r->CR = (r->CR & ~RNG_CR_RNGEN) | RNG_CR_CONDRST;
    r->CR &= ~RNG_CR_CONDRST;
    uint32_t t0 = cpu_cycles();
    while (r->CR & RNG_CR_CONDRST)
        if (cpu_cycles() - t0 > SystemCoreClock / 100u)
            return -ETIMEDOUT;
    r->SR = 0; /* (clears SEIS, CEIS) */
    r->CR |= RNG_CR_RNGEN;
    return 0;
}

/* one word, 0 or -EIO */
static int word(struct rng *g, uint32_t *out)
{
    RNG_TypeDef *r = g->r;
    for (int attempt = 0; attempt < ATTEMPTS; attempt++)
    {
        uint32_t t0 = cpu_cycles();
        for (;;)
        {
            uint32_t sr = r->SR;
            if (sr & (RNG_SR_SECS | RNG_SR_SEIS | RNG_SR_CECS))
                break;
            if (sr & RNG_SR_DRDY)
            {
                *out = r->DR;
                if (*out) /* (0 is what a word read during an error gives) */
                    return 0;
            }
            if (cpu_cycles() - t0 > SystemCoreClock / 100u)
                break;
        }
        g->errors++;
        reset(g);
    }
    dev_err(g->dev, "no entropy (%lu errors, SR %08lx)\n", (unsigned long)g->errors, (unsigned long)r->SR);
    return -EIO;
}

static int rng_read(struct file *f, void *buf, size_t len)
{
    struct rng *g = f->dev;
    if (len > READ_MAX)
        len = READ_MAX;
    int r = mutex_lock(&g->lock, WAIT_FOREVER);
    if (r)
        return r;
    uint8_t *out = buf;
    size_t done = 0;
    while (done < len)
    {
        uint32_t v;
        r = word(g, &v);
        if (r)
            break;
        size_t n = len - done < 4u ? len - done : 4u;
        memcpy(out + done, &v, n);
        done += n;
    }
    mutex_unlock(&g->lock);
    return done ? (int)done : r;
}

static int rng_write(struct file *f, const void *buf, size_t len)
{
    (void)f;
    (void)buf;
    return (int)len; /* accepted and ignored, as mixing in data would not add to hardware entropy */
}

static const struct file_ops rng_ops = {
    .read = rng_read,
    .write = rng_write,
};

static int rng_probe(struct device *dev)
{
    struct rng *g = devm_kzalloc(dev, sizeof(*g), 0);
    if (!g)
        return -ENOMEM;
    g->dev = dev;
    g->r = device_map(dev, 0);
    if (!g->r)
        return -ENODEV;
    struct clk *bus, *ker;
    int r = devm_clk_get_enabled(dev, "bus", &bus);
    if (!r)
        r = devm_clk_get_enabled(dev, "kernel", &ker);
    if (r)
        return r;
    mutex_init(&g->lock);
    g->r->CR &= ~RNG_CR_CED; /* clock error detection on */
    uint32_t v;
    if (reset(g) || word(g, &v))
    {
        dev_err(dev, "cannot start the generator\n");
        return -EIO;
    }
    dev_set_drvdata(dev, g);
    r = devfs_register("random", &rng_ops, g);
    if (!r)
    {
        r = devfs_register("urandom", &rng_ops, g);
        if (r)
            devfs_unregister("random");
    }
    if (r)
        return r;
    dev_info(dev, "/dev/random, /dev/urandom\n");
    return 0;
}

static void rng_remove(struct device *dev)
{
    struct rng *g = dev_get_drvdata(dev);
    devfs_unregister("urandom");
    devfs_unregister("random");
    g->r->CR &= ~RNG_CR_RNGEN;
}

static const struct of_device_id rng_ids[] = {
    {"st,stm32-rng", NULL},
    {NULL, NULL},
};

static struct driver rng_driver = {
    .name = "rng-stm32",
    .of_match_table = rng_ids,
    .probe = rng_probe,
    .remove = rng_remove,
};

static int init(void)
{
    return driver_register(&rng_driver);
}

static void fini(void)
{
    driver_unregister(&rng_driver);
}

MODULE("rng-stm32", "STM32 true random number generator", init, fini);
