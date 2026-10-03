/*
 * kernel/rtos/arch/irq.cpp - interrupt dispatching.
 *
 * The vector table is copied to RAM; every peripheral vector points to irq_dispatch(),
 * which looks up the handler registered with irq_request() by the active IRQ number.
 */
#include "../kernel.h"
#include <crtos/irq.h>
#include "fsl_device_registers.h"
#include <string.h>

extern "C" void (*const g_pfnVectors[])(void);

struct irq_desc {
    irq_handler_t handler;
    void *ctx;
    const char *name;
    uint32_t count;
};

static struct irq_desc s_irq[CONFIG_NUM_IRQS];
static uint32_t *s_vectors;

/* IRQs of secondary interrupt controllers (GPIO): CONFIG_NUM_IRQS + index */
struct virq_desc {
    irq_handler_t handler;
    void *ctx;
    const char *name;
    uint32_t count;
    const struct irq_chip *chip;
    void *chip_ctx;
    uint16_t hwirq;
    uint8_t allocated;
    uint8_t enabled;
};

static struct virq_desc s_virq[CONFIG_NUM_VIRQS];

static inline struct virq_desc *virq(int irq)
{
    int i = irq - CONFIG_NUM_IRQS;
    return (i >= 0 && i < CONFIG_NUM_VIRQS && s_virq[i].allocated) ? &s_virq[i] : nullptr;
}

extern "C" KERNEL_FAST void irq_dispatch(void)
{
    uint32_t irq = __get_IPSR() - 16u;
    struct irq_desc *d = &s_irq[irq];
    d->count++;
    d->handler((int)irq, d->ctx);
}

extern "C" KERNEL_FAST void SysTick_Handler(void)
{
    sched_tick();
}

static void irq_unhandled(int irq, void *)
{
    NVIC_DisableIRQ((IRQn_Type)irq);
    printk("W: unexpected IRQ %d - disabled\n", irq);
}

void irq_init(void)
{
    const uint32_t n = 16 + CONFIG_NUM_IRQS;
    /* VTOR needs the table aligned to its size rounded up to a power of two */
    s_vectors = (uint32_t *)kmalloc_aligned(n * 4, 1024, KM_FAST);
    if (!s_vectors)
        panic("no memory for the vector table");
    for (uint32_t i = 0; i < 16; i++)
        s_vectors[i] = (uint32_t)g_pfnVectors[i];
    for (uint32_t i = 16; i < n; i++)
        s_vectors[i] = (uint32_t)irq_dispatch;

    for (int i = 0; i < CONFIG_NUM_IRQS; i++) {
        s_irq[i].handler = irq_unhandled;
        s_irq[i].ctx = nullptr;
        s_irq[i].name = nullptr;
        s_irq[i].count = 0;
    }
    for (uint32_t i = 0; i < (CONFIG_NUM_IRQS + 31) / 32; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }
    for (int i = 0; i < CONFIG_NUM_IRQS; i++)
        NVIC_SetPriority((IRQn_Type)i, CONFIG_IRQ_DEFAULT_PRIO);

    uint32_t key = irq_lock();
    SCB->VTOR = (uint32_t)s_vectors;
    __DSB();
    __ISB();
    irq_unlock(key);
}

static inline bool irq_valid(int irq)
{
    return irq >= 0 && irq < CONFIG_NUM_IRQS;
}

int irq_request(int irq, irq_handler_t handler, void *ctx, int prio, const char *name)
{
    struct virq_desc *v = virq(irq);
    if (v && handler) {
        uint32_t key = irq_lock();
        if (v->handler) {
            irq_unlock(key);
            return -EBUSY;
        }
        v->handler = handler;
        v->ctx = ctx;
        v->name = name;
        v->count = 0;
        v->enabled = 1;
        irq_unlock(key);
        v->chip->unmask(v->chip_ctx, v->hwirq);
        return 0;
    }
    if (!irq_valid(irq) || !handler)
        return -EINVAL;
    if (prio < 0)
        prio = CONFIG_IRQ_DEFAULT_PRIO;
    if (prio < CONFIG_IRQ_KERNEL_PRIO || prio > 15)
        return -EINVAL;
    uint32_t key = irq_lock();
    struct irq_desc *d = &s_irq[irq];
    if (d->handler != irq_unhandled) {
        irq_unlock(key);
        return -EBUSY;
    }
    NVIC_DisableIRQ((IRQn_Type)irq);
    d->ctx = ctx;
    d->name = name;
    d->count = 0;
    d->handler = handler;
    NVIC_SetPriority((IRQn_Type)irq, (uint32_t)prio);
    NVIC_EnableIRQ((IRQn_Type)irq);
    irq_unlock(key);
    return 0;
}

