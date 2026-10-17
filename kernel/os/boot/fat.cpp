/*
 * kernel/os/boot/fat.cpp - FAT/exFAT file system (FatFs) on the SD card, mounted into the VFS.
 *
 * Contains the FatFs system glue (disk I/O on the built-in SD driver, per-volume mutexes,
 * long file name buffers from the kernel heap) and the VFS operations.
 */
#include "kernel.h"
#include "sdcard.h"
#include <crtos/rtc.h>
#include <crtos/vfs.h>
#include <string.h>

extern "C" {
#include "ff.h"
#include "diskio.h"
}

/* ---- FatFs system glue ------------------------------------------------------------------ */

extern "C" DSTATUS disk_status(BYTE pdrv)
{
    struct sdcard_info info;
    if (pdrv != 0)
        return STA_NOINIT;
    sdcard_get_info(&info);
    return info.ready ? 0 : STA_NOINIT;
}

extern "C" DSTATUS disk_initialize(BYTE pdrv)
{
    return disk_status(pdrv); /* the card is initialised by sdcard_init() */
}

/* the time FatFs writes into a file's directory entry: the wall clock (UTC) */
extern "C" DWORD get_fattime(void)
{
    return fat_time_now();
}

/*
 * Block cache: write gathering and read-ahead. A card is fast only with long transfers, while
 * FatFs moves one cluster at a time - 4 KB on cards formatted with small clusters, which makes
 * both reads and writes several times slower. So consecutive writes are gathered into one long
 * write, and a read that continues the previous one fetches ahead. FatFs asks for CTRL_SYNC
 * whenever data must be on the card (f_sync, f_close, ...); gathered writes also go out before
 * a read of them and when a write does not continue them. FatFs serialises all calls here
 * (one volume, FF_FS_REENTRANT).
 */
#define WB_BLOCKS   512u                /* 256 KB of gathered writes */
#define RA_BLOCKS   256u                /* read ahead: up to 128 KB, */
#define RA_FIRST    32u                 /* starting with 16 KB, doubled while reads go on in order */

static uint8_t *s_wb, *s_ra;
static uint32_t s_wb_lba, s_wb_n;       /* gathered blocks */
static uint32_t s_ra_lba, s_ra_n;       /* blocks read ahead */
static uint32_t s_next_read;            /* the block after the last read */
static uint32_t s_ra_window = RA_FIRST;
static bool s_cache_tried;

static void cache_init(void)
{
    if (s_cache_tried)
        return;
    s_cache_tried = true;
    s_wb = (uint8_t *)kmalloc_aligned(WB_BLOCKS * SDCARD_BLOCK_SIZE, 32, KM_LARGE);
    s_ra = (uint8_t *)kmalloc_aligned(RA_BLOCKS * SDCARD_BLOCK_SIZE, 32, KM_LARGE);
}

static bool overlaps(uint32_t a, uint32_t an, uint32_t b, uint32_t bn)
{
    return an && bn && a < b + bn && b < a + an;
}

static int wb_flush(void)
{
    if (!s_wb_n)
        return 0;
    int r = sdcard_write(s_wb_lba, s_wb, s_wb_n);
    s_wb_n = 0;
    return r;
}

/* After blocks were written around this cache (tests writing the card directly) */
void fat_cache_invalidate(void)
{
    s_ra_n = 0;
    s_next_read = 0;
}

extern "C" DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0)
        return RES_PARERR;
    cache_init();
    uint32_t lba = (uint32_t)sector;
    if (overlaps(lba, count, s_wb_lba, s_wb_n) && wb_flush())
        return RES_ERROR;
    if (s_ra_n && lba >= s_ra_lba && lba + count <= s_ra_lba + s_ra_n) { /* read ahead before */
        memcpy(buff, s_ra + (lba - s_ra_lba) * SDCARD_BLOCK_SIZE, count * SDCARD_BLOCK_SIZE);
        s_next_read = lba + count;
        return RES_OK;
    }
    bool sequential = lba == s_next_read;
    s_next_read = lba + count;
    if (!sequential)
        s_ra_window = RA_FIRST;
    /* file data only (whole clusters): single blocks are metadata and directories */
    if (sequential && s_ra && count > 1 && count < s_ra_window) {
        struct sdcard_info info;
        sdcard_get_info(&info);
        uint32_t n = s_ra_window;
        if (s_ra_window < RA_BLOCKS)
            s_ra_window *= 2;
        if (lba + n > info.blocks)
            n = info.blocks - lba;
        if (n >= count && overlaps(lba, n, s_wb_lba, s_wb_n) && wb_flush())
            return RES_ERROR;
        if (n >= count && !sdcard_read(lba, s_ra, n)) {
            s_ra_lba = lba;
            s_ra_n = n;
            memcpy(buff, s_ra, count * SDCARD_BLOCK_SIZE);
            return RES_OK;
        }
        s_ra_n = 0;
    }
    return sdcard_read(lba, buff, count) ? RES_ERROR : RES_OK;
}

