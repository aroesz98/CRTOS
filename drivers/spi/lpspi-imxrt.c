/*
 * lpspi-imxrt.ko - the LPSPI of the i.MX RT as an SPI bus master (crtos/spi.h).
 *
 * Device tree: compatible "fsl,imxrt1050-lpspi", reg, interrupts, clocks "ipg" (the gate) and
 * "per" (the LPSPI root, set to 66 MHz), pinctrl; child nodes are the devices on the bus
 * (reg = chip select PCS0..3). The chip selects are the LPSPI's own: the parts of a message
 * with the same speed go out as one transfer, so the chip select stays active between them
 * whatever their length (it changes only where a part asks for cs_change or a delay). A single
 * part goes straight from and into its buffers, several are gathered first. 8-bit words. No
 * SPI_LOOP: the LPSPI cannot send and receive on one pin at once - for a check, wire SDO to
 * SDI.
 */
#include <string.h>
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/irq.h>
#include <crtos/mm.h>
#include <crtos/module.h>
#include <crtos/printk.h>
#include <crtos/sched.h>
#include <crtos/spi.h>
#include <crtos/sync.h>
#include "fsl_lpspi.h"

#define ROOT_HZ 66000000u /* PLL3 PFD0 (261.8 MHz, set for the flash) / 4 */
#define BOUNCE 4096u      /* gathered bytes each way without a bigger buffer */
#define TIMEOUT_MS 2000u
#define NUM_CS 4

struct lpspi
{
    struct device *dev;
    LPSPI_Type *base;
    int irq;
    uint32_t clk_hz;
    lpspi_master_handle_t handle;
    struct semaphore done;
    volatile status_t status;
    struct spi_controller *ctlr;
    bool configured;
    uint32_t cur_speed, cur_mode;
    uint8_t cur_cs;
    uint8_t tx[BOUNCE], rx[BOUNCE];
};

static void lpspi_irq(int irq, void *ctx)
{
    struct lpspi *l = ctx;
    (void)irq;
    LPSPI_MasterTransferHandleIRQ(l->base, &l->handle);
}

static void lpspi_done(LPSPI_Type *base, lpspi_master_handle_t *handle, status_t status, void *user)
{
    struct lpspi *l = user;
    (void)base;
    (void)handle;
    l->status = status;
    sem_give(&l->done);
}

static void configure(struct lpspi *l, uint32_t speed, uint32_t mode, uint8_t cs)
{
    if (l->configured && speed == l->cur_speed && mode == l->cur_mode && cs == l->cur_cs)
        return;
    lpspi_master_config_t cfg;
    LPSPI_MasterGetDefaultConfig(&cfg);
    cfg.baudRate = speed;
    cfg.bitsPerFrame = 8;
    cfg.cpol = (mode & SPI_CPOL) ? kLPSPI_ClockPolarityActiveLow : kLPSPI_ClockPolarityActiveHigh;
    cfg.cpha = (mode & SPI_CPHA) ? kLPSPI_ClockPhaseSecondEdge : kLPSPI_ClockPhaseFirstEdge;
    cfg.direction = (mode & SPI_LSB_FIRST) ? kLPSPI_LsbFirst : kLPSPI_MsbFirst;
    uint32_t half_ns = 500000000u / speed; /* half an SCK period around the chip select */
    cfg.pcsToSckDelayInNanoSec = half_ns;
    cfg.lastSckToPcsDelayInNanoSec = half_ns;
    cfg.betweenTransferDelayInNanoSec = half_ns;
    cfg.whichPcs = (lpspi_which_pcs_t)cs;
    cfg.pcsActiveHighOrLow = (mode & SPI_CS_HIGH) ? kLPSPI_PcsActiveHigh : kLPSPI_PcsActiveLow;
    cfg.pinCfg = kLPSPI_SdiInSdoOut;
    cfg.dataOutConfig = kLpspiDataOutTristate; /* SDO only driven for a message (on the EVKB it
                                                * is also the ID pin of the USB port J9) */
    LPSPI_MasterInit(l->base, &cfg, l->clk_hz);
    l->configured = true;
    l->cur_speed = speed;
    l->cur_mode = mode;
    l->cur_cs = cs;
}

/* One transfer of @len bytes, the chip select held throughout; @tx NULL sends zeros, @rx NULL
 * drops what comes in */
static int run(struct lpspi *l, const uint8_t *tx, uint8_t *rx, uint32_t len, uint8_t cs)
{
    while (!sem_take(&l->done, 0)) /* a stale completion */
        ;
    lpspi_transfer_t x;
    x.txData = (uint8_t *)tx;
    x.rxData = rx;
    x.dataSize = len;
    x.configFlags = ((uint32_t)cs << LPSPI_MASTER_PCS_SHIFT) | kLPSPI_MasterPcsContinuous;
    if (LPSPI_MasterTransferNonBlocking(l->base, &l->handle, &x) != kStatus_Success)
        return -EIO;
    /* twice the time on the wire, for slow clocks */
    int r = sem_take(&l->done, TIMEOUT_MS + (uint32_t)((uint64_t)len * 16000u / l->cur_speed));
    if (r)
    { /* killed or stuck: stop it before the buffers go */
        LPSPI_MasterTransferAbort(l->base, &l->handle);
        l->configured = false;
        return r == -ETIMEDOUT ? -EIO : r;
    }
    return l->status == kStatus_Success ? 0 : -EIO;
}

