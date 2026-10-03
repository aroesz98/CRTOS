/*
 * crtos/of.h - device tree access (Linux-like "of_" API).
 *
 * The flattened device tree loaded at boot is expanded once into a tree of device_node
 * objects; property values point into the original blob and are big-endian, so use the
 * of_property_read_* helpers to get CPU-order numbers.
 */
#ifndef CRTOS_OF_H
#define CRTOS_OF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct device;

struct property {
    const char *name;
    const void *value;
    uint32_t length;
    struct property *next;
};

struct device_node {
    const char *name;              /* "serial@40184000" */
    uint32_t phandle;
    struct property *properties;
    struct device_node *parent;
    struct device_node *child;
    struct device_node *sibling;
    struct device *dev;            /* device created for this node, if any */
    uint32_t flags;
};

#define OF_MAX_PHANDLE_ARGS 8
struct of_phandle_args {
    struct device_node *np;
    int args_count;
    uint32_t args[OF_MAX_PHANDLE_ARGS];
};

static inline uint32_t of_be32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

int of_init(const void *fdt_blob, size_t size);   /* takes ownership of the blob */
struct device_node *of_root(void);
struct device_node *of_find_node_by_path(const char *path);   /* also accepts aliases */
struct device_node *of_find_node_by_phandle(uint32_t phandle);
struct device_node *of_find_compatible_node(struct device_node *from, const char *compatible);
struct device_node *of_get_child_by_name(const struct device_node *np, const char *name);
struct device_node *of_next_node(struct device_node *np);   /* depth-first walk */
int of_get_full_name(const struct device_node *np, char *buf, size_t size);

const void *of_get_property(const struct device_node *np, const char *name, uint32_t *len);
int of_property_read_bool(const struct device_node *np, const char *name);
int of_property_read_u32(const struct device_node *np, const char *name, uint32_t *out);
int of_property_read_u32_index(const struct device_node *np, const char *name, uint32_t index, uint32_t *out);
int of_property_read_u32_array(const struct device_node *np, const char *name, uint32_t *out, size_t count);
int of_property_count_u32(const struct device_node *np, const char *name);
int of_property_read_string(const struct device_node *np, const char *name, const char **out);
int of_property_read_string_index(const struct device_node *np, const char *name, int index, const char **out);
int of_property_count_strings(const struct device_node *np, const char *name);
int of_property_match_string(const struct device_node *np, const char *name, const char *string);

int of_device_is_compatible(const struct device_node *np, const char *compatible);
int of_device_is_available(const struct device_node *np);    /* status absent or "okay" */

int of_n_addr_cells(const struct device_node *np);   /* of the parent bus */
int of_n_size_cells(const struct device_node *np);
int of_get_reg(const struct device_node *np, int index, uint32_t *addr, uint32_t *size);

struct device_node *of_parse_phandle(const struct device_node *np, const char *name, int index);
int of_parse_phandle_with_args(const struct device_node *np, const char *list_name, const char *cells_name,
                               int index, struct of_phandle_args *out);
struct device_node *of_irq_parent(const struct device_node *np);
int of_irq_parse(const struct device_node *np, int index, struct of_phandle_args *out);
int of_irq_count(const struct device_node *np);

int of_alias_get_id(const struct device_node *np, const char *stem);
struct device_node *of_stdout_node(void);

#define for_each_child_of_node(parent_, child_) \
    for ((child_) = (parent_) ? (parent_)->child : NULL; (child_); (child_) = (child_)->sibling)

#ifdef __cplusplus
}
#endif

#endif
