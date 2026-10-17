/*
 * crtos/irq.h - interrupt management.
 *
 * All peripheral vectors of the (RAM) vector table go through the kernel dispatcher, which
 * calls the handler registered with irq_request(). Numbers are NVIC IRQ numbers; secondary
 * interrupt controllers (GPIO) allocate virtual numbers above CONFIG_NUM_IRQS.
 * Priority: 0 = highest; values below CONFIG_IRQ_KERNEL_PRIO are rejected because those
 * interrupts could not use the kernel API.
 */
#ifndef CRTOS_IRQ_H
#define CRTOS_IRQ_H

#include <stdint.h>
#include <crtos/arch.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*irq_handler_t)(int irq, void *ctx);

int irq_request(int irq, irq_handler_t handler, void *ctx, int prio, const char *name);
void irq_free(int irq);
void irq_enable(int irq);
void irq_disable(int irq);
void irq_set_priority(int irq, int prio);
void irq_pend(int irq);

struct irq_info {
    const char *name;
    uint32_t count;
    int prio;
    int enabled;
};
int irq_info(int irq, struct irq_info *info);

/*
 * Secondary interrupt controllers (GPIO ports) get a range of IRQ numbers above the NVIC
 * lines. Their cascade handler calls irq_handle_nested() for each pending line; the chip
 * callbacks mask/unmask lines and set the trigger type (IRQ_TYPE_* as in device trees).
 */
#define IRQ_TYPE_EDGE_RISING  1u
#define IRQ_TYPE_EDGE_FALLING 2u
#define IRQ_TYPE_EDGE_BOTH    3u
#define IRQ_TYPE_LEVEL_HIGH   4u
#define IRQ_TYPE_LEVEL_LOW    8u

struct irq_chip {
    const char *name;
    void (*mask)(void *ctx, unsigned hwirq);
    void (*unmask)(void *ctx, unsigned hwirq);
    int (*set_type)(void *ctx, unsigned hwirq, unsigned type);
};

int irq_alloc_descs(unsigned count, const struct irq_chip *chip, void *ctx);   /* first IRQ or -errno */
void irq_free_descs(int base, unsigned count);
int irq_set_type(int irq, unsigned type);
void irq_handle_nested(int irq);

#ifdef __cplusplus
}
#endif

#endif
