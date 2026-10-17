/*
 * lpuart-imxrt.ko - the LPUARTs of the i.MX RT other than the kernel console as serial ports,
 * /dev/ttyS<n> (n: the "serial<n>" alias, else the LPUART number).
 *
 * Device tree: compatible "fsl,imxrt1050-lpuart", reg, interrupts, clocks "ipg" (the gate)
 * and "per" (the UART root, 80 MHz, shared with the console), pinctrl, current-speed (baud,
 * default 115200). The port of the kernel console (chosen/stdout-path) is left alone.
 *
 * Raw bytes, 8N1: interrupt-driven rings each way; read() returns what has arrived (at least
 * one byte), write() waits for room; poll() works. ioctl: TTY_IOC_GET/SET_SPEED,
 * TTY_IOC_SET_LOOPBACK (sent bytes come back internally - a check without wires).
 */
#include <string.h>
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/irq.h>
#include <crtos/module.h>
#include <crtos/of.h>
#include <crtos/poll.h>
#include <crtos/printk.h>
#include <crtos/sync.h>
#include <crtos/tty.h>
#include <crtos/vfs.h>
#include "fsl_lpuart.h"

#define RING 2048u /* each way, a power of 2 */
#define EV_RX 0x1u
#define EV_TX 0x2u

/* STAT bits that are write-one-to-clear flags */
#define STAT_W1C (LPUART_STAT_LBKDIF_MASK | LPUART_STAT_RXEDGIF_MASK | LPUART_STAT_IDLE_MASK | LPUART_STAT_OR_MASK | \
                  LPUART_STAT_NF_MASK | LPUART_STAT_FE_MASK | LPUART_STAT_PF_MASK | LPUART_STAT_MA1F_MASK |          \
                  LPUART_STAT_MA2F_MASK)

struct ring
{
    uint8_t buf[RING];
    volatile uint32_t head, tail;
};

struct lpuart
{
    struct device *dev;
    LPUART_Type *base;
    int irq;
    uint32_t clk_hz, baud;
    char name[12];
    struct ring rx, tx; /* under irq_lock() */
    struct event ev;
    struct poll_head ph;
    uint32_t overruns, dropped;
};

static uint32_t count(const struct ring *r)
{
    return r->head - r->tail;
}

static uint32_t fifo_rx(LPUART_Type *b)
{
    return (b->WATER & LPUART_WATER_RXCOUNT_MASK) >> LPUART_WATER_RXCOUNT_SHIFT;
}

static uint32_t fifo_tx(LPUART_Type *b)
{
    return (b->WATER & LPUART_WATER_TXCOUNT_MASK) >> LPUART_WATER_TXCOUNT_SHIFT;
}

static uint32_t fifo_size(LPUART_Type *b)
{
    uint32_t f = (b->FIFO & LPUART_FIFO_TXFIFOSIZE_MASK) >> LPUART_FIFO_TXFIFOSIZE_SHIFT;
    return f ? 2u << f : 1u;
}

static void lpuart_irq(int irq, void *ctx)
{
    struct lpuart *u = ctx;
    LPUART_Type *b = u->base;
    (void)irq;
    uint32_t stat = b->STAT;
    if (stat & (LPUART_STAT_OR_MASK | LPUART_STAT_FE_MASK | LPUART_STAT_NF_MASK | LPUART_STAT_PF_MASK))
    {
        if (stat & LPUART_STAT_OR_MASK)
            u->overruns++;
        b->STAT = (stat & ~STAT_W1C) |
                  (stat & (LPUART_STAT_OR_MASK | LPUART_STAT_FE_MASK | LPUART_STAT_NF_MASK | LPUART_STAT_PF_MASK));
    }
    uint32_t events = 0;
    for (uint32_t n = fifo_rx(b); n; n--)
    {
        uint8_t c = (uint8_t)b->DATA;
        if (count(&u->rx) < RING)
        {
            u->rx.buf[u->rx.head & (RING - 1)] = c;
            u->rx.head++;
            events |= EV_RX;
        }
        else
        {
            u->dropped++;
        }
    }
    if (b->CTRL & LPUART_CTRL_TIE_MASK)
    {
        uint32_t room = fifo_size(b) - fifo_tx(b);
        while (room-- && count(&u->tx))
        {
            b->DATA = u->tx.buf[u->tx.tail & (RING - 1)];
            u->tx.tail++;
            events |= EV_TX;
        }
        if (!count(&u->tx))
            b->CTRL &= ~LPUART_CTRL_TIE_MASK;
    }
    if (events)
    {
        event_set(&u->ev, events);
        poll_notify(&u->ph);
    }
}