extern "C" DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0)
        return RES_PARERR;
    cache_init();
    uint32_t lba = (uint32_t)sector;
    if (overlaps(lba, count, s_ra_lba, s_ra_n))
        s_ra_n = 0;
    if (!s_wb)
        return sdcard_write(lba, buff, count) ? RES_ERROR : RES_OK;
    if (s_wb_n && lba >= s_wb_lba && lba + count <= s_wb_lba + s_wb_n) { /* gathered already: update */
        memcpy(s_wb + (lba - s_wb_lba) * SDCARD_BLOCK_SIZE, buff, count * SDCARD_BLOCK_SIZE);
        return RES_OK;
    }
    if (s_wb_n && lba == s_wb_lba + s_wb_n && s_wb_n + count <= WB_BLOCKS) { /* continues them */
        memcpy(s_wb + s_wb_n * SDCARD_BLOCK_SIZE, buff, count * SDCARD_BLOCK_SIZE);
        s_wb_n += count;
        return RES_OK;
    }
    if (wb_flush())
        return RES_ERROR;
    if (count >= WB_BLOCKS)
        return sdcard_write(lba, buff, count) ? RES_ERROR : RES_OK;
    memcpy(s_wb, buff, count * SDCARD_BLOCK_SIZE);
    s_wb_lba = lba;
    s_wb_n = count;
    return RES_OK;
}

