/*
 * subsys/fb.cpp - frame buffer registry and /dev/fbN.
 *
 * read()/write()/lseek() access buffer 0 (the default visible buffer). The graphics server
 * gets the buffers as shared memory handles for the 2D accelerator (FB_IOC_GET_SHM), flips
 * them with FB_IOC_SHOW and paces itself with poll(): POLLIN once a new frame started.
 *
 * fb_suggest_size() changes the mode of a display that nobody has open and whose buffers no
 * program holds as shared memory (the driver replaces them): at boot, when a touch driver
 * learns which panel is fitted, before the graphics server starts - or from kmon ("fb WxH")
 * once that is gone. Opens wait for no change: one that comes during it fails with -EAGAIN.
 */
#include "kernel.h"
#include <crtos/fb.h>
#include <crtos/vfs.h>
#include <string.h>

#define MAX_FB 2

struct fb {
    struct fb_info info;
    const struct fb_ops *ops;
    void *ctx;
    char devname[8];
    bool used;
    volatile uint32_t frames;           /* frames started */
    volatile uint32_t shown;            /* buffer on the screen */
    uint32_t opens;                     /* open files of it */
    bool resizing;                      /* fb_suggest_size() is changing its mode */
    struct poll_head ph;
    struct shm *shm[FB_MAX_BUFFERS];    /* buffers handed out as shared memory */
};

static struct fb s_fb[MAX_FB];

static struct fb *fb_by_index(int index)
{
    return (index >= 0 && index < MAX_FB && s_fb[index].used) ? &s_fb[index] : nullptr;
}

struct fb_file {
    struct fb *fb;
    uint32_t pos;
    uint32_t seen;                      /* frame counter at the last FB_IOC_GET_FRAME */
};

static int fbf_open(struct file *f)
{
    struct fb *fb = (struct fb *)f->dev;
    struct fb_file *ff = (struct fb_file *)kzalloc(sizeof(*ff), KM_ANY);
    if (!ff)
        return -ENOMEM;
    uint32_t key = irq_lock();
    bool resizing = fb->resizing;
    if (!resizing)
        fb->opens++;
    irq_unlock(key);
    if (resizing) {
        kfree(ff);
        return -EAGAIN;
    }
    ff->fb = fb;
    ff->seen = ff->fb->frames;
    f->priv = ff;
    return 0;
}

/* The buffer as a shared memory object (made on first use), with a new reference */
static struct shm *buffer_shm(struct fb *fb, unsigned index)
{
    uint32_t key = irq_lock();
    struct shm *s = fb->shm[index];
    if (s)
        shm_get(s);
    irq_unlock(key);
    if (s)
        return s;
    s = shm_wrap((void *)fb->info.buffer[index], fb->info.buffer_size, true);
    if (!s)
        return nullptr;
    key = irq_lock();
    if (fb->shm[index]) { /* somebody was faster */
        struct shm *other = fb->shm[index];
        shm_get(other);
        irq_unlock(key);
        shm_put(s);
        return other;
    }
    fb->shm[index] = s; /* the frame buffer keeps this reference */
    shm_get(s);
    irq_unlock(key);
    return s;
}

static int fbf_read(struct file *f, void *buf, size_t len)
{
    struct fb_file *ff = (struct fb_file *)f->priv;
    uint32_t size = ff->fb->info.buffer_size;
    uint32_t n = ff->pos < size ? size - ff->pos : 0;
    if (n > len)
        n = (uint32_t)len;
    memcpy(buf, (const uint8_t *)ff->fb->info.buffer[0] + ff->pos, n);
    ff->pos += n;
    return (int)n;
}

static int fbf_write(struct file *f, const void *buf, size_t len)
{
    struct fb_file *ff = (struct fb_file *)f->priv;
    uint32_t size = ff->fb->info.buffer_size;
    uint32_t n = ff->pos < size ? size - ff->pos : 0;
    if (n > len)
        n = (uint32_t)len;
    if (!n && len)
        return -ENOSPC;
    memcpy((uint8_t *)ff->fb->info.buffer[0] + ff->pos, buf, n);
    ff->pos += n;
    return (int)n;
}

