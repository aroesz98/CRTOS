/*
 * kernel/os/vfs.cpp - virtual file system: mount table, path resolution, file objects, devfs.
 */
#include "kernel.h"
#include <crtos/vfs.h>
#include <string.h>

#define MAX_MOUNTS 8

struct mount {
    char path[32];              /* "/sd" */
    size_t len;
    const struct vfs_fs_ops *ops;
    void *fs;
    uint32_t open_files;
};

static struct mount s_mounts[MAX_MOUNTS];
static struct mutex s_vfs_lock;
static bool s_vfs_ready;

static void vfs_init_once(void)
{
    if (!s_vfs_ready) {
        uint32_t key = irq_lock();
        if (!s_vfs_ready) {
            mutex_init(&s_vfs_lock);
            s_vfs_ready = true;
        }
        irq_unlock(key);
    }
}

/* Make @in canonical: absolute, no "//", "." or ".." components, no trailing "/" */
static int normalize(const char *in, char *out, size_t size)
{
    if (!in || in[0] != '/')
        return -EINVAL;
    size_t o = 0;
    out[o++] = '/';
    const char *p = in;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        const char *s = p;
        while (*p && *p != '/')
            p++;
        size_t n = (size_t)(p - s);
        if (n == 1 && s[0] == '.')
            continue;
        if (n == 2 && s[0] == '.' && s[1] == '.') {
            while (o > 1 && out[o - 1] != '/')
                o--;
            if (o > 1)
                o--;
            continue;
        }
        if (o > 1) {
            if (o + 1 >= size)
                return -ENAMETOOLONG;
            out[o++] = '/';
        }
        if (o + n >= size)
            return -ENAMETOOLONG;
        memcpy(out + o, s, n);
        o += n;
    }
    out[o] = 0;
    return 0;
}

int vfs_normalize(const char *in, char *out, size_t size)
{
    return normalize(in, out, size);
}

/* Find the mount serving @path; *rel receives the path inside it ("/" for the root) */
static struct mount *lookup(const char *path, const char **rel)
{
    struct mount *best = nullptr;
    for (int i = 0; i < MAX_MOUNTS; i++) {
        struct mount *m = &s_mounts[i];
        if (!m->ops)
            continue;
        if (!strncmp(path, m->path, m->len) && (path[m->len] == '/' || path[m->len] == 0))
            if (!best || m->len > best->len)
                best = m;
    }
    if (best)
        *rel = path[best->len] ? path + best->len : "/";
    return best;
}

int vfs_mount(const char *mountpoint, const struct vfs_fs_ops *ops, void *fs)
{
    vfs_init_once();
    char norm[32];
    int r = normalize(mountpoint, norm, sizeof(norm));
    if (r)
        return r;
    mutex_lock(&s_vfs_lock, WAIT_FOREVER);
    struct mount *free_slot = nullptr;
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (s_mounts[i].ops && !strcmp(s_mounts[i].path, norm)) {
            mutex_unlock(&s_vfs_lock);
            return -EBUSY;
        }
        if (!s_mounts[i].ops && !free_slot)
            free_slot = &s_mounts[i];
    }
    if (!free_slot) {
        mutex_unlock(&s_vfs_lock);
        return -ENOSPC;
    }
    strcpy(free_slot->path, norm);
    free_slot->len = strlen(norm);
    free_slot->fs = fs;
    free_slot->open_files = 0;
    free_slot->ops = ops;
    mutex_unlock(&s_vfs_lock);
    return 0;
}

int vfs_umount(const char *mountpoint)
{
    vfs_init_once();
    char norm[32];
    int r = normalize(mountpoint, norm, sizeof(norm));
    if (r)
        return r;
    mutex_lock(&s_vfs_lock, WAIT_FOREVER);
    r = -ENOENT;
    for (int i = 0; i < MAX_MOUNTS; i++) {
        struct mount *m = &s_mounts[i];
        if (m->ops && !strcmp(m->path, norm)) {
            if (m->open_files) {
                r = -EBUSY;
            } else {
                m->ops = nullptr;
                r = 0;
            }
            break;
        }
    }
    mutex_unlock(&s_vfs_lock);
    return r;
}

