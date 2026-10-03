/*
 * kernel/os/of.cpp - flattened device tree parser and the of_* API.
 */
#include "kernel.h"
#include <crtos/of.h>
#include <stdlib.h>
#include <string.h>

#define FDT_MAGIC       0xD00DFEEDu
#define FDT_BEGIN_NODE  1u
#define FDT_END_NODE    2u
#define FDT_PROP        3u
#define FDT_NOP         4u
#define FDT_END         9u
#define MAX_DEPTH       16

static const uint8_t *s_blob;
static size_t s_blob_size;
static struct device_node *s_root;
static struct device_node *s_aliases;

/* ---- unflattening --------------------------------------------------------------------------- */

struct fdt_cursor {
    const uint8_t *p, *end;
    const char *strings;
    uint32_t strings_size;
};

static int take_u32(struct fdt_cursor *c, uint32_t *v)
{
    if (c->p + 4 > c->end)
        return -EINVAL;
    *v = of_be32(c->p);
    c->p += 4;
    return 0;
}

static int take_str(struct fdt_cursor *c, const char **s)
{
    const uint8_t *q = c->p;
    while (q < c->end && *q)
        q++;
    if (q >= c->end)
        return -EINVAL;
    *s = (const char *)c->p;
    c->p = (const uint8_t *)ALIGN_UP((uintptr_t)(q + 1), 4u);
    return 0;
}

int of_init(const void *fdt_blob, size_t size)
{
    const uint8_t *b = (const uint8_t *)fdt_blob;
    if (size < 40 || of_be32(b) != FDT_MAGIC)
        return -EINVAL;
    uint32_t total = of_be32(b + 4), off_struct = of_be32(b + 8), off_strings = of_be32(b + 12);
    uint32_t version = of_be32(b + 20), size_strings = of_be32(b + 32), size_struct = of_be32(b + 36);
    if (total > size || version < 16 || off_struct + size_struct > total || off_strings + size_strings > total)
        return -EINVAL;

    struct fdt_cursor c = { b + off_struct, b + off_struct + size_struct, (const char *)b + off_strings, size_strings };
    struct device_node *stack[MAX_DEPTH];
    struct device_node *last_child[MAX_DEPTH];
    struct property *last_prop[MAX_DEPTH];
    int depth = -1;
    struct device_node *root = nullptr;

    for (;;) {
        uint32_t tok;
        if (take_u32(&c, &tok))
            return -EINVAL;
        if (tok == FDT_BEGIN_NODE) {
            const char *name;
            if (take_str(&c, &name) || depth + 1 >= MAX_DEPTH)
                return -EINVAL;
            struct device_node *np = (struct device_node *)kzalloc(sizeof(*np), KM_ANY);
            if (!np)
                return -ENOMEM;
            np->name = name;
            if (depth >= 0) {
                struct device_node *parent = stack[depth];
                np->parent = parent;
                if (last_child[depth])
                    last_child[depth]->sibling = np;
                else
                    parent->child = np;
                last_child[depth] = np;
            } else {
                root = np;
            }
            depth++;
            stack[depth] = np;
            last_child[depth] = nullptr;
            last_prop[depth] = nullptr;
        } else if (tok == FDT_END_NODE) {
            if (depth < 0)
                return -EINVAL;
            depth--;
        } else if (tok == FDT_PROP) {
            uint32_t len, nameoff;
            if (depth < 0 || take_u32(&c, &len) || take_u32(&c, &nameoff) || nameoff >= c.strings_size ||
                c.p + len > c.end)
                return -EINVAL;
            struct property *pp = (struct property *)kzalloc(sizeof(*pp), KM_ANY);
            if (!pp)
                return -ENOMEM;
            pp->name = c.strings + nameoff;
            pp->value = c.p;
            pp->length = len;
            c.p = (const uint8_t *)ALIGN_UP((uintptr_t)(c.p + len), 4u);
            struct device_node *np = stack[depth];
            if (last_prop[depth])
                last_prop[depth]->next = pp;
            else
                np->properties = pp;
            last_prop[depth] = pp;
            if ((!strcmp(pp->name, "phandle") || !strcmp(pp->name, "linux,phandle")) && len == 4)
                np->phandle = of_be32(pp->value);
        } else if (tok == FDT_NOP) {
            continue;
        } else if (tok == FDT_END) {
            break;
        } else {
            return -EINVAL;
        }
    }
    if (!root || depth != -1)
        return -EINVAL;
    s_blob = b;
    s_blob_size = size;
    s_root = root;
    s_aliases = of_find_node_by_path("/aliases");
    return 0;
}