void irq_free(int irq)
{
    struct virq_desc *v = virq(irq);
    if (v) {
        v->chip->mask(v->chip_ctx, v->hwirq);
        uint32_t key = irq_lock();
        v->handler = nullptr;
        v->enabled = 0;
        irq_unlock(key);
        return;
    }
    if (!irq_valid(irq))
        return;
    uint32_t key = irq_lock();
    NVIC_DisableIRQ((IRQn_Type)irq);
    s_irq[irq].handler = irq_unhandled;
    s_irq[irq].ctx = nullptr;
    s_irq[irq].name = nullptr;
    irq_unlock(key);
}

void irq_enable(int irq)
{
    struct virq_desc *v = virq(irq);
    if (v) {
        v->enabled = 1;
        v->chip->unmask(v->chip_ctx, v->hwirq);
    } else if (irq_valid(irq)) {
        NVIC_EnableIRQ((IRQn_Type)irq);
    }
}

void irq_disable(int irq)
{
    struct virq_desc *v = virq(irq);
    if (v) {
        v->chip->mask(v->chip_ctx, v->hwirq);
        v->enabled = 0;
    } else if (irq_valid(irq)) {
        NVIC_DisableIRQ((IRQn_Type)irq);
        __DSB();
        __ISB();
    }
}

void irq_set_priority(int irq, int prio)
{
    if (irq_valid(irq) && prio >= CONFIG_IRQ_KERNEL_PRIO && prio <= 15)
        NVIC_SetPriority((IRQn_Type)irq, (uint32_t)prio);
}

void irq_pend(int irq)
{
    if (irq_valid(irq))
        NVIC_SetPendingIRQ((IRQn_Type)irq);
}

int irq_info(int irq, struct irq_info *info)
{
    struct virq_desc *v = virq(irq);
    if (v) {
        if (!v->handler)
            return -ENOENT;
        info->name = v->name ? v->name : "?";
        info->count = v->count;
        info->prio = -1;
        info->enabled = v->enabled;
        return 0;
    }
    if (!irq_valid(irq))
        return -EINVAL;
    struct irq_desc *d = &s_irq[irq];
    if (d->handler == irq_unhandled)
        return -ENOENT;
    info->name = d->name ? d->name : "?";
    info->count = d->count;
    info->prio = (int)NVIC_GetPriority((IRQn_Type)irq);
    info->enabled = (int)NVIC_GetEnableIRQ((IRQn_Type)irq);
    return 0;
}

/* ---- secondary interrupt controllers ---------------------------------------------------- */

int irq_alloc_descs(unsigned count, const struct irq_chip *chip, void *ctx)
{
    if (!count || !chip || count > CONFIG_NUM_VIRQS)
        return -EINVAL;
    uint32_t key = irq_lock();
    for (unsigned base = 0; base + count <= CONFIG_NUM_VIRQS; base++) {
        unsigned n = 0;
        while (n < count && !s_virq[base + n].allocated)
            n++;
        if (n == count) {
            for (unsigned i = 0; i < count; i++) {
                struct virq_desc *v = &s_virq[base + i];
                memset(v, 0, sizeof(*v));
                v->chip = chip;
                v->chip_ctx = ctx;
                v->hwirq = (uint16_t)i;
                v->allocated = 1;
            }
            irq_unlock(key);
            return CONFIG_NUM_IRQS + (int)base;
        }
        base += n;
    }
    irq_unlock(key);
    return -ENOSPC;
}

void irq_free_descs(int base, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        struct virq_desc *v = virq(base + (int)i);
        if (!v)
            continue;
        if (v->handler)
            v->chip->mask(v->chip_ctx, v->hwirq);
        uint32_t key = irq_lock();
        v->allocated = 0;
        v->handler = nullptr;
        irq_unlock(key);
    }
}

int irq_set_type(int irq, unsigned type)
{
    struct virq_desc *v = virq(irq);
    if (!v)
        return -EINVAL;
    return v->chip->set_type ? v->chip->set_type(v->chip_ctx, v->hwirq, type) : -ENOTSUP;
}

void irq_handle_nested(int irq)
{
    struct virq_desc *v = virq(irq);
    if (!v)
        return;
    v->count++;
    if (v->handler)
        v->handler(irq, v->ctx);
    else
        v->chip->mask(v->chip_ctx, v->hwirq);
}
