/*
 * lpi2c-imxrt.ko - i.MX RT LPI2C bus controller (master), interrupt driven.
 *
 * Transfers run through the SDK transactional API; the calling thread sleeps until the
 * completion callback. A write of up to 4 bytes followed by a read of the same device is
 * sent as one transaction (register address + repeated start + read).
 *
 * Guards around the SDK: after a transfer timed out the controller is brought back to an idle
 * bus (the STOP of the aborted transfer must not end the next one) and a late completion of
 * it is dropped; a transfer whose bus transaction ended (STOP) while the SDK still waited for
 * data is ended with an error. Left alone, the unhandled stop-detect flag interrupted without
 * end and starved the whole system (seen with the touch controller and the audio codec on
 * LPI2C1).
 */
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/i2c.h>
#include <crtos/irq.h>
#include <crtos/module.h>
#include <crtos/printk.h>
#include <crtos/sched.h>
#include <crtos/sync.h>
#include "fsl_lpi2c.h"

#define XFER_TIMEOUT_MS 100
#define LPI2C_ROOT_HZ 10000000u

struct lpi2c
{
    struct device *dev;
    LPI2C_Type *base;
    int irq;
    lpi2c_master_handle_t handle;
    struct semaphore done;
    volatile status_t status;
    struct i2c_adapter *adap;
    lpi2c_master_config_t cfg;
    uint32_t rate;
    uint32_t timeouts, stray_stops;
};

static void xfer_done(LPI2C_Type *base, lpi2c_master_handle_t *h, status_t st, void *user)
{
    (void)base;
    (void)h;
    struct lpi2c *p = user;
    p->status = st;
    sem_give(&p->done);
}

static void lpi2c_irq(int irq, void *ctx)
{
    (void)irq;
    struct lpi2c *p = ctx;
    LPI2C_MasterTransferHandleIRQ(p->base, &p->handle);
    if (p->handle.state == 0)
    { /* idle: no transfer, nothing may interrupt */
        if (p->base->MIER)
            LPI2C_MasterDisableInterrupts(p->base, (uint32_t)kLPI2C_MasterIrqFlags);
        return;
    }
    if (LPI2C_MasterGetStatusFlags(p->base) & (uint32_t)kLPI2C_MasterStopDetectFlag)
    {
        /* the transaction is over on the bus, the SDK still waits for data that will not
         * come: end the transfer with an error */
        LPI2C_MasterTransferAbort(p->base, &p->handle);
        LPI2C_MasterClearStatusFlags(p->base, (uint32_t)kLPI2C_MasterClearFlags);
        p->stray_stops++;
        p->status = kStatus_Fail;
        sem_give(&p->done);
    }
}

/* after an aborted transfer: wait for the controller to finish on the bus (its STOP), empty
 * the FIFOs and clear the flags; reset the controller if it does not finish */
static void recover(struct lpi2c *p)
{
    for (int i = 0; i < 10 && (LPI2C_MasterGetStatusFlags(p->base) & (uint32_t)kLPI2C_MasterBusyFlag); i++)
        task_sleep_ms(1);
    if (LPI2C_MasterGetStatusFlags(p->base) & (uint32_t)kLPI2C_MasterBusyFlag)
    {
        LPI2C_MasterDeinit(p->base);
        LPI2C_MasterInit(p->base, &p->cfg, p->rate);
        LPI2C_MasterTransferCreateHandle(p->base, &p->handle, p->handle.completionCallback, p);
    }
    p->base->MCR |= LPI2C_MCR_RRF_MASK | LPI2C_MCR_RTF_MASK;
    LPI2C_MasterClearStatusFlags(p->base, (uint32_t)kLPI2C_MasterClearFlags);
}

static int run(struct lpi2c *p, lpi2c_master_transfer_t *t)
{
    while (sem_take(&p->done, 0) == 0)
    {
        /* a late completion of an aborted transfer */
    }
    uint32_t stray = p->stray_stops;
    if (LPI2C_MasterTransferNonBlocking(p->base, &p->handle, t) != kStatus_Success)
        return -EBUSY;
    if (sem_take(&p->done, XFER_TIMEOUT_MS))
    {
        uint32_t key = irq_lock();
        LPI2C_MasterTransferAbort(p->base, &p->handle);
        irq_unlock(key);
        recover(p);
        if (++p->timeouts <= 5)
            dev_err(p->dev, "transfer to 0x%02x timed out (%lu so far)\n", (unsigned)t->slaveAddress,
                    (unsigned long)p->timeouts);
        return -ETIMEDOUT;
    }
    if (p->stray_stops != stray)
    {
        recover(p);
        if (p->stray_stops <= 5)
            dev_err(p->dev, "transfer to 0x%02x ended early on the bus (%lu so far)\n", (unsigned)t->slaveAddress,
                    (unsigned long)p->stray_stops);
    }
    if (p->status == kStatus_Success)
        return 0;
    return p->status == kStatus_LPI2C_Nak ? -ENXIO : -EIO;
}

