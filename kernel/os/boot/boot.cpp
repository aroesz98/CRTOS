/*
 * kernel/os/boot/boot.cpp - second boot stage (kernel thread): SD card, device tree, drivers.
 *
 *   1. mount the SD card at /sd (built-in USDHC + FAT)
 *   2. load /sd/crtos/boot/board.dtb and create devices for its nodes
 *   3. bind the built-in drivers, then load driver modules for the remaining devices
 *      according to /sd/crtos/drivers/modules.alias ("<compatible> <module>" lines)
 * Holding SW8 while booting (safe mode) skips step 3.
 */
#define CRTOS_KERNEL 1
#include "kernel.h"
#include "sdcard.h"
#include <crtos/syscall.h>
#include <crtos/device.h>
#include <crtos/module.h>
#include <crtos/of.h>
#include <crtos/vfs.h>
#include <string.h>
#include "fsl_gpio.h"
#include "fsl_iomuxc.h"

#define BOOT_PRIO  (PRIO_HIGH - 2)

/* ---- built-in drivers --------------------------------------------------------------------- */

static int builtin_ok_probe(struct device *)
{
    return 0;
}

static const struct of_device_id bus_ids[] = { { "simple-bus", nullptr }, { "arm,armv7m-nvic", nullptr }, { nullptr, nullptr } };
static struct driver bus_driver = { "builtin-bus", bus_ids, builtin_ok_probe, nullptr, nullptr, LIST_HEAD_INIT(bus_driver.node) };

static int usdhc_probe(struct device *dev)
{
    /* only the controller the kernel booted from */
    uint32_t addr;
    if (device_get_reg(dev, 0, &addr, nullptr) || addr != (uint32_t)(uintptr_t)USDHC1)
        return -ENODEV;
    struct sdcard_info info;
    sdcard_get_info(&info);
    return info.ready ? 0 : -ENODEV;
}

static const struct of_device_id usdhc_ids[] = { { "fsl,imxrt1050-usdhc", nullptr }, { nullptr, nullptr } };
static struct driver usdhc_driver = { "builtin-usdhc", usdhc_ids, usdhc_probe, nullptr, nullptr, LIST_HEAD_INIT(usdhc_driver.node) };

static int earlycon_probe(struct device *dev)
{
    /* the console UART named by /chosen/stdout-path stays with the kernel console until a
     * tty driver takes it over */
    return dev->of_node == of_stdout_node() ? 0 : -ENODEV;
}

static const struct of_device_id earlycon_ids[] = { { "fsl,imxrt1050-lpuart", nullptr }, { nullptr, nullptr } };
static struct driver earlycon_driver = { "earlycon", earlycon_ids, earlycon_probe, nullptr, nullptr, LIST_HEAD_INIT(earlycon_driver.node) };

/* ---- safe mode ------------------------------------------------------------------------------ */

static bool safe_mode_requested(void)
{
    /* SW8 on the WAKEUP pad (GPIO5_IO00), active low */
    IOMUXC_SetPinMux(IOMUXC_SNVS_WAKEUP_GPIO5_IO00, 0U);
    IOMUXC_SetPinConfig(IOMUXC_SNVS_WAKEUP_GPIO5_IO00, 0x01B0A0U); /* pull-up, hysteresis */
    gpio_pin_config_t in = { kGPIO_DigitalInput, 0U, kGPIO_NoIntmode };
    GPIO_PinInit(GPIO5, 0U, &in);
    task_sleep_ms(2);
    return GPIO_PinRead(GPIO5, 0U) == 0U;
}

/* ---- module autoloading ----------------------------------------------------------------------- */

#define MAX_ALIASES 64
#define MAX_PENDING 16

struct alias {
    const char *compatible;
    const char *module;
};

struct autoload {
    struct alias aliases[MAX_ALIASES];
    int naliases;
    const char *pending[MAX_PENDING];
    int npending;
    const char *tried[MAX_ALIASES];
    int ntried;
};

static int parse_aliases(char *text, size_t len, struct autoload *al)
{
    char *p = text, *end = text + len;
    while (p < end && al->naliases < MAX_ALIASES) {
        char *line = p;
        while (p < end && *p != '\n')
            p++;
        if (p < end)
            *p++ = 0;
        else
            *end = 0;
        char *hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        char *compat = strtok(line, " \t\r");
        char *mod = strtok(nullptr, " \t\r");
        if (compat && mod)
            al->aliases[al->naliases++] = { compat, mod };
    }
    return al->naliases;
}

static bool was_tried(struct autoload *al, const char *mod)
{
    for (int i = 0; i < al->ntried; i++)
        if (!strcmp(al->tried[i], mod))
            return true;
    return false;
}

static void collect(struct device *dev, void *ctx)
{
    struct autoload *al = (struct autoload *)ctx;
    if (dev->state == DEV_BOUND || !dev->of_node)
        return;
    int n = of_property_count_strings(dev->of_node, "compatible");
    for (int i = 0; i < n; i++) {
        const char *compat;
        if (of_property_read_string_index(dev->of_node, "compatible", i, &compat))
            continue;
        for (int k = 0; k < al->naliases; k++) {
            const char *mod = al->aliases[k].module;
            /* loaded modules are skipped by module_load() (-EEXIST); no module lock here, the
             * device lock is held */
            if (strcmp(al->aliases[k].compatible, compat) || was_tried(al, mod))
                continue;
            bool dup = false;
            for (int j = 0; j < al->npending; j++)
                dup |= !strcmp(al->pending[j], mod);
            if (!dup && al->npending < MAX_PENDING)
                al->pending[al->npending++] = mod;
            return;
        }
    }
}

