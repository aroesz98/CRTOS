/*
 * pinctrl-stm32.ko - the pins of the STM32H7S7's GPIO ports.
 *
 * Group nodes below the controller list their pins as Linux and Zephyr do:
 *     pinmux = <STM32_PINMUX(D, 1, AF8)>, ...;    (dt-bindings/pinctrl/stm32-pinctrl.h)
 *     bias-pull-up; | bias-pull-down; | bias-disable;
 *     drive-open-drain; | drive-push-pull;
 *     slew-rate = <0..3>;                          (output speed: low ... very high)
 * A pin's function is an alternate function AF0-AF15, ANALOG or GPIO (then only the bias is
 * set here; the GPIO driver sets the direction). The port's clock is switched on as needed.
 */
#include <stdbool.h>
#include <crtos/arch.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/module.h>
#include <crtos/pinctrl.h>
#include "stm32h7rsxx.h"
#include <dt-bindings/pinctrl/stm32-pinctrl.h>

#define NPORTS 16

static GPIO_TypeDef *port(unsigned n)
{
    return (GPIO_TypeDef *)(GPIOA_BASE + 0x400u * n);
}

/* the port's AHB4 clock (bit n of AHB4ENR; ports I-L do not exist on this chip) */
static void port_clock(unsigned n)
{
    RCC->AHB4ENR |= 1u << n;
    (void)RCC->AHB4ENR;
}

static void set2(volatile uint32_t *reg, unsigned pin, uint32_t v)
{
    *reg = (*reg & ~(3u << (pin * 2u))) | (v << (pin * 2u));
}

static int stm32_apply(void *ctx, const struct device_node *group)
{
    (void)ctx;
    uint32_t len;
    const uint8_t *v = of_get_property(group, "pinmux", &len);
    if (!v || !len || len % 4)
        return -EINVAL;
    int pull = of_property_read_bool(group, "bias-pull-up") ? 1 : of_property_read_bool(group, "bias-pull-down") ? 2 : 0;
    bool od = of_property_read_bool(group, "drive-open-drain");
    uint32_t speed = 0;
    of_property_read_u32(group, "slew-rate", &speed);
    for (uint32_t i = 0; i < len; i += 4) {
        uint32_t m = of_be32(v + i);
        unsigned n = (m >> 12) & 0xFu, pin = (m >> 8) & 0xFu, func = m & 0xFFu;
        if (func > GPIO || (n >= 8 && n < 12))
            return -EINVAL;
        port_clock(n);
        GPIO_TypeDef *g = port(n);
        uint32_t key = irq_lock();
        set2(&g->PUPDR, pin, (uint32_t)pull);
        if (func == ANALOG) {
            set2(&g->MODER, pin, 3u);
        } else if (func == GPIO) {
            if ((g->MODER >> (pin * 2u) & 3u) == 3u || (g->MODER >> (pin * 2u) & 3u) == 2u)
                set2(&g->MODER, pin, 0u);   /* an input until its driver says */
            g->OTYPER = (g->OTYPER & ~(1u << pin)) | ((od ? 1u : 0u) << pin);
            set2(&g->OSPEEDR, pin, speed & 3u);
        } else {
            g->AFR[pin >> 3] = (g->AFR[pin >> 3] & ~(0xFu << ((pin & 7u) * 4u))) | (func << ((pin & 7u) * 4u));
            g->OTYPER = (g->OTYPER & ~(1u << pin)) | ((od ? 1u : 0u) << pin);
            set2(&g->OSPEEDR, pin, speed & 3u);
            set2(&g->MODER, pin, 2u);
        }
        irq_unlock(key);
    }
    return 0;
}

static const struct pinctrl_ops stm32_pinctrl_ops = { stm32_apply };

static int stm32_pinctrl_probe(struct device *dev)
{
    int r = pinctrl_register(dev->of_node, &stm32_pinctrl_ops, NULL);
    if (r)
        return r;
    dev_info(dev, "STM32 pin controller\n");
    return 0;
}

static void stm32_pinctrl_remove(struct device *dev)
{
    pinctrl_unregister(dev->of_node);
}

static const struct of_device_id stm32_pinctrl_ids[] = {
    { "st,stm32h7rs-pinctrl", NULL },
    { NULL, NULL },
};

static struct driver stm32_pinctrl_driver = {
    .name = "pinctrl-stm32",
    .of_match_table = stm32_pinctrl_ids,
    .probe = stm32_pinctrl_probe,
    .remove = stm32_pinctrl_remove,
};

static int init(void)
{
    return driver_register(&stm32_pinctrl_driver);
}

static void fini(void)
{
    driver_unregister(&stm32_pinctrl_driver);
}

MODULE("pinctrl-stm32", "STM32 GPIO pin controller", init, fini);
