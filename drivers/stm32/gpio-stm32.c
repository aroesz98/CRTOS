/*
 * gpio-stm32.ko - the STM32H7S7's GPIO ports (16 pins each) and their interrupts (EXTI).
 *
 * The EXTI has one line per pin number, 0-15, each with an NVIC vector of its own (EXTI0-15);
 * a line serves one port at a time, chosen in the SBS (EXTICR). The EXTI node allocates 16 IRQ
 * numbers (irq_alloc_descs) and a port routes a line to itself when a driver asks for the
 * IRQ of one of its pins (gpiod_to_irq, or "interrupts = <pin type>" with the port as
 * interrupt-parent). Two ports cannot use the same line number at once (-EBUSY). The EXTI
 * only knows edges: a level trigger is taken as the edge into that level.
 */
#include <stdbool.h>
#include <crtos/arch.h>
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/gpio.h>
#include <crtos/irq.h>
#include <crtos/module.h>
#include "stm32h7rsxx.h"

#define LINES 16

struct exti {
    struct device *dev;
    int irq_base;
    int nvic[LINES];
    int8_t owner[LINES];        /* port routed to the line, -1: none */
};

struct port {
    struct device *dev;
    GPIO_TypeDef *g;
    unsigned n;                 /* A = 0 ... P = 15 */
};

static struct exti *s_exti;

/* ---- EXTI ---------------------------------------------------------------------------------- */

static void exti_mask(void *ctx, unsigned line)
{
    (void)ctx;
    uint32_t key = irq_lock();
    EXTI->IMR1 &= ~(1u << line);
    irq_unlock(key);
}

static void exti_unmask(void *ctx, unsigned line)
{
    (void)ctx;
    uint32_t key = irq_lock();
    EXTI->PR1 = 1u << line;     /* drop an edge latched while masked */
    EXTI->IMR1 |= 1u << line;
    irq_unlock(key);
}

static int exti_set_type(void *ctx, unsigned line, unsigned type)
{
    (void)ctx;
    bool rise = type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_LEVEL_HIGH);
    bool fall = type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_LEVEL_LOW);
    if (!rise && !fall)
        return -EINVAL;
    uint32_t key = irq_lock();
    EXTI->RTSR1 = (EXTI->RTSR1 & ~(1u << line)) | ((rise ? 1u : 0u) << line);
    EXTI->FTSR1 = (EXTI->FTSR1 & ~(1u << line)) | ((fall ? 1u : 0u) << line);
    EXTI->PR1 = 1u << line;
    irq_unlock(key);
    return 0;
}

static const struct irq_chip exti_chip = { "exti", exti_mask, exti_unmask, exti_set_type };

static void exti_irq(int irq, void *ctx)
{
    struct exti *e = ctx;
    for (int line = 0; line < LINES; line++) {
        if (e->nvic[line] != irq)
            continue;
        if (EXTI->PR1 & (1u << line)) {
            EXTI->PR1 = 1u << line;     /* before handling: a new edge is kept */
            irq_handle_nested(e->irq_base + line);
        }
        break;
    }
    __asm volatile("dsb 0xF" ::: "memory");
}

/* the line of @pin to port @n: its IRQ number, or -EBUSY when another port has the line */
static int exti_route(unsigned n, unsigned pin)
{
    struct exti *e = s_exti;
    if (!e)
        return -EPROBE_DEFER;
    uint32_t key = irq_lock();
    if (e->owner[pin] >= 0 && (unsigned)e->owner[pin] != n) {
        irq_unlock(key);
        return -EBUSY;
    }
    e->owner[pin] = (int8_t)n;
    SBS->EXTICR[pin >> 2] = (SBS->EXTICR[pin >> 2] & ~(0xFu << ((pin & 3u) * 4u))) | (n << ((pin & 3u) * 4u));
    irq_unlock(key);
    return e->irq_base + (int)pin;
}

static int exti_probe(struct device *dev)
{
    struct exti *e = devm_kzalloc(dev, sizeof(*e), 0);
    if (!e)
        return -ENOMEM;
    e->dev = dev;
    struct clk *clk;
    int r = devm_clk_get_enabled(dev, NULL, &clk);     /* the SBS (EXTICR) */
    if (r)
        return r;
    EXTI->IMR1 &= ~0xFFFFu;
    EXTI->PR1 = 0xFFFFu;
    e->irq_base = irq_alloc_descs(LINES, &exti_chip, e);
    if (e->irq_base < 0)
        return e->irq_base;
    for (int i = 0; i < LINES; i++) {
        e->owner[i] = -1;
        e->nvic[i] = device_get_irq(dev, i);
        if (e->nvic[i] < 0 || irq_request(e->nvic[i], exti_irq, e, 8, "exti")) {
            for (int k = 0; k < i; k++)
                irq_free(e->nvic[k]);
            irq_free_descs(e->irq_base, LINES);
            return -EBUSY;
        }
    }
    dev_set_drvdata(dev, e);
    s_exti = e;
    dev_info(dev, "EXTI lines 0-15 as IRQs %d-%d\n", e->irq_base, e->irq_base + LINES - 1);
    return 0;
}