/* ---- root directory: lists the mount points ------------------------------------------ */

static int root_readdir(struct file *f, struct vfs_dirent *de)
{
    uintptr_t idx = (uintptr_t)f->priv;
    while (idx < MAX_MOUNTS) {
        struct mount *m = &s_mounts[idx++];
        if (m->ops && m->len > 1 && !strchr(m->path + 1, '/')) {
            f->priv = (void *)idx;
            strncpy(de->name, m->path + 1, sizeof(de->name) - 1);
            de->name[sizeof(de->name) - 1] = 0;
            de->mode = VFS_S_IFDIR;
            de->size = 0;
            return 1;
        }
    }
    f->priv = (void *)idx;
    return 0;
}

static const struct file_ops root_dir_ops = {
    nullptr, nullptr, nullptr, nullptr, nullptr, root_readdir, nullptr, nullptr, nullptr,
};

/* ---- file objects ---------------------------------------------------------------------- */

static struct file *file_alloc(uint32_t flags)
{
    struct file *f = (struct file *)kzalloc(sizeof(*f), KM_ANY);
    if (f) {
        f->flags = flags;
        f->refs = 1;
    }
    return f;
}

/* A file object that belongs to no file system (pipes): @ops/@priv are the caller's */
struct file *vfs_file_new(const struct file_ops *ops, void *priv, uint32_t flags)
{
    struct module *owner;
    if (module_get_addr(ops, &owner))
        return nullptr;
    struct file *f = file_alloc(flags);
    if (f) {
        f->ops = ops;
        f->priv = priv;
        f->owner = owner;
    } else {
        module_put(owner);
    }
    return f;
}

static int open_common(const char *path, uint32_t flags, bool dir, struct file **out)
{
    vfs_init_once();
    char norm[VFS_PATH_MAX];
    int r = normalize(path, norm, sizeof(norm));
    if (r)
        return r;
    struct file *f = file_alloc(flags);
    if (!f)
        return -ENOMEM;

    mutex_lock(&s_vfs_lock, WAIT_FOREVER);
    const char *rel = nullptr;
    struct mount *m = lookup(norm, &rel);
    if (m)
        m->open_files++; /* pins the mount while we open without the lock */
    mutex_unlock(&s_vfs_lock);

    if (!m) {
        if (dir && !strcmp(norm, "/")) {
            f->ops = &root_dir_ops;
            *out = f;
            return 0;
        }
        kfree(f);
        return -ENOENT;
    }
    f->mnt = m;
    if (dir)
        r = m->ops->opendir ? m->ops->opendir(m->fs, rel, f) : -ENOTDIR;
    else
        r = m->ops->open ? m->ops->open(m->fs, rel, flags, f) : -ENOSYS;
    if (r) {
        mutex_lock(&s_vfs_lock, WAIT_FOREVER);
        m->open_files--;
        mutex_unlock(&s_vfs_lock);
        kfree(f);
        return r;
    }
    *out = f;
    return 0;
}

int vfs_open(const char *path, uint32_t flags, struct file **out)
{
    return open_common(path, flags, (flags & VFS_O_DIRECTORY) != 0, out);
}

int vfs_opendir(const char *path, struct file **out)
{
    return open_common(path, VFS_O_RDONLY | VFS_O_DIRECTORY, true, out);
}

void vfs_file_get(struct file *f)
{
    uint32_t key = irq_lock();
    f->refs++;
    irq_unlock(key);
}

int vfs_close(struct file *f)
{
    if (!f)
        return -EBADF;
    uint32_t key = irq_lock();
    bool last = --f->refs == 0;
    irq_unlock(key);
    if (!last)
        return 0;
    int r = f->ops && f->ops->close ? f->ops->close(f) : 0;
    module_put((struct module *)f->owner);
    struct mount *m = (struct mount *)f->mnt;
    if (m) {
        mutex_lock(&s_vfs_lock, WAIT_FOREVER);
        m->open_files--;
        mutex_unlock(&s_vfs_lock);
    }
    kfree(f);
    return r;
}

