/*
 * hello_dep.ko - uses a symbol exported by hello.ko; the loader loads hello.ko first.
 */
#include <crtos/module.h>
#include <crtos/printk.h>

int hello_greet(const char *who);

static int dep_init(void)
{
    int n = hello_greet("hello_dep");
    printk("hello_dep: hello.ko answered %d\n", n);
    return 0;
}

static void dep_exit(void)
{
    hello_greet("hello_dep (leaving)");
}

MODULE_DEPENDS("hello_dep", "depends on hello.ko", "hello", dep_init, dep_exit);
