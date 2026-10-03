/*
 * subsys/clk.cpp - clock framework: providers, consumer handles, enable reference counts.
 */
#include "kernel.h"
#include <crtos/clk.h>
#include <crtos/device.h>
#include <string.h>

#define MAX_PROVIDERS 4
#define MAX_REFS      48

struct clk_provider {
    struct device_node *np;
    const struct clk_ops *ops;
    void *ctx;
};

struct clk {
    struct clk_provider *prov;
    uint32_t id;
    uint32_t enabled;       /* enables held through this handle */
};

static struct clk_provider s_prov[MAX_PROVIDERS];

/* enable counts are per clock, shared by all handles */
static struct {
    struct clk_provider *prov;
    uint32_t id;
    uint32_t count;
} s_refs[MAX_REFS];
static struct mutex s_lock;
static bool s_ready;

static void clk_init_once(void)
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

int clk_provider_register(struct device_node *np, const struct clk_ops *ops, void *ctx)
{
    clk_init_once();
    mutex_lock(&s_lock, WAIT_FOREVER);
    for (int i = 0; i < MAX_PROVIDERS; i++) {
        if (!s_prov[i].np) {
            s_prov[i].ops = ops;
            s_prov[i].ctx = ctx;
            s_prov[i].np = np;
            mutex_unlock(&s_lock);
            return 0;
        }
    }
    mutex_unlock(&s_lock);
    return -ENOSPC;
}

void clk_provider_unregister(struct device_node *np)
{
    clk_init_once();
    mutex_lock(&s_lock, WAIT_FOREVER);
    for (int i = 0; i < MAX_PROVIDERS; i++) {
        if (s_prov[i].np == np) {
            s_prov[i].np = nullptr;
            for (int k = 0; k < MAX_REFS; k++)
                if (s_refs[k].prov == &s_prov[i])
                    s_refs[k].prov = nullptr;
        }
    }
    mutex_unlock(&s_lock);
}

static void clk_release(void *arg)
{
    struct clk *c = (struct clk *)arg;
    while (c->enabled)
        clk_disable(c);
}

int devm_clk_get(struct device *dev, const char *name, struct clk **out)
{
    clk_init_once();
    const struct device_node *np = dev->of_node;
    int idx = name ? of_property_match_string(np, "clock-names", name) : 0;
    if (idx < 0)
        return -ENOENT;
    struct of_phandle_args a;
    if (of_parse_phandle_with_args(np, "clocks", "#clock-cells", idx, &a))
        return -ENOENT;
    struct clk_provider *prov = nullptr;
    mutex_lock(&s_lock, WAIT_FOREVER);
    for (int i = 0; i < MAX_PROVIDERS; i++)
        if (s_prov[i].np == a.np)
            prov = &s_prov[i];
    mutex_unlock(&s_lock);
    if (!prov)
        return -EPROBE_DEFER;
    struct clk *c = (struct clk *)devm_kzalloc(dev, sizeof(*c), KM_ANY);
    if (!c)
        return -ENOMEM;
    c->prov = prov;
    c->id = a.args_count ? a.args[0] : 0;
    int r = devm_add_action(dev, clk_release, c);
    if (r)
        return r;
    *out = c;
    return 0;
}

int devm_clk_get_enabled(struct device *dev, const char *name, struct clk **out)
{
    int r = devm_clk_get(dev, name, out);
    return r ? r : clk_enable(*out);
}

int clk_enable(struct clk *c)
{
    mutex_lock(&s_lock, WAIT_FOREVER);
    int slot = -1, free_slot = -1;
    for (int i = 0; i < MAX_REFS; i++) {
        if (s_refs[i].prov == c->prov && s_refs[i].id == c->id)
            slot = i;
        else if (!s_refs[i].prov && free_slot < 0)
            free_slot = i;
    }
    int r = 0;
    if (slot < 0) {
        if (free_slot < 0) {
            mutex_unlock(&s_lock);
            return -ENOSPC;
        }
        slot = free_slot;
        s_refs[slot].prov = c->prov;
        s_refs[slot].id = c->id;
        s_refs[slot].count = 0;
    }
    if (s_refs[slot].count == 0 && c->prov->ops->enable)
        r = c->prov->ops->enable(c->prov->ctx, c->id);
    if (!r) {
        s_refs[slot].count++;
        c->enabled++;
    }
    mutex_unlock(&s_lock);
    return r;
}

void clk_disable(struct clk *c)
{
    mutex_lock(&s_lock, WAIT_FOREVER);
    if (c->enabled) {
        c->enabled--;
        for (int i = 0; i < MAX_REFS; i++) {
            if (s_refs[i].prov == c->prov && s_refs[i].id == c->id && s_refs[i].count) {
                if (--s_refs[i].count == 0 && c->prov->ops->disable)
                    c->prov->ops->disable(c->prov->ctx, c->id);
                break;
            }
        }
    }
    mutex_unlock(&s_lock);
}

uint32_t clk_get_rate(struct clk *c)
{
    return c->prov && c->prov->ops->get_rate ? c->prov->ops->get_rate(c->prov->ctx, c->id) : 0;
}

int clk_set_rate(struct clk *c, uint32_t rate)
{
    if (!c->prov || !c->prov->ops->set_rate)
        return -ENOTSUP;
    mutex_lock(&s_lock, WAIT_FOREVER);
    int r = c->prov->ops->set_rate(c->prov->ctx, c->id, rate);
    mutex_unlock(&s_lock);
    return r;
}