struct device_node *of_root(void)
{
    return s_root;
}

struct device_node *of_next_node(struct device_node *np)
{
    if (!np)
        return s_root;
    if (np->child)
        return np->child;
    while (np) {
        if (np->sibling)
            return np->sibling;
        np = np->parent;
    }
    return nullptr;
}

/* Match one path component: "serial" matches "serial@40184000" too */
static bool name_matches(const char *node, const char *comp, size_t n)
{
    if (strncmp(node, comp, n))
        return false;
    return node[n] == 0 || (node[n] == '@' && !memchr(comp, '@', n));
}

struct device_node *of_find_node_by_path(const char *path)
{
    if (!s_root || !path)
        return nullptr;
    if (path[0] != '/') { /* alias, optionally followed by ":options" */
        if (!s_aliases)
            return nullptr;
        size_t n = strcspn(path, ":/");
        for (struct property *pp = s_aliases->properties; pp; pp = pp->next) {
            if (strlen(pp->name) == n && !strncmp(pp->name, path, n))
                return of_find_node_by_path((const char *)pp->value);
        }
        return nullptr;
    }
    struct device_node *np = s_root;
    const char *p = path;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p || *p == ':')
            break;
        size_t n = strcspn(p, "/:");
        struct device_node *child;
        struct device_node *found = nullptr;
        for_each_child_of_node(np, child) {
            if (name_matches(child->name, p, n)) {
                found = child;
                break;
            }
        }
        if (!found)
            return nullptr;
        np = found;
        p += n;
    }
    return np;
}

struct device_node *of_find_node_by_phandle(uint32_t phandle)
{
    if (!phandle)
        return nullptr;
    for (struct device_node *np = of_next_node(nullptr); np; np = of_next_node(np))
        if (np->phandle == phandle)
            return np;
    return nullptr;
}

struct device_node *of_find_compatible_node(struct device_node *from, const char *compatible)
{
    for (struct device_node *np = of_next_node(from); np; np = of_next_node(np))
        if (of_device_is_compatible(np, compatible))
            return np;
    return nullptr;
}

struct device_node *of_get_child_by_name(const struct device_node *np, const char *name)
{
    struct device_node *child;
    size_t n = strlen(name);
    for_each_child_of_node(np, child)
        if (name_matches(child->name, name, n))
            return child;
    return nullptr;
}

int of_get_full_name(const struct device_node *np, char *buf, size_t size)
{
    const struct device_node *chain[MAX_DEPTH];
    int n = 0;
    for (; np && np->parent && n < MAX_DEPTH; np = np->parent)
        chain[n++] = np;
    size_t pos = 0;
    if (size)
        buf[0] = 0;
    if (!n)
        return (int)ksnprintf(buf, size, "/");
    while (n--) {
        int w = ksnprintf(buf + pos, pos < size ? size - pos : 0, "/%s", chain[n]->name);
        pos += (size_t)w;
    }
    return (int)pos;
}

/* ---- properties ------------------------------------------------------------------------------ */

const void *of_get_property(const struct device_node *np, const char *name, uint32_t *len)
{
    if (!np)
        return nullptr;
    for (struct property *pp = np->properties; pp; pp = pp->next) {
        if (!strcmp(pp->name, name)) {
            if (len)
                *len = pp->length;
            return pp->value;
        }
    }
    return nullptr;
}

int of_property_read_bool(const struct device_node *np, const char *name)
{
    return of_get_property(np, name, nullptr) != nullptr;
}

