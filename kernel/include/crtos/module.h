/*
 * crtos/module.h - loadable kernel modules (.ko).
 *
 * A module is a relocatable ELF object (ld -r) built with -mlong-calls. It describes itself
 * with MODULE() and may export functions/data to other modules with EXPORT_SYMBOL().
 * Undefined symbols are resolved against the kernel's export table and the exports of
 * modules already loaded. init() runs after relocation; a non-zero return aborts the load.
 *
 *     static int hello_init(void) { pr_info("hello\n"); return 0; }
 *     static void hello_exit(void) { }
 *     MODULE("hello", "example module", hello_init, hello_exit);
 *
 * MODULE_DEPENDS() also names modules that must be loaded first (from the same directory).
 */
#ifndef CRTOS_MODULE_H
#define CRTOS_MODULE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODULE_MAGIC 0x4D4B5243u    /* "CRKM" */
#define MODULE_ABI   2u     /* 2: newlib errno values, file_ops.poll, sized ioctl numbers */

struct module_info {
    uint32_t magic;
    uint32_t abi;
    const char *name;
    const char *description;
    const char *depends;        /* comma separated module names loaded first, or NULL */
    int (*init)(void);
    void (*exit)(void);
};

struct ksym {
    const char *name;
    const void *addr;
};

#define MODULE_INFO_(name_, desc_, deps_, init_, exit_)                                           \
    __attribute__((used, section(".crtos_module"))) const struct module_info __crtos_module_info = { \
        MODULE_MAGIC, MODULE_ABI, name_, desc_, deps_, init_, exit_                               \
    }
#define MODULE(name_, desc_, init_, exit_) MODULE_INFO_(name_, desc_, NULL, init_, exit_)
/* The dependency list is also stored raw in its own section: the loader reads it before
 * relocating the module */
#define MODULE_DEPENDS(name_, desc_, deps_, init_, exit_)                                         \
    __attribute__((used, section(".crtos_depends"))) const char __crtos_module_depends[] = deps_;  \
    MODULE_INFO_(name_, desc_, __crtos_module_depends, init_, exit_)

#define EXPORT_SYMBOL(sym)                                                                         \
    static const char __kstrtab_##sym[] = #sym;                                                    \
    __attribute__((used, section(".crtos_ksymtab"))) const struct ksym __ksymtab_##sym = {         \
        __kstrtab_##sym, (const void *)&sym                                                        \
    }

struct module;

int module_load(const char *path, struct module **out);    /* -EEXIST if already loaded */
int module_unload(const char *name);
struct module *module_find(const char *name);
const char *module_name(const struct module *m);
/* From a module's init(): it can never be unloaded (it starts threads or keeps state that
 * cannot be taken down again) */
void module_pin(void);

/* Address of an exported symbol (kernel or loaded modules), NULL if unknown */
const void *ksym_lookup(const char *name);

#ifdef __cplusplus
}
#endif

#endif
