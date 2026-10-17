/*
 * uart-stm32.ko - the USARTs and UARTs of the STM32H7S7 other than the kernel console as serial
 * ports, /dev/ttyS<n> (n: the "serial<n>" alias, else the order of probing from 1).
 *
 * Device tree: compatible "st,stm32-uart", reg, interrupts, clocks (the bus clock gate; its
 * rate, PCLK1 or PCLK2, is the UART's kernel clock as the reset leaves it), pinctrl,
 * current-speed (baud, default 115200). The port of the kernel console (chosen/stdout-path,
 * UART4 on the STM32H7S78-DK, kernel/platform/stm32h7s78dk/console_uart.cpp) is left alone.
 *
 * Raw bytes, 8N1, the FIFOs on: interrupt-driven rings each way (the receive interrupt
 * empties the FIFO whenever it holds a byte, the transmit one fills it while the ring has
 * some); read() returns what has arrived (at least one byte), write() waits for room; poll()
 * works. ioctl: TTY_IOC_GET/SET_SPEED, TTY_IOC_SET_LOOPBACK - the USART has no internal
 * loopback: half-duplex mode, where the receiver hears what TX sends (on the TX pin, the RX pin
 * unused), serves as one.
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
#include "stm32h7rsxx.h"

#define RING 2048u /* each way, a power of 2 */
#define EV_RX 0x1u
#define EV_TX 0x2u
#define ISR_ERRORS (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)
#define ICR_ERRORS (USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF)

struct ring
{
    uint8_t buf[RING];
    volatile uint32_t head, tail;
};

struct uart
{
    struct device *dev;
    USART_TypeDef *base;
    int irq;
    uint32_t clk_hz, baud;
    char name[12];
    struct ring rx, tx; /* under irq_lock() */
    struct event ev;
    struct poll_head ph;
    uint32_t overruns, errors, dropped;
};

static int s_ports; /* (names of ports without an alias) */

static uint32_t count(const struct ring *r)
{
    return r->head - r->tail;
}

