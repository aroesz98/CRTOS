/*
 * subsys/pinctrl.cpp - pin control framework.
 */
#include "kernel.h"
#include <crtos/device.h>
#include <crtos/pinctrl.h>
#include <string.h>

#define MAX_CONTROLLERS 4

static struct {
    struct device_node *np;
    const struct pinctrl_ops *ops;
    void *ctx;
} s_ctl[MAX_CONTROLLERS];

int pinctrl_register(struct device_node *controller, const struct pinctrl_ops *ops, void *ctx)
{
    uint32_t key = irq_lock();
    for (int i = 0; i < MAX_CONTROLLERS; i++) {
        if (!s_ctl[i].np) {
            s_ctl[i].ops = ops;
            s_ctl[i].ctx = ctx;
            s_ctl[i].np = controller;
            irq_unlock(key);
            return 0;
        }
    }
    irq_unlock(key);
    return -ENOSPC;
}

void pinctrl_unregister(struct device_node *controller)
{
    uint32_t key = irq_lock();
    for (int i = 0; i < MAX_CONTROLLERS; i++)
        if (s_ctl[i].np == controller)
            s_ctl[i].np = nullptr;
    irq_unlock(key);
}

/* The controller owning a pin group is its closest registered ancestor */
static int find_controller(const struct device_node *group)
{
    for (const struct device_node *p = group->parent; p; p = p->parent)
        for (int i = 0; i < MAX_CONTROLLERS; i++)
            if (s_ctl[i].np == p)
                return i;
    return -1;
}

int pinctrl_select_state(struct device *dev, const char *state)
{
    const struct device_node *np = dev->of_node;
    if (!np)
        return -ENOENT;
    int idx = of_property_match_string(np, "pinctrl-names", state);
    if (idx < 0) {
        if (strcmp(state, "default") || !of_get_property(np, "pinctrl-0", nullptr))
            return -ENOENT;
        idx = 0;
    }
    char prop[16];
    ksnprintf(prop, sizeof(prop), "pinctrl-%d", idx);
    uint32_t len;
    const uint8_t *v = (const uint8_t *)of_get_property(np, prop, &len);
    if (!v)
        return -ENOENT;
    /* all controllers must be ready before anything is changed */
    for (uint32_t i = 0; i + 4 <= len; i += 4) {
        const struct device_node *group = of_find_node_by_phandle(of_be32(v + i));
        if (!group)
            return -EINVAL;
        if (find_controller(group) < 0)
            return -EPROBE_DEFER;
    }
    for (uint32_t i = 0; i + 4 <= len; i += 4) {
        const struct device_node *group = of_find_node_by_phandle(of_be32(v + i));
        int c = find_controller(group);
        int r = s_ctl[c].ops->apply(s_ctl[c].ctx, group);
        if (r)
            return r;
    }
    return 0;
}
