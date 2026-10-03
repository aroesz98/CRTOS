/*
 * crtos/pinctrl.h - pin configuration.
 *
 * A pin controller driver registers itself for its device tree node; consumers name their
 * pin groups with "pinctrl-names" / "pinctrl-N" (phandles to group nodes below the
 * controller). The driver core applies the "default" state before probe().
 */
#ifndef CRTOS_PINCTRL_H
#define CRTOS_PINCTRL_H

#include <crtos/of.h>

#ifdef __cplusplus
extern "C" {
#endif

struct device;

struct pinctrl_ops {
    int (*apply)(void *ctx, const struct device_node *group);  /* configure all pins of a group */
};

int pinctrl_register(struct device_node *controller, const struct pinctrl_ops *ops, void *ctx);
void pinctrl_unregister(struct device_node *controller);

/* 0, -ENOENT if the device has no such state, -EPROBE_DEFER if the controller is not ready */
int pinctrl_select_state(struct device *dev, const char *state);

#ifdef __cplusplus
}
#endif

#endif
