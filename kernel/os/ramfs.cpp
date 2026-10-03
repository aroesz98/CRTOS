/*
 * kernel/os/ramfs.cpp - file system in RAM (mounted on /ram): scratch space for uploads,
 * tests and temporary files. Files are single heap blocks in SDRAM that grow by doubling.
 * Directories are nodes made by mkdir, but they also exist implicitly (a file "/a/b/c" makes
 * "/a" and "/a/b" appear).
 */
#include "kernel.h"
#include <crtos/rtc.h>
#include <crtos/vfs.h>
#include <string.h>

struct rnode {
    char path[VFS_PATH_MAX];    /* "/drivers/hello.ko" */
    bool dir;
    uint8_t *data;
    uint32_t size, cap;
    uint32_t opens;
    uint32_t mtime;             /* FAT timestamp of the last write */
    struct list_head node;
};

struct rfile {
    struct rnode *n;
    uint32_t pos;
};

static struct list_head s_nodes = LIST_HEAD_INIT(s_nodes);
static struct mutex s_lock;

static struct rnode *find(const char *path)
{
    struct list_head *pos;
    list_for_each(pos, &s_nodes) {
        struct rnode *n = list_entry(pos, struct rnode, node);
        if (!strcmp(n->path, path))
            return n;
    }
    return nullptr;
}

/* Does anything live below @dir ("/a/b")? */
static bool has_children(const char *dir)
{
    size_t l = strlen(dir);
    struct list_head *pos;
    list_for_each(pos, &s_nodes) {
        struct rnode *n = list_entry(pos, struct rnode, node);
        if (!strncmp(n->path, dir, l) && n->path[l] == '/')
            return true;
    }
    return false;
}

/* Is @dir ("/" or "/a/b") a directory: made by mkdir, or a prefix of some file? */
static bool is_dir(const char *dir)
{
    if (!strcmp(dir, "/"))
        return true;
    struct rnode *n = find(dir);
    return n ? n->dir : has_children(dir);
}

static int r_read(struct file *f, void *buf, size_t len)
{
    struct rfile *rf = (struct rfile *)f->priv;
    mutex_lock(&s_lock, WAIT_FOREVER);
    uint32_t n = rf->pos < rf->n->size ? rf->n->size - rf->pos : 0;
    if (n > len)
        n = (uint32_t)len;
    memcpy(buf, rf->n->data + rf->pos, n);
    rf->pos += n;
    mutex_unlock(&s_lock);
    return (int)n;
}

static int r_write(struct file *f, const void *buf, size_t len)
{
    struct rfile *rf = (struct rfile *)f->priv;
    mutex_lock(&s_lock, WAIT_FOREVER);
    struct rnode *n = rf->n;
    if (f->flags & VFS_O_APPEND)
        rf->pos = n->size;
    uint32_t end = rf->pos + (uint32_t)len;
    if (end > n->cap) {
        uint32_t cap = n->cap ? n->cap : 1024;
        while (cap < end)
            cap *= 2;
        uint8_t *d = (uint8_t *)kmalloc_aligned(cap, 32, KM_LARGE);
        if (!d) {
            mutex_unlock(&s_lock);
            return -ENOSPC;
        }
        if (n->data) {
            memcpy(d, n->data, n->size);
            kfree(n->data);
        }
        n->data = d;
        n->cap = cap;
    }
    if (rf->pos > n->size)
        memset(n->data + n->size, 0, rf->pos - n->size);
    memcpy(n->data + rf->pos, buf, len);
    rf->pos = end;
    if (end > n->size)
        n->size = end;
    n->mtime = fat_time_now();
    mutex_unlock(&s_lock);
    return (int)len;
}

static int64_t r_lseek(struct file *f, int64_t off, int whence)
{
    struct rfile *rf = (struct rfile *)f->priv;
    int64_t base = whence == VFS_SEEK_SET ? 0 : whence == VFS_SEEK_CUR ? rf->pos : rf->n->size;
    int64_t p = base + off;
    if (p < 0 || p > 0x7FFFFFFF)
        return -EINVAL;
    rf->pos = (uint32_t)p;
    return p;
}