static int64_t fbf_lseek(struct file *f, int64_t off, int whence)
{
    struct fb_file *ff = (struct fb_file *)f->priv;
    int64_t base = whence == VFS_SEEK_SET ? 0 : whence == VFS_SEEK_CUR ? ff->pos : ff->fb->info.buffer_size;
    int64_t p = base + off;
    if (p < 0 || p > ff->fb->info.buffer_size)
        return -EINVAL;
    ff->pos = (uint32_t)p;
    return p;
}

static int fbf_ioctl(struct file *f, unsigned cmd, void *arg)
{
    struct fb *fb = ((struct fb_file *)f->priv)->fb;
    switch (cmd) {
    case FB_IOC_GET_INFO:
        memcpy(arg, &fb->info, sizeof(fb->info));
        return 0;
    case FB_IOC_SHOW:
        return fb->ops->show ? fb->ops->show(fb->ctx, (unsigned)(uintptr_t)arg) : -ENOTSUP;
    case FB_IOC_WAIT_VSYNC:
        return fb->ops->wait_vsync ? fb->ops->wait_vsync(fb->ctx, 100) : -ENOTSUP;
    case FB_IOC_BLANK:
        return fb->ops->blank ? fb->ops->blank(fb->ctx, (int)(uintptr_t)arg) : -ENOTSUP;
    case FB_IOC_GET_FRAME: {
        struct fb_file *ff = (struct fb_file *)f->priv;
        ff->seen = fb->frames;
        *(uint32_t *)arg = ff->seen;
        return 0;
    }
    case FB_IOC_GET_SHOWN:
        *(uint32_t *)arg = fb->shown;
        return 0;
    case FB_IOC_GET_SHM: {
        struct fb_shm_req *q = (struct fb_shm_req *)arg;
        struct proc *p = g_current->proc;
        if (!p)
            return -EPERM;
        if (q->index >= fb->info.nbuffers)
            return -EINVAL;
        struct shm *s = buffer_shm(fb, q->index);
        if (!s)
            return -ENOMEM;
        int h = handle_install(p, H_SHM, 0, s, 0);
        if (h < 0) {
            shm_put(s);
            return h;
        }
        q->handle = h;
        return 0;
    }
    default:
        return -ENOTTY;
    }
}

static int fbf_poll(struct file *f, struct poll_entry *e)
{
    struct fb_file *ff = (struct fb_file *)f->priv;
    uint32_t key = irq_lock();
    int mask = POLLOUT | (ff->fb->frames != ff->seen ? POLLIN : 0);
    poll_add(&ff->fb->ph, e);
    irq_unlock(key);
    return mask;
}

static int fbf_close(struct file *f)
{
    struct fb_file *ff = (struct fb_file *)f->priv;
    uint32_t key = irq_lock();
    ff->fb->opens--;
    irq_unlock(key);
    kfree(ff);
    return 0;
}

static const struct file_ops fb_file_ops = {
    fbf_open, fbf_read, fbf_write, fbf_lseek, fbf_ioctl, nullptr, nullptr, nullptr, fbf_close, fbf_poll,
};

int fb_register(const struct fb_info *info, const struct fb_ops *ops, void *ctx)
{
    uint32_t key = irq_lock();
    int idx = -1;
    for (int i = 0; i < MAX_FB && idx < 0; i++)
        if (!s_fb[i].used)
            idx = i;
    if (idx >= 0)
        s_fb[idx].used = true;
    irq_unlock(key);
    if (idx < 0)
        return -ENOSPC;
    struct fb *fb = &s_fb[idx];
    fb->info = *info;
    fb->ops = ops;
    fb->ctx = ctx;
    fb->frames = 0;
    fb->shown = 0;
    fb->opens = 0;
    fb->resizing = false;
    poll_head_init(&fb->ph);
    memset(fb->shm, 0, sizeof(fb->shm));
    ksnprintf(fb->devname, sizeof(fb->devname), "fb%d", idx);
    int r = devfs_register(fb->devname, &fb_file_ops, fb);
    if (r) {
        fb->used = false;
        return r;
    }
    printk("fb%d: %ux%u %lu bpp, %lu buffer(s) at %08lx\n", idx, info->width, info->height, (unsigned long)info->bpp,
           (unsigned long)info->nbuffers, (unsigned long)info->buffer[0]);
    return idx;
}

