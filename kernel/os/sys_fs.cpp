/*
 * kernel/os/sys_fs.cpp - file system calls.
 *
 * Paths are resolved against the process' current directory and normalised before any
 * check, so "/sd/../dev/fb0" is treated as "/dev/fb0". Without CAP_DEV a process may only
 * open the console, /dev/null, /dev/zero, the random number devices and the sound output
 * (/dev/audio) among the devices. Pointers are checked against the caller's memory; ioctl
 * arguments by the size encoded in the command.
 */
#define CRTOS_KERNEL 1
#include "kernel.h"
#include <crtos/syscall.h>
#include <crtos/ioctl.h>
#include <crtos/vfs.h>
#include <string.h>

/* Normalised absolute kernel copy of a user path (kfree it). The path may be up to
 * VFS_RAW_PATH_MAX long as given, the normalised one up to VFS_PATH_MAX. */
static int user_path(const char *upath, char **out)
{
    struct proc *p = g_current->proc;
    char *raw = (char *)kmalloc(VFS_RAW_PATH_MAX + VFS_PATH_MAX, KM_ANY);
    if (!raw)
        return -ENOMEM;
    char *norm = raw + VFS_RAW_PATH_MAX;
    size_t pre = 0;
    char first = 0;
    int r = copy_from_user(&first, upath, 1);
    if (!r && first != '/' && p) {
        pre = strlen(p->cwd);
        memcpy(raw, p->cwd, pre);
        if (pre > 1)
            raw[pre++] = '/';
    }
    if (!r) {
        r = strncpy_from_user(raw + pre, upath, VFS_RAW_PATH_MAX - pre);
        if (r == 0)
            r = -ENOENT;
    }
    if (r >= 0)
        r = vfs_normalize(raw, norm, VFS_PATH_MAX);
    if (r < 0) {
        kfree(raw);
        return r;
    }
    memmove(raw, norm, strlen(norm) + 1);
    *out = raw;
    return 0;
}

static bool dev_allowed(const char *path)
{
    struct proc *p = g_current->proc;
    if (!p || (p->caps & CAP_DEV) || strncmp(path, "/dev/", 5))
        return true;
    return !strcmp(path, "/dev/console") || !strcmp(path, "/dev/null") || !strcmp(path, "/dev/zero") ||
           !strcmp(path, "/dev/random") || !strcmp(path, "/dev/urandom") || !strcmp(path, "/dev/audio");
}

static struct file *file_of(int fd)
{
    uint8_t type = H_FILE;
    return (struct file *)handle_ref(g_current->proc, fd, &type, nullptr);
}

int64_t sys_open(const char *upath, uint32_t flags)
{
    char *path;
    int r = user_path(upath, &path);
    if (r)
        return r;
    if (!dev_allowed(path)) {
        kfree(path);
        return -EACCES;
    }
    struct file *f;
    r = vfs_open(path, flags, &f);
    kfree(path);
    if (r)
        return r;
    r = handle_install(g_current->proc, H_FILE, 0, f, 0);
    if (r < 0)
        vfs_close(f);
    return r;
}

int64_t sys_close(int h)
{
    return handle_close(g_current->proc, h);
}

/* read or write with the buffer in the program's emulated memory (vmem.cpp): through a kernel
 * buffer, a page at a time, as long as the file takes or gives whole pieces */
static int rw_vmem(struct file *f, uint8_t *ubuf, uint32_t len, bool rd)
{
    uint8_t *k = (uint8_t *)kmalloc(EMU_PAGE, KM_LARGE);
    if (!k)
        return -ENOMEM;
    int done = 0;
    while ((uint32_t)done < len) {
        uint32_t n = len - (uint32_t)done < EMU_PAGE ? len - (uint32_t)done : EMU_PAGE;
        int r;
        if (rd) {
            r = vfs_read(f, k, n);
            if (r > 0) {
                int e = vmem_copy_out(ubuf + done, k, (size_t)r);
                if (e)
                    r = e;
            }
        } else {
            r = vmem_copy_in(k, ubuf + done, n);
            if (!r)
                r = vfs_write(f, k, n);
        }
        if (r < 0) {
            if (!done)
                done = r;
            break;
        }
        done += r;
        if ((uint32_t)r < n)
            break;
    }
    kfree(k);
    return done;
}

int64_t sys_read(int fd, void *buf, uint32_t len)
{
    struct file *f = file_of(fd);
    if (!f)
        return -EBADF;
    int r;
    if (uaccess_ok(buf, len, 1))
        r = vfs_read(f, buf, len);
    else if (vmem_contains(g_current->proc, buf, len))
        r = rw_vmem(f, (uint8_t *)buf, len, true);
    else
        r = -EFAULT;
    vfs_close(f);
    return r;
}

int64_t sys_write(int fd, const void *buf, uint32_t len)
{
    struct file *f = file_of(fd);
    if (!f)
        return -EBADF;
    int r;
    if (uaccess_ok(buf, len, 0))
        r = vfs_write(f, buf, len);
    else if (vmem_contains(g_current->proc, buf, len))
        r = rw_vmem(f, (uint8_t *)buf, len, false);
    else
        r = -EFAULT;
    vfs_close(f);
    return r;
}

int64_t sys_lseek(int fd, uint32_t lo, uint32_t hi, int whence)
{
    struct file *f = file_of(fd);
    if (!f)
        return -EBADF;
    int64_t r = vfs_lseek(f, (int64_t)(((uint64_t)hi << 32) | lo), whence);
    vfs_close(f);
    return r;
}

