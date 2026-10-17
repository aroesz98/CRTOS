/*
 * subsys/spi.cpp - SPI framework: controllers, devices from the device tree, messages, and
 * /dev/spidevB.C for programs (the ioctls of Linux spidev).
 *
 * Programs' data goes through kernel buffers: the controller (and its interrupt) never
 * touches the memory of a process, which may end while a transfer runs.
 */
#include "kernel.h"
#include <crtos/device.h>
#include <crtos/spi.h>
#include <crtos/uaccess.h>
#include <crtos/vfs.h>
#include <string.h>

#define MAX_DEVICES     8
#define SPIDEV_MAX      16384u      /* bytes of one message through /dev/spidev */

struct spidev {
    struct spi_device spi;
    char name[16];                  /* spidevB.C */
};

struct spi_controller {
    struct device *dev;
    const struct spi_controller_ops *ops;
    void *ctx;
    struct mutex lock;
    int bus, num_cs;
    struct spi_device *devices[MAX_DEVICES];    /* children with a driver */
    struct spidev *spidevs[MAX_DEVICES];        /* children for programs */
    int ndevices, nspidevs;
    struct list_head node;
};

static struct list_head s_ctlrs = LIST_HEAD_INIT(s_ctlrs);
static struct mutex s_lock;
static bool s_ready;
static int s_next_bus;

static void spi_init_once(void)
{
    if (s_ready)
        return;
    uint32_t key = irq_lock();
    if (!s_ready) {
        mutex_init(&s_lock);
        s_ready = true;
    }
    irq_unlock(key);
}

/* ---- messages ------------------------------------------------------------------------------ */

int spi_sync(struct spi_device *spi, struct spi_transfer *xfers, int num)
{
    struct spi_controller *c = spi->ctlr;
    for (int i = 0; i < num; i++)
        if (!xfers[i].len)
            return -EINVAL;
    int r = mutex_lock(&c->lock, WAIT_FOREVER);
    if (r)
        return r;
    r = c->ops->transfer(c->ctx, spi, xfers, num);
    mutex_unlock(&c->lock);
    return r;
}

int spi_write(struct spi_device *spi, const void *buf, size_t len)
{
    struct spi_transfer t = { buf, nullptr, (uint32_t)len, 0, 0, 0, 0 };
    return spi_sync(spi, &t, 1);
}

int spi_read(struct spi_device *spi, void *buf, size_t len)
{
    struct spi_transfer t = { nullptr, buf, (uint32_t)len, 0, 0, 0, 0 };
    return spi_sync(spi, &t, 1);
}

int spi_write_then_read(struct spi_device *spi, const void *tx, size_t ntx, void *rx, size_t nrx)
{
    struct spi_transfer t[2] = {
        { tx, nullptr, (uint32_t)ntx, 0, 0, 0, 0 },
        { nullptr, rx, (uint32_t)nrx, 0, 0, 0, 0 },
    };
    return spi_sync(spi, t, 2);
}

/* ---- /dev/spidevB.C ------------------------------------------------------------------------ */

/* A message of a program: its parts, their data in one kernel buffer each way */
static int spidev_message(struct spi_device *spi, const struct spi_ioc_transfer *u, int num)
{
    uint32_t total = 0;
    for (int i = 0; i < num; i++) {
        if (!u[i].len || u[i].len > SPIDEV_MAX || total + u[i].len > SPIDEV_MAX)
            return -EMSGSIZE;
        if ((u[i].tx_buf && !uaccess_ok((const void *)(uintptr_t)u[i].tx_buf, u[i].len, 0)) ||
            (u[i].rx_buf && !uaccess_ok((const void *)(uintptr_t)u[i].rx_buf, u[i].len, 1)))
            return -EFAULT;
        total += u[i].len;
    }
    struct spi_transfer *k = (struct spi_transfer *)kzalloc(sizeof(*k) * (size_t)num, KM_ANY);
    uint8_t *buf = (uint8_t *)kmalloc(2u * total, KM_ANY); /* send ... receive */
    int r = -ENOMEM;
    if (k && buf) {
        uint32_t off = 0;
        for (int i = 0; i < num; i++) {
            uint8_t *tx = buf + off, *rx = buf + total + off;
            if (u[i].tx_buf)
                memcpy(tx, (const void *)(uintptr_t)u[i].tx_buf, u[i].len);
            else
                memset(tx, 0, u[i].len);
            k[i].tx_buf = tx;
            k[i].rx_buf = u[i].rx_buf ? rx : nullptr;
            k[i].len = u[i].len;
            k[i].speed_hz = u[i].speed_hz;
            k[i].delay_us = u[i].delay_usecs;
            k[i].bits_per_word = u[i].bits_per_word;
            k[i].cs_change = u[i].cs_change;
            off += u[i].len;
        }
        r = spi_sync(spi, k, num);
        if (!r) {
            for (int i = 0; i < num; i++)
                if (u[i].rx_buf)
                    memcpy((void *)(uintptr_t)u[i].rx_buf, k[i].rx_buf, u[i].len);
            r = (int)total;
        }
    }
    kfree(buf);
    kfree(k);
    return r;
}