static int lpi2c_xfer(void *ctx, struct i2c_msg *msgs, int num)
{
    struct lpi2c *p = ctx;
    for (int i = 0; i < num; i++)
    {
        lpi2c_master_transfer_t t = {0};
        t.slaveAddress = msgs[i].addr;
        if (i + 1 < num && !(msgs[i].flags & I2C_M_RD) && (msgs[i + 1].flags & I2C_M_RD) && msgs[i].len <= 4 &&
            msgs[i].addr == msgs[i + 1].addr)
        {
            uint32_t sub = 0;
            for (int k = 0; k < msgs[i].len; k++)
                sub = (sub << 8) | msgs[i].buf[k];
            t.flags = kLPI2C_TransferDefaultFlag;
            t.direction = kLPI2C_Read;
            t.subaddress = sub;
            t.subaddressSize = msgs[i].len;
            t.data = msgs[i + 1].buf;
            t.dataSize = msgs[i + 1].len;
            int r = run(p, &t);
            if (r)
                return r;
            i++;
            continue;
        }
        t.flags = (i > 0 ? kLPI2C_TransferRepeatedStartFlag : 0) | (i + 1 < num ? kLPI2C_TransferNoStopFlag : 0);
        t.direction = (msgs[i].flags & I2C_M_RD) ? kLPI2C_Read : kLPI2C_Write;
        t.data = msgs[i].buf;
        t.dataSize = msgs[i].len;
        int r = run(p, &t);
        if (r)
            return r;
    }
    return num;
}

static const struct i2c_adapter_ops lpi2c_ops = {lpi2c_xfer};

static int lpi2c_probe(struct device *dev)
{
    struct lpi2c *p = devm_kzalloc(dev, sizeof(*p), 0);
    if (!p)
        return -ENOMEM;
    p->dev = dev;
    p->base = device_map(dev, 0);
    struct clk *ipg, *per;
    int r = devm_clk_get_enabled(dev, "ipg", &ipg);
    if (!r)
        r = devm_clk_get(dev, "per", &per);
    if (r)
        return r;
    uint32_t rate = clk_get_rate(per);
    if (!rate || rate > 60000000u)
    {
        clk_set_rate(per, LPI2C_ROOT_HZ);
        rate = clk_get_rate(per);
    }
    uint32_t bus_hz = 100000;
    of_property_read_u32(dev->of_node, "clock-frequency", &bus_hz);

    LPI2C_MasterGetDefaultConfig(&p->cfg);
    p->cfg.baudRate_Hz = bus_hz;
    p->rate = rate;
    LPI2C_MasterInit(p->base, &p->cfg, rate);
    sem_init(&p->done, 0, 1);

    p->irq = device_get_irq(dev, 0);
    if (p->irq < 0)
        return p->irq;
    r = irq_request(p->irq, lpi2c_irq, p, 6, dev->name);
    if (r)
        return r;
    LPI2C_MasterTransferCreateHandle(p->base, &p->handle, xfer_done, p);
    dev_set_drvdata(dev, p);
    dev_info(dev, "LPI2C %lu Hz (module clock %lu Hz)\n", (unsigned long)bus_hz, (unsigned long)rate);
    return i2c_adapter_register(dev, &lpi2c_ops, p, &p->adap);
}

static void lpi2c_remove(struct device *dev)
{
    struct lpi2c *p = dev_get_drvdata(dev);
    i2c_adapter_unregister(p->adap);
    irq_free(p->irq);
    LPI2C_MasterDeinit(p->base);
}

static const struct of_device_id lpi2c_ids[] = {
    {"fsl,imxrt1050-lpi2c", NULL},
    {NULL, NULL},
};

static struct driver lpi2c_driver = {
    .name = "lpi2c-imxrt",
    .of_match_table = lpi2c_ids,
    .probe = lpi2c_probe,
    .remove = lpi2c_remove,
};

static int init(void)
{
    return driver_register(&lpi2c_driver);
}

static void fini(void)
{
    driver_unregister(&lpi2c_driver);
}

MODULE("lpi2c-imxrt", "i.MX RT LPI2C bus controller", init, fini);
