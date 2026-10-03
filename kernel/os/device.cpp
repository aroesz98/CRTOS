/*
 * kernel/os/device.cpp - driver model core: devices from the device tree, driver matching,
 * deferred probing, managed resources and interrupt domains.
 */
#include "kernel.h"
#include <crtos/device.h>
#include <crtos/pinctrl.h>
#include <string.h>

static struct list_head s_devices = LIST_HEAD_INIT(s_devices);
static struct list_head s_drivers = LIST_HEAD_INIT(s_drivers);
static struct mutex s_lock;
static bool s_ready;

static void core_init_once(void)
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

/* ---- managed resources -------------------------------------------------------------------- */

struct devres {
    struct list_head node;
    void (*action)(void *);     /* NULL: the block itself is the allocation */
    void *arg;
};

void *devm_kzalloc(struct device *dev, size_t size, unsigned kmflags)
{
    struct devres *dr = (struct devres *)kzalloc(ALIGN_UP(sizeof(struct devres), 8u) + size, kmflags);
    if (!dr)
        return nullptr;
    dr->action = nullptr;
    list_add(&dr->node, &dev->devres);
    return (uint8_t *)dr + ALIGN_UP(sizeof(struct devres), 8u);
}

int devm_add_action(struct device *dev, void (*action)(void *), void *arg)
{
    struct devres *dr = (struct devres *)kzalloc(sizeof(*dr), KM_ANY);
    if (!dr)
        return -ENOMEM;
    dr->action = action;
    dr->arg = arg;
    list_add(&dr->node, &dev->devres);
    return 0;
}

static void devres_release_all(struct device *dev)
{
    while (!list_empty(&dev->devres)) {
        struct devres *dr = list_first_entry(&dev->devres, struct devres, node);
        list_del(&dr->node);
        if (dr->action)
            dr->action(dr->arg);
        kfree(dr);
    }
}

/* ---- matching and probing ------------------------------------------------------------------- */

static const struct of_device_id *match(const struct driver *drv, const struct device *dev)
{
    if (!drv->of_match_table || !dev->of_node)
        return nullptr;
    for (const struct of_device_id *id = drv->of_match_table; id->compatible; id++)
        if (of_device_is_compatible(dev->of_node, id->compatible))
            return id;
    return nullptr;
}

static int really_probe(struct device *dev, struct driver *drv, const struct of_device_id *id)
{
    dev->driver = drv;
    dev->match = id;
    /* pins first: the "default" state of the device (if it has one) */
    int r = pinctrl_select_state(dev, "default");
    if (r == -ENOENT)
        r = 0;
    if (!r && drv->probe)
        r = drv->probe(dev);
    if (r) {
        devres_release_all(dev);
        dev->driver = nullptr;
        dev->match = nullptr;
        dev->driver_data = nullptr;
        dev->state = r == -EPROBE_DEFER ? DEV_DEFERRED : DEV_FAILED;
        dev->probe_err = r;
        if (r != -EPROBE_DEFER && r != -ENODEV)
            printk("E: %s: probe by '%s' failed (%d)\n", dev->name, drv->name, r);
        return r;
    }
    dev->state = DEV_BOUND;
    dev->probe_err = 0;
    printk("%s: bound to '%s'\n", dev->name, drv->name);
    return 0;
}

/* Try every matching driver; 0 when one bound */
static int bind_device(struct device *dev)
{
    int r = -ENODEV;
    struct list_head *pos;
    list_for_each(pos, &s_drivers) {
        struct driver *drv = list_entry(pos, struct driver, node);
        const struct of_device_id *id = match(drv, dev);
        if (!id)
            continue;
        r = really_probe(dev, drv, id);
        if (r == 0 || r == -EPROBE_DEFER)
            return r;
    }
    return r;
}

/* A driver bound: devices that waited for it may succeed now */
static void retry_deferred(void)
{
    bool progress = true;
    while (progress) {
        progress = false;
        struct list_head *pos;
        list_for_each(pos, &s_devices) {
            struct device *dev = list_entry(pos, struct device, node);
            if (dev->state == DEV_DEFERRED && bind_device(dev) == 0) {
                progress = true;
                break; /* the list may have changed: rescan */
            }
        }
    }
}

static void release_driver(struct device *dev)
{
    if (dev->driver && dev->driver->remove)
        dev->driver->remove(dev);
    devres_release_all(dev);
    dev->driver = nullptr;
    dev->match = nullptr;
    dev->driver_data = nullptr;
    dev->state = DEV_UNBOUND;
}

int driver_register(struct driver *drv)
{
    core_init_once();
    mutex_lock(&s_lock, WAIT_FOREVER);
    drv->owner = module_loading();
    list_add_tail(&drv->node, &s_drivers);
    bool bound = false;
    struct list_head *pos;
    list_for_each(pos, &s_devices) {
        struct device *dev = list_entry(pos, struct device, node);
        if (dev->state == DEV_BOUND)
            continue;
        const struct of_device_id *id = match(drv, dev);
        if (id && really_probe(dev, drv, id) == 0)
            bound = true;
    }
    if (bound)
        retry_deferred();
    mutex_unlock(&s_lock);
    return 0;
}

void driver_unregister(struct driver *drv)
{
    core_init_once();
    mutex_lock(&s_lock, WAIT_FOREVER);
    struct list_head *pos;
    list_for_each(pos, &s_devices) {
        struct device *dev = list_entry(pos, struct device, node);
        if (dev->driver == drv) {
            release_driver(dev);
            printk("%s: unbound from '%s'\n", dev->name, drv->name);
        }
    }
    list_del(&drv->node);
    mutex_unlock(&s_lock);
}