int vfs_read(struct file *f, void *buf, size_t len)
{
    if ((f->flags & VFS_O_ACCMODE) == VFS_O_WRONLY)
        return -EBADF;
    return f->ops->read ? f->ops->read(f, buf, len) : -EINVAL;
}

int vfs_write(struct file *f, const void *buf, size_t len)
{
    if ((f->flags & VFS_O_ACCMODE) == VFS_O_RDONLY)
        return -EBADF;
    return f->ops->write ? f->ops->write(f, buf, len) : -EINVAL;
}

int64_t vfs_lseek(struct file *f, int64_t off, int whence)
{
    return f->ops->lseek ? f->ops->lseek(f, off, whence) : -ESPIPE;
}

int vfs_ioctl(struct file *f, unsigned cmd, void *arg)
{
    return f->ops->ioctl ? f->ops->ioctl(f, cmd, arg) : -ENOTTY;
}

int vfs_readdir(struct file *f, struct vfs_dirent *de)
{
    return f->ops->readdir ? f->ops->readdir(f, de) : -ENOTDIR;
}

int vfs_fstat(struct file *f, struct vfs_stat *st)
{
    if (f->ops->fstat)
        return f->ops->fstat(f, st);
    memset(st, 0, sizeof(*st));
    st->mode = f->ops->readdir ? VFS_S_IFDIR : VFS_S_IFCHR;
    return 0;
}

int vfs_poll(struct file *f, struct poll_entry *e)
{
    return f->ops->poll ? f->ops->poll(f, e) : (POLLIN | POLLOUT);
}

int vfs_sync(struct file *f)
{
    return f->ops->sync ? f->ops->sync(f) : 0;
}

int vfs_xip(struct file *f, uintptr_t *addr, uint32_t *size)
{
    return f->ops->xip ? f->ops->xip(f, addr, size) : -EOPNOTSUPP;
}

/* Path operations without an open file */
typedef int (*path_fn)(struct mount *m, const char *rel, void *arg);

static int with_path(const char *path, path_fn fn, void *arg)
{
    vfs_init_once();
    char norm[VFS_PATH_MAX];
    int r = normalize(path, norm, sizeof(norm));
    if (r)
        return r;
    mutex_lock(&s_vfs_lock, WAIT_FOREVER);
    const char *rel = nullptr;
    struct mount *m = lookup(norm, &rel);
    if (m)
        m->open_files++;
    mutex_unlock(&s_vfs_lock);
    if (!m)
        return !strcmp(norm, "/") ? -EISDIR : -ENOENT;
    r = fn(m, rel, arg);
    mutex_lock(&s_vfs_lock, WAIT_FOREVER);
    m->open_files--;
    mutex_unlock(&s_vfs_lock);
    return r;
}

static int do_stat(struct mount *m, const char *rel, void *arg)
{
    if (!strcmp(rel, "/")) {
        struct vfs_stat *st = (struct vfs_stat *)arg;
        memset(st, 0, sizeof(*st));
        st->mode = VFS_S_IFDIR;
        return 0;
    }
    return m->ops->stat ? m->ops->stat(m->fs, rel, (struct vfs_stat *)arg) : -ENOSYS;
}

int vfs_stat(const char *path, struct vfs_stat *st)
{
    char norm[4];
    if (!normalize(path, norm, sizeof(norm)) && !strcmp(norm, "/")) {
        memset(st, 0, sizeof(*st));
        st->mode = VFS_S_IFDIR;
        return 0;
    }
    return with_path(path, do_stat, st);
}

static int do_mkdir(struct mount *m, const char *rel, void *)
{
    return m->ops->mkdir ? m->ops->mkdir(m->fs, rel) : -EROFS;
}

int vfs_mkdir(const char *path)
{
    return with_path(path, do_mkdir, nullptr);
}

static int do_unlink(struct mount *m, const char *rel, void *)
{
    return m->ops->unlink ? m->ops->unlink(m->fs, rel) : -EROFS;
}

int vfs_unlink(const char *path)
{
    return with_path(path, do_unlink, nullptr);
}

struct statfs_args {
    uint64_t *total, *free;
};