static void autoload_modules(const char *dir)
{
    void *text;
    size_t len;
    char *path = (char *)kmalloc(VFS_PATH_MAX, KM_ANY);
    if (!path)
        return;
    ksnprintf(path, VFS_PATH_MAX, "%s/modules.alias", dir);
    if (vfs_load_file(path, &text, &len, KM_ANY)) {
        printk("no %s: no driver modules loaded\n", path);
        kfree(path);
        return;
    }
    /* vfs_load_file() does not terminate the text: make room for it */
    char *buf = (char *)kmalloc(len + 1, KM_ANY);
    if (!buf) {
        kfree(text);
        kfree(path);
        return;
    }
    memcpy(buf, text, len);
    kfree(text);
    struct autoload *al = (struct autoload *)kzalloc(sizeof(*al), KM_ANY);
    if (!al) {
        kfree(buf);
        kfree(path);
        return;
    }
    parse_aliases(buf, len, al);
    for (;;) {
        al->npending = 0;
        device_foreach(collect, al);
        if (!al->npending)
            break;
        for (int i = 0; i < al->npending; i++) {
            const char *mod = al->pending[i];
            if (al->ntried < MAX_ALIASES)
                al->tried[al->ntried++] = mod;
            ksnprintf(path, VFS_PATH_MAX, "%s/%s.ko", dir, mod);
            int r = module_load(path, nullptr);
            if (r && r != -EEXIST)
                printk("E: %s: %d\n", path, r);
        }
    }
    kfree(al);
    kfree(buf);
    kfree(path);
}

/* ---- boot thread --------------------------------------------------------------------------------- */

static void mount_sd(void)
{
    int r = sdcard_init();
    if (r) {
        printk("sd: %s (%d)\n", r == -ENODEV ? "no card" : "card init failed", r);
        return;
    }
    struct sdcard_info info;
    sdcard_get_info(&info);
    printk("sd: %s card, %lu MB, %d-bit bus @ %lu MHz\n", info.high_capacity ? "SDHC/SDXC" : "SDSC",
           (unsigned long)(info.blocks / 2048u), info.bus_width, (unsigned long)(info.clock_hz / 1000000u));
    r = fat_mount("/sd");
    /* no free-space query here: on exFAT it scans the whole allocation bitmap (~1 s) */
    if (!r)
        printk("sd: %s mounted on /sd\n", fat_type_name());
}

static int load_device_tree(const char *dtb)
{
    void *blob;
    size_t size;
    int r = vfs_load_file(dtb, &blob, &size, KM_ANY);
    if (r) {
        printk("W: %s: %d - no device tree, no drivers\n", dtb, r);
        return r;
    }
    r = of_init(blob, size);
    if (r) {
        printk("E: %s: invalid device tree (%d)\n", dtb, r);
        kfree(blob);
        return r;
    }
    const char *model = "?";
    of_property_read_string(of_root(), "model", &model);
    printk("dt: %s (%lu bytes)\n", model, (unsigned long)size);
    return 0;
}

/* Load a device tree, create its devices and load driver modules from @driver_dir (NULL:
 * none). Used at boot and by kmon "dtload" when the board booted without one. */
int boot_setup_devices(const char *dtb, const char *driver_dir)
{
    if (of_root())
        return -EEXIST;
    int r = load_device_tree(dtb);
    if (r)
        return r;
    driver_register(&bus_driver);
    driver_register(&usdhc_driver);
    driver_register(&earlycon_driver);
    device_populate();
    if (driver_dir)
        autoload_modules(driver_dir);
    return 0;
}

/* First user process: starts the services and the shell (it gets every capability) */
static void start_init(void)
{
    struct vfs_stat st;
    if (vfs_stat(CRTOS_INIT_PATH, &st)) {
        printk("no %s: the console stays with kmon\n", CRTOS_INIT_PATH);
        return;
    }
    struct file *con = tty_open_console();
    struct file *stdio[3] = { con, con, con };
    static const char *const argv[] = { "init", nullptr };
    static const char *const envp[] = { CRTOS_PROGRAM_ENV, nullptr };
    int err = 0;
    struct proc *p = app_spawn(nullptr, CRTOS_INIT_PATH, argv, envp, stdio, CAP_ALL, PRIO_NORMAL + 1, &err);
    if (con)
        vfs_close(con);
    if (!p) {
        printk("E: %s: %d\n", CRTOS_INIT_PATH, err);
        return;
    }
    printk("init started (pid %d)\n", p->pid);
    proc_put(p);
}

static void boot_thread(void *)
{
    uint64_t t0 = time_us();
    vfs_init();
    tty_init();
    uevent_init();
    gpu2d_init();
    ramfs_mount("/ram");
    mount_sd();
    bool safe = safe_mode_requested();
    if (safe)
        printk("safe mode (SW8 held): driver modules and init are not loaded\n");
    boot_setup_devices(CRTOS_DTB_PATH, safe ? nullptr : CRTOS_DRIVER_DIR);
    printk("boot: done in %lu ms\n", (unsigned long)((time_us() - t0) / 1000u));
    if (!safe)
        start_init();
}

void boot_start(void)
{
    if (!kthread_create("boot", boot_thread, nullptr, BOOT_PRIO, 4096))
        panic("cannot start the boot thread");
}
