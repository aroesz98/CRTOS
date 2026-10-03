/*
 * crtos/clk.h - clocks.
 *
 * Clock providers (the CCM driver) register for their device tree node; consumers reference
 * clocks with "clocks = <&ccm ID>" and optional "clock-names". Clock handles obtained with
 * devm_clk_get*() are released (and disabled) automatically when the device is unbound.
 */
#ifndef CRTOS_CLK_H
#define CRTOS_CLK_H

#include <stdint.h>
#include <crtos/of.h>

#ifdef __cplusplus
extern "C" {
#endif

struct device;
struct clk;

struct clk_ops {
    int (*enable)(void *ctx, uint32_t id);
    void (*disable)(void *ctx, uint32_t id);
    uint32_t (*get_rate)(void *ctx, uint32_t id);
    int (*set_rate)(void *ctx, uint32_t id, uint32_t rate);   /* may be NULL */
};

int clk_provider_register(struct device_node *np, const struct clk_ops *ops, void *ctx);
void clk_provider_unregister(struct device_node *np);

/* @name NULL = first clock. 0, -ENOENT (no such clock) or -EPROBE_DEFER (provider not bound) */
int devm_clk_get(struct device *dev, const char *name, struct clk **out);
int devm_clk_get_enabled(struct device *dev, const char *name, struct clk **out);

int clk_enable(struct clk *c);
void clk_disable(struct clk *c);
uint32_t clk_get_rate(struct clk *c);
int clk_set_rate(struct clk *c, uint32_t rate);

#ifdef __cplusplus
}
#endif

#endif
