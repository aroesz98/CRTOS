/*
 * kernel/rtos/swdcon.cpp - the kernel monitor over the debug probe (SWD).
 *
 * The host reads and writes a control block in RAM through the debugger while the core
 * runs: an "up" ring with the monitor's output and a "down" ring with its input. A second
 * kmon instance serves it, so the UART console stays free for the user and the channel
 * does not depend on the probe's USB-serial bridge. tools/swdcon.py finds the block by its
 * magic number among the kernel's initialised data at the start of DTCM (g_swd_chan is
 * there) and also uploads files by writing them straight into RAM (kmon "stage" /
 * "savestage"), much faster than through a serial line.
 *
 * Ordering: the writer fills the data, then (after a barrier) moves its head; the reader
 * reads the head, then the data, then moves its tail. Nobody else touches the other index.
 */
#include "kernel.h"
#include <crtos/vfs.h>
#include <string.h>

#define SWD_MAGIC   0x57535243u     /* "CRSW" */
#define UP_SIZE     4096u
#define DOWN_SIZE   512u

struct swd_chan {
    uint32_t magic;
    uint32_t version;
    uint32_t up_size, down_size;
    volatile uint32_t up_head, up_tail;         /* target writes up_head, the host up_tail */
    volatile uint32_t down_head, down_tail;     /* the host writes down_head, target down_tail */
    uint8_t up[UP_SIZE];
    uint8_t down[DOWN_SIZE];
};

extern "C" {
struct swd_chan g_swd_chan __attribute__((used)) = { SWD_MAGIC, 1, UP_SIZE, DOWN_SIZE, 0, 0, 0, 0, {}, {} };
}

/* Output: waits up to ~0.5 s for room, then drops the rest. After such a wait the host
 * counts as away and output is dropped at once until it reads again: a program logging to
 * the probe's console must not crawl along at one write per half second. */
static bool s_host_away;

static void swd_write(const char *s, size_t n)
{
    struct swd_chan *c = &g_swd_chan;
    int waited = 0;
    while (n) {
        uint32_t head = c->up_head, tail = c->up_tail;
        uint32_t room = UP_SIZE - (head - tail);
        if (!room) {
            if (s_host_away || waited++ > 250) {
                s_host_away = true;
                return;
            }
            task_sleep_ms(2);
            continue;
        }
        s_host_away = false;
        uint32_t k = room < n ? room : (uint32_t)n;
        for (uint32_t i = 0; i < k; i++)
            c->up[(head + i) % UP_SIZE] = (uint8_t)s[i];
        dmb();
        c->up_head = head + k;
        s += k;
        n -= k;
    }
}

static int swd_getc(uint32_t timeout)
{
    struct swd_chan *c = &g_swd_chan;
    uint32_t t0 = tick_get();
    for (;;) {
        uint32_t tail = c->down_tail;
        if (c->down_head != tail) {
            dmb();
            uint8_t ch = c->down[tail % DOWN_SIZE];
            c->down_tail = tail + 1;
            return ch;
        }
        if (timeout != WAIT_FOREVER && tick_get() - t0 >= timeout)
            return -ETIMEDOUT;
        task_sleep_ms(10);
    }
}

#if CONFIG_OS
/* /dev/swdcon: output of programs started from this monitor */
static int swdf_write(struct file *, const void *buf, size_t len)
{
    swd_write((const char *)buf, len);
    return (int)len;
}

static int swdf_read(struct file *, void *, size_t)
{
    return 0; /* no input for programs */
}

static const struct file_ops swdf_ops = {
    nullptr, swdf_read, swdf_write, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
};

/* Console device for programs the calling monitor starts */
const char *kmon_console_path(void)
{
    task_t *t = task_current();
    return t && t->con_write == swd_write ? "/dev/swdcon" : "/dev/console";
}
#endif

static void swdcon_thread(void *arg)
{
    task_t *t = task_current();
    t->con_write = swd_write;
    t->con_getc = swd_getc;
#if CONFIG_OS
    devfs_register("swdcon", &swdf_ops, nullptr);
#endif
    kmon_run(arg);
}

void swdcon_start(void)
{
    kthread_create("kmon-swd", swdcon_thread, nullptr, PRIO_HIGH + 1, 3072);
}
