/*
 * crtos/gpio.h - general purpose I/O.
 *
 * GPIO controller drivers register a chip for their device tree node. Consumers get pins
 * from "<con_id>-gpios" (or "gpios") properties: <&gpioN pin flags>, flag bit 0 = active
 * low. Values are logical (active level = 1). Descriptors from gpiod_get() are released on
 * unbind.
 */
#ifndef CRTOS_GPIO_H
#define CRTOS_GPIO_H

#include <stdint.h>
#include <crtos/of.h>

#ifdef __cplusplus
extern "C" {
#endif

struct device;
struct gpio_desc;

struct gpio_chip_ops {
    int (*get)(void *ctx, unsigned pin);
    void (*set)(void *ctx, unsigned pin, int value);
    int (*direction_input)(void *ctx, unsigned pin);
    int (*direction_output)(void *ctx, unsigned pin, int value);
    int (*to_irq)(void *ctx, unsigned pin);            /* IRQ number (see irq_alloc_descs) */
};

int gpiochip_register(struct device_node *np, unsigned npins, const struct gpio_chip_ops *ops, void *ctx);
void gpiochip_unregister(struct device_node *np);

#define GPIOD_ASIS      0
#define GPIOD_IN        1
#define GPIOD_OUT_LOW   2   /* output, logically inactive */
#define GPIOD_OUT_HIGH  3   /* output, logically active */

/* 0, -ENOENT (property missing), -EPROBE_DEFER (controller not bound) */
int gpiod_get(struct device *dev, const char *con_id, int flags, struct gpio_desc **out);
int gpiod_get_index(struct device *dev, const struct device_node *np, const char *propname, int index,
                    int flags, struct gpio_desc **out);

int gpiod_get_value(struct gpio_desc *d);
void gpiod_set_value(struct gpio_desc *d, int value);
int gpiod_direction_input(struct gpio_desc *d);
int gpiod_direction_output(struct gpio_desc *d, int value);
int gpiod_to_irq(struct gpio_desc *d);

#ifdef __cplusplus
}
#endif

#endif
