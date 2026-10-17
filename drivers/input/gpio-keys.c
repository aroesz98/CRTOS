/*
 * gpio-keys.ko - buttons on GPIO lines (device tree binding "gpio-keys").
 *
 * Each child node is a key: gpios, linux,code, optional label and debounce-interval.
 * Both edges interrupt; after the debounce interval a thread samples all keys and reports
 * the ones that changed.
 */
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/gpio.h>
#include <crtos/input.h>
#include <crtos/irq.h>
#include <crtos/module.h>
#include <crtos/sched.h>
#include <crtos/sync.h>

#define MAX_KEYS 8

struct gkey
{
    struct gpio_desc *gpio;
    uint32_t code;
    int irq;
    int state;
    const char *label;
};

struct gkeys
{
    struct device *dev;
    struct gkey keys[MAX_KEYS];
    int n;
    uint32_t debounce_ms;
    struct input_dev *input;
    struct semaphore sem;
    task_t *thread;
};

static void key_irq(int irq, void *ctx)
{
    (void)irq;
    struct gkeys *k = ctx;
    sem_give(&k->sem);
}

static void keys_thread(void *arg)
{
    struct gkeys *k = arg;
    while (!task_should_stop())
    {
        if (sem_take(&k->sem, WAIT_FOREVER) == -EINTR)
            break;
        task_sleep_ms(k->debounce_ms);
        int changed = 0;
        for (int i = 0; i < k->n; i++)
        {
            int v = gpiod_get_value(k->keys[i].gpio);
            if (v != k->keys[i].state)
            {
                k->keys[i].state = v;
                input_report(k->input, EV_KEY, (uint16_t)k->keys[i].code, v);
                changed = 1;
            }
        }
        if (changed)
            input_sync(k->input);
    }
}

static int keys_probe(struct device *dev)
{
    struct gkeys *k = devm_kzalloc(dev, sizeof(*k), 0);
    if (!k)
        return -ENOMEM;
    k->dev = dev;
    k->debounce_ms = 5;
    struct device_node *np;
    for_each_child_of_node(dev->of_node, np)
    {
        if (k->n >= MAX_KEYS)
            break;
        struct gkey *key = &k->keys[k->n];
        int r = gpiod_get_index(dev, np, "gpios", 0, GPIOD_IN, &key->gpio);
        if (r)
            return r; /* -EPROBE_DEFER until the GPIO controller is there */
        if (of_property_read_u32(np, "linux,code", &key->code))
            continue;
        uint32_t deb = 0;
        if (!of_property_read_u32(np, "debounce-interval", &deb) && deb > k->debounce_ms)
            k->debounce_ms = deb;
        if (of_property_read_string(np, "label", &key->label))
            key->label = np->name;
        key->state = gpiod_get_value(key->gpio);
        key->irq = gpiod_to_irq(key->gpio);
        k->n++;
    }
    if (!k->n)
        return -ENODEV;
    k->input = input_register("gpio-keys");
    if (!k->input)
        return -ENOMEM;
    for (int i = 0; i < k->n; i++)
        input_set_key(k->input, (uint16_t)k->keys[i].code);
    sem_init(&k->sem, 0, 1);
    for (int i = 0; i < k->n; i++)
    {
        if (k->keys[i].irq < 0)
            continue;
        irq_set_type(k->keys[i].irq, IRQ_TYPE_EDGE_BOTH);
        if (irq_request(k->keys[i].irq, key_irq, k, 0, k->keys[i].label))
            k->keys[i].irq = -1;
    }
    k->thread = kthread_create("gpio-keys", keys_thread, k, PRIO_HIGH, 1024);
    if (!k->thread)
    {
        input_unregister(k->input);
        return -ENOMEM;
    }
    dev_set_drvdata(dev, k);
    for (int i = 0; i < k->n; i++)
        dev_info(dev, "key '%s' code %lu (%s)\n", k->keys[i].label, (unsigned long)k->keys[i].code,
                 k->keys[i].state ? "pressed" : "released");
    return 0;
}

static void keys_remove(struct device *dev)
{
    struct gkeys *k = dev_get_drvdata(dev);
    for (int i = 0; i < k->n; i++)
        if (k->keys[i].irq >= 0)
            irq_free(k->keys[i].irq);
    kthread_stop(k->thread);
    input_unregister(k->input);
}

static const struct of_device_id keys_ids[] = {
    {"gpio-keys", NULL},
    {NULL, NULL},
};

static struct driver keys_driver = {
    .name = "gpio-keys",
    .of_match_table = keys_ids,
    .probe = keys_probe,
    .remove = keys_remove,
};

static int init(void)
{
    return driver_register(&keys_driver);
}

static void fini(void)
{
    driver_unregister(&keys_driver);
}

MODULE("gpio-keys", "buttons on GPIO lines", init, fini);