static int do_statfs(struct mount *m, const char *, void *arg)
{
    struct statfs_args *a = (struct statfs_args *)arg;
    return m->ops->statfs ? m->ops->statfs(m->fs, a->total, a->free) : -ENOSYS;
}

int vfs_statfs(const char *path, uint64_t *total, uint64_t *free)
{
    struct statfs_args a = { total, free };
    return with_path(path, do_statfs, &a);
}

static int rename_normalized(const char *nf, const char *nt);

int vfs_rename(const char *from, const char *to)
{
    vfs_init_once();
    char *nf = (char *)kmalloc(2 * VFS_PATH_MAX, KM_ANY);
    if (!nf)
        return -ENOMEM;
    char *nt = nf + VFS_PATH_MAX;
    int r = normalize(from, nf, VFS_PATH_MAX);
    if (!r)
        r = normalize(to, nt, VFS_PATH_MAX);
    if (!r)
        r = rename_normalized(nf, nt);
    kfree(nf);
    return r;
}

static int rename_normalized(const char *nf, const char *nt)
{
    int r;
    mutex_lock(&s_vfs_lock, WAIT_FOREVER);
    const char *rf = nullptr, *rt = nullptr;
    struct mount *mf = lookup(nf, &rf);
    struct mount *mt = lookup(nt, &rt);
    if (mf && mf == mt)
        mf->open_files++;
    mutex_unlock(&s_vfs_lock);
    if (!mf || !mt)
        return -ENOENT;
    if (mf != mt)
        return -EXDEV;
    r = mf->ops->rename ? mf->ops->rename(mf->fs, rf, rt) : -EROFS;
    mutex_lock(&s_vfs_lock, WAIT_FOREVER);
    mf->open_files--;
    mutex_unlock(&s_vfs_lock);
    return r;
}

int vfs_load_file(const char *path, void **data, size_t *size, unsigned kmflags)
{
    struct file *f;
    int r = vfs_open(path, VFS_O_RDONLY, &f);
    if (r)
        return r;
    struct vfs_stat st;
    r = vfs_fstat(f, &st);
    if (r || st.size < 0 || st.size > 16 * 1024 * 1024) {
        vfs_close(f);
        return r ? r : -EFBIG;
    }
    size_t n = (size_t)st.size;
    uint8_t *buf = (uint8_t *)kmalloc_aligned(n ? n : 1, 32, kmflags);
    if (!buf) {
        vfs_close(f);
        return -ENOMEM;
    }
    size_t got = 0;
    while (got < n) {
        r = vfs_read(f, buf + got, n - got);
        if (r <= 0)
            break;
        got += (size_t)r;
    }
    vfs_close(f);
    if (got != n) {
        kfree(buf);
        return r < 0 ? r : -EIO;
    }
    *data = buf;
    *size = n;
    return 0;
}

/* ---- devfs ------------------------------------------------------------------------------- */

#define DEVFS_MAX 48

struct devnode {
    char name[24];
    const struct file_ops *ops;
    void *dev;
};

static struct devnode s_devs[DEVFS_MAX];
static struct mutex s_devfs_lock;
static bool s_devfs_mounted;

static int devfs_open(void *, const char *rel, uint32_t, struct file *f)
{
    const char *name = rel + 1;
    mutex_lock(&s_devfs_lock, WAIT_FOREVER);
    struct devnode *d = nullptr;
    for (int i = 0; i < DEVFS_MAX; i++)
        if (s_devs[i].ops && !strcmp(s_devs[i].name, name))
            d = &s_devs[i];
    if (!d) {
        mutex_unlock(&s_devfs_lock);
        return -ENOENT;
    }
    /* a device of a module: the module stays loaded while the file is open */
    struct module *owner;
    int r = module_get_addr(d->ops, &owner);
    if (!r) {
        f->ops = d->ops;
        f->dev = d->dev;
        f->owner = owner;
    }
    mutex_unlock(&s_devfs_lock);
    if (r)
        return r;
    r = f->ops->open ? f->ops->open(f) : 0;
    if (r) {
        module_put(owner);
        f->owner = nullptr;
    }
    return r;
}