void fb_unregister(int index)
{
    struct fb *fb = fb_by_index(index);
    if (!fb)
        return;
    devfs_unregister(fb->devname);
    /* the driver frees the buffers: whoever still holds a handle must not reach them */
    for (int i = 0; i < FB_MAX_BUFFERS; i++) {
        uint32_t key = irq_lock();
        struct shm *s = fb->shm[i];
        fb->shm[i] = nullptr;
        irq_unlock(key);
        if (s) {
            shm_revoke(s);
            shm_put(s);
        }
    }
    fb->used = false;
}

void fb_vsync(int index, unsigned shown)
{
    if (index < 0 || index >= MAX_FB || !s_fb[index].used)
        return;
    struct fb *fb = &s_fb[index];
    fb->shown = shown;
    fb->frames++;
    poll_notify(&fb->ph);
}

int fb_get_info(int index, struct fb_info *info)
{
    struct fb *fb = fb_by_index(index);
    if (!fb)
        return -ENODEV;
    *info = fb->info;
    return 0;
}

int fb_show(int index, unsigned buffer)
{
    struct fb *fb = fb_by_index(index);
    if (!fb)
        return -ENODEV;
    if (buffer >= fb->info.nbuffers)
        return -EINVAL;
    return fb->ops->show ? fb->ops->show(fb->ctx, buffer) : -ENOTSUP;
}

int fb_wait_vsync(int index, uint32_t timeout)
{
    struct fb *fb = fb_by_index(index);
    if (!fb)
        return -ENODEV;
    return fb->ops->wait_vsync ? fb->ops->wait_vsync(fb->ctx, timeout) : -ENOTSUP;
}

int fb_blank(int index, int blank)
{
    struct fb *fb = fb_by_index(index);
    if (!fb)
        return -ENODEV;
    return fb->ops->blank ? fb->ops->blank(fb->ctx, blank) : -ENOTSUP;
}

int fb_suggest_size(unsigned width, unsigned height)
{
    struct fb *fb = fb_by_index(0);
    if (!fb)
        return -ENODEV;
    if (fb->info.width == width && fb->info.height == height)
        return 0;
    if (!fb->ops->set_size)
        return -ENOENT;
    struct shm *drop[FB_MAX_BUFFERS] = {};
    uint32_t key = irq_lock();
    bool busy = fb->opens || fb->resizing;
    for (int i = 0; i < FB_MAX_BUFFERS && !busy; i++)
        busy = fb->shm[i] && !shm_unshared(fb->shm[i]);
    if (!busy) {
        fb->resizing = true;
        for (int i = 0; i < FB_MAX_BUFFERS; i++) { /* (handed out once, held by nobody now) */
            drop[i] = fb->shm[i];
            fb->shm[i] = nullptr;
        }
    }
    irq_unlock(key);
    if (busy) {
        printk("fb0: stays %ux%u, %ux%u asked for but the display is in use\n", fb->info.width, fb->info.height,
               width, height);
        return -EBUSY;
    }
    for (int i = 0; i < FB_MAX_BUFFERS; i++) {
        if (drop[i]) {
            shm_revoke(drop[i]);
            shm_put(drop[i]);
        }
    }
    struct fb_info info;
    int r = fb->ops->set_size(fb->ctx, width, height, &info);
    key = irq_lock();
    if (!r)
        fb->info = info;
    fb->resizing = false;
    irq_unlock(key);
    if (r)
        printk("fb0: stays %ux%u, no %ux%u mode (%d)\n", fb->info.width, fb->info.height, width, height, r);
    else
        printk("fb0: now %ux%u %lu bpp, %lu buffer(s) at %08lx\n", info.width, info.height, (unsigned long)info.bpp,
               (unsigned long)info.nbuffers, (unsigned long)info.buffer[0]);
    return r;
}
