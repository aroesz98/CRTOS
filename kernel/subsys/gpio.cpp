/*
 * subsys/gpio.cpp - GPIO framework: chips and consumer descriptors.
 */
#include "kernel.h"
#include <crtos/device.h>
#include <crtos/gpio.h>
#include <string.h>

#define MAX_CHIPS 8

struct gpio_chip {
    struct device_node *np;
    unsigned npins;
    const struct gpio_chip_ops *ops;
    void *ctx;
};

struct gpio_desc {
    struct gpio_chip *chip;
    unsigned pin;
    bool active_low;
};

static struct gpio_chip s_chips[MAX_CHIPS];

int gpiochip_register(struct device_node *np, unsigned npins, const struct gpio_chip_ops *ops, void *ctx)
{
    uint32_t key = irq_lock();
    for (int i = 0; i < MAX_CHIPS; i++) {
        if (!s_chips[i].np) {
            s_chips[i].npins = npins;
            s_chips[i].ops = ops;
            s_chips[i].ctx = ctx;
            s_chips[i].np = np;
            irq_unlock(key);
            return 0;
        }
    }
    irq_unlock(key);
    return -ENOSPC;
}

void gpiochip_unregister(struct device_node *np)
{
    uint32_t key = irq_lock();
    for (int i = 0; i < MAX_CHIPS; i++)
        if (s_chips[i].np == np)
            s_chips[i].np = nullptr;
    irq_unlock(key);
}

int gpiod_get_index(struct device *dev, const struct device_node *np, const char *propname, int index, int flags,
                    struct gpio_desc **out)
{
    struct of_phandle_args a;
    if (of_parse_phandle_with_args(np, propname, "#gpio-cells", index, &a) || a.args_count < 1)
        return -ENOENT;
    struct gpio_chip *chip = nullptr;
    for (int i = 0; i < MAX_CHIPS; i++)
        if (s_chips[i].np == a.np)
            chip = &s_chips[i];
    if (!chip)
        return -EPROBE_DEFER;
    if (a.args[0] >= chip->npins)
        return -EINVAL;
    struct gpio_desc *d = (struct gpio_desc *)devm_kzalloc(dev, sizeof(*d), KM_ANY);
    if (!d)
        return -ENOMEM;
    d->chip = chip;
    d->pin = a.args[0];
    d->active_low = a.args_count > 1 && (a.args[1] & 1u);
    int r = 0;
    switch (flags) {
    case GPIOD_IN: r = gpiod_direction_input(d); break;
    case GPIOD_OUT_LOW: r = gpiod_direction_output(d, 0); break;
    case GPIOD_OUT_HIGH: r = gpiod_direction_output(d, 1); break;
    default: break;
    }
    if (r)
        return r;
    *out = d;
    return 0;
}

int gpiod_get(struct device *dev, const char *con_id, int flags, struct gpio_desc **out)
{
    char prop[32];
    if (con_id)
        ksnprintf(prop, sizeof(prop), "%s-gpios", con_id);
    else
        strcpy(prop, "gpios");
    return gpiod_get_index(dev, dev->of_node, prop, 0, flags, out);
}

int gpiod_get_value(struct gpio_desc *d)
{
    int v = d->chip->ops->get(d->chip->ctx, d->pin);
    return (v != 0) ^ d->active_low;
}

void gpiod_set_value(struct gpio_desc *d, int value)
{
    d->chip->ops->set(d->chip->ctx, d->pin, (value != 0) ^ d->active_low);
}

int gpiod_direction_input(struct gpio_desc *d)
{
    return d->chip->ops->direction_input(d->chip->ctx, d->pin);
}

int gpiod_direction_output(struct gpio_desc *d, int value)
{
    return d->chip->ops->direction_output(d->chip->ctx, d->pin, (value != 0) ^ d->active_low);
}

int gpiod_to_irq(struct gpio_desc *d)
{
    return d->chip->ops->to_irq ? d->chip->ops->to_irq(d->chip->ctx, d->pin) : -ENXIO;
}