static void uart_irq(int irq, void *ctx)
{
    struct uart *u = ctx;
    USART_TypeDef *b = u->base;
    (void)irq;
    uint32_t isr = b->ISR;
    if (isr & ISR_ERRORS)
    {
        if (isr & USART_ISR_ORE)
            u->overruns++;
        else
            u->errors++;
        b->ICR = ICR_ERRORS;
    }
    uint32_t events = 0;
    while (b->ISR & USART_ISR_RXNE_RXFNE)
    {
        uint8_t c = (uint8_t)b->RDR;
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
    if (b->CR1 & USART_CR1_TXEIE_TXFNFIE)
    {
        while ((b->ISR & USART_ISR_TXE_TXFNF) && count(&u->tx))
        {
            b->TDR = u->tx.buf[u->tx.tail & (RING - 1)];
            u->tx.tail++;
            events |= EV_TX;
        }
        if (!count(&u->tx))
            b->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
    }
    if (events)
    {
        event_set(&u->ev, events);
        poll_notify(&u->ph);
    }
}

static int uart_read(struct file *f, void *buf, size_t len)
{
    struct uart *u = f->dev;
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

static int uart_write(struct file *f, const void *buf, size_t len)
{
    struct uart *u = f->dev;
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
            u->base->CR1 |= USART_CR1_TXEIE_TXFNFIE; /* the interrupt sends it */
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

static int uart_poll(struct file *f, struct poll_entry *e)
{
    struct uart *u = f->dev;
    uint32_t key = irq_lock();
    int mask = (count(&u->rx) ? POLLIN : 0) | (count(&u->tx) < RING ? POLLOUT : 0);
    poll_add(&u->ph, e);
    irq_unlock(key);
    return mask;
}

/* BRR and CR3's half-duplex bit take a change only with the USART off: off, set, on again
 * (a byte on the way is lost) */
static void reconfigure(struct uart *u, uint32_t brr, uint32_t hdsel)
{
    USART_TypeDef *b = u->base;
    uint32_t key = irq_lock();
    uint32_t cr1 = b->CR1;
    b->CR1 = cr1 & ~USART_CR1_UE;
    b->BRR = brr;
    b->CR3 = (b->CR3 & ~USART_CR3_HDSEL) | hdsel;
    b->ICR = 0xFFFFFFFFu;
    b->CR1 = cr1 | USART_CR1_UE;
    irq_unlock(key);
}

/* 16 times oversampling: BRR = the kernel clock / baud, from 16 to 0xFFFF */
static int baud_brr(const struct uart *u, uint32_t baud, uint32_t *brr)
{
    if (!baud)
        return -EINVAL;
    uint32_t v = (u->clk_hz + baud / 2u) / baud;
    if (v < 16u || v > 0xFFFFu)
        return -EINVAL;
    *brr = v;
    return 0;
}

static int set_speed(struct uart *u, uint32_t baud)
{
    uint32_t brr;
    int r = baud_brr(u, baud, &brr);
    if (r)
        return r;
    reconfigure(u, brr, u->base->CR3 & USART_CR3_HDSEL);
    u->baud = baud;
    return 0;
}

static int uart_ioctl(struct file *f, unsigned cmd, void *arg)
{
    struct uart *u = f->dev;
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
        reconfigure(u, u->base->BRR, arg ? USART_CR3_HDSEL : 0u);
        return 0;
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

static int uart_fstat(struct file *f, struct vfs_stat *st)
{
    (void)f;
    memset(st, 0, sizeof(*st));
    st->mode = VFS_S_IFCHR;
    return 0;
}

static const struct file_ops uart_ops = {
    .read = uart_read,
    .write = uart_write,
    .ioctl = uart_ioctl,
    .fstat = uart_fstat,
    .poll = uart_poll,
};

static int uart_probe(struct device *dev)
{
    if (dev->of_node == of_stdout_node())
        return -ENODEV; /* the kernel console */
    struct uart *u = devm_kzalloc(dev, sizeof(*u), 0);
    if (!u)
        return -ENOMEM;
    u->dev = dev;
    u->base = device_map(dev, 0);
    u->irq = device_get_irq(dev, 0);
    if (!u->base || u->irq < 0)
        return u->irq == -EPROBE_DEFER ? -EPROBE_DEFER : -ENODEV;
    struct clk *clk;
    int r = devm_clk_get_enabled(dev, NULL, &clk);
    if (r)
        return r;
    u->clk_hz = clk_get_rate(clk);
    u->baud = 115200;
    of_property_read_u32(dev->of_node, "current-speed", &u->baud);
    uint32_t brr;
    if (baud_brr(u, u->baud, &brr))
    {
        dev_err(dev, "%lu baud is not possible from %lu Hz\n", (unsigned long)u->baud, (unsigned long)u->clk_hz);
        return -EINVAL;
    }
    event_init(&u->ev);
    poll_head_init(&u->ph);

    USART_TypeDef *b = u->base;
    b->CR1 = 0;
    b->PRESC = 0;
    b->BRR = brr;
    b->CR2 = 0;
    b->CR3 = USART_CR3_EIE; /* (errors: counted, the FIFO keeps going) */
    b->ICR = 0xFFFFFFFFu;
    b->CR1 = USART_CR1_FIFOEN | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    int n = of_alias_get_id(dev->of_node, "serial");
    if (n < 0)
        n = ++s_ports;
    ksnprintf(u->name, sizeof(u->name), "ttyS%d", n);
    r = irq_request(u->irq, uart_irq, u, 8, dev->name);
    if (r)
        return r;
    b->CR1 |= USART_CR1_RXNEIE_RXFNEIE;
    dev_set_drvdata(dev, u);
    r = devfs_register(u->name, &uart_ops, u);
    if (r)
    {
        b->CR1 = 0;
        irq_free(u->irq);
        return r;
    }
    dev_info(dev, "/dev/%s, %lu baud\n", u->name, (unsigned long)u->baud);
    return 0;
}

static void uart_remove(struct device *dev)
{
    struct uart *u = dev_get_drvdata(dev);
    devfs_unregister(u->name);
    u->base->CR1 = 0;
    irq_free(u->irq);
    if (u->overruns || u->errors || u->dropped)
        dev_info(dev, "%lu overruns, %lu line errors, %lu bytes dropped (the ring full)\n",
                 (unsigned long)u->overruns, (unsigned long)u->errors, (unsigned long)u->dropped);
}

static const struct of_device_id uart_ids[] = {
    {"st,stm32-uart", NULL},
    {NULL, NULL},
};

static struct driver uart_driver = {
    .name = "uart-stm32",
    .of_match_table = uart_ids,
    .probe = uart_probe,
    .remove = uart_remove,
};

static int init(void)
{
    return driver_register(&uart_driver);
}

static void fini(void)
{
    driver_unregister(&uart_driver);
}

MODULE("uart-stm32", "STM32 USART/UART serial ports /dev/ttyS*", init, fini);
