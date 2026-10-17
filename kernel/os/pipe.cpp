/*
 * kernel/os/pipe.cpp - pipes (SYS_PIPE): a byte stream from one handle to another.
 *
 * read() blocks until there is data and returns 0 once the writing end is closed and the
 * pipe is empty; write() blocks until there is room and fails with -EPIPE when nobody reads
 * any more. A pipe made with PIPE_TTY also answers the terminal ioctls (isatty, mode, the
 * foreground process for Ctrl-C), so a shell and the programs it starts behave as on a
 * terminal: the graphical terminal runs the shell over two such pipes.
 *
 * Several processes may write one end (a shell and its children share stdout): writers are
 * serialised, readers too; the ring indices change under irq_lock(), the data is copied
 * outside of it.
 */
#define CRTOS_KERNEL 1
#include "kernel.h"
#include <crtos/syscall.h>
#include <crtos/tty.h>
#include <crtos/vfs.h>
#include <string.h>

#define PIPE_SIZE 4096u

struct pipe {
    uint8_t buf[PIPE_SIZE];
    uint32_t head, tail;        /* bytes written, bytes read */
    int readers, writers;       /* open ends */
    bool tty;
    uint32_t mode;
    int fg_pid;
    struct tty_size size;
    struct wait_queue rq, wq;   /* waiting for data / for room */
    struct poll_head ph;
    struct mutex rlock, wlock;
};

static void pipe_release(struct pipe *p, bool reader)
{
    uint32_t key = irq_lock();
    if (reader)
        p->readers--;
    else
        p->writers--;
    bool last = !p->readers && !p->writers;
    wq_wake_all(&p->rq, 0);
    wq_wake_all(&p->wq, 0);
    poll_notify(&p->ph);
    irq_unlock(key);
    if (last)
        kfree(p);
}

static int pipe_read(struct file *f, void *buf, size_t len)
{
    struct pipe *p = (struct pipe *)f->priv;
    if (!len)
        return 0;
    int r = mutex_lock(&p->rlock, WAIT_FOREVER);
    if (r)
        return r;
    uint32_t avail;
    for (;;) {
        uint32_t key = irq_lock();
        avail = p->head - p->tail;
        if (avail) {
            irq_unlock(key);
            break;
        }
        if (!p->writers || (f->flags & VFS_O_NONBLOCK)) {
            irq_unlock(key);
            mutex_unlock(&p->rlock);
            return p->writers ? -EAGAIN : 0;
        }
        r = sched_block(&p->rq, WAIT_FOREVER, key);
        if (r == -EINTR) {
            mutex_unlock(&p->rlock);
            return r;
        }
    }
    uint32_t n = avail < len ? avail : (uint32_t)len;
    uint32_t off = p->tail % PIPE_SIZE, first = PIPE_SIZE - off < n ? PIPE_SIZE - off : n;
    memcpy(buf, p->buf + off, first);
    memcpy((uint8_t *)buf + first, p->buf, n - first);
    uint32_t key = irq_lock();
    p->tail += n;
    wq_wake_all(&p->wq, 0);
    poll_notify(&p->ph);
    irq_unlock(key);
    mutex_unlock(&p->rlock);
    return (int)n;
}

static int pipe_write(struct file *f, const void *buf, size_t len)
{
    struct pipe *p = (struct pipe *)f->priv;
    int r = mutex_lock(&p->wlock, WAIT_FOREVER);
    if (r)
        return r;
    size_t done = 0;
    while (done < len) {
        uint32_t key = irq_lock();
        if (!p->readers) {
            irq_unlock(key);
            mutex_unlock(&p->wlock);
            return done ? (int)done : -EPIPE;
        }
        uint32_t room = PIPE_SIZE - (p->head - p->tail);
        if (!room) {
            if (f->flags & VFS_O_NONBLOCK) {
                irq_unlock(key);
                mutex_unlock(&p->wlock);
                return done ? (int)done : -EAGAIN;
            }
            r = sched_block(&p->wq, WAIT_FOREVER, key);
            if (r == -EINTR) {
                mutex_unlock(&p->wlock);
                return done ? (int)done : r;
            }
            continue;
        }
        irq_unlock(key);
        uint32_t n = room < len - done ? room : (uint32_t)(len - done);
        uint32_t off = p->head % PIPE_SIZE, first = PIPE_SIZE - off < n ? PIPE_SIZE - off : n;
        memcpy(p->buf + off, (const uint8_t *)buf + done, first);
        memcpy(p->buf, (const uint8_t *)buf + done + first, n - first);
        key = irq_lock();
        p->head += n;
        wq_wake_all(&p->rq, 0);
        poll_notify(&p->ph);
        irq_unlock(key);
        done += n;
    }
    mutex_unlock(&p->wlock);
    return (int)done;
}

