/*
 * i2c-stm32.ko - STM32 I2C bus controller (master), interrupt driven.
 *
 * The messages of one transfer go out back to back: each but the last ends without STOP and
 * the next starts with a repeated START (the usual register read: write the register number,
 * then read). Up to 255 bytes go in one go, longer messages continue with RELOAD. The event
 * and error interrupts share one handler; the calling thread sleeps until the STOP. A transfer
 * that times out, or a bus error, resets the controller (PE off and on) so the next transfer
 * starts on an idle bus.
 *
 * Bus timing: from the kernel clock (PCLK1 by default) for 100 kHz or 400 kHz, the
 * "clock-frequency" of the device tree node.
 */
#include <stdbool.h>
#include <crtos/arch.h>
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/i2c.h>
#include <crtos/irq.h>
#include <crtos/module.h>
#include <crtos/of.h>
#include <crtos/pinctrl.h>
#include <crtos/printk.h>
#include <crtos/sched.h>
#include <crtos/sync.h>
#include "stm32h7rsxx.h"

#define XFER_TIMEOUT_MS 100
#define CR1_IRQS (I2C_CR1_TXIE | I2C_CR1_RXIE | I2C_CR1_NACKIE | I2C_CR1_STOPIE | I2C_CR1_TCIE | I2C_CR1_ERRIE)
#define ICR_ALL (I2C_ICR_ADDRCF | I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF | \
                 I2C_ICR_OVRCF | I2C_ICR_PECCF | I2C_ICR_TIMOUTCF | I2C_ICR_ALERTCF)

struct i2c {
    struct device *dev;
    I2C_TypeDef *r;
    int irq_ev, irq_er;
    struct semaphore done;
    struct i2c_adapter *adap;
    uint32_t timing;
    /* the transfer in progress (the interrupt handler's) */
    struct i2c_msg *msgs;
    int num, cur;
    uint32_t pos;           /* bytes of msgs[cur] moved */
    uint32_t left;          /* bytes of msgs[cur] not yet given to NBYTES */
    volatile int result;    /* 1: running, 0: done, <0: error */
    int nack;               /* the slave said NACK: -ENXIO once the STOP is out */
    uint32_t timeouts, errors;
};

/* NBYTES for the next part of the current message, RELOAD when more follows */
static uint32_t chunk(struct i2c *p, bool *reload)
{
    uint32_t n = p->left > 255u ? 255u : p->left;
    p->left -= n;
    *reload = p->left != 0;
    return n;
}

static void start_msg(struct i2c *p)
{
    struct i2c_msg *m = &p->msgs[p->cur];
    p->pos = 0;
    p->left = m->len;
    bool reload;
    uint32_t n = chunk(p, &reload);
    uint32_t cr2 = ((uint32_t)m->addr << 1) | (n << I2C_CR2_NBYTES_Pos) | I2C_CR2_START;
    if (m->flags & I2C_M_RD)
        cr2 |= I2C_CR2_RD_WRN;
    if (reload)
        cr2 |= I2C_CR2_RELOAD;
    p->r->CR2 = cr2;    /* (no AUTOEND: the TC interrupt chains the next message or the STOP) */
}

static void finish(struct i2c *p, int result)
{
    p->r->CR1 &= ~CR1_IRQS;
    p->result = result;
    sem_give(&p->done);
}

