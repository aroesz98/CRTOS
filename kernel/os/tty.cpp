/*
 * kernel/os/tty.cpp - the console terminal (/dev/console) and /dev/null, /dev/zero.
 *
 * Output goes into the kernel log ring like printk (the console driver sends it); writers
 * wait for room instead of overwriting unsent text. Input comes from the console when user
 * space has the focus (see crtos/tty.h). In canonical mode lines are edited here: echo,
 * backspace, Ctrl-U, arrow-key sequences are ignored; Ctrl-C ends the foreground process
 * (from the UART interrupt, so it works while the process does not read) or cancels the
 * line being typed.
 */
#define CRTOS_KERNEL 1
#include "kernel.h"
#include <crtos/syscall.h>
#include <crtos/tty.h>
#include <crtos/vfs.h>
#include <string.h>

#define LINE_MAX 256

struct tty {
    struct mutex rlock;             /* one reader at a time */
    volatile uint32_t mode;
    volatile int fg_pid;            /* Ctrl-C ends this process */
    char line[LINE_MAX];
    uint32_t len, pos;              /* characters typed, characters already read */
    bool ready;                     /* the line is complete */
    bool eof;
    uint8_t esc;                    /* inside an escape sequence */
    uint8_t prev;
    struct poll_head ph;
};

static struct tty s_tty;

static void echo(const char *s, size_t n)
{
    if (s_tty.mode & TTY_MODE_ECHO)
        log_write(s, n);
}

static void kill_fg_work(void *arg)
{
    struct proc *p = proc_get_by_pid((int)(intptr_t)arg);
    if (p) {
        proc_kill(p, -EINTR);
        proc_put(p);
    }
}

/* Console interrupt, user space has the focus: consume Ctrl-C when it ends a process */
bool tty_rx_filter(uint8_t c)
{
    if (c != 3 || !(s_tty.mode & TTY_MODE_CANON) || !s_tty.fg_pid)
        return false;
    log_write("^C\n", 3);
    kworker_queue(kill_fg_work, (void *)(intptr_t)s_tty.fg_pid);
    return true;
}

void tty_rx_notify(void)
{
    poll_notify(&s_tty.ph);
}

/* Canonical input: returns 1 when the line is complete, -EINTR on Ctrl-C */
static int canon_input(uint8_t c)
{
    struct tty *t = &s_tty;
    if (t->esc) { /* ESC [ ... final byte */
        if (t->esc == 1 && c == '[') {
            t->esc = 2;
            return 0;
        }
        if (t->esc == 1 || (c >= 0x40 && c <= 0x7E))
            t->esc = 0;
        return 0;
    }
    uint8_t prev = t->prev;
    t->prev = c;
    switch (c) {
    case '\n':
        if (prev == '\r') /* CR LF: the CR ended the line already */
            return 0;
        /* fall through */
    case '\r':
        t->line[t->len++] = '\n';
        echo("\n", 1);
        return 1;
    case 8:
    case 127:
        if (t->len) {
            t->len--;
            echo("\b \b", 3);
        }
        return 0;
    case 21: /* Ctrl-U */
        while (t->len) {
            t->len--;
            echo("\b \b", 3);
        }
        return 0;
    case 3: /* Ctrl-C without a foreground process */
        echo("^C\n", 3);
        t->len = 0;
        return -EINTR;
    case 4: /* Ctrl-D */
        if (!t->len)
            t->eof = true;
        return 1;
    case 27:
        t->esc = 1;
        return 0;
    default:
        if (c < 32)
            return 0;
        if (t->len < LINE_MAX - 1) {
            t->line[t->len++] = (char)c;
            char e = (char)c;
            echo(&e, 1);
        }
        return 0;
    }
}

