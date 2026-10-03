/*
 * subsys/i2c.cpp - I2C framework: adapters, client devices from the device tree, transfers.
 */
#include "kernel.h"
#include <crtos/device.h>
#include <crtos/i2c.h>
#include <string.h>

#define MAX_CLIENTS 8

struct i2c_adapter {
    struct device *dev;
    const struct i2c_adapter_ops *ops;
    void *ctx;
    struct mutex lock;
    int bus;
    struct i2c_client *clients[MAX_CLIENTS];
    int nclients;
    struct list_head node;
};

static struct list_head s_adapters = LIST_HEAD_INIT(s_adapters);
static struct mutex s_lock;
static bool s_ready;
static int s_next_bus;

static void i2c_init_once(void)
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

int i2c_adapter_register(struct device *dev, const struct i2c_adapter_ops *ops, void *ctx, struct i2c_adapter **out)
{
    i2c_init_once();
    struct i2c_adapter *a = (struct i2c_adapter *)kzalloc(sizeof(*a), KM_ANY);
    if (!a)
        return -ENOMEM;
    a->dev = dev;
    a->ops = ops;
    a->ctx = ctx;
    mutex_init(&a->lock);
    int id = dev->of_node ? of_alias_get_id(dev->of_node, "i2c") : -1;
    mutex_lock(&s_lock, WAIT_FOREVER);
    a->bus = id >= 0 ? id : s_next_bus++;
    list_add_tail(&a->node, &s_adapters);
    mutex_unlock(&s_lock);
    *out = a;

    /* one device per child node: reg = 7-bit address */
    struct device_node *np;
    for_each_child_of_node(dev->of_node, np) {
        uint32_t addr;
        if (!of_get_property(np, "compatible", nullptr) || !of_device_is_available(np) ||
            of_property_read_u32(np, "reg", &addr) || addr > 0x7F || a->nclients >= MAX_CLIENTS)
            continue;
        struct i2c_client *c = (struct i2c_client *)kzalloc(sizeof(*c), KM_ANY);
        if (!c)
            break;
        c->adapter = a;
        c->addr = (uint16_t)addr;
        a->clients[a->nclients++] = c;
        c->dev = device_create_of(np, dev, c);
    }
    printk("i2c-%d: %s, %d device(s)\n", a->bus, dev->name, a->nclients);
    return 0;
}

void i2c_adapter_unregister(struct i2c_adapter *a)
{
    for (int i = a->nclients - 1; i >= 0; i--) {
        if (a->clients[i]->dev)
            device_destroy(a->clients[i]->dev);
        kfree(a->clients[i]);
    }
    mutex_lock(&s_lock, WAIT_FOREVER);
    list_del(&a->node);
    mutex_unlock(&s_lock);
    kfree(a);
}

struct i2c_adapter *i2c_adapter_get(int bus)
{
    i2c_init_once();
    struct i2c_adapter *found = nullptr;
    mutex_lock(&s_lock, WAIT_FOREVER);
    struct list_head *pos;
    list_for_each(pos, &s_adapters) {
        struct i2c_adapter *a = list_entry(pos, struct i2c_adapter, node);
        if (a->bus == bus)
            found = a;
    }
    mutex_unlock(&s_lock);
    return found;
}

struct i2c_client *i2c_client_get(struct device *dev)
{
    return (struct i2c_client *)dev->bus_data;
}

int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
    mutex_lock(&adap->lock, WAIT_FOREVER);
    int r = adap->ops->xfer(adap->ctx, msgs, num);
    mutex_unlock(&adap->lock);
    return r;
}

int i2c_write(struct i2c_client *c, const void *buf, size_t len)
{
    struct i2c_msg m = { c->addr, 0, (uint16_t)len, (uint8_t *)buf };
    int r = i2c_transfer(c->adapter, &m, 1);
    return r == 1 ? 0 : (r < 0 ? r : -EIO);
}

int i2c_read(struct i2c_client *c, void *buf, size_t len)
{
    struct i2c_msg m = { c->addr, I2C_M_RD, (uint16_t)len, (uint8_t *)buf };
    int r = i2c_transfer(c->adapter, &m, 1);
    return r == 1 ? 0 : (r < 0 ? r : -EIO);
}

int i2c_write_read(struct i2c_client *c, const void *wbuf, size_t wlen, void *rbuf, size_t rlen)
{
    struct i2c_msg m[2] = {
        { c->addr, 0, (uint16_t)wlen, (uint8_t *)wbuf },
        { c->addr, I2C_M_RD, (uint16_t)rlen, (uint8_t *)rbuf },
    };
    int r = i2c_transfer(c->adapter, m, 2);
    return r == 2 ? 0 : (r < 0 ? r : -EIO);
}