int of_property_read_u32_index(const struct device_node *np, const char *name, uint32_t index, uint32_t *out)
{
    uint32_t len;
    const uint8_t *v = (const uint8_t *)of_get_property(np, name, &len);
    if (!v)
        return -EINVAL;
    if ((index + 1) * 4 > len)
        return -EOVERFLOW;
    *out = of_be32(v + index * 4);
    return 0;
}

int of_property_read_u32(const struct device_node *np, const char *name, uint32_t *out)
{
    return of_property_read_u32_index(np, name, 0, out);
}

int of_property_read_u32_array(const struct device_node *np, const char *name, uint32_t *out, size_t count)
{
    uint32_t len;
    const uint8_t *v = (const uint8_t *)of_get_property(np, name, &len);
    if (!v)
        return -EINVAL;
    if (count * 4 > len)
        return -EOVERFLOW;
    for (size_t i = 0; i < count; i++)
        out[i] = of_be32(v + i * 4);
    return 0;
}

int of_property_count_u32(const struct device_node *np, const char *name)
{
    uint32_t len;
    if (!of_get_property(np, name, &len))
        return -EINVAL;
    return (int)(len / 4);
}

int of_property_read_string_index(const struct device_node *np, const char *name, int index, const char **out)
{
    uint32_t len;
    const char *v = (const char *)of_get_property(np, name, &len);
    if (!v)
        return -EINVAL;
    const char *end = v + len;
    for (int i = 0; v < end; i++) {
        size_t n = strnlen(v, (size_t)(end - v));
        if (v + n >= end)
            return -EILSEQ;
        if (i == index) {
            *out = v;
            return 0;
        }
        v += n + 1;
    }
    return -ENODATA;
}

int of_property_read_string(const struct device_node *np, const char *name, const char **out)
{
    return of_property_read_string_index(np, name, 0, out);
}

int of_property_count_strings(const struct device_node *np, const char *name)
{
    uint32_t len;
    const char *v = (const char *)of_get_property(np, name, &len);
    if (!v)
        return -EINVAL;
    int n = 0;
    for (const char *end = v + len; v < end; v += strnlen(v, (size_t)(end - v)) + 1)
        n++;
    return n;
}

int of_property_match_string(const struct device_node *np, const char *name, const char *string)
{
    int n = of_property_count_strings(np, name);
    for (int i = 0; i < n; i++) {
        const char *s;
        if (!of_property_read_string_index(np, name, i, &s) && !strcmp(s, string))
            return i;
    }
    return -ENODATA;
}

int of_device_is_compatible(const struct device_node *np, const char *compatible)
{
    return of_property_match_string(np, "compatible", compatible) >= 0;
}

int of_device_is_available(const struct device_node *np)
{
    const char *status;
    if (of_property_read_string(np, "status", &status))
        return 1;
    return !strcmp(status, "okay") || !strcmp(status, "ok");
}

/* ---- addresses and phandle lists ------------------------------------------------------------- */

int of_n_addr_cells(const struct device_node *np)
{
    for (const struct device_node *p = np ? np->parent : nullptr; p; p = p->parent) {
        uint32_t v;
        if (!of_property_read_u32(p, "#address-cells", &v))
            return (int)v;
    }
    return 1;
}

int of_n_size_cells(const struct device_node *np)
{
    for (const struct device_node *p = np ? np->parent : nullptr; p; p = p->parent) {
        uint32_t v;
        if (!of_property_read_u32(p, "#size-cells", &v))
            return (int)v;
    }
    return 1;
}

int of_get_reg(const struct device_node *np, int index, uint32_t *addr, uint32_t *size)
{
    int ac = of_n_addr_cells(np), sc = of_n_size_cells(np);
    if (ac < 1 || ac > 2 || sc < 0 || sc > 2)
        return -EINVAL;
    uint32_t len;
    const uint8_t *v = (const uint8_t *)of_get_property(np, "reg", &len);
    if (!v)
        return -EINVAL;
    uint32_t stride = (uint32_t)(ac + sc) * 4;
    if ((uint32_t)(index + 1) * stride > len)
        return -EOVERFLOW;
    v += (uint32_t)index * stride;
    *addr = of_be32(v + (ac - 1) * 4); /* 32-bit SoC: low cell */
    if (size)
        *size = sc ? of_be32(v + ac * 4 + (sc - 1) * 4) : 0;
    return 0;
}