static int pipe_ioctl(struct file *f, unsigned cmd, void *arg)
{
    struct pipe *p = (struct pipe *)f->priv;
    if (!p->tty)
        return -ENOTTY;
    switch (cmd) {
    case TTY_IOC_GET_MODE:
        *(uint32_t *)arg = p->mode;
        return 0;
    case TTY_IOC_SET_MODE:
        p->mode = (uint32_t)(uintptr_t)arg & (TTY_MODE_CANON | TTY_MODE_ECHO);
        poll_notify(&p->ph);
        return 0;
    case TTY_IOC_SET_FG:
        p->fg_pid = (int)(intptr_t)arg;
        return 0;
    case TTY_IOC_GET_FG:
        *(int32_t *)arg = p->fg_pid;
        return 0;
    case TTY_IOC_FOCUS:
        return 0;
    case TTY_IOC_GET_SIZE:
        *(struct tty_size *)arg = p->size;
        return 0;
    case TTY_IOC_SET_SIZE:
        p->size = *(const struct tty_size *)arg;
        return 0;
    default:
        return -ENOTTY;
    }
}

static int pipe_poll(struct file *f, struct poll_entry *e)
{
    struct pipe *p = (struct pipe *)f->priv;
    bool reader = (f->flags & VFS_O_ACCMODE) == VFS_O_RDONLY;
    uint32_t key = irq_lock();
    int mask = 0;
    if (reader)
        mask = (p->head != p->tail ? POLLIN : 0) | (!p->writers ? POLLHUP : 0);
    else
        mask = (p->head - p->tail < PIPE_SIZE ? POLLOUT : 0) | (!p->readers ? POLLHUP | POLLERR : 0);
    poll_add(&p->ph, e);
    irq_unlock(key);
    return mask;
}

static int pipe_fstat(struct file *f, struct vfs_stat *st)
{
    struct pipe *p = (struct pipe *)f->priv;
    memset(st, 0, sizeof(*st));
    st->mode = p->tty ? VFS_S_IFCHR : VFS_S_IFIFO;
    st->size = p->head - p->tail;
    return 0;
}

static int pipe_close(struct file *f)
{
    pipe_release((struct pipe *)f->priv, (f->flags & VFS_O_ACCMODE) == VFS_O_RDONLY);
    return 0;
}

static const struct file_ops pipe_ops = {
    nullptr, pipe_read, pipe_write, nullptr, pipe_ioctl, nullptr, pipe_fstat, nullptr, pipe_close, pipe_poll,
};

int64_t sys_pipe(int32_t *ufds, uint32_t flags)
{
    struct proc *proc = g_current->proc;
    if (!uaccess_ok(ufds, 2 * sizeof(int32_t), 1) && !vmem_contains(proc, ufds, 2 * sizeof(int32_t)))
        return -EFAULT;
    struct pipe *p = (struct pipe *)kzalloc(sizeof(*p), KM_LARGE);
    if (!p)
        return -ENOMEM;
    wq_init(&p->rq);
    wq_init(&p->wq);
    poll_head_init(&p->ph);
    mutex_init(&p->rlock);
    mutex_init(&p->wlock);
    p->tty = flags & PIPE_TTY;
    p->mode = TTY_MODE_CANON | TTY_MODE_ECHO;
    p->size.cols = 80;
    p->size.rows = 24;
    /* each end holds a share of the pipe: closing the last one frees it */
    struct file *rf = vfs_file_new(&pipe_ops, p, VFS_O_RDONLY);
    if (!rf) {
        kfree(p);
        return -ENOMEM;
    }
    p->readers = 1;
    struct file *wf = vfs_file_new(&pipe_ops, p, VFS_O_WRONLY);
    if (!wf) {
        vfs_close(rf);
        return -ENOMEM;
    }
    p->writers = 1;
    int hr = handle_install(proc, H_FILE, 0, rf, 0);
    if (hr < 0) {
        vfs_close(rf);
        vfs_close(wf);
        return hr;
    }
    int hw = handle_install(proc, H_FILE, 0, wf, 0);
    if (hw < 0) {
        handle_close(proc, hr);
        vfs_close(wf);
        return hw;
    }
    int32_t fds[2] = { hr, hw };
    if (copy_to_user(ufds, fds, sizeof(fds))) {
        handle_close(proc, hr);
        handle_close(proc, hw);
        return -EFAULT;
    }
    return 0;
}