static int spidev_ioctl(struct file *f, unsigned cmd, void *arg)
{
    struct spi_device *spi = &((struct spidev *)f->dev)->spi;
    if (_IOC_TYPE(cmd) == SPI_IOC_MAGIC && _IOC_NR(cmd) == 0 && _IOC_DIR(cmd) == _IOC_WRITE) {
        uint32_t size = _IOC_SIZE(cmd);
        if (!size || size % sizeof(struct spi_ioc_transfer))
            return -EINVAL;
        return spidev_message(spi, (const struct spi_ioc_transfer *)arg, (int)(size / sizeof(struct spi_ioc_transfer)));
    }
    switch (cmd) {
    case SPI_IOC_RD_MODE:
        *(uint8_t *)arg = (uint8_t)spi->mode;
        return 0;
    case SPI_IOC_RD_MODE32:
        *(uint32_t *)arg = spi->mode;
        return 0;
    case SPI_IOC_WR_MODE:
        spi->mode = *(const uint8_t *)arg;
        return 0;
    case SPI_IOC_WR_MODE32:
        spi->mode = *(const uint32_t *)arg;
        return 0;
    case SPI_IOC_RD_LSB_FIRST:
        *(uint8_t *)arg = (spi->mode & SPI_LSB_FIRST) ? 1 : 0;
        return 0;
    case SPI_IOC_WR_LSB_FIRST:
        spi->mode = *(const uint8_t *)arg ? spi->mode | SPI_LSB_FIRST : spi->mode & ~SPI_LSB_FIRST;
        return 0;
    case SPI_IOC_RD_BITS_PER_WORD:
        *(uint8_t *)arg = spi->bits_per_word;
        return 0;
    case SPI_IOC_WR_BITS_PER_WORD: {
        uint8_t b = *(const uint8_t *)arg;
        if (b && (b < 4 || b > 32))
            return -EINVAL;
        spi->bits_per_word = b ? b : 8;
        return 0;
    }
    case SPI_IOC_RD_MAX_SPEED_HZ:
        *(uint32_t *)arg = spi->max_speed_hz;
        return 0;
    case SPI_IOC_WR_MAX_SPEED_HZ:
        if (!*(const uint32_t *)arg)
            return -EINVAL;
        spi->max_speed_hz = *(const uint32_t *)arg;
        return 0;
    default:
        return -ENOTTY;
    }
}

/* read() and write(): one-part messages (half duplex) */
static int spidev_rw(struct file *f, void *buf, size_t len, bool write)
{
    if (!len)
        return 0;
    if (len > SPIDEV_MAX)
        len = SPIDEV_MAX;
    struct spi_ioc_transfer t;
    memset(&t, 0, sizeof(t));
    t.len = (uint32_t)len;
    if (write)
        t.tx_buf = (uintptr_t)buf;
    else
        t.rx_buf = (uintptr_t)buf;
    return spidev_message(&((struct spidev *)f->dev)->spi, &t, 1);
}

static int spidev_read(struct file *f, void *buf, size_t len)
{
    return spidev_rw(f, buf, len, false);
}

static int spidev_write(struct file *f, const void *buf, size_t len)
{
    return spidev_rw(f, (void *)buf, len, true);
}

/* An open spidev keeps the controller's module loaded (these file functions are the
 * kernel's, so the VFS would not) */
static int spidev_open(struct file *f)
{
    struct module *owner;
    int r = module_get_addr(((struct spidev *)f->dev)->spi.ctlr->ops, &owner);
    f->priv = owner;
    return r;
}