static int lpuart_read(struct file *f, void *buf, size_t len)
{
    struct lpuart *u = f->dev;
    uint8_t *p = buf;
    if (!len)
        return 0;
    for (;;)
    {
        uint32_t key = irq_lock();
        uint32_t n = 0;
        while (n < len && count(&u->rx))
        {
            p[n++] = u->rx.buf[u->rx.tail & (RING - 1)];
            u->rx.tail++;
        }
        if (!n)
            event_clear(&u->ev, EV_RX);
        irq_unlock(key);
        if (n)
            return (int)n;
        if (f->flags & VFS_O_NONBLOCK)
            return -EAGAIN;
        int32_t r = event_wait(&u->ev, EV_RX, EVENT_ANY, WAIT_FOREVER);
        if (r < 0)
            return r;
    }
}

static int lpuart_write(struct file *f, const void *buf, size_t len)
{
    struct lpuart *u = f->dev;
    const uint8_t *p = buf;
    size_t done = 0;
    while (done < len)
    {
        uint32_t key = irq_lock();
        uint32_t n = 0;
        while (done + n < len && count(&u->tx) < RING)
        {
            u->tx.buf[u->tx.head & (RING - 1)] = p[done + n];
            u->tx.head++;
            n++;
        }
        if (n)
            u->base->CTRL |= LPUART_CTRL_TIE_MASK; /* the interrupt sends it */
        else
            event_clear(&u->ev, EV_TX);
        irq_unlock(key);
        done += n;
        if (done == len)
            break;
        if (!n)
        {
            if (f->flags & VFS_O_NONBLOCK)
                return done ? (int)done : -EAGAIN;
            int32_t r = event_wait(&u->ev, EV_TX, EVENT_ANY, WAIT_FOREVER);
            if (r < 0)
                return done ? (int)done : r;
        }
    }
    return (int)len;
}

static int lpuart_poll(struct file *f, struct poll_entry *e)
{
    struct lpuart *u = f->dev;
    uint32_t key = irq_lock();
    int mask = (count(&u->rx) ? POLLIN : 0) | (count(&u->tx) < RING ? POLLOUT : 0);
    poll_add(&u->ph, e);
    irq_unlock(key);
    return mask;
}

static int set_speed(struct lpuart *u, uint32_t baud)
{
    if (!baud || LPUART_SetBaudRate(u->base, baud, u->clk_hz) != kStatus_Success)
        return -EINVAL;
    u->baud = baud;
    return 0;
}

static int lpuart_ioctl(struct file *f, unsigned cmd, void *arg)
{
    struct lpuart *u = f->dev;
    switch (cmd)
    {
    case TTY_IOC_GET_MODE: /* raw: no line editing here */
        *(uint32_t *)arg = 0;
        return 0;
    case TTY_IOC_GET_SPEED:
        *(uint32_t *)arg = u->baud;
        return 0;
    case TTY_IOC_SET_SPEED:
        return set_speed(u, (uint32_t)(uintptr_t)arg);
    case TTY_IOC_SET_LOOPBACK:
    {
        uint32_t key = irq_lock();
        if (arg)
            u->base->CTRL = (u->base->CTRL | LPUART_CTRL_LOOPS_MASK) & ~LPUART_CTRL_RSRC_MASK;
        else
            u->base->CTRL &= ~LPUART_CTRL_LOOPS_MASK;
        irq_unlock(key);
        return 0;
    }
    case TTY_IOC_GET_SIZE:
    {
        struct tty_size *sz = arg;
        sz->cols = 80;
        sz->rows = 24;
        return 0;
    }
    default:
        return -ENOTTY;
    }
}

