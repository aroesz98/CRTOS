/*
 * kernel/os/module.cpp - kernel modules (.ko).
 *
 * A module is a relocatable ELF object (elf.cpp). All its sections go into one block of
 * executable kernel memory, so calls between the module's own functions stay in BL range;
 * calls into the kernel and other modules use absolute addresses (built with -mlong-calls).
 * Undefined symbols are resolved against the kernel's export table and the exports of the
 * modules already loaded, which then cannot be unloaded while this one uses them.
 */
#include "kernel.h"
#include "elf.h"
#include <crtos/module.h>
#include <crtos/vfs.h>
#include <string.h>
#include "fsl_device_registers.h"

#define MAX_DEPS 16

struct module {
    char name[24];
    uint8_t *base;
    uint32_t size;
    const struct module_info *info;
    const struct ksym *exports;
    uint32_t nexports;
    void (**fini)(void);
    uint32_t nfini;
    struct module *deps[MAX_DEPS];
    int ndeps;
    uint32_t refs;              /* dependants, pins, open files (under irq_lock) */
    bool going;                 /* being unloaded: no new references */
    struct list_head node;      /* s_modules: changed under s_mod_lock and irq_lock */
};

static struct list_head s_modules = LIST_HEAD_INIT(s_modules);
static struct mutex s_mod_lock;
static bool s_mod_ready;
static struct module *s_loading;

static void mod_init_once(void)
{
    if (s_mod_ready)
        return;
    uint32_t key = irq_lock();
    if (!s_mod_ready) {
        mutex_init(&s_mod_lock);
        s_mod_ready = true;
    }
    irq_unlock(key);
}

struct module *module_loading(void)
{
    return s_loading;
}

const char *module_name(const struct module *m)
{
    return m ? m->name : "kernel";
}

void module_pin(void)
{
    if (s_loading) { /* under s_mod_lock: init() runs with it held */
        uint32_t key = irq_lock();
        s_loading->refs++;
        irq_unlock(key);
    }
}

int module_get_addr(const void *addr, struct module **out)
{
    *out = nullptr;
    uint32_t key = irq_lock(); /* the list and the counts do not change meanwhile */
    struct list_head *pos;
    list_for_each(pos, &s_modules) {
        struct module *m = list_entry(pos, struct module, node);
        if ((const uint8_t *)addr >= m->base && (const uint8_t *)addr < m->base + m->size) {
            if (m->going) {
                irq_unlock(key);
                return -ENODEV;
            }
            m->refs++;
            *out = m;
            break;
        }
    }
    irq_unlock(key);
    return 0;
}

void module_put(struct module *m)
{
    if (m) {
        uint32_t key = irq_lock();
        m->refs--;
        irq_unlock(key);
    }
}

/* ---- symbols ------------------------------------------------------------------------------------ */

static const void *lookup_module_export(const char *name, struct module **provider)
{
    struct list_head *pos;
    list_for_each(pos, &s_modules) {
        struct module *m = list_entry(pos, struct module, node);
        for (uint32_t i = 0; i < m->nexports; i++) {
            if (!strcmp(m->exports[i].name, name)) {
                if (provider)
                    *provider = m;
                return m->exports[i].addr;
            }
        }
    }
    return nullptr;
}

const void *ksym_lookup(const char *name)
{
    const void *a = ksym_kernel_lookup(name);
    if (a)
        return a;
    mod_init_once();
    mutex_lock(&s_mod_lock, WAIT_FOREVER);
    a = lookup_module_export(name, nullptr);
    mutex_unlock(&s_mod_lock);
    return a;
}

static struct module *find_locked(const char *name)
{
    struct list_head *pos;
    list_for_each(pos, &s_modules) {
        struct module *m = list_entry(pos, struct module, node);
        if (!strcmp(m->name, name))
            return m;
    }
    return nullptr;
}

struct module *module_find(const char *name)
{
    mod_init_once();
    mutex_lock(&s_mod_lock, WAIT_FOREVER);
    struct module *found = find_locked(name);
    mutex_unlock(&s_mod_lock);
    return found;
}

/* For fault reports: which module contains @addr */
const char *module_addr_lookup(uintptr_t addr, uint32_t *offset)
{
    struct list_head *pos;
    list_for_each(pos, &s_modules) {
        struct module *m = list_entry(pos, struct module, node);
        if (addr >= (uintptr_t)m->base && addr < (uintptr_t)m->base + m->size) {
            *offset = (uint32_t)(addr - (uintptr_t)m->base);
            return m->name;
        }
    }
    return nullptr;
}

