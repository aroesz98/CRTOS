/*
 * flashfs.ko - a file system on a flash partition (/flash0 on the HyperFlash), for programs
 * that run in place (XIP) and for anything else worth keeping out of the SD card.
 *
 * Layout on the partition (erase blocks of 256 KB):
 *   block 0, block 1   the record log, one of them active (the higher valid generation)
 *   block 2 ..         data: every file is one run of whole blocks, from its first byte
 *
 * The log is a list of 256-byte records - a header in the first slot, then one record per
 * change: a file (name -> blocks, size, time, CRC of the data), a directory, a removal, a
 * rename. Each record carries a CRC-32; mounting replays the valid ones. A file's record is
 * written only after its data, so a power cut during a write leaves the old state: the
 * half-written blocks are free again (a block is erased when it is used, not when it is freed).
 * When the active log is full, the live entries are written to the other block (erased first),
 * whose header - a higher generation - goes last and makes it the active one.
 *
 * Files are written once, from the start (O_CREAT/O_TRUNC, sequential writes): the writer
 * takes the largest free run of blocks and gives back what it did not use when it closes -
 * or, told the size first (FLASHFS_IOC_RESERVE on the new file), the shortest run that holds
 * it, which keeps the long runs for long files (the flash has no way to move files). A
 * file written again is a new file that replaces the old one at its close; a file removed or
 * replaced while open (a running program) keeps its blocks until its last close. Such a file
 * is pinned in place, so the loader can run its code from the flash (file_ops.xip).
 *
 * The flash stands still while it programs or erases, and so does the whole system (the
 * kernel runs from it): writing costs about 0.5 ms per 512 bytes, plus up to about a second
 * per erase block. Reads are plain memory reads of the mapped flash.
 *
 * /dev/flashfs0 (crtos/flashfs.h): information, a check of every file's data, formatting.
 */
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/flashfs.h>
#include <crtos/mm.h>
#include <crtos/module.h>
#include <crtos/mtd.h>
#include <crtos/of.h>
#include <crtos/printk.h>
#include <crtos/rtc.h>
#include <crtos/sync.h>
#include <crtos/syscall.h>
#include <crtos/uaccess.h>
#include <crtos/vfs.h>

uint32_t crc32(uint32_t crc, const void *data, size_t len); /* the kernel's */

#define FS_MAGIC 0x30534646u /* "FFS0" */
#define FS_VERSION 1u
#define REC_MAGIC 0x43455246u /* "FREC" */
#define SLOT 256u
#define LOG_BLOCKS 2u
#define NAME_LEN (SLOT - 36u) /* with its NUL */
#define PAGE 512u             /* what the writer programs at once */
#define MAX_ENTRIES 1000u     /* the log must hold them all after compaction */

enum
{
    R_FILE = 1,
    R_DIR = 2,
    R_DEL = 3,
    R_RENAME = 4
};

struct head
{
    uint32_t magic, version, seq, erasesize, blocks, reserved[2], crc;
};

struct rec
{
    uint32_t magic;
    uint16_t type, namelen;
    uint32_t id;
    uint32_t block; /* R_FILE: first data block (in the partition) */
    uint32_t size;  /* R_FILE: bytes */
    uint32_t mtime; /* FAT timestamp */
    uint32_t dcrc;  /* R_FILE: CRC-32 of the data */
    uint32_t nblocks;
    char name[NAME_LEN]; /* in the file system: "/bin/gcc.app" */
    uint32_t crc;        /* of all of the above */
};

_Static_assert(sizeof(struct rec) == SLOT, "a record is one slot");
_Static_assert(sizeof(struct head) <= SLOT, "the header fits a slot");

struct ent
{
    uint32_t id;
    uint8_t type; /* R_FILE or R_DIR */
    bool gone;    /* removed or replaced while open: freed at the last close */
    uint16_t opens;
    uint32_t block, nblocks, size, mtime, dcrc;
    char *name;
};

enum
{
    B_FREE = 0,
    B_FILE,
    B_WRITING
};

struct fs
{
    struct mtd_part *part;
    struct mtd_geometry geo;
    const char *mountpoint;
    struct mutex lock;
    bool mounted;
    uint32_t nblocks; /* all blocks of the partition */
    uint8_t *bmap;    /* B_* per block (the log blocks count as used) */
    struct ent **ents;
    uint32_t nents, cap;
    uint32_t next_id;
    uint32_t log; /* the active log block */
    uint32_t seq;
    uint32_t next_slot; /* in the active log */
    uint32_t opens;     /* open files (not counting the control device) */
};

struct writer
{
    struct fs *fs;
    char name[NAME_LEN];
    uint32_t first, avail; /* the reserved run of blocks */
    uint32_t size, crc;
    uint32_t erased; /* blocks of the run made ready (erased) so far */
    uint32_t nbuf;
    int err;
    uint8_t buf[PAGE];
};

struct reader
{
    struct ent *e;
    uint32_t pos;
};

struct dirh
{
    char dir[NAME_LEN];
    uint32_t index;
};

static struct fs s_fs;

static uint32_t slots(const struct fs *fs)
{
    return fs->geo.erasesize / SLOT;
}

