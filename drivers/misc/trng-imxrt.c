/*
 * trng-imxrt.ko - the i.MX RT true random number generator (TRNG) as /dev/random and
 * /dev/urandom: entropy for TLS keys and anything else that must not be predictable.
 *
 * The TRNG samples a free-running ring oscillator and, once its statistical self tests have
 * passed, holds 512 bits of entropy in ENT0..ENT15; reading ENT15 starts the next
 * generation (a few milliseconds with the SDK's default settings). A reader sleeps until the
 * entropy-valid (or error) interrupt. Both files give the same hardware entropy, each bit
 * handed out once; there is no software pool that could be predicted.
 */
#include <stdbool.h>
#include <string.h>
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/irq.h>
#include <crtos/module.h>
#include <crtos/printk.h>
#include <crtos/sched.h>
#include <crtos/sync.h>
#include <crtos/vfs.h>
#include "fsl_trng.h"

#define ENT_WORDS 16u
#define WAIT_MS 200u   /* for one generation: normally a few ms */
#define ATTEMPTS 4     /* generations tried before a read fails */
#define READ_MAX 4096u /* bytes per read() */

struct trng
{
    struct device *dev;
    TRNG_Type *base;
    int irq;
    struct mutex lock;      /* one reader at a time */
    struct semaphore ready; /* given by the interrupt */
    uint32_t pool[ENT_WORDS];
    uint32_t avail; /* bytes not handed out yet, at the end of pool */
    uint32_t errors;
};

static void trng_irq(int irq, void *ctx)
{
    struct trng *t = ctx;
    (void)irq;
    t->base->INT_MASK = 0; /* the reader unmasks it again when it waits */
    t->base->INT_CTRL = 0; /* writing 0 clears the status bits */
    sem_give(&t->ready);
}

static bool done_or_failed(TRNG_Type *b)
{
    return (b->MCTL & (TRNG_MCTL_ENT_VAL_MASK | TRNG_MCTL_ERR_MASK)) != 0;
}

/* Wait for the generation in progress and take its 512 bits into the pool */
static int generate(struct trng *t)
{
    TRNG_Type *b = t->base;
    for (int attempt = 0; attempt < ATTEMPTS; attempt++)
    {
        if (!done_or_failed(b))
        {
            while (!sem_take(&t->ready, 0)) /* a stale wake-up */
                ;
            b->INT_CTRL = 0;
            b->INT_MASK = TRNG_INT_MASK_ENT_VAL_MASK | TRNG_INT_MASK_HW_ERR_MASK;
            if (!done_or_failed(b)) /* (finished before the unmasking: no interrupt comes) */
                sem_take(&t->ready, WAIT_MS);
            b->INT_MASK = 0;
        }
        if (b->MCTL & TRNG_MCTL_ERR_MASK)
        {
            /* a self test failed: clear it and start over */
            t->errors++;
            b->MCTL |= TRNG_MCTL_ERR_MASK; /* write 1 to clear */
            (void)b->ENT[ENT_WORDS - 1];
            continue;
        }
        if (b->MCTL & TRNG_MCTL_ENT_VAL_MASK)
        {
            for (uint32_t i = 0; i < ENT_WORDS; i++) /* ENT15 last: the next generation starts */
                t->pool[i] = b->ENT[i];
            t->avail = sizeof(t->pool);
            return 0;
        }
    }
    dev_err(t->dev, "no entropy (%lu self-test failures)\n", (unsigned long)t->errors);
    return -EIO;
}

static int trng_read(struct file *f, void *buf, size_t len)
{
    struct trng *t = f->dev;
    if (len > READ_MAX)
        len = READ_MAX;
    int r = mutex_lock(&t->lock, WAIT_FOREVER);
    if (r)
        return r;
    uint8_t *out = buf;
    size_t done = 0;
    while (done < len)
    {
        if (!t->avail)
        {
            r = generate(t);
            if (r)
                break;
        }
        size_t n = len - done < t->avail ? len - done : t->avail;
        uint8_t *src = (uint8_t *)t->pool + sizeof(t->pool) - t->avail;
        memcpy(out + done, src, n);
        memset(src, 0, n);
        t->avail -= (uint32_t)n;
        done += n;
    }
    mutex_unlock(&t->lock);
    return done ? (int)done : r;
}

static int trng_write(struct file *f, const void *buf, size_t len)
{
    (void)f;
    (void)buf;
    return (int)len; /* accepted and ignored, as mixing in data would not add to hardware entropy */
}

static const struct file_ops trng_ops = {
    .read = trng_read,
    .write = trng_write,
};

static int trng_probe(struct device *dev)
{
    struct trng *t = devm_kzalloc(dev, sizeof(*t), 0);
    if (!t)
        return -ENOMEM;
    t->dev = dev;
    t->base = device_map(dev, 0);
    if (!t->base)
        return -ENODEV;
    struct clk *clk;
    int r = devm_clk_get_enabled(dev, NULL, &clk);
    if (r)
        return r;
    mutex_init(&t->lock);
    sem_init(&t->ready, 0, 1);

    trng_config_t cfg;
    TRNG_GetDefaultConfig(&cfg);
    if (TRNG_Init(t->base, &cfg) != kStatus_Success)
    {
        dev_err(dev, "cannot start the generator\n");
        return -EIO;
    }
    t->base->INT_MASK = 0;
    t->irq = device_get_irq(dev, 0);
    if (t->irq < 0)
        return t->irq;
    r = irq_request(t->irq, trng_irq, t, 8, dev->name);
    if (r)
        return r;
    dev_set_drvdata(dev, t);
    r = devfs_register("random", &trng_ops, t);
    if (!r)
    {
        r = devfs_register("urandom", &trng_ops, t);
        if (r)
            devfs_unregister("random");
    }
    if (r)
    {
        irq_free(t->irq);
        return r;
    }
    dev_info(dev, "/dev/random, /dev/urandom\n");
    return 0;
}

static void trng_remove(struct device *dev)
{
    struct trng *t = dev_get_drvdata(dev);
    devfs_unregister("urandom");
    devfs_unregister("random");
    irq_free(t->irq);
    TRNG_Deinit(t->base);
}

static const struct of_device_id trng_ids[] = {
    {"fsl,imxrt1050-trng", NULL},
    {NULL, NULL},
};

static struct driver trng_driver = {
    .name = "trng-imxrt",
    .of_match_table = trng_ids,
    .probe = trng_probe,
    .remove = trng_remove,
};

static int init(void)
{
    return driver_register(&trng_driver);
}

static void fini(void)
{
    driver_unregister(&trng_driver);
}

MODULE("trng-imxrt", "i.MX RT true random number generator", init, fini);