/* ---- loading ------------------------------------------------------------------------------------ */

static void add_dep(struct module *m, struct module *dep)
{
    for (int i = 0; i < m->ndeps; i++)
        if (m->deps[i] == dep)
            return;
    if (m->ndeps < MAX_DEPS) {
        m->deps[m->ndeps++] = dep;
        uint32_t key = irq_lock();
        dep->refs++;
        irq_unlock(key);
    }
}

static uintptr_t mod_resolve(const char *name, void *ctx)
{
    struct module *m = (struct module *)ctx;
    const void *a = ksym_kernel_lookup(name);
    if (a)
        return (uintptr_t)a;
    struct module *prov = nullptr;
    a = lookup_module_export(name, &prov);
    if (a && prov)
        add_dep(m, prov);
    return (uintptr_t)a;
}

static int module_load_depth(const char *path, struct module **out, int depth);

/* Load the modules listed in ".crtos_depends" from the directory of @path */
static __attribute__((noinline)) int load_dependencies(const struct elf_ctx *c, const char *path, int depth)
{
    const Elf32_Shdr *ds = nullptr;
    for (uint32_t i = 0; i < c->eh->e_shnum; i++)
        if (!strcmp(c->shstr + c->sh[i].sh_name, ".crtos_depends"))
            ds = &c->sh[i];
    if (!ds || ds->sh_type == SHT_NOBITS)
        return 0;
    if (depth > 4)
        return -ELOOP;
    const char *p = (const char *)c->file + ds->sh_offset;
    const char *end = p + ds->sh_size;
    const char *slash = strrchr(path, '/');
    size_t dirlen = slash ? (size_t)(slash - path) : 0;
    while (p < end && *p) {
        while (p < end && (*p == ',' || *p == ' '))
            p++;
        const char *q = p;
        while (q < end && *q && *q != ',' && *q != ' ')
            q++;
        size_t n = (size_t)(q - p);
        if (n && n < 24) {
            char name[24];
            memcpy(name, p, n);
            name[n] = 0;
            if (!find_locked(name)) {
                char *dep = (char *)kmalloc(VFS_PATH_MAX, KM_ANY);
                if (!dep)
                    return -ENOMEM;
                ksnprintf(dep, VFS_PATH_MAX, "%.*s/%s.ko", (int)dirlen, path, name);
                int r = module_load_depth(dep, nullptr, depth + 1);
                kfree(dep);
                if (r && r != -EEXIST) {
                    printk("E: %s: dependency '%s' failed (%d)\n", path, name, r);
                    return r;
                }
            }
        }
        p = q;
    }
    return 0;
}

static void drop_deps(struct module *m)
{
    uint32_t key = irq_lock();
    for (int i = 0; i < m->ndeps; i++)
        m->deps[i]->refs--;
    irq_unlock(key);
    m->ndeps = 0;
}

int module_load(const char *path, struct module **out)
{
    return module_load_depth(path, out, 0);
}