static const uint8_t *block_addr(const struct fs *fs, uint32_t b)
{
    return fs->geo.mapped + (size_t)b * fs->geo.erasesize;
}

/* ---- entries ------------------------------------------------------------------------------ */

static struct ent *find(struct fs *fs, const char *name)
{
    for (uint32_t i = 0; i < fs->nents; i++)
        if (!fs->ents[i]->gone && !strcmp(fs->ents[i]->name, name))
            return fs->ents[i];
    return NULL;
}

static struct ent *find_id(struct fs *fs, uint32_t id)
{
    for (uint32_t i = 0; i < fs->nents; i++)
        if (!fs->ents[i]->gone && fs->ents[i]->id == id)
            return fs->ents[i];
    return NULL;
}

static struct ent *add_ent(struct fs *fs, uint32_t id, uint8_t type, const char *name)
{
    if (fs->nents == fs->cap)
    {
        uint32_t cap = fs->cap ? fs->cap * 2 : 64;
        struct ent **e = (struct ent **)kmalloc(cap * sizeof(*e), KM_ANY);
        if (!e)
            return NULL;
        if (fs->ents)
        {
            memcpy(e, fs->ents, fs->nents * sizeof(*e));
            kfree(fs->ents);
        }
        fs->ents = e;
        fs->cap = cap;
    }
    struct ent *e = (struct ent *)kzalloc(sizeof(*e), KM_ANY);
    char *n = (char *)kmalloc(strlen(name) + 1, KM_ANY);
    if (!e || !n)
    {
        kfree(e);
        kfree(n);
        return NULL;
    }
    strcpy(n, name);
    e->id = id;
    e->type = type;
    e->name = n;
    fs->ents[fs->nents++] = e;
    if (id >= fs->next_id)
        fs->next_id = id + 1;
    return e;
}

static void set_blocks(struct fs *fs, uint32_t first, uint32_t n, uint8_t state)
{
    for (uint32_t b = first; b < first + n && b < fs->nblocks; b++)
        fs->bmap[b] = state;
}

/* the entry leaves the tree; its blocks are free now or at its last close */
static void drop_ent(struct fs *fs, struct ent *e)
{
    if (e->opens)
    {
        e->gone = true;
        return;
    }
    if (e->type == R_FILE)
        set_blocks(fs, e->block, e->nblocks, B_FREE);
    for (uint32_t i = 0; i < fs->nents; i++)
    {
        if (fs->ents[i] == e)
        {
            fs->ents[i] = fs->ents[--fs->nents];
            break;
        }
    }
    kfree(e->name);
    kfree(e);
}

static void free_all(struct fs *fs)
{
    for (uint32_t i = 0; i < fs->nents; i++)
    {
        kfree(fs->ents[i]->name);
        kfree(fs->ents[i]);
    }
    kfree(fs->ents);
    fs->ents = NULL;
    fs->nents = fs->cap = 0;
}

/* "/a/b/c" -> is its parent "/a/b" a directory ("/" always is)? */
static bool parent_is_dir(struct fs *fs, const char *name)
{
    const char *slash = strrchr(name, '/');
    if (!slash || slash == name)
        return true;
    char parent[NAME_LEN];
    size_t n = (size_t)(slash - name);
    memcpy(parent, name, n);
    parent[n] = 0;
    struct ent *p = find(fs, parent);
    return p && p->type == R_DIR;
}

static bool has_children(struct fs *fs, const char *dir)
{
    size_t l = strlen(dir);
    for (uint32_t i = 0; i < fs->nents; i++)
    {
        const struct ent *e = fs->ents[i];
        if (!e->gone && !strncmp(e->name, dir, l) && e->name[l] == '/')
            return true;
    }
    return false;
}

/* ---- the log --------------------------------------------------------------------------------- */

static uint32_t rec_crc(const struct rec *r)
{
    return crc32(0, r, offsetof(struct rec, crc));
}

static uint32_t head_crc(const struct head *h)
{
    return crc32(0, h, offsetof(struct head, crc));
}

static bool slot_erased(const uint8_t *p)
{
    for (uint32_t i = 0; i < SLOT; i++)
        if (p[i] != 0xFF)
            return false;
    return true;
}

static bool block_erased(const struct fs *fs, uint32_t b)
{
    const uint32_t *p = (const uint32_t *)block_addr(fs, b);
    for (uint32_t i = 0; i < fs->geo.erasesize / 4u; i++)
        if (p[i] != 0xFFFFFFFFu)
            return false;
    return true;
}

