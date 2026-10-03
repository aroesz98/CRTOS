/*
 * kernel/rtos/printk.cpp - formatted output and the kernel log ring.
 *
 * printk() formats into a small stack buffer and appends it to a ring buffer; the console
 * driver sends the ring from its TX interrupt. Nothing here waits for the UART, so printk
 * is cheap and safe in interrupts. When the console falls behind by more than the ring
 * size, the oldest text is overwritten (and counted as lost).
 */
#include "kernel.h"
#include "fsl_device_registers.h"
#include "../lib/kformat.h"
#include <string.h>

/* ---- log ring ------------------------------------------------------------------------- */

#define LOG_MASK (CONFIG_LOG_BUF_SIZE - 1u)
static_assert((CONFIG_LOG_BUF_SIZE & LOG_MASK) == 0, "log size must be a power of two");

static char s_log[CONFIG_LOG_BUF_SIZE] __attribute__((section(".bss.$SRAM_OC"), aligned(32)));
static volatile uint32_t s_log_head;    /* total bytes ever written */
static volatile uint32_t s_con_tail;    /* next byte for the console */
static volatile uint32_t s_log_lost;    /* bytes the console never got */
static volatile bool s_line_start = true;
static struct wait_queue s_log_wq = { LIST_HEAD_INIT(s_log_wq.waiters) }; /* writers waiting for room */

void log_write(const char *s, size_t len)
{
    if (!len)
        return;
    uint32_t key = irq_lock();
    uint32_t head = s_log_head;
    if (len > CONFIG_LOG_BUF_SIZE) { /* keep only the tail */
        s += len - CONFIG_LOG_BUF_SIZE;
        head += (uint32_t)(len - CONFIG_LOG_BUF_SIZE);
        len = CONFIG_LOG_BUF_SIZE;
    }
    uint32_t off = head & LOG_MASK;
    uint32_t first = CONFIG_LOG_BUF_SIZE - off;
    if (first > len)
        first = (uint32_t)len;
    memcpy(&s_log[off], s, first);
    if (len > first)
        memcpy(&s_log[0], s + first, len - first);
    head += (uint32_t)len;
    s_log_head = head;
    if (head - s_con_tail > CONFIG_LOG_BUF_SIZE) {
        s_log_lost += head - s_con_tail - CONFIG_LOG_BUF_SIZE;
        s_con_tail = head - CONFIG_LOG_BUF_SIZE;
    }
    irq_unlock(key);
    console_kick();
}

size_t log_console_read_locked(char *dst, size_t max)
{
    uint32_t tail = s_con_tail;
    uint32_t avail = s_log_head - tail;
    if (avail > max)
        avail = (uint32_t)max;
    for (uint32_t i = 0; i < avail; i++)
        dst[i] = s_log[(tail + i) & LOG_MASK];
    s_con_tail = tail + avail;
    if (avail && !list_empty(&s_log_wq.waiters))
        wq_wake_all(&s_log_wq, 0);
    return avail;
}

/* log_write() for user output: waits until the console has room instead of overwriting
 * text that was not sent yet (at most ~100 ms per chunk, in case the UART is stuck) */
void log_write_wait(const char *s, size_t len)
{
    while (len) {
        size_t chunk = len > 1024u ? 1024u : len;
        for (int tries = 0; tries < 100; tries++) {
            uint32_t key = irq_lock();
            uint32_t pending = s_log_head - s_con_tail;
            if (CONFIG_LOG_BUF_SIZE - pending >= chunk || in_interrupt() || key != 0) {
                irq_unlock(key);
                break;
            }
            int r = sched_block(&s_log_wq, 1, key);
            if (r == -EINTR)
                return;
        }
        log_write(s, chunk);
        s += chunk;
        len -= chunk;
    }
}

/* Copy of the last @max bytes of the log (for dmesg); returns the length */
size_t log_snapshot(char *dst, size_t max)
{
    uint32_t key = irq_lock();
    uint32_t head = s_log_head;
    uint32_t n = head < CONFIG_LOG_BUF_SIZE ? head : CONFIG_LOG_BUF_SIZE;
    if (n > max)
        n = (uint32_t)max;
    for (uint32_t i = 0; i < n; i++)
        dst[i] = s_log[(head - n + i) & LOG_MASK];
    irq_unlock(key);
    return n;
}

uint32_t log_lost_bytes(void)
{
    return s_log_lost;
}

/* Push everything still queued to the console synchronously (panic path, IRQs off) */
void log_panic_flush(void)
{
    char buf[32];
    size_t n;
    while ((n = log_console_read_locked(buf, sizeof(buf))) != 0)
        console_panic_write(buf, n);
}

/* ---- panic record ----------------------------------------------------------------------- */

/* The panic report is also kept in DTCM that start-up does not clear (TCM: no cache to lose
 * it in): the reboot that follows prints it, so a panic nobody watched is not lost. */
#define PANIC_MAGIC 0x43494E50u /* "PNIC" */

struct panic_record {
    uint32_t magic;
    uint32_t len;
    uint32_t sum;
    char text[1012];
};
static struct panic_record s_panic_rec __attribute__((section(".noinit")));

static uint32_t panic_sum(const struct panic_record *r)
{
    uint32_t s = r->len;
    for (uint32_t i = 0; i < r->len && i < sizeof(r->text); i++)
        s = s * 31u + (uint8_t)r->text[i];
    return s;
}