static int r_fstat(struct file *f, struct vfs_stat *st)
{
    struct rfile *rf = (struct rfile *)f->priv;
    memset(st, 0, sizeof(*st));
    st->mode = VFS_S_IFREG;
    st->size = rf->n->size;
    st->mtime = rf->n->mtime;
    return 0;
}

static int r_close(struct file *f)
{
    struct rfile *rf = (struct rfile *)f->priv;
    mutex_lock(&s_lock, WAIT_FOREVER);
    rf->n->opens--;
    mutex_unlock(&s_lock);
    kfree(rf);
    return 0;
}

static const struct file_ops ramfs_file_ops = {
    nullptr, r_read, r_write, r_lseek, nullptr, nullptr, r_fstat, nullptr, r_close,
};

static int r_open(void *, const char *rel, uint32_t flags, struct file *f)
{
    if (strlen(rel) >= VFS_PATH_MAX || !strcmp(rel, "/"))
        return -EINVAL;
    struct rfile *rf = (struct rfile *)kzalloc(sizeof(*rf), KM_ANY);
    if (!rf)
        return -ENOMEM;
    mutex_lock(&s_lock, WAIT_FOREVER);
    struct rnode *n = find(rel);
    int r = 0;
    if (n && n->dir) {
        r = -EISDIR;
    } else if (!n) {
        if (!(flags & VFS_O_CREAT)) {
            r = is_dir(rel) ? -EISDIR : -ENOENT;
        } else {
            n = (struct rnode *)kzalloc(sizeof(*n), KM_ANY);
            if (!n) {
                r = -ENOMEM;
            } else {
                strcpy(n->path, rel);
                n->mtime = fat_time_now();
                list_add_tail(&n->node, &s_nodes);
            }
        }
    } else if ((flags & VFS_O_CREAT) && (flags & VFS_O_EXCL)) {
        r = -EEXIST;
    } else if (flags & VFS_O_TRUNC) {
        n->size = 0;
        n->mtime = fat_time_now();
    }
    if (!r) {
        n->opens++;
        rf->n = n;
    }
    mutex_unlock(&s_lock);
    if (r) {
        kfree(rf);
        return r;
    }
    f->ops = &ramfs_file_ops;
    f->priv = rf;
    return 0;
}

/* Directory listing: entries are generated from the file list on each call */
struct rdir {
    char dir[VFS_PATH_MAX];
    uint32_t index;
};

static int r_readdir(struct file *f, struct vfs_dirent *de)
{
    struct rdir *d = (struct rdir *)f->priv;
    size_t l = !strcmp(d->dir, "/") ? 0 : strlen(d->dir);
    mutex_lock(&s_lock, WAIT_FOREVER);
    uint32_t i = 0;
    struct list_head *pos;
    list_for_each(pos, &s_nodes) {
        struct rnode *n = list_entry(pos, struct rnode, node);
        if (strncmp(n->path, d->dir, l) || n->path[l] != '/')
            continue;
        const char *name = n->path + l + 1;
        const char *slash = strchr(name, '/');
        size_t nl = slash ? (size_t)(slash - name) : strlen(name);
        /* a subdirectory is reported once: at its node or its first file, whichever
         * comes first in the list */
        bool first = true;
        if (slash || n->dir) {
            struct list_head *q;
            list_for_each(q, &s_nodes) {
                if (q == pos)
                    break;
                struct rnode *o = list_entry(q, struct rnode, node);
                char end = o->path[l + 1 + nl];
                if (!strncmp(o->path, n->path, l + 1 + nl) && (end == '/' || (end == 0 && o->dir)))
                    first = false;
            }
        }
        if (!first)
            continue;
        if (i++ < d->index)
            continue;
        d->index++;
        memcpy(de->name, name, nl);
        de->name[nl] = 0;
        bool dir = slash || n->dir;
        de->mode = dir ? VFS_S_IFDIR : VFS_S_IFREG;
        de->size = dir ? 0 : n->size;
        mutex_unlock(&s_lock);
        return 1;
    }
    mutex_unlock(&s_lock);
    return 0;
}

