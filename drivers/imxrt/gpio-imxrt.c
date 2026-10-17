/*
 * gpio-imxrt.ko - i.MX RT GPIO ports: 32 pins each, with a secondary interrupt controller.
 *
 * Each port has two NVIC lines (pins 0-15 and 16-31). Their handlers dispatch the pending,
 * unmasked pins to the per-pin IRQ numbers allocated with irq_alloc_descs(), so drivers
 * can simply irq_request() the number they get from the device tree or gpiod_to_irq().
 */
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/gpio.h>
#include <crtos/irq.h>
#include <crtos/module.h>
#include <crtos/arch.h>
#include "fsl_device_registers.h"

struct imx_gpio
{
    struct device *dev;
    GPIO_Type *base;
    int irq_base;
    int nvic[2];
};

/* ---- GPIO chip ------------------------------------------------------------------------------ */

static int g_get(void *ctx, unsigned pin)
{
    struct imx_gpio *g = ctx;
    uint32_t bit = 1u << pin;
    return (g->base->GDIR & bit) ? !!(g->base->DR & bit) : !!(g->base->PSR & bit);
}

static void g_set(void *ctx, unsigned pin, int value)
{
    struct imx_gpio *g = ctx;
    if (value)
        g->base->DR_SET = 1u << pin;
    else
        g->base->DR_CLEAR = 1u << pin;
}

static int g_dir_in(void *ctx, unsigned pin)
{
    struct imx_gpio *g = ctx;
    uint32_t key = irq_lock();
    g->base->GDIR &= ~(1u << pin);
    irq_unlock(key);
    return 0;
}

static int g_dir_out(void *ctx, unsigned pin, int value)
{
    struct imx_gpio *g = ctx;
    g_set(ctx, pin, value);
    uint32_t key = irq_lock();
    g->base->GDIR |= 1u << pin;
    irq_unlock(key);
    return 0;
}

static int g_to_irq(void *ctx, unsigned pin)
{
    struct imx_gpio *g = ctx;
    return g->irq_base + (int)pin;
}

static const struct gpio_chip_ops gpio_ops = {g_get, g_set, g_dir_in, g_dir_out, g_to_irq};

/* ---- interrupt controller ------------------------------------------------------------------- */

static void gi_mask(void *ctx, unsigned pin)
{
    struct imx_gpio *g = ctx;
    uint32_t key = irq_lock();
    g->base->IMR &= ~(1u << pin);
    irq_unlock(key);
}

static void gi_unmask(void *ctx, unsigned pin)
{
    struct imx_gpio *g = ctx;
    uint32_t key = irq_lock();
    g->base->ISR = 1u << pin; /* drop an edge latched while masked */
    g->base->IMR |= 1u << pin;
    irq_unlock(key);
}

static int gi_set_type(void *ctx, unsigned pin, unsigned type)
{
    struct imx_gpio *g = ctx;
    uint32_t icr;
    switch (type)
    {
    case IRQ_TYPE_LEVEL_LOW:
        icr = 0;
        break;
    case IRQ_TYPE_LEVEL_HIGH:
        icr = 1;
        break;
    case IRQ_TYPE_EDGE_RISING:
        icr = 2;
        break;
    case IRQ_TYPE_EDGE_FALLING:
        icr = 3;
        break;
    case IRQ_TYPE_EDGE_BOTH:
        icr = 0;
        break;
    default:
        return -EINVAL;
    }
    uint32_t key = irq_lock();
    volatile uint32_t *reg = pin < 16 ? &g->base->ICR1 : &g->base->ICR2;
    uint32_t shift = (pin & 15u) * 2u;
    *reg = (*reg & ~(3u << shift)) | (icr << shift);
    if (type == IRQ_TYPE_EDGE_BOTH)
        g->base->EDGE_SEL |= 1u << pin;
    else
        g->base->EDGE_SEL &= ~(1u << pin);
    g->base->ISR = 1u << pin;
    irq_unlock(key);
    return 0;
}

static const struct irq_chip gpio_irq_chip = {"gpio", gi_mask, gi_unmask, gi_set_type};

static void cascade(int irq, void *ctx)
{
    struct imx_gpio *g = ctx;
    uint32_t mask = irq == g->nvic[0] ? 0x0000FFFFu : 0xFFFF0000u;
    uint32_t pending = g->base->ISR & g->base->IMR & mask;
    g->base->ISR = pending; /* write one to clear, before handling: new edges are kept */
    while (pending)
    {
        unsigned pin = (unsigned)__builtin_ctz(pending);
        pending &= pending - 1;
        irq_handle_nested(g->irq_base + (int)pin);
    }
    __asm volatile("dsb 0xF" ::: "memory");
}

/* <&gpioN pin type> -> IRQ number, configuring the trigger */
static int gpio_xlate(void *ctx, const uint32_t *args, int nargs)
{
    struct imx_gpio *g = ctx;
    if (nargs < 1 || args[0] > 31)
        return -EINVAL;
    if (nargs > 1 && args[1])
        gi_set_type(ctx, args[0], args[1]);
    return g->irq_base + (int)args[0];
}

/* ---- driver ---------------------------------------------------------------------------------- */

static int gpio_probe(struct device *dev)
{
    struct imx_gpio *g = devm_kzalloc(dev, sizeof(*g), 0);
    if (!g)
        return -ENOMEM;
    g->dev = dev;
    g->base = device_map(dev, 0);
    if (!g->base)
        return -EINVAL;
    struct clk *clk;
    int r = devm_clk_get_enabled(dev, NULL, &clk);
    if (r == -EPROBE_DEFER)
        return r;

    g->base->IMR = 0;
    g->base->ISR = 0xFFFFFFFFu;
    g->irq_base = irq_alloc_descs(32, &gpio_irq_chip, g);
    if (g->irq_base < 0)
        return g->irq_base;
    for (int i = 0; i < 2; i++)
    {
        g->nvic[i] = device_get_irq(dev, i);
        if (g->nvic[i] < 0 || irq_request(g->nvic[i], cascade, g, 8, dev->name))
        {
            for (int k = 0; k < i; k++)
                irq_free(g->nvic[k]);
            irq_free_descs(g->irq_base, 32);
            return -EBUSY;
        }
    }
    irq_domain_add(dev->of_node, gpio_xlate, g);
    gpiochip_register(dev->of_node, 32, &gpio_ops, g);
    dev_set_drvdata(dev, g);
    dev_info(dev, "32 GPIOs, IRQs %d-%d\n", g->irq_base, g->irq_base + 31);
    return 0;
}

static void gpio_remove(struct device *dev)
{
    struct imx_gpio *g = dev_get_drvdata(dev);
    gpiochip_unregister(dev->of_node);
    irq_domain_remove(dev->of_node);
    g->base->IMR = 0;
    irq_free(g->nvic[0]);
    irq_free(g->nvic[1]);
    irq_free_descs(g->irq_base, 32);
}

static const struct of_device_id gpio_ids[] = {
    {"fsl,imxrt1050-gpio", NULL},
    {NULL, NULL},
};

static struct driver gpio_driver = {
    .name = "gpio-imxrt",
    .of_match_table = gpio_ids,
    .probe = gpio_probe,
    .remove = gpio_remove,
};

static int init(void)
{
    return driver_register(&gpio_driver);
}

static void fini(void)
{
    driver_unregister(&gpio_driver);
}

MODULE("gpio-imxrt", "i.MX RT GPIO ports and GPIO interrupts", init, fini);