/* Safety net for module unload: drivers the module forgot to unregister */
void driver_unregister_owner(struct module *owner)
{
    core_init_once();
    for (;;) {
        struct driver *found = nullptr;
        mutex_lock(&s_lock, WAIT_FOREVER);
        struct list_head *pos;
        list_for_each(pos, &s_drivers) {
            struct driver *drv = list_entry(pos, struct driver, node);
            if (drv->owner == owner)
                found = drv;
        }
        mutex_unlock(&s_lock);
        if (!found)
            return;
        printk("W: driver '%s' still registered at module unload\n", found->name);
        driver_unregister(found);
    }
}

struct device *device_create_of(struct device_node *np, struct device *parent, void *bus_data)
{
    core_init_once();
    if (np->dev)
        return np->dev;
    struct device *dev = (struct device *)kzalloc(sizeof(*dev), KM_ANY);
    if (!dev)
        return nullptr;
    strncpy(dev->name, np->name, sizeof(dev->name) - 1);
    dev->of_node = np;
    dev->parent = parent;
    dev->bus_data = bus_data;
    list_init(&dev->devres);
    np->dev = dev;
    mutex_lock(&s_lock, WAIT_FOREVER);
    list_add_tail(&dev->node, &s_devices);
    if (bind_device(dev) == 0)
        retry_deferred();
    mutex_unlock(&s_lock);
    return dev;
}

void device_destroy(struct device *dev)
{
    mutex_lock(&s_lock, WAIT_FOREVER);
    if (dev->driver)
        release_driver(dev);
    list_del(&dev->node);
    if (dev->of_node)
        dev->of_node->dev = nullptr;
    mutex_unlock(&s_lock);
    kfree(dev);
}

static void populate_bus(struct device_node *bus, struct device *parent)
{
    struct device_node *np;
    for_each_child_of_node(bus, np) {
        if (!of_get_property(np, "compatible", nullptr) || !of_device_is_available(np))
            continue;
        struct device *dev = device_create_of(np, parent, nullptr);
        if (dev && of_device_is_compatible(np, "simple-bus"))
            populate_bus(np, dev);
    }
}

int device_populate(void)
{
    core_init_once();
    if (!of_root())
        return -ENODEV;
    populate_bus(of_root(), nullptr);
    return 0;
}

const void *device_get_match_data(const struct device *dev)
{
    return dev->match ? dev->match->data : nullptr;
}

int device_get_reg(const struct device *dev, int index, uint32_t *addr, uint32_t *size)
{
    return dev->of_node ? of_get_reg(dev->of_node, index, addr, size) : -ENODEV;
}

void *device_map(const struct device *dev, int index)
{
    uint32_t addr;
    return device_get_reg(dev, index, &addr, nullptr) ? nullptr : (void *)(uintptr_t)addr;
}

/* ---- interrupt domains ------------------------------------------------------------------------ */

#define MAX_IRQ_DOMAINS 8

static struct {
    struct device_node *np;
    irq_xlate_t xlate;
    void *ctx;
} s_domains[MAX_IRQ_DOMAINS];

int irq_domain_add(struct device_node *controller, irq_xlate_t xlate, void *ctx)
{
    uint32_t key = irq_lock();
    for (int i = 0; i < MAX_IRQ_DOMAINS; i++) {
        if (!s_domains[i].np) {
            s_domains[i].np = controller;
            s_domains[i].xlate = xlate;
            s_domains[i].ctx = ctx;
            irq_unlock(key);
            return 0;
        }
    }
    irq_unlock(key);
    return -ENOSPC;
}

void irq_domain_remove(struct device_node *controller)
{
    uint32_t key = irq_lock();
    for (int i = 0; i < MAX_IRQ_DOMAINS; i++)
        if (s_domains[i].np == controller)
            s_domains[i].np = nullptr;
    irq_unlock(key);
}

int device_get_irq(const struct device *dev, int index)
{
    struct of_phandle_args a;
    int r = of_irq_parse(dev->of_node, index, &a);
    if (r)
        return r;
    if (of_device_is_compatible(a.np, "arm,armv7m-nvic"))
        return a.args_count >= 1 ? (int)a.args[0] : -EINVAL;
    irq_xlate_t xlate = nullptr;
    void *ctx = nullptr;
    uint32_t key = irq_lock();
    for (int i = 0; i < MAX_IRQ_DOMAINS; i++) {
        if (s_domains[i].np == a.np) {
            xlate = s_domains[i].xlate;
            ctx = s_domains[i].ctx;
        }
    }
    irq_unlock(key);
    if (!xlate)
        return -EPROBE_DEFER; /* the interrupt controller's driver is not bound yet */
    return xlate(ctx, a.args, a.args_count);
}

/* ---- iteration --------------------------------------------------------------------------------- */

void device_foreach(void (*fn)(struct device *dev, void *ctx), void *ctx)
{
    core_init_once();
    mutex_lock(&s_lock, WAIT_FOREVER);
    struct list_head *pos;
    list_for_each(pos, &s_devices)
        fn(list_entry(pos, struct device, node), ctx);
    mutex_unlock(&s_lock);
}

void driver_foreach(void (*fn)(struct driver *drv, void *ctx), void *ctx)
{
    core_init_once();
    mutex_lock(&s_lock, WAIT_FOREVER);
    struct list_head *pos;
    list_for_each(pos, &s_drivers)
        fn(list_entry(pos, struct driver, node), ctx);
    mutex_unlock(&s_lock);
}