static void apply(struct fs *fs, const struct rec *r)
{
    struct ent *e;
    switch (r->type)
    {
    case R_FILE:
        e = find(fs, r->name);
        if (e)
            drop_ent(fs, e); /* replaced */
        e = add_ent(fs, r->id, R_FILE, r->name);
        if (e)
        {
            e->block = r->block;
            e->nblocks = r->nblocks;
            e->size = r->size;
            e->mtime = r->mtime;
            e->dcrc = r->dcrc;
            set_blocks(fs, e->block, e->nblocks, B_FILE);
        }
        break;
    case R_DIR:
        if (!find(fs, r->name))
        {
            e = add_ent(fs, r->id, R_DIR, r->name);
            if (e)
                e->mtime = r->mtime;
        }
        break;
    case R_DEL:
        e = find_id(fs, r->id);
        if (e)
            drop_ent(fs, e);
        break;
    case R_RENAME:
    {
        e = find_id(fs, r->id);
        struct ent *old = find(fs, r->name);
        if (!e)
            break;
        if (old && old != e)
            drop_ent(fs, old); /* renamed onto: replaced */
        char *n = (char *)kmalloc(strlen(r->name) + 1, KM_ANY);
        if (n)
        {
            strcpy(n, r->name);
            kfree(e->name);
            e->name = n;
            e->mtime = r->mtime;
        }
        break;
    }
    }
    if (r->id >= fs->next_id)
        fs->next_id = r->id + 1;
}

static void make_rec(struct rec *r, uint16_t type, uint32_t id, const char *name)
{
    memset(r, 0, sizeof(*r));
    r->magic = REC_MAGIC;
    r->type = type;
    r->id = id;
    r->mtime = fat_time_now();
    if (name)
    {
        size_t n = strlen(name); /* (callers keep names shorter than NAME_LEN) */
        if (n > NAME_LEN - 1)
            n = NAME_LEN - 1;
        memcpy(r->name, name, n);
        r->namelen = (uint16_t)n;
    }
}

static int compact(struct fs *fs);

/* one record at the end of the active log (compacting it first when full) */
static int append(struct fs *fs, struct rec *r)
{
    if (fs->next_slot >= slots(fs))
    {
        int c = compact(fs);
        if (c)
            return c;
    }
    r->crc = rec_crc(r);
    uint32_t off = fs->log * fs->geo.erasesize + fs->next_slot * SLOT;
    int e = mtd_part_program(fs->part, off, r, SLOT);
    fs->next_slot++; /* (a failed slot is garbage: the next record goes after it) */
    return e;
}

/* the live entries into the other log block; its header last makes it the active one */
static int compact(struct fs *fs)
{
    uint32_t live = 0;
    for (uint32_t i = 0; i < fs->nents; i++)
        live += !fs->ents[i]->gone;
    if (live + 1 >= slots(fs))
        return -ENOSPC;
    uint32_t other = fs->log ^ 1u;
    int e = mtd_part_erase(fs->part, other * fs->geo.erasesize, fs->geo.erasesize);
    uint32_t slot = 1;
    struct rec *r = (struct rec *)kmalloc(sizeof(*r), KM_ANY);
    if (!r)
        return -ENOMEM;
    /* directories first (replaying does not need their order) */
    for (int pass = 0; pass < 2 && !e; pass++)
    {
        for (uint32_t i = 0; i < fs->nents && !e; i++)
        {
            const struct ent *x = fs->ents[i];
            if (x->gone || (pass == 0) != (x->type == R_DIR))
                continue;
            make_rec(r, x->type, x->id, x->name);
            r->mtime = x->mtime;
            r->block = x->block;
            r->nblocks = x->nblocks;
            r->size = x->size;
            r->dcrc = x->dcrc;
            r->crc = rec_crc(r);
            e = mtd_part_program(fs->part, other * fs->geo.erasesize + slot * SLOT, r, SLOT);
            slot++;
        }
    }
    kfree(r);
    if (e)
        return e;
    struct head h;
    memset(&h, 0xFF, sizeof(h));
    h.magic = FS_MAGIC;
    h.version = FS_VERSION;
    h.seq = fs->seq + 1;
    h.erasesize = fs->geo.erasesize;
    h.blocks = fs->nblocks;
    h.crc = head_crc(&h);
    e = mtd_part_program(fs->part, other * fs->geo.erasesize, &h, sizeof(h));
    if (e)
        return e;
    fs->log = other;
    fs->seq++;
    fs->next_slot = slot;
    printk("flashfs: log compacted: %lu entries, generation %lu\n", (unsigned long)live, (unsigned long)fs->seq);
    return 0;
}

static bool valid_head(const struct fs *fs, const struct head *h)
{
    return h->magic == FS_MAGIC && h->version == FS_VERSION && h->crc == head_crc(h) &&
           h->erasesize == fs->geo.erasesize && h->blocks == fs->nblocks;
}

/* read the log into the entries; -ENODEV when there is no file system */
static int replay(struct fs *fs)
{
    const struct head *h0 = (const struct head *)block_addr(fs, 0);
    const struct head *h1 = (const struct head *)block_addr(fs, 1);
    bool v0 = valid_head(fs, h0), v1 = valid_head(fs, h1);
    if (!v0 && !v1)
        return -ENODEV;
    fs->log = v0 && (!v1 || (int32_t)(h0->seq - h1->seq) > 0) ? 0 : 1;
    fs->seq = fs->log ? h1->seq : h0->seq;
    memset(fs->bmap, B_FREE, fs->nblocks);
    set_blocks(fs, 0, LOG_BLOCKS, B_FILE);
    free_all(fs);
    fs->next_id = 1;
    const uint8_t *base = block_addr(fs, fs->log);
    uint32_t n = slots(fs), s, bad = 0;
    for (s = 1; s < n; s++)
    {
        const struct rec *r = (const struct rec *)(base + s * SLOT);
        if (slot_erased((const uint8_t *)r))
            break;
        if (r->magic != REC_MAGIC || r->crc != rec_crc(r) || r->name[NAME_LEN - 1] ||
            (r->type == R_FILE && (r->block < LOG_BLOCKS || r->block + r->nblocks > fs->nblocks)))
        {
            bad++;
            continue;
        }
        apply(fs, r);
    }
    fs->next_slot = s;
    if (bad)
        printk("flashfs: %lu damaged log record(s) skipped\n", (unsigned long)bad);
    return 0;
}

