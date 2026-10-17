/*
 * hello.ko - example driver module.
 *
 * Binds to device tree nodes with compatible = "crtos,hello", exports hello_greet() for
 * other modules and exercises the loader (data relocations, bss, libgcc helpers).
 */
#include <crtos/device.h>
#include <crtos/module.h>
#include <crtos/printk.h>
#include <crtos/sched.h>

struct hello_priv
{
    const char *label;
    uint32_t probe_us;
};

static int greetings;                       /* .bss */
static const char *const moods[] = { "calm", "busy", "happy" };   /* data with relocations */

int hello_greet(const char *who)
{
    greetings++;
    printk("hello: hi %s, you are greeting #%d (%s)\n", who, greetings, moods[greetings % 3]);
    return greetings;
}
EXPORT_SYMBOL(hello_greet);

static int hello_probe(struct device *dev)
{
    struct hello_priv *p = devm_kzalloc(dev, sizeof(*p), 0);
    if (!p)
        return -12;
    if (of_property_read_string(dev->of_node, "label", &p->label))
        p->label = "(no label)";
    uint64_t t = time_us();
    p->probe_us = (uint32_t)(t % 1000000u);  /* 64-bit modulo: __aeabi_uldivmod from libgcc */
    dev_set_drvdata(dev, p);
    dev_info(dev, "hello driver bound, label '%s', %u s since boot\n", p->label, (unsigned)(t / 1000000u));
    return 0;
}

static void hello_remove(struct device *dev)
{
    struct hello_priv *p = dev_get_drvdata(dev);
    dev_info(dev, "hello driver removed (label '%s')\n", p->label);
}

static const struct of_device_id hello_ids[] = {
    { "crtos,hello", NULL },
    { NULL, NULL },
};

static struct driver hello_driver = {
    .name = "hello",
    .of_match_table = hello_ids,
    .probe = hello_probe,
    .remove = hello_remove,
};

static int hello_init(void)
{
    printk("hello: module init\n");
    return driver_register(&hello_driver);
}

static void hello_exit(void)
{
    driver_unregister(&hello_driver);
    printk("hello: module exit after %d greetings\n", greetings);
}

MODULE("hello", "example driver module", hello_init, hello_exit);