static int tty_read(struct file *f, void *buf, size_t len)
{
    struct tty *t = &s_tty;
    if (!len)
        return 0;
    uint32_t timeout = (f->flags & VFS_O_NONBLOCK) ? NO_WAIT : WAIT_FOREVER;
    int r = mutex_lock(&t->rlock, WAIT_FOREVER);
    if (r)
        return r;
    if (!(t->mode & TTY_MODE_CANON)) {
        r = console_read_as(CON_TTY, buf, len, timeout, 0);
        if (r > 0)
            echo((const char *)buf, (size_t)r);
        mutex_unlock(&t->rlock);
        return r == -ETIMEDOUT ? -EAGAIN : r;
    }
    while (!t->ready) {
        uint8_t c;
        r = console_read_as(CON_TTY, &c, 1, timeout, 0);
        if (r < 0) {
            mutex_unlock(&t->rlock);
            return r == -ETIMEDOUT ? -EAGAIN : r;
        }
        r = canon_input(c);
        if (r < 0) {
            mutex_unlock(&t->rlock);
            return r;
        }
        if (r)
            t->ready = true;
    }
    size_t n = 0;
    if (t->eof) {
        t->eof = false;
    } else {
        n = t->len - t->pos;
        if (n > len)
            n = len;
        memcpy(buf, t->line + t->pos, n);
        t->pos += n;
        if (t->pos < t->len) {
            mutex_unlock(&t->rlock);
            return (int)n;
        }
    }
    t->ready = false;
    t->len = t->pos = 0;
    mutex_unlock(&t->rlock);
    return (int)n;
}

static int tty_write(struct file *, const void *buf, size_t len)
{
    log_write_wait((const char *)buf, len);
    return (int)len;
}

static int tty_ioctl(struct file *, unsigned cmd, void *arg)
{
    struct proc *p = g_current->proc;
    switch (cmd) {
    case TTY_IOC_GET_MODE:
        *(uint32_t *)arg = s_tty.mode;
        return 0;
    case TTY_IOC_SET_MODE:
        s_tty.mode = (uint32_t)(uintptr_t)arg & (TTY_MODE_CANON | TTY_MODE_ECHO);
        return 0;
    case TTY_IOC_SET_FG:
        s_tty.fg_pid = (int)(intptr_t)arg;
        return 0;
    case TTY_IOC_GET_FG:
        *(int32_t *)arg = s_tty.fg_pid;
        return 0;
    case TTY_IOC_FOCUS:
        if (p && !(p->caps & CAP_SYS))
            return -EPERM;
        console_set_focus(CON_TTY);
        return 0;
    case TTY_IOC_GET_SIZE: {
        struct tty_size *sz = (struct tty_size *)arg;
        sz->cols = 80;
        sz->rows = 24;
        return 0;
    }
    default:
        return -ENOTTY;
    }
}

static int tty_poll(struct file *, struct poll_entry *e)
{
    uint32_t key = irq_lock();
    int mask = POLLOUT;
    if (s_tty.ready || (console_focus() == CON_TTY && console_rx_pending()))
        mask |= POLLIN;
    poll_add(&s_tty.ph, e);
    irq_unlock(key);
    return mask;
}

static int tty_fstat(struct file *, struct vfs_stat *st)
{
    memset(st, 0, sizeof(*st));
    st->mode = VFS_S_IFCHR;
    return 0;
}

static const struct file_ops tty_ops = {
    nullptr, tty_read, tty_write, nullptr, tty_ioctl, nullptr, tty_fstat, nullptr, nullptr, tty_poll,
};

/* ---- /dev/null and /dev/zero ------------------------------------------------------------------- */

static int null_read(struct file *, void *, size_t)
{
    return 0;
}

static int zero_read(struct file *, void *buf, size_t len)
{
    memset(buf, 0, len);
    return (int)len;
}

static int sink_write(struct file *, const void *, size_t len)
{
    return (int)len;
}

static const struct file_ops null_ops = {
    nullptr, null_read, sink_write, nullptr, nullptr, nullptr, tty_fstat, nullptr, nullptr, nullptr,
};
static const struct file_ops zero_ops = {
    nullptr, zero_read, sink_write, nullptr, nullptr, nullptr, tty_fstat, nullptr, nullptr, nullptr,
};

void tty_init(void)
{
    mutex_init(&s_tty.rlock);
    poll_head_init(&s_tty.ph);
    s_tty.mode = TTY_MODE_CANON | TTY_MODE_ECHO;
    devfs_register("console", &tty_ops, &s_tty);
    devfs_register("null", &null_ops, nullptr);
    devfs_register("zero", &zero_ops, nullptr);
}

struct file *tty_open_console(void)
{
    struct file *f = nullptr;
    return vfs_open("/dev/console", VFS_O_RDWR, &f) ? nullptr : f;
}