/* ---- free space ------------------------------------------------------------------------------- */

/* the largest run of free blocks: *first, and its length */
static uint32_t largest_free(struct fs *fs, uint32_t *first)
{
    uint32_t best = 0, start = LOG_BLOCKS;
    *first = 0;
    for (uint32_t b = LOG_BLOCKS; b < fs->nblocks; b++)
    {
        if (fs->bmap[b] != B_FREE)
        {
            start = b + 1;
            continue;
        }
        if (b + 1 - start > best)
        {
            best = b + 1 - start;
            *first = start;
        }
    }
    return best;
}

/* the shortest run of free blocks that holds @need: *first, and its length (0: none) */
static uint32_t fitting_free(struct fs *fs, uint32_t need, uint32_t *first)
{
    uint32_t best = 0;
    *first = 0;
    for (uint32_t b = LOG_BLOCKS; b < fs->nblocks;)
    {
        if (fs->bmap[b] != B_FREE)
        {
            b++;
            continue;
        }
        uint32_t start = b;
        while (b < fs->nblocks && fs->bmap[b] == B_FREE)
            b++;
        uint32_t len = b - start;
        if (len >= need && (!best || len < best))
        {
            best = len;
            *first = start;
        }
    }
    return best;
}

/* ---- files ------------------------------------------------------------------------------------ */

static int w_flush(struct writer *w)
{
    struct fs *fs = w->fs;
    if (!w->nbuf || w->err)
        return w->err;
    uint32_t off = w->size - w->nbuf; /* where the buffer's bytes go in the file */
    /* the blocks the buffer reaches must be erased (they may hold an old file's data) */
    while (w->erased <= (off + w->nbuf - 1) / fs->geo.erasesize)
    {
        uint32_t b = w->first + w->erased;
        if (!block_erased(fs, b))
        {
            int e = mtd_part_erase(fs->part, b * fs->geo.erasesize, fs->geo.erasesize);
            if (e)
                return w->err = e;
        }
        w->erased++;
    }
    int e = mtd_part_program(fs->part, w->first * fs->geo.erasesize + off, w->buf, w->nbuf);
    w->nbuf = 0;
    return w->err = e;
}

static int w_write(struct file *f, const void *buf, size_t len)
{
    struct writer *w = (struct writer *)f->priv;
    struct fs *fs = w->fs;
    if (w->err)
        return w->err;
    if ((uint64_t)w->size + len > (uint64_t)w->avail * fs->geo.erasesize)
        return -ENOSPC;
    const uint8_t *p = (const uint8_t *)buf;
    size_t left = len;
    mutex_lock(&fs->lock, WAIT_FOREVER);
    while (left)
    {
        uint32_t n = PAGE - w->nbuf < left ? PAGE - w->nbuf : (uint32_t)left;
        memcpy(w->buf + w->nbuf, p, n);
        w->crc = crc32(w->crc, p, n);
        w->nbuf += n;
        w->size += n;
        p += n;
        left -= n;
        if (w->nbuf == PAGE && w_flush(w))
            break;
    }
    mutex_unlock(&fs->lock);
    return w->err ? w->err : (int)len;
}

static int64_t w_lseek(struct file *f, int64_t off, int whence)
{
    struct writer *w = (struct writer *)f->priv;
    /* sequential only: telling where it is works, moving does not */
    int64_t pos = whence == VFS_SEEK_SET ? off : off + w->size;
    return pos == w->size ? pos : -ESPIPE;
}

/* The size the file will have, told before the first write (FLASHFS_IOC_RESERVE): the writer
 * moves from the largest free run, taken when it did not know, to the shortest one that holds
 * the file - the long runs stay for long files */
static int w_reserve(struct writer *w, uint32_t size)
{
    struct fs *fs = w->fs;
    if (w->size || w->nbuf || w->erased)
        return -EBUSY; /* written to already */
    uint32_t need = (size + fs->geo.erasesize - 1) / fs->geo.erasesize;
    set_blocks(fs, w->first, w->avail, B_FREE);
    uint32_t first, n = fitting_free(fs, need ? need : 1, &first);
    if (!n)
    {
        set_blocks(fs, w->first, w->avail, B_WRITING);
        return -ENOSPC; /* now rather than after the data */
    }
    w->first = first;
    w->avail = n;
    set_blocks(fs, first, n, B_WRITING);
    return 0;
}

static int w_ioctl(struct file *f, unsigned cmd, void *arg)
{
    struct writer *w = (struct writer *)f->priv;
    if (cmd != FLASHFS_IOC_RESERVE)
        return -ENOTTY;
    mutex_lock(&w->fs->lock, WAIT_FOREVER);
    int r = w_reserve(w, *(const uint32_t *)arg);
    mutex_unlock(&w->fs->lock);
    return r;
}