int of_parse_phandle_with_args(const struct device_node *np, const char *list_name, const char *cells_name,
                               int index, struct of_phandle_args *out)
{
    uint32_t len;
    const uint8_t *v = (const uint8_t *)of_get_property(np, list_name, &len);
    if (!v)
        return -ENOENT;
    uint32_t n = len / 4, i = 0;
    for (int cur = 0; i < n; cur++) {
        uint32_t ph = of_be32(v + i * 4);
        struct device_node *target = of_find_node_by_phandle(ph);
        uint32_t cells = 0;
        if (cells_name) {
            if (!target || of_property_read_u32(target, cells_name, &cells))
                return -EINVAL;
        }
        if (i + 1 + cells > n || cells > OF_MAX_PHANDLE_ARGS)
            return -EINVAL;
        if (cur == index) {
            if (!target)
                return -ENOENT;
            out->np = target;
            out->args_count = (int)cells;
            for (uint32_t k = 0; k < cells; k++)
                out->args[k] = of_be32(v + (i + 1 + k) * 4);
            return 0;
        }
        i += 1 + cells;
    }
    return -ENOENT;
}

struct device_node *of_parse_phandle(const struct device_node *np, const char *name, int index)
{
    struct of_phandle_args a;
    return of_parse_phandle_with_args(np, name, nullptr, index, &a) ? nullptr : a.np;
}

struct device_node *of_irq_parent(const struct device_node *np)
{
    for (const struct device_node *p = np; p; p = p->parent) {
        uint32_t ph;
        if (!of_property_read_u32(p, "interrupt-parent", &ph))
            return of_find_node_by_phandle(ph);
    }
    return nullptr;
}

int of_irq_parse(const struct device_node *np, int index, struct of_phandle_args *out)
{
    /* "interrupts-extended" names the controller per interrupt */
    if (of_get_property(np, "interrupts-extended", nullptr))
        return of_parse_phandle_with_args(np, "interrupts-extended", "#interrupt-cells", index, out);
    struct device_node *parent = of_irq_parent(np);
    uint32_t cells;
    if (!parent || of_property_read_u32(parent, "#interrupt-cells", &cells) || cells > OF_MAX_PHANDLE_ARGS)
        return -EINVAL;
    uint32_t len;
    const uint8_t *v = (const uint8_t *)of_get_property(np, "interrupts", &len);
    if (!v)
        return -ENOENT;
    if ((uint32_t)(index + 1) * cells * 4 > len)
        return -EOVERFLOW;
    out->np = parent;
    out->args_count = (int)cells;
    for (uint32_t k = 0; k < cells; k++)
        out->args[k] = of_be32(v + ((uint32_t)index * cells + k) * 4);
    return 0;
}

int of_irq_count(const struct device_node *np)
{
    struct of_phandle_args a;
    int n = 0;
    while (!of_irq_parse(np, n, &a))
        n++;
    return n;
}

int of_alias_get_id(const struct device_node *np, const char *stem)
{
    if (!s_aliases || !np)
        return -ENODEV;
    size_t sl = strlen(stem);
    for (struct property *pp = s_aliases->properties; pp; pp = pp->next) {
        if (strncmp(pp->name, stem, sl) || pp->name[sl] < '0' || pp->name[sl] > '9')
            continue;
        if (of_find_node_by_path((const char *)pp->value) == np)
            return (int)strtoul(pp->name + sl, nullptr, 10);
    }
    return -ENODEV;
}

struct device_node *of_stdout_node(void)
{
    struct device_node *chosen = of_find_node_by_path("/chosen");
    const char *path;
    if (!chosen || of_property_read_string(chosen, "stdout-path", &path))
        return nullptr;
    return of_find_node_by_path(path);
}