static int module_load_depth(const char *path, struct module **out, int depth)
{
    mod_init_once();
    struct elf_ctx c;
    memset(&c, 0, sizeof(c));
    void *file = nullptr;
    size_t size = 0;
    int r = vfs_load_file(path, &file, &size, KM_LARGE);
    if (r)
        return r;
    c.file = (const uint8_t *)file;
    c.size = size;
    c.who = path;

    mutex_lock(&s_mod_lock, WAIT_FOREVER);
    struct module *m = (struct module *)kzalloc(sizeof(*m), KM_ANY);
    if (!m) {
        r = -ENOMEM;
        goto out;
    }
    r = elf_check(&c);
    if (!r)
        r = load_dependencies(&c, path, depth);
    if (r)
        goto fail;
    {
        uint32_t msize, align;
        r = elf_layout(&c, &msize, &align);
        if (r)
            goto fail;
        m->base = (uint8_t *)kmalloc_aligned(msize, align > 32u ? align : 32u, KM_EXEC);
        if (!m->base) {
            r = -ENOMEM;
            goto fail;
        }
        m->size = msize;
        elf_place(&c, m->base);
    }
    r = elf_symbols(&c, mod_resolve, m);
    if (!r)
        r = elf_relocate(&c);
    if (r)
        goto fail;

    {
        uint32_t idx;
        const Elf32_Shdr *info = elf_section(&c, ".crtos_module", &idx);
        if (!info || info->sh_size < sizeof(struct module_info)) {
            printk("E: %s: no MODULE() descriptor\n", path);
            r = -ENOEXEC;
            goto fail;
        }
        m->info = (const struct module_info *)(uintptr_t)c.secaddr[idx];
        if (m->info->magic != MODULE_MAGIC || m->info->abi != MODULE_ABI) {
            printk("E: %s: module ABI %lu, kernel %lu\n", path, (unsigned long)m->info->abi, (unsigned long)MODULE_ABI);
            r = -ENOEXEC;
            goto fail;
        }
        strncpy(m->name, m->info->name ? m->info->name : "?", sizeof(m->name) - 1);
        if (find_locked(m->name)) {
            r = -EEXIST;
            goto fail;
        }
        const Elf32_Shdr *ks = elf_section(&c, ".crtos_ksymtab", &idx);
        if (ks) {
            m->exports = (const struct ksym *)(uintptr_t)c.secaddr[idx];
            m->nexports = ks->sh_size / sizeof(struct ksym);
        }
        const Elf32_Shdr *fa = elf_section(&c, ".fini_array", &idx);
        if (fa) {
            m->fini = (void (**)(void))(uintptr_t)c.secaddr[idx];
            m->nfini = fa->sh_size / 4;
        }
        SCB_CleanDCache_by_Addr(m->base, (int32_t)ALIGN_UP(m->size, 32u));
        SCB_InvalidateICache();

        uint32_t key = irq_lock();
        list_add_tail(&m->node, &s_modules);
        irq_unlock(key);
        s_loading = m;
        const Elf32_Shdr *ia = elf_section(&c, ".init_array", &idx);
        if (ia) {
            void (**ctor)(void) = (void (**)(void))(uintptr_t)c.secaddr[idx];
            for (uint32_t i = 0; i < ia->sh_size / 4; i++)
                ctor[i]();
        }
        r = m->info->init ? m->info->init() : 0;
        s_loading = nullptr;
        if (r) {
            printk("E: %s: init failed (%d)\n", m->name, r);
            driver_unregister_owner(m);
            key = irq_lock();
            list_del(&m->node);
            irq_unlock(key);
            goto fail;
        }
    }
    printk("module %s loaded at %p (%lu bytes)\n", m->name, m->base, (unsigned long)m->size);
    if (out)
        *out = m;
    m = nullptr;
    r = 0;
    goto out;

fail:
    if (m) {
        drop_deps(m);
        kfree(m->base);
        kfree(m);
    }
out:
    mutex_unlock(&s_mod_lock);
    elf_release(&c);
    kfree(file);
    return r;
}

int module_unload(const char *name)
{
    mod_init_once();
    mutex_lock(&s_mod_lock, WAIT_FOREVER);
    struct module *m = nullptr;
    struct list_head *pos;
    list_for_each(pos, &s_modules) {
        struct module *x = list_entry(pos, struct module, node);
        if (!strcmp(x->name, name))
            m = x;
    }
    if (!m) {
        mutex_unlock(&s_mod_lock);
        return -ENOENT;
    }
    uint32_t key = irq_lock();
    bool busy = m->refs != 0;
    m->going = !busy; /* from here on nobody takes a reference (module_get_addr) */
    irq_unlock(key);
    if (busy) {
        mutex_unlock(&s_mod_lock);
        return -EBUSY;
    }
    if (m->info->exit)
        m->info->exit();
    driver_unregister_owner(m);
    for (uint32_t i = m->nfini; i > 0; i--)
        m->fini[i - 1]();
    key = irq_lock();
    list_del(&m->node);
    irq_unlock(key);
    drop_deps(m);
    mutex_unlock(&s_mod_lock);
    printk("module %s unloaded\n", m->name);
    kfree(m->base);
    kfree(m);
    return 0;
}

void module_foreach(void (*fn)(const char *name, const char *desc, void *base, uint32_t size, uint32_t refs, void *ctx),
                    void *ctx)
{
    mod_init_once();
    mutex_lock(&s_mod_lock, WAIT_FOREVER);
    struct list_head *pos;
    list_for_each(pos, &s_modules) {
        struct module *m = list_entry(pos, struct module, node);
        fn(m->name, m->info->description ? m->info->description : "", m->base, m->size, m->refs, ctx);
    }
    mutex_unlock(&s_mod_lock);
}