static void i2c_irq(int irq, void *ctx)
{
    (void)irq;
    struct i2c *p = ctx;
    I2C_TypeDef *r = p->r;
    uint32_t isr = r->ISR;
    if (p->result != 1) {           /* nothing running: nothing may interrupt */
        r->CR1 &= ~CR1_IRQS;
        r->ICR = ICR_ALL;
        return;
    }
    struct i2c_msg *m = &p->msgs[p->cur];
    if (isr & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) {
        r->ICR = I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
        finish(p, -EIO);
        return;
    }
    if (isr & I2C_ISR_NACKF) {
        r->ICR = I2C_ICR_NACKCF;
        p->nack = -ENXIO;           /* the STOP that follows ends it */
        if (!(isr & I2C_ISR_STOPF))
            r->CR2 |= I2C_CR2_STOP;
    }
    if ((isr & I2C_ISR_RXNE) && (m->flags & I2C_M_RD) && p->pos < m->len)
        m->buf[p->pos++] = (uint8_t)r->RXDR;
    if ((isr & I2C_ISR_TXIS) && !(m->flags & I2C_M_RD) && p->pos < m->len)
        r->TXDR = m->buf[p->pos++];
    if (isr & I2C_ISR_TCR) {        /* RELOAD: the next part of the message */
        bool reload;
        uint32_t n = chunk(p, &reload);
        r->CR2 = (r->CR2 & ~(I2C_CR2_NBYTES | I2C_CR2_RELOAD)) | (n << I2C_CR2_NBYTES_Pos) |
                 (reload ? I2C_CR2_RELOAD : 0u);
    }
    if (isr & I2C_ISR_TC) {         /* a message done: the next with a repeated START, or STOP */
        if (p->cur + 1 < p->num) {
            p->cur++;
            start_msg(p);
        } else {
            r->CR2 |= I2C_CR2_STOP;
        }
    }
    if (isr & I2C_ISR_STOPF) {
        r->ICR = I2C_ICR_STOPCF;
        finish(p, p->nack);
    }
}

static void reset_controller(struct i2c *p)
{
    p->r->CR1 &= ~I2C_CR1_PE;
    for (volatile int i = 0; i < 100; i++) {   /* PE low for at least 3 APB clocks */
    }
    p->r->ICR = ICR_ALL;
    p->r->TIMINGR = p->timing;
    p->r->CR1 = I2C_CR1_PE;
}

static int i2c_xfer(void *ctx, struct i2c_msg *msgs, int num)
{
    struct i2c *p = ctx;
    if (num <= 0)
        return 0;
    for (int i = 0; i < num; i++)
        if (!msgs[i].len)
            return -EINVAL;     /* (a zero-length message would need NBYTES 0 handling) */
    while (sem_take(&p->done, 0) == 0) {
        /* a late completion of a transfer that timed out */
    }
    if (p->r->ISR & I2C_ISR_BUSY) {
        task_sleep_ms(1);
        if (p->r->ISR & I2C_ISR_BUSY)
            reset_controller(p);
    }
    p->msgs = msgs;
    p->num = num;
    p->cur = 0;
    p->nack = 0;
    p->result = 1;
    uint32_t key = irq_lock();
    p->r->ICR = ICR_ALL;
    p->r->CR1 |= CR1_IRQS;
    start_msg(p);
    irq_unlock(key);
    if (sem_take(&p->done, XFER_TIMEOUT_MS)) {
        key = irq_lock();
        p->result = -ETIMEDOUT;
        p->r->CR1 &= ~CR1_IRQS;
        irq_unlock(key);
        reset_controller(p);
        if (++p->timeouts <= 5)
            dev_err(p->dev, "transfer to 0x%02x timed out (%lu so far)\n", (unsigned)msgs[0].addr,
                    (unsigned long)p->timeouts);
        return -ETIMEDOUT;
    }
    if (p->result == -EIO) {
        reset_controller(p);
        p->errors++;
    }
    return p->result ? p->result : num;
}

static const struct i2c_adapter_ops i2c_ops = { i2c_xfer };