static int devfs_dir_readdir(struct file *f, struct vfs_dirent *de)
{
    uintptr_t idx = (uintptr_t)f->priv;
    mutex_lock(&s_devfs_lock, WAIT_FOREVER);
    while (idx < DEVFS_MAX) {
        struct devnode *d = &s_devs[idx++];
        if (d->ops) {
            strcpy(de->name, d->name);
            de->mode = VFS_S_IFCHR;
            de->size = 0;
            f->priv = (void *)idx;
            mutex_unlock(&s_devfs_lock);
            return 1;
        }
    }
    f->priv = (void *)idx;
    mutex_unlock(&s_devfs_lock);
    return 0;
}

static const struct file_ops devfs_dir_ops = {
    nullptr, nullptr, nullptr, nullptr, nullptr, devfs_dir_readdir, nullptr, nullptr, nullptr,
};

static int devfs_opendir(void *, const char *rel, struct file *f)
{
    if (strcmp(rel, "/"))
        return -ENOTDIR;
    f->ops = &devfs_dir_ops;
    f->priv = (void *)0;
    return 0;
}

static int devfs_stat(void *, const char *rel, struct vfs_stat *st)
{
    mutex_lock(&s_devfs_lock, WAIT_FOREVER);
    int r = -ENOENT;
    for (int i = 0; i < DEVFS_MAX; i++)
        if (s_devs[i].ops && !strcmp(s_devs[i].name, rel + 1))
            r = 0;
    mutex_unlock(&s_devfs_lock);
    if (!r) {
        memset(st, 0, sizeof(*st));
        st->mode = VFS_S_IFCHR;
    }
    return r;
}

static const struct vfs_fs_ops devfs_ops = {
    devfs_open, devfs_opendir, devfs_stat, nullptr, nullptr, nullptr, nullptr,
};

static void devfs_init_once(void)
{
    uint32_t key = irq_lock();
    bool first = !s_devfs_mounted;
    if (first) {
        mutex_init(&s_devfs_lock);
        s_devfs_mounted = true;
    }
    irq_unlock(key);
    if (first)
        vfs_mount("/dev", &devfs_ops, nullptr);
}

int devfs_register(const char *name, const struct file_ops *ops, void *dev)
{
    devfs_init_once();
    if (!name || !ops || strlen(name) >= sizeof(s_devs[0].name) || strchr(name, '/'))
        return -EINVAL;
    mutex_lock(&s_devfs_lock, WAIT_FOREVER);
    struct devnode *slot = nullptr;
    for (int i = 0; i < DEVFS_MAX; i++) {
        if (s_devs[i].ops && !strcmp(s_devs[i].name, name)) {
            mutex_unlock(&s_devfs_lock);
            return -EEXIST;
        }
        if (!s_devs[i].ops && !slot)
            slot = &s_devs[i];
    }
    if (!slot) {
        mutex_unlock(&s_devfs_lock);
        return -ENOSPC;
    }
    strcpy(slot->name, name);
    slot->dev = dev;
    slot->ops = ops;
    mutex_unlock(&s_devfs_lock);
    uevent_emit("add", name);
    return 0;
}

int devfs_unregister(const char *name)
{
    devfs_init_once();
    mutex_lock(&s_devfs_lock, WAIT_FOREVER);
    int r = -ENOENT;
    for (int i = 0; i < DEVFS_MAX; i++) {
        if (s_devs[i].ops && !strcmp(s_devs[i].name, name)) {
            s_devs[i].ops = nullptr;
            r = 0;
        }
    }
    mutex_unlock(&s_devfs_lock);
    if (!r)
        uevent_emit("remove", name);
    return r;
}

void devfs_foreach(void (*fn)(const char *name, void *ctx), void *ctx)
{
    devfs_init_once();
    mutex_lock(&s_devfs_lock, WAIT_FOREVER);
    for (int i = 0; i < DEVFS_MAX; i++)
        if (s_devs[i].ops)
            fn(s_devs[i].name, ctx);
    mutex_unlock(&s_devfs_lock);
}

void vfs_init(void)
{
    vfs_init_once();
    devfs_init_once();
}
