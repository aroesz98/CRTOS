/*
 * crtos/device.h - driver model.
 *
 * Devices are created from device tree nodes (children of the root and of "simple-bus"
 * nodes; bus drivers create devices for their own child nodes). A driver matches devices
 * by "compatible" strings. probe() may return -EPROBE_DEFER when something it needs
 * (clock, pin controller, GPIO...) is not available yet; it is retried whenever another
 * driver binds. Memory from devm_* helpers is released automatically on unbind.
 */
#ifndef CRTOS_DEVICE_H
#define CRTOS_DEVICE_H

#include <stddef.h>
#include <stdint.h>
#include <crtos/list.h>
#include <crtos/of.h>
#include <crtos/printk.h>

#ifdef __cplusplus
extern "C" {
#endif

struct module;
struct driver;

struct of_device_id {
    const char *compatible;
    const void *data;           /* driver-defined, see device_get_match_data() */
};

enum device_state { DEV_UNBOUND = 0, DEV_BOUND, DEV_DEFERRED, DEV_FAILED };

struct device {
    char name[32];
    struct device_node *of_node;
    struct device *parent;
    struct driver *driver;
    const struct of_device_id *match;
    void *driver_data;
    void *bus_data;             /* set by the bus that created the device (e.g. i2c_client) */
    int state;                  /* enum device_state */
    int probe_err;
    struct list_head node;
    struct list_head devres;
};

struct driver {
    const char *name;
    const struct of_device_id *of_match_table;  /* terminated by an entry with compatible == NULL */
    int (*probe)(struct device *dev);
    void (*remove)(struct device *dev);
    struct module *owner;       /* filled in by driver_register() */
    struct list_head node;
};

int driver_register(struct driver *drv);
void driver_unregister(struct driver *drv);

/* For bus drivers: create/destroy a device for a DT node (probed immediately if possible) */
struct device *device_create_of(struct device_node *np, struct device *parent, void *bus_data);
void device_destroy(struct device *dev);

static inline void dev_set_drvdata(struct device *dev, void *data) { dev->driver_data = data; }
static inline void *dev_get_drvdata(const struct device *dev) { return dev->driver_data; }
const void *device_get_match_data(const struct device *dev);

int device_get_reg(const struct device *dev, int index, uint32_t *addr, uint32_t *size);
void *device_map(const struct device *dev, int index);     /* register block address or NULL */
int device_get_irq(const struct device *dev, int index);   /* IRQ number or -errno */

/* Managed resources: freed/undone in reverse order when the device is unbound */
void *devm_kzalloc(struct device *dev, size_t size, unsigned kmflags);
int devm_add_action(struct device *dev, void (*action)(void *), void *arg);

/* Interrupt controllers other than the NVIC translate their DT specifiers to IRQ numbers */
typedef int (*irq_xlate_t)(void *ctx, const uint32_t *args, int nargs);
int irq_domain_add(struct device_node *controller, irq_xlate_t xlate, void *ctx);
void irq_domain_remove(struct device_node *controller);

void device_foreach(void (*fn)(struct device *dev, void *ctx), void *ctx);
void driver_foreach(void (*fn)(struct driver *drv, void *ctx), void *ctx);

#define dev_info(dev, fmt, ...) printk("%s: " fmt, (dev)->name, ##__VA_ARGS__)
#define dev_warn(dev, fmt, ...) printk("W: %s: " fmt, (dev)->name, ##__VA_ARGS__)
#define dev_err(dev, fmt, ...)  printk("E: %s: " fmt, (dev)->name, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