static int w_fstat(struct file *f, struct vfs_stat *st)
{
    struct writer *w = (struct writer *)f->priv;
    memset(st, 0, sizeof(*st));
    st->mode = VFS_S_IFREG;
    st->size = w->size;
    st->mtime = fat_time_now();
    return 0;
}

static int w_close(struct file *f)
{
    struct writer *w = (struct writer *)f->priv;
    struct fs *fs = w->fs;
    mutex_lock(&fs->lock, WAIT_FOREVER);
    int e = w_flush(w);
    uint32_t used = (w->size + fs->geo.erasesize - 1) / fs->geo.erasesize;
    struct rec *r = e ? NULL : (struct rec *)kmalloc(sizeof(*r), KM_ANY);
    if (!e && !r)
        e = -ENOMEM;
    if (!e && !parent_is_dir(fs, w->name))
        e = -ENOENT; /* its directory went away meanwhile */
    if (!e)
    {
        make_rec(r, R_FILE, fs->next_id, w->name);
        r->block = used ? w->first : 0;
        r->nblocks = used;
        r->size = w->size;
        r->dcrc = w->crc;
        e = append(fs, r);
    }
    set_blocks(fs, w->first, w->avail, B_FREE); /* the file's own come back as B_FILE */
    if (!e)
        apply(fs, r);
    kfree(r);
    fs->opens--;
    mutex_unlock(&fs->lock);
    if (e)
        printk("flashfs: %s not saved (%d)\n", w->name, e);
    kfree(w);
    return e;
}

static int r_read(struct file *f, void *buf, size_t len)
{
    struct reader *rd = (struct reader *)f->priv;
    const struct ent *e = rd->e;
    if (rd->pos >= e->size)
        return 0;
    uint32_t n = e->size - rd->pos < len ? e->size - rd->pos : (uint32_t)len;
    memcpy(buf, block_addr(&s_fs, e->block) + rd->pos, n);
    rd->pos += n;
    return (int)n;
}

static int64_t r_lseek(struct file *f, int64_t off, int whence)
{
    struct reader *rd = (struct reader *)f->priv;
    int64_t base = whence == VFS_SEEK_SET ? 0 : whence == VFS_SEEK_CUR ? rd->pos
                                                                       : rd->e->size;
    int64_t pos = base + off;
    if (pos < 0 || pos > 0x7FFFFFFF)
        return -EINVAL;
    rd->pos = (uint32_t)pos;
    return pos;
}

static int r_fstat(struct file *f, struct vfs_stat *st)
{
    struct reader *rd = (struct reader *)f->priv;
    memset(st, 0, sizeof(*st));
    st->mode = VFS_S_IFREG;
    st->size = rd->e->size;
    st->mtime = rd->e->mtime;
    return 0;
}

static int r_xip(struct file *f, uintptr_t *addr, uint32_t *size)
{
    struct reader *rd = (struct reader *)f->priv;
    *addr = (uintptr_t)block_addr(&s_fs, rd->e->block);
    *size = rd->e->size;
    return 0;
}

static int r_close(struct file *f)
{
    struct reader *rd = (struct reader *)f->priv;
    struct fs *fs = &s_fs;
    mutex_lock(&fs->lock, WAIT_FOREVER);
    struct ent *e = rd->e;
    e->opens--;
    fs->opens--;
    if (!e->opens && e->gone)
    {
        e->gone = false; /* (drop_ent frees it now) */
        drop_ent(fs, e);
    }
    mutex_unlock(&fs->lock);
    kfree(rd);
    return 0;
}

static const struct file_ops s_writer_ops = {
    .write = w_write,
    .lseek = w_lseek,
    .ioctl = w_ioctl,
    .fstat = w_fstat,
    .close = w_close,
};

static const struct file_ops s_reader_ops = {
    .read = r_read,
    .lseek = r_lseek,
    .fstat = r_fstat,
    .close = r_close,
    .xip = r_xip,
};

