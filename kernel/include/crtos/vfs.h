/*
 * crtos/vfs.h - virtual file system.
 *
 * Paths are absolute ("/sd/crtos/boot/board.dtb"); file systems are mounted on directories
 * of the root ("/sd", "/dev"). Every open file is a struct file with an operations table;
 * device drivers publish files under /dev with devfs_register(). All calls return 0 / a
 * byte count, or -Exxx.
 */
#ifndef CRTOS_VFS_H
#define CRTOS_VFS_H

#include <stddef.h>
#include <stdint.h>
#include <crtos/poll.h>

#ifdef __cplusplus
extern "C" {
#endif

/* open() flags - same values as the C library used by applications (newlib) */
#define VFS_O_RDONLY    0x0000u
#define VFS_O_WRONLY    0x0001u
#define VFS_O_RDWR      0x0002u
#define VFS_O_ACCMODE   0x0003u
#define VFS_O_APPEND    0x0008u
#define VFS_O_CREAT     0x0200u
#define VFS_O_TRUNC     0x0400u
#define VFS_O_EXCL      0x0800u
#define VFS_O_NONBLOCK  0x4000u
#define VFS_O_DIRECTORY 0x200000u

#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

#define VFS_S_IFMT   0170000u
#define VFS_S_IFDIR  0040000u
#define VFS_S_IFCHR  0020000u
#define VFS_S_IFIFO  0010000u
#define VFS_S_IFREG  0100000u
#define VFS_S_IFSOCK 0140000u

#define VFS_NAME_MAX 255
#define VFS_PATH_MAX 128   /* kernel paths live on stacks guarded by 256 B */
/* A path a program passes may be longer before it is normalised: compilers join directories
 * with "..", e.g. "/flash0/gcc/bin/../lib/gcc/arm-none-eabi/14.3.1/../../../../include".
 * Only the normalised result must fit VFS_PATH_MAX. */
#define VFS_RAW_PATH_MAX 1024

struct vfs_stat {
    uint32_t mode;      /* VFS_S_IF* */
    uint32_t mtime;     /* FAT date/time (0 if unknown) */
    int64_t size;
};

struct vfs_dirent {
    uint32_t mode;
    int64_t size;
    char name[VFS_NAME_MAX + 1];
};

struct file;

struct file_ops {
    int (*open)(struct file *f);                              /* devices: per-open setup */
    int (*read)(struct file *f, void *buf, size_t len);
    int (*write)(struct file *f, const void *buf, size_t len);
    int64_t (*lseek)(struct file *f, int64_t off, int whence);
    int (*ioctl)(struct file *f, unsigned cmd, void *arg);
    int (*readdir)(struct file *f, struct vfs_dirent *de);    /* 1 entry, 0 end, <0 error */
    int (*fstat)(struct file *f, struct vfs_stat *st);
    int (*sync)(struct file *f);
    int (*close)(struct file *f);                             /* last reference dropped */
    /* POLL* state; with @e != NULL also poll_add(e) to the object's poll_head. NULL:
     * always readable and writable. */
    int (*poll)(struct file *f, struct poll_entry *e);
    /* A file stored in one piece in the address space (flash): where and how long, so its
     * code can run in place. It stays there, unchanged, as long as the file is open. NULL:
     * not possible (-EOPNOTSUPP). */
    int (*xip)(struct file *f, uintptr_t *addr, uint32_t *size);
};

struct file {
    const struct file_ops *ops;
    void *priv;         /* file system / driver data */
    void *dev;          /* devfs: the registered device's private pointer */
    uint32_t flags;     /* VFS_O_* */
    uint32_t refs;
    void *mnt;          /* private to the VFS */
    void *owner;        /* private to the VFS: the module of @ops, kept loaded while open */
};

int vfs_open(const char *path, uint32_t flags, struct file **out);
struct file *vfs_file_new(const struct file_ops *ops, void *priv, uint32_t flags);  /* no file system */
int vfs_opendir(const char *path, struct file **out);
void vfs_file_get(struct file *f);
int vfs_close(struct file *f);
int vfs_read(struct file *f, void *buf, size_t len);
int vfs_write(struct file *f, const void *buf, size_t len);
int64_t vfs_lseek(struct file *f, int64_t off, int whence);
int vfs_ioctl(struct file *f, unsigned cmd, void *arg);
int vfs_readdir(struct file *f, struct vfs_dirent *de);
int vfs_fstat(struct file *f, struct vfs_stat *st);
int vfs_sync(struct file *f);
int vfs_poll(struct file *f, struct poll_entry *e);
int vfs_xip(struct file *f, uintptr_t *addr, uint32_t *size);   /* see file_ops.xip */
int vfs_stat(const char *path, struct vfs_stat *st);
int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);
int vfs_rename(const char *from, const char *to);
int vfs_statfs(const char *path, uint64_t *total, uint64_t *free);

/* Canonical form of an absolute path: no "//", "." or "..", no trailing "/" */
int vfs_normalize(const char *in, char *out, size_t size);

/* Read a whole file into memory from kmalloc(@kmflags); the caller kfree()s it */
int vfs_load_file(const char *path, void **data, size_t *size, unsigned kmflags);

/* File systems */
struct vfs_fs_ops {
    int (*open)(void *fs, const char *rel, uint32_t flags, struct file *f);
    int (*opendir)(void *fs, const char *rel, struct file *f);
    int (*stat)(void *fs, const char *rel, struct vfs_stat *st);
    int (*mkdir)(void *fs, const char *rel);
    int (*unlink)(void *fs, const char *rel);
    int (*rename)(void *fs, const char *from, const char *to);
    int (*statfs)(void *fs, uint64_t *total, uint64_t *free);
};

/* @mountpoint is a directory of the root ("/sd"); @rel paths passed to the file system
 * start with "/" */
int vfs_mount(const char *mountpoint, const struct vfs_fs_ops *ops, void *fs);
int vfs_umount(const char *mountpoint);

/* devfs: publish /dev/<name>. @dev is stored in file->dev for every open. */
int devfs_register(const char *name, const struct file_ops *ops, void *dev);
int devfs_unregister(const char *name);
void devfs_foreach(void (*fn)(const char *name, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif

#endif