static int spidev_close(struct file *f)
{
    module_put((struct module *)f->priv);
    return 0;
}

static const struct file_ops spidev_ops = {
    spidev_open, spidev_read, spidev_write, nullptr, spidev_ioctl, nullptr, nullptr, nullptr, spidev_close, nullptr,
};

/* ---- controllers ----------------------------------------------------------------------------- */

static void read_settings(struct device_node *np, struct spi_device *spi)
{
    uint32_t hz = 1000000;
    of_property_read_u32(np, "spi-max-frequency", &hz);
    spi->max_speed_hz = hz;
    spi->bits_per_word = 8;
    spi->mode = (of_property_read_bool(np, "spi-cpha") ? SPI_CPHA : 0) |
                (of_property_read_bool(np, "spi-cpol") ? SPI_CPOL : 0) |
                (of_property_read_bool(np, "spi-cs-high") ? SPI_CS_HIGH : 0) |
                (of_property_read_bool(np, "spi-lsb-first") ? SPI_LSB_FIRST : 0);
}

int spi_controller_register(struct device *dev, const struct spi_controller_ops *ops, void *ctx, int num_cs,
                            struct spi_controller **out)
{
    spi_init_once();
    struct spi_controller *c = (struct spi_controller *)kzalloc(sizeof(*c), KM_ANY);
    if (!c)
        return -ENOMEM;
    c->dev = dev;
    c->ops = ops;
    c->ctx = ctx;
    c->num_cs = num_cs;
    mutex_init(&c->lock);
    int id = dev->of_node ? of_alias_get_id(dev->of_node, "spi") : -1;
    mutex_lock(&s_lock, WAIT_FOREVER);
    c->bus = id >= 0 ? id : s_next_bus++;
    list_add_tail(&c->node, &s_ctlrs);
    mutex_unlock(&s_lock);
    *out = c;

    /* one device per child node: reg = chip select */
    struct device_node *np;
    for_each_child_of_node(dev->of_node, np) {
        uint32_t cs;
        if (!of_get_property(np, "compatible", nullptr) || !of_device_is_available(np) ||
            of_property_read_u32(np, "reg", &cs) || (int)cs >= num_cs)
            continue;
        if (of_device_is_compatible(np, "crtos,spidev")) {
            if (c->nspidevs >= MAX_DEVICES)
                continue;
            struct spidev *sd = (struct spidev *)kzalloc(sizeof(*sd), KM_ANY);
            if (!sd)
                break;
            sd->spi.ctlr = c;
            sd->spi.cs = (uint8_t)cs;
            read_settings(np, &sd->spi);
            ksnprintf(sd->name, sizeof(sd->name), "spidev%d.%lu", c->bus, (unsigned long)cs);
            if (devfs_register(sd->name, &spidev_ops, sd)) {
                kfree(sd);
                continue;
            }
            c->spidevs[c->nspidevs++] = sd;
        } else {
            if (c->ndevices >= MAX_DEVICES)
                continue;
            struct spi_device *spi = (struct spi_device *)kzalloc(sizeof(*spi), KM_ANY);
            if (!spi)
                break;
            spi->ctlr = c;
            spi->cs = (uint8_t)cs;
            read_settings(np, spi);
            c->devices[c->ndevices++] = spi;
            spi->dev = device_create_of(np, dev, spi);
        }
    }
    printk("spi%d: %s, %d device(s), %d for programs (/dev/spidev%d.*)\n", c->bus, dev->name, c->ndevices,
           c->nspidevs, c->bus);
    return 0;
}

void spi_controller_unregister(struct spi_controller *c)
{
    for (int i = c->nspidevs - 1; i >= 0; i--) {
        devfs_unregister(c->spidevs[i]->name);
        kfree(c->spidevs[i]);
    }
    for (int i = c->ndevices - 1; i >= 0; i--) {
        if (c->devices[i]->dev)
            device_destroy(c->devices[i]->dev);
        kfree(c->devices[i]);
    }
    mutex_lock(&s_lock, WAIT_FOREVER);
    list_del(&c->node);
    mutex_unlock(&s_lock);
    kfree(c);
}

int spi_controller_bus(struct spi_controller *c)
{
    return c->bus;
}

struct spi_device *spi_device_get(struct device *dev)
{
    return (struct spi_device *)dev->bus_data;
}