static int lpuart_fstat(struct file *f, struct vfs_stat *st)
{
    (void)f;
    memset(st, 0, sizeof(*st));
    st->mode = VFS_S_IFCHR;
    return 0;
}

static const struct file_ops lpuart_ops = {
    .read = lpuart_read,
    .write = lpuart_write,
    .ioctl = lpuart_ioctl,
    .fstat = lpuart_fstat,
    .poll = lpuart_poll,
};

static int lpuart_probe(struct device *dev)
{
    if (dev->of_node == of_stdout_node())
        return -ENODEV; /* the kernel console */
    struct lpuart *u = devm_kzalloc(dev, sizeof(*u), 0);
    if (!u)
        return -ENOMEM;
    u->dev = dev;
    u->base = device_map(dev, 0);
    u->irq = device_get_irq(dev, 0);
    if (!u->base || u->irq < 0)
        return u->irq == -EPROBE_DEFER ? -EPROBE_DEFER : -ENODEV;
    struct clk *ipg, *per;
    int r = devm_clk_get_enabled(dev, "ipg", &ipg);
    if (!r)
        r = devm_clk_get(dev, "per", &per);
    if (r)
        return r;
    u->clk_hz = clk_get_rate(per);
    u->baud = 115200;
    of_property_read_u32(dev->of_node, "current-speed", &u->baud);
    event_init(&u->ev);
    poll_head_init(&u->ph);

    lpuart_config_t cfg;
    LPUART_GetDefaultConfig(&cfg);
    cfg.baudRate_Bps = u->baud;
    cfg.txFifoWatermark = 1;
    cfg.rxFifoWatermark = 2;
    cfg.enableTx = true;
    cfg.enableRx = true;
    if (LPUART_Init(u->base, &cfg, u->clk_hz) != kStatus_Success)
    {
        dev_err(dev, "%lu baud is not possible from %lu Hz\n", (unsigned long)u->baud, (unsigned long)u->clk_hz);
        return -EINVAL;
    }
    /* RDRF also after one idle character with bytes below the watermark (as the console) */
    u->base->CTRL &= ~(LPUART_CTRL_TE_MASK | LPUART_CTRL_RE_MASK);
    u->base->FIFO = (u->base->FIFO & ~(LPUART_FIFO_RXIDEN_MASK | LPUART_FIFO_TXOF_MASK | LPUART_FIFO_RXUF_MASK)) |
                    LPUART_FIFO_RXIDEN(1);
    u->base->CTRL |= LPUART_CTRL_TE_MASK | LPUART_CTRL_RE_MASK | LPUART_CTRL_RIE_MASK | LPUART_CTRL_ORIE_MASK;

    int n = of_alias_get_id(dev->of_node, "serial");
    if (n < 0)
        n = (int)(((uintptr_t)u->base - LPUART1_BASE) / 0x4000u) + 1;
    ksnprintf(u->name, sizeof(u->name), "ttyS%d", n);
    r = irq_request(u->irq, lpuart_irq, u, 8, dev->name);
    if (r)
        return r;
    dev_set_drvdata(dev, u);
    r = devfs_register(u->name, &lpuart_ops, u);
    if (r)
    {
        irq_free(u->irq);
        return r;
    }
    dev_info(dev, "/dev/%s, %lu baud\n", u->name, (unsigned long)u->baud);
    return 0;
}

static void lpuart_remove(struct device *dev)
{
    struct lpuart *u = dev_get_drvdata(dev);
    devfs_unregister(u->name);
    irq_free(u->irq);
    LPUART_Deinit(u->base);
}

static const struct of_device_id lpuart_ids[] = {
    {"fsl,imxrt1050-lpuart", NULL},
    {NULL, NULL},
};

static struct driver lpuart_driver = {
    .name = "lpuart-imxrt",
    .of_match_table = lpuart_ids,
    .probe = lpuart_probe,
    .remove = lpuart_remove,
};

static int init(void)
{
    return driver_register(&lpuart_driver);
}

static void fini(void)
{
    driver_unregister(&lpuart_driver);
}

MODULE("lpuart-imxrt", "i.MX RT LPUART serial ports /dev/ttyS*", init, fini);