static int lpspi_transfer(void *ctx, struct spi_device *spi, struct spi_transfer *xfers, int num)
{
    struct lpspi *l = ctx;
    for (int i = 0; i < num; i++)
        if (xfers[i].bits_per_word && xfers[i].bits_per_word != 8)
            return -EINVAL; /* 8-bit words only */
    if (spi->bits_per_word != 8)
        return -EINVAL;
    if (spi->mode & SPI_LOOP)
        return -ENOTSUP;
    for (int i = 0; i < num;)
    {
        /* the parts that go out as one transfer: same speed, no cs_change or delay before
         * the last */
        uint32_t speed = xfers[i].speed_hz ? xfers[i].speed_hz : spi->max_speed_hz, total = 0;
        int j = i;
        while (j < num)
        {
            uint32_t s = xfers[j].speed_hz ? xfers[j].speed_hz : spi->max_speed_hz;
            if (s != speed || (j > i && (xfers[j - 1].cs_change || xfers[j - 1].delay_us)))
                break;
            total += xfers[j].len;
            j++;
        }
        if (!speed)
            return -EINVAL;
        configure(l, speed, spi->mode, spi->cs);
        int r;
        if (j == i + 1 && (xfers[i].tx_buf || xfers[i].rx_buf))
        {
            /* one part: straight from and into its own buffers */
            r = total ? run(l, (const uint8_t *)xfers[i].tx_buf, (uint8_t *)xfers[i].rx_buf, total, spi->cs) : 0;
        }
        else
        {
            /* several (or clocks only): gathered each way into one buffer, a bigger one than
             * the driver's own when they need it */
            uint8_t *big = total > BOUNCE ? (uint8_t *)kmalloc(2 * total, KM_LARGE) : NULL;
            if (total > BOUNCE && !big)
                return -ENOMEM;
            uint8_t *tx = big ? big : l->tx, *rx = big ? big + total : l->rx;
            uint32_t off = 0;
            for (int k = i; k < j; k++)
            {
                if (xfers[k].tx_buf)
                    memcpy(tx + off, xfers[k].tx_buf, xfers[k].len);
                else
                    memset(tx + off, 0, xfers[k].len);
                off += xfers[k].len;
            }
            r = total ? run(l, tx, rx, total, spi->cs) : 0;
            off = 0;
            for (int k = i; k < j && !r; k++)
            {
                if (xfers[k].rx_buf)
                    memcpy(xfers[k].rx_buf, rx + off, xfers[k].len);
                off += xfers[k].len;
            }
            kfree(big);
        }
        if (r)
            return r;
        if (xfers[j - 1].delay_us)
            SDK_DelayAtLeastUs(xfers[j - 1].delay_us, SystemCoreClock);
        i = j;
    }
    return 0;
}

static const struct spi_controller_ops lpspi_ops = {lpspi_transfer};

static int lpspi_probe(struct device *dev)
{
    struct lpspi *l = devm_kzalloc(dev, sizeof(*l), 0);
    if (!l)
        return -ENOMEM;
    l->dev = dev;
    l->base = device_map(dev, 0);
    l->irq = device_get_irq(dev, 0);
    if (!l->base || l->irq < 0)
        return l->irq == -EPROBE_DEFER ? -EPROBE_DEFER : -ENODEV;
    /* the LPSPI root first (its gates off), then the gate of this one */
    struct clk *per, *ipg;
    int r = devm_clk_get(dev, "per", &per);
    if (!r)
    {
        clk_set_rate(per, ROOT_HZ);
        r = devm_clk_get_enabled(dev, "ipg", &ipg);
    }
    if (r)
        return r;
    l->clk_hz = clk_get_rate(per);
    sem_init(&l->done, 0, 1);
    configure(l, 1000000, 0, 0);
    LPSPI_MasterTransferCreateHandle(l->base, &l->handle, lpspi_done, l);
    r = irq_request(l->irq, lpspi_irq, l, 8, dev->name);
    if (r)
        return r;
    dev_set_drvdata(dev, l);
    r = spi_controller_register(dev, &lpspi_ops, l, NUM_CS, &l->ctlr);
    if (r)
    {
        irq_free(l->irq);
        return r;
    }
    dev_info(dev, "LPSPI, %lu MHz functional clock (SCK up to %lu MHz), bus spi%d\n",
             (unsigned long)(l->clk_hz / 1000000u), (unsigned long)(l->clk_hz / 2000000u), spi_controller_bus(l->ctlr));
    return 0;
}

static void lpspi_remove(struct device *dev)
{
    struct lpspi *l = dev_get_drvdata(dev);
    spi_controller_unregister(l->ctlr);
    irq_free(l->irq);
    LPSPI_Deinit(l->base);
}

static const struct of_device_id lpspi_ids[] = {
    {"fsl,imxrt1050-lpspi", NULL},
    {NULL, NULL},
};

static struct driver lpspi_driver = {
    .name = "lpspi-imxrt",
    .of_match_table = lpspi_ids,
    .probe = lpspi_probe,
    .remove = lpspi_remove,
};

static int init(void)
{
    return driver_register(&lpspi_driver);
}

static void fini(void)
{
    driver_unregister(&lpspi_driver);
}

MODULE("lpspi-imxrt", "i.MX RT LPSPI: SPI bus master", init, fini);