static int r_closedir(struct file *f)
{
    kfree(f->priv);
    return 0;
}

static const struct file_ops ramfs_dir_ops = {
    nullptr, nullptr, nullptr, nullptr, nullptr, r_readdir, nullptr, nullptr, r_closedir,
};

static int r_opendir(void *, const char *rel, struct file *f)
{
    mutex_lock(&s_lock, WAIT_FOREVER);
    bool ok = is_dir(rel);
    mutex_unlock(&s_lock);
    if (!ok)
        return -ENOENT;
    struct rdir *d = (struct rdir *)kzalloc(sizeof(*d), KM_ANY);
    if (!d)
        return -ENOMEM;
    strncpy(d->dir, rel, sizeof(d->dir) - 1);
    f->ops = &ramfs_dir_ops;
    f->priv = d;
    return 0;
}

static int r_stat(void *, const char *rel, struct vfs_stat *st)
{
    mutex_lock(&s_lock, WAIT_FOREVER);
    struct rnode *n = find(rel);
    bool dir = n ? n->dir : is_dir(rel);
    memset(st, 0, sizeof(*st));
    if (n && !n->dir) {
        st->mode = VFS_S_IFREG;
        st->size = n->size;
        st->mtime = n->mtime;
    } else if (dir) {
        st->mode = VFS_S_IFDIR;
    }
    mutex_unlock(&s_lock);
    return n || dir ? 0 : -ENOENT;
}

static int r_mkdir(void *, const char *rel)
{
    if (strlen(rel) >= VFS_PATH_MAX || !strcmp(rel, "/"))
        return -EEXIST;
    struct rnode *n = (struct rnode *)kzalloc(sizeof(*n), KM_ANY);
    if (!n)
        return -ENOMEM;
    strcpy(n->path, rel);
    n->dir = true;
    mutex_lock(&s_lock, WAIT_FOREVER);
    int r = find(rel) || has_children(rel) ? -EEXIST : 0;
    if (!r)
        list_add_tail(&n->node, &s_nodes);
    mutex_unlock(&s_lock);
    if (r)
        kfree(n);
    return r;
}

static int r_unlink(void *, const char *rel)
{
    mutex_lock(&s_lock, WAIT_FOREVER);
    struct rnode *n = find(rel);
    int r = 0;
    if (has_children(rel))
        r = -ENOTEMPTY;
    else if (!n)
        r = -ENOENT;
    else if (n->opens)
        r = -EBUSY;
    if (!r)
        list_del(&n->node);
    mutex_unlock(&s_lock);
    if (!r) {
        kfree(n->data);
        kfree(n);
    }
    return r;
}

static int r_rename(void *, const char *from, const char *to)
{
    if (strlen(to) >= VFS_PATH_MAX)
        return -ENAMETOOLONG;
    mutex_lock(&s_lock, WAIT_FOREVER);
    struct rnode *n = find(from);
    int r = n ? 0 : -ENOENT;
    if (!r && find(to))
        r = -EEXIST;
    if (!r)
        strcpy(n->path, to);
    mutex_unlock(&s_lock);
    return r;
}

static int r_statfs(void *, uint64_t *total, uint64_t *free)
{
    uint64_t used = 0;
    mutex_lock(&s_lock, WAIT_FOREVER);
    struct list_head *pos;
    list_for_each(pos, &s_nodes)
        used += list_entry(pos, struct rnode, node)->cap;
    mutex_unlock(&s_lock);
    struct mm_pool_info pi;
    mm_pool_info(3, &pi); /* SDRAM pool */
    *free = pi.free;
    *total = pi.free + used;
    return 0;
}

static const struct vfs_fs_ops ramfs_ops = {
    r_open, r_opendir, r_stat, r_mkdir, r_unlink, r_rename, r_statfs,
};

int ramfs_mount(const char *mountpoint)
{
    mutex_init(&s_lock);
    return vfs_mount(mountpoint, &ramfs_ops, nullptr);
}