extern "C" DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    struct sdcard_info info;
    if (pdrv != 0)
        return RES_PARERR;
    sdcard_get_info(&info);
    switch (cmd) {
    case CTRL_SYNC: /* FatFs needs the data on the card now */
        return wb_flush() ? RES_ERROR : RES_OK;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = info.blocks;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = SDCARD_BLOCK_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

extern "C" void *ff_memalloc(UINT msize)
{
    return kmalloc(msize, KM_ANY);
}

extern "C" void ff_memfree(void *mblock)
{
    kfree(mblock);
}

static struct mutex s_ff_mutex[FF_VOLUMES + 1];

extern "C" int ff_mutex_create(int vol)
{
    mutex_init(&s_ff_mutex[vol]);
    return 1;
}

extern "C" void ff_mutex_delete(int)
{
}

extern "C" int ff_mutex_take(int vol)
{
    return mutex_lock(&s_ff_mutex[vol], FF_FS_TIMEOUT) == 0;
}

extern "C" void ff_mutex_give(int vol)
{
    mutex_unlock(&s_ff_mutex[vol]);
}

/* ---- VFS operations ------------------------------------------------------------------------ */

static FATFS s_fatfs;

static int fr_errno(FRESULT fr)
{
    switch (fr) {
    case FR_OK: return 0;
    case FR_NO_FILE:
    case FR_NO_PATH:
    case FR_INVALID_NAME: return -ENOENT;
    case FR_DENIED: return -EACCES;
    case FR_EXIST: return -EEXIST;
    case FR_WRITE_PROTECTED: return -EROFS;
    case FR_NOT_READY:
    case FR_NOT_ENABLED:
    case FR_NO_FILESYSTEM: return -ENODEV;
    case FR_TIMEOUT: return -ETIMEDOUT;
    case FR_LOCKED: return -EBUSY;
    case FR_NOT_ENOUGH_CORE: return -ENOMEM;
    case FR_TOO_MANY_OPEN_FILES: return -EMFILE;
    case FR_INVALID_PARAMETER:
    case FR_INVALID_OBJECT: return -EINVAL;
    default: return -EIO;
    }
}

/* "/crtos/boot" -> "0:/crtos/boot" */
static int ffpath(const char *rel, char *out, size_t size)
{
    size_t n = strlen(rel);
    if (n + 3 > size)
        return -ENAMETOOLONG;
    out[0] = '0';
    out[1] = ':';
    memcpy(out + 2, rel, n + 1);
    return 0;
}

static int fat_read(struct file *f, void *buf, size_t len)
{
    UINT got = 0;
    FRESULT fr = f_read((FIL *)f->priv, buf, (UINT)len, &got);
    return fr ? fr_errno(fr) : (int)got;
}

static int fat_write(struct file *f, const void *buf, size_t len)
{
    FIL *fp = (FIL *)f->priv;
    if ((f->flags & VFS_O_APPEND) && f_lseek(fp, f_size(fp)) != FR_OK)
        return -EIO;
    UINT put = 0;
    FRESULT fr = f_write(fp, buf, (UINT)len, &put);
    if (fr)
        return fr_errno(fr);
    return put ? (int)put : (len ? -ENOSPC : 0);
}

static int64_t fat_lseek(struct file *f, int64_t off, int whence)
{
    FIL *fp = (FIL *)f->priv;
    int64_t base = whence == VFS_SEEK_SET ? 0 : whence == VFS_SEEK_CUR ? (int64_t)f_tell(fp) : (int64_t)f_size(fp);
    int64_t pos = base + off;
    if (pos < 0)
        return -EINVAL;
    FRESULT fr = f_lseek(fp, (FSIZE_t)pos);
    return fr ? fr_errno(fr) : (int64_t)f_tell(fp);
}

/* An open file: FatFs's object and the path, for the time of the last write (fstat) */
struct fat_file {
    FIL fil;                            /* first: aligned for its sector buffer */
    char path[VFS_PATH_MAX + 3];
};

static int fat_fstat(struct file *f, struct vfs_stat *st)
{
    struct fat_file *ff = (struct fat_file *)f->priv;
    memset(st, 0, sizeof(*st));
    st->mode = VFS_S_IFREG;
    st->size = (int64_t)f_size(&ff->fil);
    /* the directory entry holds the time (renamed meanwhile: none) */
    FILINFO *fi = (FILINFO *)kmalloc(sizeof(FILINFO), KM_ANY);
    if (fi) {
        if (f_stat(ff->path, fi) == FR_OK)
            st->mtime = (uint32_t)fi->fdate << 16 | fi->ftime;
        kfree(fi);
    }
    return 0;
}

static int fat_sync(struct file *f)
{
    return fr_errno(f_sync((FIL *)f->priv));
}

static int fat_close(struct file *f)
{
    FRESULT fr = f_close((FIL *)f->priv);
    kfree(f->priv);
    return fr_errno(fr);
}

static const struct file_ops fat_file_ops = {
    nullptr, fat_read, fat_write, fat_lseek, nullptr, nullptr, fat_fstat, fat_sync, fat_close,
};

static int fat_open(void *, const char *rel, uint32_t flags, struct file *f)
{
    char path[VFS_PATH_MAX + 3];
    int r = ffpath(rel, path, sizeof(path));
    if (r)
        return r;
    BYTE mode = 0;
    switch (flags & VFS_O_ACCMODE) {
    case VFS_O_RDONLY: mode = FA_READ; break;
    case VFS_O_WRONLY: mode = FA_WRITE; break;
    default: mode = FA_READ | FA_WRITE; break;
    }
    if (flags & VFS_O_CREAT)
        mode |= (flags & VFS_O_EXCL) ? FA_CREATE_NEW : (flags & VFS_O_TRUNC) ? FA_CREATE_ALWAYS : FA_OPEN_ALWAYS;
    else if (flags & VFS_O_TRUNC)
        mode |= FA_CREATE_ALWAYS;
    struct fat_file *ff = (struct fat_file *)kmalloc_aligned(sizeof(struct fat_file), 32, KM_ANY);
    if (!ff)
        return -ENOMEM;
    FRESULT fr = f_open(&ff->fil, path, mode);
    if (fr) {
        kfree(ff);
        return fr_errno(fr);
    }
    strcpy(ff->path, path);
    f->ops = &fat_file_ops;
    f->priv = ff;
    return 0;
}

/* Directory handle: FILINFO (long names) kept off the stack */
struct fat_dir {
    DIR dir;
    FILINFO fi;
};

/* Scratch for path operations, also kept off the (guarded, small) kernel stacks */
struct fat_scratch {
    char path[VFS_PATH_MAX + 3];
    char path2[VFS_PATH_MAX + 3];
    FILINFO fi;
};

static int fat_readdir(struct file *f, struct vfs_dirent *de)
{
    struct fat_dir *d = (struct fat_dir *)f->priv;
    FRESULT fr = f_readdir(&d->dir, &d->fi);
    if (fr)
        return fr_errno(fr);
    if (!d->fi.fname[0])
        return 0;
    strncpy(de->name, d->fi.fname, sizeof(de->name) - 1);
    de->name[sizeof(de->name) - 1] = 0;
    de->mode = (d->fi.fattrib & AM_DIR) ? VFS_S_IFDIR : VFS_S_IFREG;
    de->size = (int64_t)d->fi.fsize;
    return 1;
}

static int fat_closedir(struct file *f)
{
    FRESULT fr = f_closedir(&((struct fat_dir *)f->priv)->dir);
    kfree(f->priv);
    return fr_errno(fr);
}

/* The card blocks of an open file stored in one piece (exFAT, not fragmented) - for tests
 * that rewrite a file of their own directly */
int fat_file_extent(struct file *f, uint32_t *lba, uint32_t *blocks)
{
    if (f->ops != &fat_file_ops)
        return -EINVAL;
    FIL *fp = (FIL *)f->priv;
    FATFS *fs = fp->obj.fs;
    if (!fs || fs->fs_type != FS_EXFAT || fp->obj.stat != 2 || fp->obj.sclust < 2)
        return -ENOENT;
    *lba = (uint32_t)(fs->database + (LBA_t)fs->csize * (fp->obj.sclust - 2));
    *blocks = (uint32_t)((fp->obj.objsize + 511u) / 512u);
    return 0;
}

/* The swap file of emulated memory (os/vmem.cpp): @size bytes in one piece at card block
 * *lba, so that its pages are read and written as blocks (fat_raw_io), without the file system
 * in between. A file of that size already stored in one piece (exFAT) is used as it is;
 * otherwise it is created anew with f_expand, which allocates without writing the data. */
int fat_swap_create(const char *rel, uint32_t size, uint32_t *lba)
{
    struct fat_file *ff = (struct fat_file *)kmalloc_aligned(sizeof(struct fat_file), 32, KM_ANY);
    if (!ff)
        return -ENOMEM;
    int r = ffpath(rel, ff->path, sizeof(ff->path));
    if (r) {
        kfree(ff);
        return r;
    }
    FIL *fp = &ff->fil;
    FRESULT fr = f_open(fp, ff->path, FA_READ | FA_WRITE | FA_OPEN_EXISTING);
    bool reuse = fr == FR_OK && f_size(fp) == size && fp->obj.fs->fs_type == FS_EXFAT && fp->obj.stat == 2;
    if (fr == FR_OK && !reuse) {
        f_close(fp);
        f_unlink(ff->path);
    }
    if (!reuse) {
        char *slash = strrchr(ff->path, '/'); /* its directory (the card's /crtos/var) */
        if (slash && slash > ff->path + 2) {
            *slash = 0;
            f_mkdir(ff->path);
            *slash = '/';
        }
        fr = f_open(fp, ff->path, FA_READ | FA_WRITE | FA_CREATE_ALWAYS);
        if (fr == FR_OK) {
            fr = f_expand(fp, (FSIZE_t)size, 1);
            if (fr != FR_OK) {
                f_close(fp);
                f_unlink(ff->path);
            }
        }
    }
    if (fr == FR_OK) {
        FATFS *fs = fp->obj.fs;
        *lba = (uint32_t)(fs->database + (LBA_t)fs->csize * (fp->obj.sclust - 2));
        fr = f_close(fp);
    }
    kfree(ff);
    return fr_errno(fr);
}

/* Blocks of the card read or written the way FatFs does it (through the block cache, under
 * the volume's lock), so that they stay coherent with the file system's own */
int fat_raw_io(uint32_t lba, void *buf, uint32_t count, bool write)
{
    if (!ff_mutex_take(0))
        return -ETIMEDOUT;
    DRESULT dr = write ? disk_write(0, (const BYTE *)buf, lba, count) : disk_read(0, (BYTE *)buf, lba, count);
    ff_mutex_give(0);
    return dr == RES_OK ? 0 : -EIO;
}

static const struct file_ops fat_dir_ops = {
    nullptr, nullptr, nullptr, nullptr, nullptr, fat_readdir, nullptr, nullptr, fat_closedir,
};

static int fat_opendir(void *, const char *rel, struct file *f)
{
    char path[VFS_PATH_MAX + 3];
    int r = ffpath(rel, path, sizeof(path));
    if (r)
        return r;
    struct fat_dir *d = (struct fat_dir *)kmalloc(sizeof(*d), KM_ANY);
    if (!d)
        return -ENOMEM;
    FRESULT fr = f_opendir(&d->dir, path);
    if (fr) {
        kfree(d);
        return fr_errno(fr);
    }
    f->ops = &fat_dir_ops;
    f->priv = d;
    return 0;
}

static int fat_stat(void *, const char *rel, struct vfs_stat *st)
{
    struct fat_scratch *t = (struct fat_scratch *)kmalloc(sizeof(*t), KM_ANY);
    if (!t)
        return -ENOMEM;
    int r = ffpath(rel, t->path, sizeof(t->path));
    if (!r) {
        FRESULT fr = f_stat(t->path, &t->fi);
        r = fr_errno(fr);
        if (!r) {
            memset(st, 0, sizeof(*st));
            st->mode = (t->fi.fattrib & AM_DIR) ? VFS_S_IFDIR : VFS_S_IFREG;
            st->size = (int64_t)t->fi.fsize;
            st->mtime = ((uint32_t)t->fi.fdate << 16) | t->fi.ftime;
        }
    }
    kfree(t);
    return r;
}

static int fat_mkdir(void *, const char *rel)
{
    char path[VFS_PATH_MAX + 3];
    int r = ffpath(rel, path, sizeof(path));
    return r ? r : fr_errno(f_mkdir(path));
}

static int fat_unlink(void *, const char *rel)
{
    char path[VFS_PATH_MAX + 3];
    int r = ffpath(rel, path, sizeof(path));
    return r ? r : fr_errno(f_unlink(path));
}

static int fat_rename(void *, const char *from, const char *to)
{
    struct fat_scratch *t = (struct fat_scratch *)kmalloc(sizeof(*t), KM_ANY);
    if (!t)
        return -ENOMEM;
    int r = ffpath(from, t->path, sizeof(t->path));
    if (!r)
        r = ffpath(to, t->path2, sizeof(t->path2));
    /* the destination must not carry a drive prefix */
    if (!r)
        r = fr_errno(f_rename(t->path, t->path2 + 2));
    kfree(t);
    return r;
}

static int fat_statfs(void *, uint64_t *total, uint64_t *free)
{
    DWORD nclst = 0;
    FATFS *fs = nullptr;
    FRESULT fr = f_getfree("0:", &nclst, &fs);
    if (fr)
        return fr_errno(fr);
    uint64_t csize = (uint64_t)fs->csize * SDCARD_BLOCK_SIZE;
    *total = (uint64_t)(fs->n_fatent - 2) * csize;
    *free = (uint64_t)nclst * csize;
    return 0;
}

static const struct vfs_fs_ops fat_fs_ops = {
    fat_open, fat_opendir, fat_stat, fat_mkdir, fat_unlink, fat_rename, fat_statfs,
};

int fat_mount(const char *mountpoint)
{
    FRESULT fr = f_mount(&s_fatfs, "0:", 1);
    if (fr) {
        printk("E: sd: no FAT/exFAT file system (FatFs error %d)\n", (int)fr);
        return fr_errno(fr);
    }
    int r = vfs_mount(mountpoint, &fat_fs_ops, &s_fatfs);
    if (r)
        f_unmount("0:");
    return r;
}

uint32_t fat_cluster_size(void)
{
    return (uint32_t)s_fatfs.csize * 512u;
}

const char *fat_type_name(void)
{
    switch (s_fatfs.fs_type) {
    case FS_FAT12: return "FAT12";
    case FS_FAT16: return "FAT16";
    case FS_FAT32: return "FAT32";
    case FS_EXFAT: return "exFAT";
    default: return "?";
    }
}