int64_t sys_ioctl(int fd, uint32_t cmd, uintptr_t arg)
{
    uint32_t size = _IOC_SIZE(cmd);
    if (size && !uaccess_ok((const void *)arg, size, _IOC_DIR(cmd) & _IOC_READ))
        return -EFAULT;
    struct file *f = file_of(fd);
    if (!f)
        return -EBADF;
    int r;
    if (cmd == FIO_GETFL) {
        *(uint32_t *)arg = f->flags;
        r = 0;
    } else if (cmd == FIO_SETFL) {
        const uint32_t changeable = VFS_O_NONBLOCK | VFS_O_APPEND;
        f->flags = (f->flags & ~changeable) | ((uint32_t)arg & changeable);
        r = 0;
    } else {
        r = vfs_ioctl(f, cmd, (void *)arg);
    }
    vfs_close(f);
    return r;
}

static int put_stat(struct crtos_stat *ust, const struct vfs_stat *st)
{
    struct crtos_stat s;
    s.mode = st->mode;
    s.mtime = st->mtime;
    s.size = st->size;
    return copy_to_user(ust, &s, sizeof(s));
}

int64_t sys_fstat(int fd, struct crtos_stat *ust)
{
    struct file *f = file_of(fd);
    if (!f)
        return -EBADF;
    struct vfs_stat st;
    int r = vfs_fstat(f, &st);
    vfs_close(f);
    return r ? r : put_stat(ust, &st);
}

int64_t sys_stat(const char *upath, struct crtos_stat *ust)
{
    char *path;
    int r = user_path(upath, &path);
    if (r)
        return r;
    struct vfs_stat st;
    r = vfs_stat(path, &st);
    kfree(path);
    return r ? r : put_stat(ust, &st);
}

int64_t sys_statfs(const char *upath, struct crtos_statfs *usf)
{
    char *path;
    int r = user_path(upath, &path);
    if (r)
        return r;
    struct crtos_statfs sf;
    r = vfs_statfs(path, &sf.total, &sf.free);
    kfree(path);
    return r ? r : copy_to_user(usf, &sf, sizeof(sf));
}

int64_t sys_readdir(int fd, struct crtos_dirent *ude)
{
    if (!uaccess_ok(ude, sizeof(*ude), 1) && !vmem_contains(g_current->proc, ude, sizeof(*ude)))
        return -EFAULT;
    struct file *f = file_of(fd);
    if (!f)
        return -EBADF;
    struct vfs_dirent *de = (struct vfs_dirent *)kmalloc(sizeof(*de), KM_FAST);
    struct crtos_dirent *kde = (struct crtos_dirent *)kmalloc(sizeof(*kde), KM_ANY);
    int r = de && kde ? vfs_readdir(f, de) : -ENOMEM;
    vfs_close(f);
    if (r == 1) {
        kde->mode = de->mode;
        kde->reserved = 0;
        kde->size = de->size;
        memcpy(kde->name, de->name, sizeof(kde->name));
        kde->name[sizeof(kde->name) - 1] = 0;
        int e = copy_to_user(ude, kde, sizeof(*kde));
        if (e)
            r = e;
    }
    kfree(de);
    kfree(kde);
    return r;
}

static int64_t path_op(const char *upath, int (*op)(const char *))
{
    char *path;
    int r = user_path(upath, &path);
    if (r)
        return r;
    r = op(path);
    kfree(path);
    return r;
}

int64_t sys_mkdir(const char *upath)
{
    return path_op(upath, vfs_mkdir);
}

int64_t sys_unlink(const char *upath)
{
    return path_op(upath, vfs_unlink);
}

int64_t sys_rename(const char *ufrom, const char *uto)
{
    char *from, *to;
    int r = user_path(ufrom, &from);
    if (r)
        return r;
    r = user_path(uto, &to);
    if (r) {
        kfree(from);
        return r;
    }
    r = vfs_rename(from, to);
    kfree(from);
    kfree(to);
    return r;
}

int64_t sys_chdir(const char *upath)
{
    char *path;
    int r = user_path(upath, &path);
    if (r)
        return r;
    struct vfs_stat st;
    r = vfs_stat(path, &st);
    if (!r && (st.mode & VFS_S_IFMT) != VFS_S_IFDIR)
        r = -ENOTDIR;
    if (!r) {
        struct proc *p = g_current->proc;
        uint32_t key = irq_lock();
        strncpy(p->cwd, path, sizeof(p->cwd) - 1);
        irq_unlock(key);
    }
    kfree(path);
    return r;
}

int64_t sys_getcwd(char *ubuf, uint32_t size)
{
    struct proc *p = g_current->proc;
    size_t n = strlen(p->cwd);
    if (n + 1 > size)
        return -ERANGE;
    if (copy_to_user(ubuf, p->cwd, n + 1))
        return -EFAULT;
    return (int64_t)n;
}

int64_t sys_dup(int h)
{
    struct proc *p = g_current->proc;
    uint8_t type = H_FREE, rights = 0;
    void *obj = handle_ref(p, h, &type, &rights);
    if (!obj)
        return -EBADF;
    int r = handle_install(p, type, rights & (uint8_t)~HR_RECV, obj, 0);
    if (r < 0)
        obj_put(type, obj);
    return r;
}

int64_t sys_dup2(int h, int newh)
{
    struct proc *p = g_current->proc;
    if (newh < 0 || newh >= CONFIG_MAX_HANDLES)
        return -EBADF;
    uint8_t type = H_FREE, rights = 0;
    void *obj = handle_ref(p, h, &type, &rights);
    if (!obj)
        return -EBADF;
    if (h == newh) {
        obj_put(type, obj);
        return newh;
    }
    return handle_install_at(p, newh, type, rights & (uint8_t)~HR_RECV, obj);
}

int64_t sys_fsync(int fd)
{
    struct file *f = file_of(fd);
    if (!f)
        return -EBADF;
    int r = vfs_sync(f);
    vfs_close(f);
    return r;
}