static int fs_open(void *vfs, const char *rel, uint32_t flags, struct file *f)
{
    struct fs *fs = (struct fs *)vfs;
    if (strlen(rel) >= NAME_LEN)
        return -ENAMETOOLONG;
    if (!strcmp(rel, "/"))
        return -EISDIR;
    mutex_lock(&fs->lock, WAIT_FOREVER);
    struct ent *e = find(fs, rel);
    int r = 0;
    bool writing = (flags & VFS_O_ACCMODE) != VFS_O_RDONLY;
    if (e && e->type == R_DIR)
    {
        r = -EISDIR;
    }
    else if (!writing)
    {
        if (!e)
        {
            r = -ENOENT;
        }
        else
        {
            struct reader *rd = (struct reader *)kzalloc(sizeof(*rd), KM_ANY);
            if (!rd)
            {
                r = -ENOMEM;
            }
            else
            {
                rd->e = e;
                e->opens++;
                fs->opens++;
                f->ops = &s_reader_ops;
                f->priv = rd;
            }
        }
    }
    else if (e && !(flags & VFS_O_TRUNC))
    {
        r = -EINVAL; /* written once: only anew (O_TRUNC) */
    }
    else if (!e && !(flags & VFS_O_CREAT))
    {
        r = -ENOENT;
    }
    else if (e && (flags & VFS_O_CREAT) && (flags & VFS_O_EXCL))
    {
        r = -EEXIST;
    }
    else if (!parent_is_dir(fs, rel))
    {
        r = -ENOENT;
    }
    else if (!e && fs->nents >= MAX_ENTRIES)
    {
        r = -ENOSPC;
    }
    else
    {
        struct writer *w = (struct writer *)kzalloc(sizeof(*w), KM_ANY);
        uint32_t first, n = largest_free(fs, &first);
        if (!w)
        {
            r = -ENOMEM;
        }
        else
        {
            w->fs = fs;
            strcpy(w->name, rel);
            w->first = first;
            w->avail = n;
            set_blocks(fs, first, n, B_WRITING);
            fs->opens++;
            f->ops = &s_writer_ops;
            f->priv = w;
        }
    }
    mutex_unlock(&fs->lock);
    return r;
}

static int d_readdir(struct file *f, struct vfs_dirent *de)
{
    struct dirh *d = (struct dirh *)f->priv;
    struct fs *fs = &s_fs;
    size_t l = strlen(d->dir);
    mutex_lock(&fs->lock, WAIT_FOREVER);
    int r = 0;
    while (d->index < fs->nents)
    {
        const struct ent *e = fs->ents[d->index++];
        const char *n = e->name;
        /* a direct child: "<dir>/<name>" with no further "/" */
        if (e->gone || strncmp(n, d->dir, l) || n[l] != '/' || strchr(n + l + 1, '/'))
            continue;
        memset(de, 0, sizeof(*de));
        de->mode = e->type == R_DIR ? VFS_S_IFDIR : VFS_S_IFREG;
        de->size = e->type == R_FILE ? e->size : 0;
        strncpy(de->name, n + l + 1, VFS_NAME_MAX);
        r = 1;
        break;
    }
    mutex_unlock(&fs->lock);
    return r;
}

static int d_close(struct file *f)
{
    kfree(f->priv);
    return 0;
}

static const struct file_ops s_dir_ops = {
    .readdir = d_readdir,
    .close = d_close,
};

static int fs_opendir(void *vfs, const char *rel, struct file *f)
{
    struct fs *fs = (struct fs *)vfs;
    mutex_lock(&fs->lock, WAIT_FOREVER);
    struct ent *e = strcmp(rel, "/") ? find(fs, rel) : NULL;
    int r = strcmp(rel, "/") && !e ? -ENOENT : e && e->type != R_DIR ? -ENOTDIR
                                                                     : 0;
    mutex_unlock(&fs->lock);
    if (r)
        return r;
    struct dirh *d = (struct dirh *)kzalloc(sizeof(*d), KM_ANY);
    if (!d)
        return -ENOMEM;
    strncpy(d->dir, strcmp(rel, "/") ? rel : "", NAME_LEN - 1);
    f->ops = &s_dir_ops;
    f->priv = d;
    return 0;
}

static int fs_stat(void *vfs, const char *rel, struct vfs_stat *st)
{
    struct fs *fs = (struct fs *)vfs;
    memset(st, 0, sizeof(*st));
    if (!strcmp(rel, "/"))
    {
        st->mode = VFS_S_IFDIR;
        return 0;
    }
    mutex_lock(&fs->lock, WAIT_FOREVER);
    struct ent *e = find(fs, rel);
    if (e)
    {
        st->mode = e->type == R_DIR ? VFS_S_IFDIR : VFS_S_IFREG;
        st->size = e->type == R_FILE ? e->size : 0;
        st->mtime = e->mtime;
    }
    mutex_unlock(&fs->lock);
    return e ? 0 : -ENOENT;
}

static int fs_mkdir(void *vfs, const char *rel)
{
    struct fs *fs = (struct fs *)vfs;
    if (strlen(rel) >= NAME_LEN)
        return -ENAMETOOLONG;
    if (!strcmp(rel, "/"))
        return -EEXIST;
    mutex_lock(&fs->lock, WAIT_FOREVER);
    int r = find(fs, rel) ? -EEXIST : !parent_is_dir(fs, rel) ? -ENOENT
                                  : fs->nents >= MAX_ENTRIES  ? -ENOSPC
                                                              : 0;
    if (!r)
    {
        struct rec *rec = (struct rec *)kmalloc(sizeof(*rec), KM_ANY);
        if (!rec)
        {
            r = -ENOMEM;
        }
        else
        {
            make_rec(rec, R_DIR, fs->next_id, rel);
            r = append(fs, rec);
            if (!r)
                apply(fs, rec);
            kfree(rec);
        }
    }
    mutex_unlock(&fs->lock);
    return r;
}