static void exti_remove(struct device *dev)
{
    struct exti *e = dev_get_drvdata(dev);
    s_exti = NULL;
    EXTI->IMR1 &= ~0xFFFFu;
    for (int i = 0; i < LINES; i++)
        irq_free(e->nvic[i]);
    irq_free_descs(e->irq_base, LINES);
}

/* ---- GPIO ports ------------------------------------------------------------------------------ */

static int g_get(void *ctx, unsigned pin)
{
    struct port *p = ctx;
    bool out = ((p->g->MODER >> (pin * 2u)) & 3u) == 1u;
    return out ? !!(p->g->ODR & (1u << pin)) : !!(p->g->IDR & (1u << pin));
}

static void g_set(void *ctx, unsigned pin, int value)
{
    struct port *p = ctx;
    p->g->BSRR = value ? 1u << pin : 1u << (pin + 16u);
}

static int g_dir_in(void *ctx, unsigned pin)
{
    struct port *p = ctx;
    uint32_t key = irq_lock();
    p->g->MODER &= ~(3u << (pin * 2u));
    irq_unlock(key);
    return 0;
}

static int g_dir_out(void *ctx, unsigned pin, int value)
{
    struct port *p = ctx;
    g_set(ctx, pin, value);
    uint32_t key = irq_lock();
    p->g->MODER = (p->g->MODER & ~(3u << (pin * 2u))) | (1u << (pin * 2u));
    irq_unlock(key);
    return 0;
}

static int g_to_irq(void *ctx, unsigned pin)
{
    struct port *p = ctx;
    return pin < LINES ? exti_route(p->n, pin) : -EINVAL;
}

static const struct gpio_chip_ops gpio_ops = { g_get, g_set, g_dir_in, g_dir_out, g_to_irq };

/* <&gpioX pin type> -> IRQ number, configuring the trigger */
static int gpio_xlate(void *ctx, const uint32_t *args, int nargs)
{
    struct port *p = ctx;
    if (nargs < 1 || args[0] >= LINES)
        return -EINVAL;
    int irq = exti_route(p->n, args[0]);
    if (irq >= 0 && nargs > 1 && args[1])
        exti_set_type(NULL, args[0], args[1]);
    return irq;
}

static int gpio_probe(struct device *dev)
{
    struct port *p = devm_kzalloc(dev, sizeof(*p), 0);
    if (!p)
        return -ENOMEM;
    p->dev = dev;
    p->g = device_map(dev, 0);
    uint32_t n = 0;
    if (!p->g || of_property_read_u32(dev->of_node, "st,port", &n) || n > 15)
        return -EINVAL;
    p->n = n;
    struct clk *clk;
    int r = devm_clk_get_enabled(dev, NULL, &clk);
    if (r)
        return r;
    r = irq_domain_add(dev->of_node, gpio_xlate, p);
    if (!r) {
        r = gpiochip_register(dev->of_node, 16, &gpio_ops, p);
        if (r)
            irq_domain_remove(dev->of_node);
    }
    if (r) {
        dev_err(dev, "cannot register port %lu (%d)\n", (unsigned long)n, r);
        return r;
    }
    dev_set_drvdata(dev, p);
    return 0;
}

static void gpio_remove(struct device *dev)
{
    gpiochip_unregister(dev->of_node);
    irq_domain_remove(dev->of_node);
}

static const struct of_device_id exti_ids[] = {
    { "st,stm32h7rs-exti", NULL },
    { NULL, NULL },
};

static struct driver exti_driver = {
    .name = "exti-stm32",
    .of_match_table = exti_ids,
    .probe = exti_probe,
    .remove = exti_remove,
};

static const struct of_device_id gpio_ids[] = {
    { "st,stm32h7rs-gpio", NULL },
    { NULL, NULL },
};

static struct driver gpio_driver = {
    .name = "gpio-stm32",
    .of_match_table = gpio_ids,
    .probe = gpio_probe,
    .remove = gpio_remove,
};

static int init(void)
{
    int r = driver_register(&exti_driver);
    if (r)
        return r;
    r = driver_register(&gpio_driver);
    if (r)
        driver_unregister(&exti_driver);
    return r;
}

static void fini(void)
{
    driver_unregister(&gpio_driver);
    driver_unregister(&exti_driver);
}

MODULE("gpio-stm32", "STM32 GPIO ports and their interrupts (EXTI)", init, fini);