/* Panic output: synchronously to the console, and into the record */
void log_panic_write(const char *s, size_t len)
{
    console_panic_write(s, len);
    struct panic_record *r = &s_panic_rec;
    if (r->magic != PANIC_MAGIC) {
        r->magic = PANIC_MAGIC;
        r->len = 0;
    }
    size_t room = sizeof(r->text) - r->len;
    if (len > room)
        len = room;
    memcpy(r->text + r->len, s, len);
    r->len += (uint32_t)len;
    r->sum = panic_sum(r);
}

/* At boot: print the report of a panic that ended the previous run, then forget it */
void log_panic_previous(void)
{
    struct panic_record *r = &s_panic_rec;
    if (r->magic == PANIC_MAGIC && r->len <= sizeof(r->text) && r->sum == panic_sum(r)) {
        printk("*** the previous run ended with a panic:");
        log_write(r->text, r->len);
        printk("*** (end of the panic report)\n");
    }
    r->magic = 0;
    r->len = 0;
}

/* ---- printk ---------------------------------------------------------------------------- */

struct pk_ctx {
    char buf[120];
    size_t n;
    char last;
};

static void pk_emit(char c, void *vctx)
{
    struct pk_ctx *p = (struct pk_ctx *)vctx;
    p->last = c;
    p->buf[p->n++] = c;
    if (p->n == sizeof(p->buf)) {
        log_write(p->buf, p->n);
        p->n = 0;
    }
}

static int pk_timestamp(struct pk_ctx *ctx)
{
    uint64_t us = time_us();
    uint32_t sec = (uint32_t)(us / 1000000u);
    uint32_t frac = (uint32_t)(us - (uint64_t)sec * 1000000u);
    return ksnprintf(ctx->buf, sizeof(ctx->buf), "[%5lu.%06lu] ", (unsigned long)sec, (unsigned long)frac);
}

int vprintk(const char *fmt, va_list ap)
{
    struct pk_ctx ctx;
    ctx.n = 0;
    ctx.last = 0;
    if (s_line_start)
        ctx.n = (size_t)pk_timestamp(&ctx);
    int n = kformat(pk_emit, &ctx, fmt, ap);
    if (ctx.n)
        log_write(ctx.buf, ctx.n);
    if (n)
        s_line_start = (ctx.last == '\n');
    return n;
}

int printk(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vprintk(fmt, ap);
    va_end(ap);
    return n;
}

/* Console output of the calling thread: the log (UART) or its own console */
void con_write(const char *s, size_t n)
{
    task_t *t = g_current;
    if (t && t->con_write)
        t->con_write(s, n);
    else
        log_write(s, n);
}

int con_getc(uint32_t timeout)
{
    task_t *t = g_current;
    return t && t->con_getc ? t->con_getc(timeout) : console_getc(timeout);
}

static void cp_emit(char c, void *vctx)
{
    struct pk_ctx *p = (struct pk_ctx *)vctx;
    p->buf[p->n++] = c;
    if (p->n == sizeof(p->buf)) {
        con_write(p->buf, p->n);
        p->n = 0;
    }
}

int cprintf(const char *fmt, ...)
{
    struct pk_ctx ctx;
    ctx.n = 0;
    ctx.last = 0;
    va_list ap;
    va_start(ap, fmt);
    int n = kformat(cp_emit, &ctx, fmt, ap);
    va_end(ap);
    if (ctx.n)
        con_write(ctx.buf, ctx.n);
    return n;
}

/* ---- panic ------------------------------------------------------------------------------ */

static volatile int s_panicking;

void panic(const char *fmt, ...)
{
    __disable_irq();
    if (s_panicking++) {
        for (;;) {
        }
    }
    log_panic_flush();

    char buf[192];
    task_t *t = g_current;
    int n = ksnprintf(buf, sizeof(buf), "\n\n*** KERNEL PANIC (task %d '%s'): ", t ? t->id : -1, t ? t->name : "-");
    if (n > (int)sizeof(buf) - 1)
        n = sizeof(buf) - 1;
    va_list ap;
    va_start(ap, fmt);
    int m = kvsnprintf(buf + n, sizeof(buf) - (size_t)n, fmt, ap);
    va_end(ap);
    n += m;
    if (n > (int)sizeof(buf) - 1)
        n = sizeof(buf) - 1;
    log_panic_write(buf, (size_t)n);
    log_panic_write("\n", 1);
    static const char tail[] = "*** system halted - rebooting in 10 s ('r' now, 'h' to stay halted)\n";
    console_panic_write(tail, sizeof(tail) - 1);
    /* no interrupts: count processor cycles */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint64_t waited = 0, limit = (uint64_t)SystemCoreClock * 10u;
    uint32_t last = cpu_cycles();
    bool hold = false;
    for (;;) {
        int c = console_panic_getc();
        if (c == 'r' || c == 'R')
            NVIC_SystemReset();
        if (c == 'h' || c == 'H') {
            hold = true;
            static const char held[] = "*** staying halted - 'r' reboots\n";
            console_panic_write(held, sizeof(held) - 1);
        }
        uint32_t now = cpu_cycles();
        waited += now - last;
        last = now;
        if (!hold && waited >= limit)
            NVIC_SystemReset();
    }
}