static int fs_unlink(void *vfs, const char *rel)
{
    struct fs *fs = (struct fs *)vfs;
    mutex_lock(&fs->lock, WAIT_FOREVER);
    struct ent *e = find(fs, rel);
    int r = !e ? -ENOENT : e->type == R_DIR && has_children(fs, rel) ? -ENOTEMPTY
                                                                     : 0;
    if (!r)
    {
        struct rec *rec = (struct rec *)kmalloc(sizeof(*rec), KM_ANY);
        if (!rec)
        {
            r = -ENOMEM;
        }
        else
        {
            make_rec(rec, R_DEL, e->id, NULL);
            r = append(fs, rec);
            if (!r)
                drop_ent(fs, e);
            kfree(rec);
        }
    }
    mutex_unlock(&fs->lock);
    return r;
}

static int fs_rename(void *vfs, const char *from, const char *to)
{
    struct fs *fs = (struct fs *)vfs;
    if (strlen(to) >= NAME_LEN)
        return -ENAMETOOLONG;
    mutex_lock(&fs->lock, WAIT_FOREVER);
    struct ent *e = find(fs, from), *old = find(fs, to);
    int r = 0;
    if (!e)
        r = -ENOENT;
    else if (e->type == R_DIR && has_children(fs, from))
        r = -EOPNOTSUPP; /* (every entry below would need a record of its own) */
    else if (old && (old->type == R_DIR || e->type == R_DIR))
        r = -EEXIST;
    else if (!parent_is_dir(fs, to))
        r = -ENOENT;
    if (!r && e != old)
    {
        struct rec *rec = (struct rec *)kmalloc(sizeof(*rec), KM_ANY);
        if (!rec)
        {
            r = -ENOMEM;
        }
        else
        {
            make_rec(rec, R_RENAME, e->id, to);
            r = append(fs, rec);
            if (!r)
                apply(fs, rec); /* (a file of that name is replaced) */
            kfree(rec);
        }
    }
    mutex_unlock(&fs->lock);
    return r;
}

static int fs_statfs(void *vfs, uint64_t *total, uint64_t *free)
{
    struct fs *fs = (struct fs *)vfs;
    mutex_lock(&fs->lock, WAIT_FOREVER);
    uint32_t n = 0;
    for (uint32_t b = LOG_BLOCKS; b < fs->nblocks; b++)
        n += fs->bmap[b] == B_FREE;
    mutex_unlock(&fs->lock);
    *total = (uint64_t)(fs->nblocks - LOG_BLOCKS) * fs->geo.erasesize;
    *free = (uint64_t)n * fs->geo.erasesize;
    return 0;
}

static const struct vfs_fs_ops s_fs_ops = {
    fs_open,
    fs_opendir,
    fs_stat,
    fs_mkdir,
    fs_unlink,
    fs_rename,
    fs_statfs,
};

/* ---- mounting, formatting --------------------------------------------------------------------- */

static int mount(struct fs *fs)
{
    int r = replay(fs);
    if (r)
        return r;
    r = vfs_mount(fs->mountpoint, &s_fs_ops, fs);
    if (r)
        return r;
    fs->mounted = true;
    uint64_t total, free;
    fs_statfs(fs, &total, &free);
    uint32_t files = 0;
    for (uint32_t i = 0; i < fs->nents; i++)
        files += fs->ents[i]->type == R_FILE;
    printk("flashfs: %s: %lu files, %lu KB free of %lu KB (log generation %lu, %lu of %lu slots)\n",
           fs->mountpoint, (unsigned long)files, (unsigned long)(free / 1024u), (unsigned long)(total / 1024u),
           (unsigned long)fs->seq, (unsigned long)fs->next_slot, (unsigned long)slots(fs));
    return 0;
}

static int format(struct fs *fs)
{
    if (fs->opens)
        return -EBUSY;
    if (fs->mounted)
    {
        int r = vfs_umount(fs->mountpoint);
        if (r)
            return r;
        fs->mounted = false;
    }
    free_all(fs);
    for (uint32_t b = 0; b < LOG_BLOCKS; b++)
    {
        int r = mtd_part_erase(fs->part, b * fs->geo.erasesize, fs->geo.erasesize);
        if (r)
            return r;
    }
    struct head h;
    memset(&h, 0xFF, sizeof(h));
    h.magic = FS_MAGIC;
    h.version = FS_VERSION;
    h.seq = 1;
    h.erasesize = fs->geo.erasesize;
    h.blocks = fs->nblocks;
    h.crc = head_crc(&h);
    int r = mtd_part_program(fs->part, 0, &h, sizeof(h));
    if (r)
        return r;
    printk("flashfs: %s formatted (%lu blocks of %lu KB)\n", fs->mountpoint, (unsigned long)fs->nblocks,
           (unsigned long)(fs->geo.erasesize / 1024u));
    return mount(fs);
}

/* every file's data against its CRC; the number of damaged ones */
static int check(struct fs *fs)
{
    int bad = 0;
    for (uint32_t i = 0; i < fs->nents; i++)
    {
        const struct ent *e = fs->ents[i];
        if (e->type != R_FILE || e->gone)
            continue;
        if (crc32(0, block_addr(fs, e->block), e->size) != e->dcrc)
        {
            printk("flashfs: %s%s: data damaged\n", fs->mountpoint, e->name);
            bad++;
        }
    }
    return bad;
}