/* TIMINGR for @bus_hz from a kernel clock of @f Hz (I2C specification minimums with margin) */
static uint32_t timing(uint32_t f, uint32_t bus_hz)
{
    bool fast = bus_hz > 100000u;
    uint32_t tp_ns = fast ? 25u : 100u;                     /* the prescaled clock period */
    uint32_t presc = (uint32_t)(((uint64_t)f * tp_ns + 999999999u) / 1000000000u);
    presc = presc < 1u ? 1u : presc > 16u ? 16u : presc;
    uint32_t tp = (uint32_t)(1000000000ull * presc / f);   /* ns */
    uint32_t low = fast ? 1450u : 5000u, high = fast ? 750u : 4200u;     /* tLOW, tHIGH */
    uint32_t scldel = fast ? 350u : 1250u, sdadel = fast ? 100u : 500u;
    uint32_t scll = (low + tp - 1u) / tp - 1u, sclh = (high + tp - 1u) / tp - 1u;
    uint32_t sd = (scldel + tp - 1u) / tp - 1u, dd = sdadel / tp;
    scll = scll > 255u ? 255u : scll;
    sclh = sclh > 255u ? 255u : sclh;
    sd = sd > 15u ? 15u : sd;
    dd = dd > 15u ? 15u : dd;
    return ((presc - 1u) << I2C_TIMINGR_PRESC_Pos) | (sd << I2C_TIMINGR_SCLDEL_Pos) |
           (dd << I2C_TIMINGR_SDADEL_Pos) | (sclh << I2C_TIMINGR_SCLH_Pos) | (scll << I2C_TIMINGR_SCLL_Pos);
}

static int i2c_probe(struct device *dev)
{
    struct i2c *p = devm_kzalloc(dev, sizeof(*p), 0);
    if (!p)
        return -ENOMEM;
    p->dev = dev;
    p->r = device_map(dev, 0);
    if (!p->r)
        return -EINVAL;
    struct clk *clk;
    int r = devm_clk_get_enabled(dev, NULL, &clk);
    if (r)
        return r;
    uint32_t f = clk_get_rate(clk), bus_hz = 100000;
    of_property_read_u32(dev->of_node, "clock-frequency", &bus_hz);
    p->timing = timing(f, bus_hz);
    sem_init(&p->done, 0, 1);
    p->r->CR1 = 0;
    reset_controller(p);

    p->irq_ev = device_get_irq(dev, 0);
    p->irq_er = device_get_irq(dev, 1);
    if (p->irq_ev < 0 || p->irq_er < 0)
        return -EINVAL;
    r = irq_request(p->irq_ev, i2c_irq, p, 6, dev->name);
    if (!r) {
        r = irq_request(p->irq_er, i2c_irq, p, 6, dev->name);
        if (r)
            irq_free(p->irq_ev);
    }
    if (r)
        return r;
    dev_set_drvdata(dev, p);
    r = i2c_adapter_register(dev, &i2c_ops, p, &p->adap);
    if (r) {
        irq_free(p->irq_ev);
        irq_free(p->irq_er);
        p->r->CR1 = 0;
        return r;
    }
    dev_info(dev, "I2C %lu Hz (kernel clock %lu MHz, TIMINGR %08lx)\n", (unsigned long)bus_hz,
             (unsigned long)(f / 1000000u), (unsigned long)p->timing);
    return 0;
}

static void i2c_remove(struct device *dev)
{
    struct i2c *p = dev_get_drvdata(dev);
    i2c_adapter_unregister(p->adap);
    irq_free(p->irq_ev);
    irq_free(p->irq_er);
    p->r->CR1 = 0;
    if (p->timeouts || p->errors)
        dev_info(dev, "%lu transfers timed out, %lu bus errors\n", (unsigned long)p->timeouts,
                 (unsigned long)p->errors);
}

static const struct of_device_id i2c_ids[] = {
    { "st,stm32-i2c", NULL },
    { NULL, NULL },
};

static struct driver i2c_driver = {
    .name = "i2c-stm32",
    .of_match_table = i2c_ids,
    .probe = i2c_probe,
    .remove = i2c_remove,
};

static int init(void)
{
    return driver_register(&i2c_driver);
}

static void fini(void)
{
    driver_unregister(&i2c_driver);
}

MODULE("i2c-stm32", "STM32 I2C bus controller", init, fini);