static int ctl_ioctl(struct file *f, unsigned cmd, void *arg)
{
    struct fs *fs = &s_fs;
    (void)f;
    switch (cmd)
    {
    case FLASHFS_IOC_INFO:
    {
        struct flashfs_info *in = (struct flashfs_info *)arg;
        memset(in, 0, sizeof(*in));
        mutex_lock(&fs->lock, WAIT_FOREVER);
        in->state = fs->mounted ? FLASHFS_STATE_MOUNTED : FLASHFS_STATE_UNFORMATTED;
        in->erasesize = fs->geo.erasesize;
        in->blocks = fs->nblocks - LOG_BLOCKS;
        for (uint32_t b = LOG_BLOCKS; b < fs->nblocks; b++)
            in->free_blocks += fs->bmap[b] == B_FREE;
        uint32_t first;
        in->largest_free = largest_free(fs, &first);
        for (uint32_t i = 0; i < fs->nents; i++)
        {
            if (fs->ents[i]->gone)
                continue;
            if (fs->ents[i]->type == R_FILE)
                in->files++;
            else
                in->dirs++;
        }
        in->log_seq = fs->seq;
        in->log_used = fs->next_slot;
        in->log_slots = slots(fs);
        in->open_files = fs->opens;
        mutex_unlock(&fs->lock);
        return 0;
    }
    case FLASHFS_IOC_FILE:
    {
        struct flashfs_file *ff = (struct flashfs_file *)arg;
        ff->path[sizeof(ff->path) - 1] = 0;
        mutex_lock(&fs->lock, WAIT_FOREVER);
        const struct ent *e = fs->mounted ? find(fs, ff->path) : NULL;
        if (e && e->type == R_FILE)
        {
            ff->block = e->block;
            ff->blocks = e->nblocks;
            ff->size = e->size;
            ff->crc32 = e->dcrc;
            ff->address = (uint32_t)(uintptr_t)block_addr(fs, e->block);
            ff->opens = e->opens;
        }
        mutex_unlock(&fs->lock);
        return e ? (e->type == R_FILE ? 0 : -EISDIR) : -ENOENT;
    }
    case FLASHFS_IOC_CHECK:
    {
        mutex_lock(&fs->lock, WAIT_FOREVER);
        int r = fs->mounted ? check(fs) : -ENODEV;
        mutex_unlock(&fs->lock);
        return r;
    }
    case FLASHFS_IOC_FORMAT:
    {
        if (!capable(CAP_SYS))
            return -EPERM;
        mutex_lock(&fs->lock, WAIT_FOREVER);
        int r = format(fs);
        mutex_unlock(&fs->lock);
        return r;
    }
    default:
        return -ENOTTY;
    }
}

static const struct file_ops s_ctl_ops = {
    .ioctl = ctl_ioctl,
};

/* ---- the device ----------------------------------------------------------------------------- */

static int flashfs_probe(struct device *dev)
{
    struct fs *fs = &s_fs;
    struct device_node *pn = of_parse_phandle(dev->of_node, "partition", 0);
    fs->part = pn ? mtd_part_of_node(pn) : NULL;
    if (!fs->part)
    {
        dev_info(dev, "no flash partition (property \"partition\")\n");
        return -ENODEV;
    }
    fs->mountpoint = "/flash0";
    of_property_read_string(dev->of_node, "mountpoint", &fs->mountpoint);
    mtd_part_geometry(fs->part, &fs->geo);
    fs->nblocks = fs->geo.size / fs->geo.erasesize;
    if (fs->nblocks < LOG_BLOCKS + 1 || fs->geo.erasesize % SLOT)
        return -EINVAL;
    int r = mtd_part_claim(fs->part, "flashfs");
    if (r)
        return r;
    mutex_init(&fs->lock);
    fs->bmap = (uint8_t *)kzalloc(fs->nblocks, KM_ANY);
    if (!fs->bmap)
    {
        mtd_part_release(fs->part);
        return -ENOMEM;
    }
    r = mount(fs);
    if (r == -ENODEV)
        dev_info(dev, "%s: no file system on the partition (\"flashfs format\" makes one)\n", fs->mountpoint);
    else if (r)
        dev_info(dev, "%s: mount failed (%d)\n", fs->mountpoint, r);
    devfs_register("flashfs0", &s_ctl_ops, fs);
    return 0;
}

static void flashfs_remove(struct device *dev)
{
    struct fs *fs = &s_fs;
    (void)dev;
    devfs_unregister("flashfs0");
    if (fs->mounted)
        vfs_umount(fs->mountpoint);
    fs->mounted = false;
    free_all(fs);
    kfree(fs->bmap);
    fs->bmap = NULL;
    mtd_part_release(fs->part);
}

static const struct of_device_id flashfs_ids[] = {
    {"crtos,flashfs", NULL},
    {NULL, NULL},
};

static struct driver flashfs_driver = {
    .name = "flashfs",
    .of_match_table = flashfs_ids,
    .probe = flashfs_probe,
    .remove = flashfs_remove,
};

static int init(void)
{
    return driver_register(&flashfs_driver);
}

static void fini(void)
{
    driver_unregister(&flashfs_driver);
}

MODULE_DEPENDS("flashfs", "file system on a flash partition (/flash0), files run in place", "flexspi-mtd", init, fini);
