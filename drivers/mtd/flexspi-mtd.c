/*
 * flexspi-mtd.ko - the HyperFlash on the FlexSPI (the board's 64 MB boot flash, S26KS512S)
 * as /dev/mtd<n>, one per partition of the device tree (crtos/mtd.h).
 *
 * The kernel runs from this flash (XIP), and while the flash erases or programs it cannot be
 * read. Those steps therefore run from RAM (this module lives in OCRAM) with every interrupt
 * masked and both caches off (as NXP's example does: no instruction may come from the flash
 * meanwhile, not even a speculative one into a cache). A 512-byte page takes about half a
 * millisecond, a 256 KB erase block well under a second - the whole system stands still for
 * that long. Reading goes through the memory-mapped window at 0x60000000.
 *
 * The boot ROM's configuration of the FlexSPI stays (its read sequence 0 serves XIP); the
 * commands for status, write enable, erase and program go into free LUT sequences. Programming
 * runs at a quarter of the clock (the HyperFlash loads program data at most at 50 MHz) and
 * the clock and DLL come back to exactly what they were.
 *
 * MTD_IOC_KERNEL_UPDATE on the kernel partition writes a new kernel image (see crtos/mtd.h)
 * and restarts the board from RAM: the old kernel's code never runs again in between.
 *
 * Other modules (the flashfs file system) use a partition through mtd_part_*() (crtos/mtd.h);
 * one that claims it keeps /dev/mtd<n> from writing there. After every program or erase the
 * FlexSPI's read buffers are reset and the caches come back invalidated, so reads through the
 * mapped window - and code run from there - see the new contents.
 */
#include <string.h>
#include <crtos/arch.h>
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/module.h>
#include <crtos/mm.h>
#include <crtos/mtd.h>
#include <crtos/of.h>
#include <crtos/printk.h>
#include <crtos/sched.h>
#include <crtos/sync.h>
#include <crtos/syscall.h>
#include <crtos/uaccess.h>
#include <crtos/vfs.h>
#include "fsl_clock.h"
#include "fsl_flexspi.h"

uint32_t crc32(uint32_t crc, const void *data, size_t len); /* the kernel's */

#define AMBA 0x60000000u
#define SECTOR 0x40000u /* erase block: 256 KB */
#define PAGE 512u       /* program burst */
#define MAX_PARTS 4
#define BUSY_TIMEOUT_S 5u
#define IMAGE_TAG_FCFB 0x42464346u /* FlexSPI configuration block ("FCFB") at offset 0 */
#define IVT_OFFSET 0x1000u
#define IVT_TAG 0xD1u

/* HyperFlash command sequences, from NXP's evkbimxrt1050 flexspi hyper_flash example
 * (Copyright 2016-2020, 2024-2025 NXP, BSD-3-Clause); they go to LUT sequences 2..11 */
enum
{
    SEQ_FIRST = 2,
    SEQ_READSTATUS = 2,
    SEQ_WRITEENABLE = 4,
    SEQ_ERASESECTOR = 6,
    SEQ_PAGEPROGRAM = 10,
    SEQ_END = 12
};
#define LUT(seq, i) [4 * ((seq) - SEQ_FIRST) + (i)]
#define D8(v) kFLEXSPI_Command_DDR, kFLEXSPI_8PAD, (v)
#define LSEQ(...) FLEXSPI_LUT_SEQ(__VA_ARGS__) /* (D8 expands to three arguments first) */
static const uint32_t s_lut[4 * (SEQ_END - SEQ_FIRST)] = {
    /* read status: 0x70 to 0x555, then read the status word */
    LUT(SEQ_READSTATUS, 0) = LSEQ(D8(0x00), D8(0x00)),
    LUT(SEQ_READSTATUS, 1) = LSEQ(D8(0x00), D8(0xAA)),
    LUT(SEQ_READSTATUS, 2) = LSEQ(D8(0x00), D8(0x05)),
    LUT(SEQ_READSTATUS, 3) = LSEQ(D8(0x00), D8(0x70)),
    LUT(SEQ_READSTATUS, 4) = LSEQ(D8(0xA0), kFLEXSPI_Command_RADDR_DDR, kFLEXSPI_8PAD, 0x18),
    LUT(SEQ_READSTATUS, 5) = LSEQ(kFLEXSPI_Command_CADDR_DDR, kFLEXSPI_8PAD, 0x10,
                                  kFLEXSPI_Command_DUMMY_RWDS_DDR, kFLEXSPI_8PAD, 0x0B),
    LUT(SEQ_READSTATUS, 6) = LSEQ(kFLEXSPI_Command_READ_DDR, kFLEXSPI_8PAD, 0x04,
                                  kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x0),
    /* write enable: the unlock cycles 0xAA to 0x555, 0x55 to 0x2AA */
    LUT(SEQ_WRITEENABLE, 0) = LSEQ(D8(0x20), D8(0x00)),
    LUT(SEQ_WRITEENABLE, 1) = LSEQ(D8(0x00), D8(0xAA)),
    LUT(SEQ_WRITEENABLE, 2) = LSEQ(D8(0x00), D8(0x05)),
    LUT(SEQ_WRITEENABLE, 3) = LSEQ(D8(0x00), D8(0xAA)),
    LUT(SEQ_WRITEENABLE, 4) = LSEQ(D8(0x20), D8(0x00)),
    LUT(SEQ_WRITEENABLE, 5) = LSEQ(D8(0x00), D8(0x55)),
    LUT(SEQ_WRITEENABLE, 6) = LSEQ(D8(0x00), D8(0x02)),
    LUT(SEQ_WRITEENABLE, 7) = LSEQ(D8(0x00), D8(0x55)),
    /* sector erase: 0x80 to 0x555, unlock again, 0x30 to the sector */
    LUT(SEQ_ERASESECTOR, 0) = LSEQ(D8(0x00), D8(0x00)),
    LUT(SEQ_ERASESECTOR, 1) = LSEQ(D8(0x00), D8(0xAA)),
    LUT(SEQ_ERASESECTOR, 2) = LSEQ(D8(0x00), D8(0x05)),
    LUT(SEQ_ERASESECTOR, 3) = LSEQ(D8(0x00), D8(0x80)),
    LUT(SEQ_ERASESECTOR, 4) = LSEQ(D8(0x00), D8(0x00)),
    LUT(SEQ_ERASESECTOR, 5) = LSEQ(D8(0x00), D8(0xAA)),
    LUT(SEQ_ERASESECTOR, 6) = LSEQ(D8(0x00), D8(0x05)),
    LUT(SEQ_ERASESECTOR, 7) = LSEQ(D8(0x00), D8(0xAA)),
    LUT(SEQ_ERASESECTOR, 8) = LSEQ(D8(0x00), D8(0x00)),
    LUT(SEQ_ERASESECTOR, 9) = LSEQ(D8(0x00), D8(0x55)),
    LUT(SEQ_ERASESECTOR, 10) = LSEQ(D8(0x00), D8(0x02)),
    LUT(SEQ_ERASESECTOR, 11) = LSEQ(D8(0x00), D8(0x55)),
    LUT(SEQ_ERASESECTOR, 12) = LSEQ(D8(0x00), kFLEXSPI_Command_RADDR_DDR, kFLEXSPI_8PAD, 0x18),
    LUT(SEQ_ERASESECTOR, 13) = LSEQ(kFLEXSPI_Command_CADDR_DDR, kFLEXSPI_8PAD, 0x10, D8(0x00)),
    LUT(SEQ_ERASESECTOR, 14) = LSEQ(D8(0x30), kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0x00),
    /* word program: 0xA0 to 0x555, then a burst of up to 512 bytes to the address */
    LUT(SEQ_PAGEPROGRAM, 0) = LSEQ(D8(0x20), D8(0x00)),
    LUT(SEQ_PAGEPROGRAM, 1) = LSEQ(D8(0x00), D8(0xAA)),
    LUT(SEQ_PAGEPROGRAM, 2) = LSEQ(D8(0x00), D8(0x05)),
    LUT(SEQ_PAGEPROGRAM, 3) = LSEQ(D8(0x00), D8(0xA0)),
    LUT(SEQ_PAGEPROGRAM, 4) = LSEQ(D8(0x20), kFLEXSPI_Command_RADDR_DDR, kFLEXSPI_8PAD, 0x18),
    LUT(SEQ_PAGEPROGRAM, 5) = LSEQ(kFLEXSPI_Command_CADDR_DDR, kFLEXSPI_8PAD, 0x10,
                                   kFLEXSPI_Command_WRITE_DDR, kFLEXSPI_8PAD, 0x80),
};

struct mtd;

struct mtd_part
{
    struct mtd *m;
    char name[8]; /* mtd<n> */
    char label[16];
    uint32_t off, size;
    bool ro, kernel;
    const struct device_node *np;
    const char *claimed; /* by a module (mtd_part_claim), NULL: free */
};

struct mtd
{
    struct device *dev;
    FLEXSPI_Type *base;
    uint32_t podf, root_hz; /* the clock as the boot ROM set it */
    uint32_t slow_podf, slow_hz;
    struct mutex lock;
    struct mtd_part parts[MAX_PARTS];
    int nparts;
    uint32_t page[PAGE / 4];
};

struct opened
{
    struct mtd_part *p;
    uint32_t pos;
};

/* ---- flash operations: RAM only, interrupts masked, caches off ---------------------------- */

struct quiet
{
    uint32_t primask;
    bool dc, ic;
};

static void quiet_enter(struct quiet *q)
{
    q->primask = __get_PRIMASK();
    __disable_irq();
    q->ic = (SCB->CCR & SCB_CCR_IC_Msk) != 0;
    q->dc = (SCB->CCR & SCB_CCR_DC_Msk) != 0;
    if (q->ic)
        SCB_DisableICache();
    if (q->dc)
        SCB_DisableDCache(); /* (cleans it) */
}

static void quiet_leave(struct quiet *q)
{
    if (q->dc)
        SCB_EnableDCache(); /* (invalidates it: no stale flash data) */
    if (q->ic)
        SCB_EnableICache();
    __set_PRIMASK(q->primask);
}

static status_t ip(FLEXSPI_Type *b, uint32_t addr, flexspi_command_type_t type, uint8_t seq, uint8_t nseq, uint32_t *data,
                   size_t size)
{
    flexspi_transfer_t x;
    x.deviceAddress = addr;
    x.port = kFLEXSPI_PortA1;
    x.cmdType = type;
    x.seqIndex = seq;
    x.SeqNumber = nseq;
    x.data = data;
    x.dataSize = size;
    return FLEXSPI_TransferBlocking(b, &x);
}

static status_t wait_ready(FLEXSPI_Type *b)
{
    uint32_t t0 = cpu_cycles(), limit = SystemCoreClock * BUSY_TIMEOUT_S;
    for (;;)
    {
        uint32_t v = 0;
        status_t st = ip(b, 0, kFLEXSPI_Read, SEQ_READSTATUS, 2, &v, 2);
        if (st != kStatus_Success)
            return st;
        if (v & 0x8000u)                                           /* device ready */
            return (v & 0x3200u) ? kStatus_Fail : kStatus_Success; /* erase/program/lock errors */
        if (cpu_cycles() - t0 > limit)
            return kStatus_Timeout;
    }
}

static void set_clock(struct mtd *m, uint32_t podf, uint32_t root_hz)
{
    FLEXSPI_Type *b = m->base;
    while (!FLEXSPI_GetBusIdleStatus(b))
    {
    }
    FLEXSPI_Enable(b, false);
    CLOCK_DisableClock(kCLOCK_FlexSpi);
    CLOCK_SetDiv(kCLOCK_FlexspiDiv, podf);
    CLOCK_EnableClock(kCLOCK_FlexSpi);
    FLEXSPI_Enable(b, true);
    flexspi_device_config_t dc;
    memset(&dc, 0, sizeof(dc));
    dc.flexspiRootClk = root_hz;
    dc.isSck2Enabled = false;
    dc.dataValidTime = 1;
    FLEXSPI_UpdateDllValue(b, &dc, kFLEXSPI_PortA1);
    FLEXSPI_SoftwareReset(b);
}

static status_t erase_sector(struct mtd *m, uint32_t addr)
{
    status_t st = ip(m->base, addr, kFLEXSPI_Command, SEQ_WRITEENABLE, 2, NULL, 0);
    if (st == kStatus_Success)
        st = ip(m->base, addr, kFLEXSPI_Command, SEQ_ERASESECTOR, 4, NULL, 0);
    if (st == kStatus_Success)
        st = wait_ready(m->base);
    FLEXSPI_SoftwareReset(m->base); /* (the AHB buffers may hold the old data) */
    return st;
}

/* @len bytes (even, within one 512-byte page) from m->page */
static status_t program(struct mtd *m, uint32_t addr, uint32_t len)
{
    set_clock(m, m->slow_podf, m->slow_hz);
    status_t st = ip(m->base, addr, kFLEXSPI_Command, SEQ_WRITEENABLE, 2, NULL, 0);
    if (st == kStatus_Success)
        st = ip(m->base, addr, kFLEXSPI_Write, SEQ_PAGEPROGRAM, 2, m->page, len);
    if (st == kStatus_Success)
        st = wait_ready(m->base);
    set_clock(m, m->podf, m->root_hz);
    FLEXSPI_SoftwareReset(m->base); /* (the AHB buffers may hold the old data) */
    return st;
}

static int to_errno(status_t st)
{
    return st == kStatus_Success ? 0 : st == kStatus_Timeout ? -ETIMEDOUT
                                                             : -EIO;
}

/* ---- the kernel update: from RAM to the restart ----------------------------------------------- */

static void uart_say(const char *s)
{
    for (; *s; s++)
    {
        while (!(LPUART1->STAT & LPUART_STAT_TDRE_MASK))
        {
        }
        LPUART1->DATA = (uint8_t)*s;
    }
}

static bool same(const uint8_t *img, uint32_t len)
{
    const volatile uint8_t *f = (const volatile uint8_t *)AMBA;
    for (uint32_t i = 0; i < len; i++)
        if (f[i] != img[i])
            return false;
    return true;
}

static void __attribute__((noreturn)) kernel_update(struct mtd *m, const uint8_t *img, uint32_t len)
{
    struct quiet q;
    quiet_enter(&q); /* for good: this kernel does not run again */
    for (int attempt = 0; attempt < 3; attempt++)
    {
        bool ok = true;
        for (uint32_t s = 0; s < len && ok; s += SECTOR)
            ok = erase_sector(m, s) == kStatus_Success;
        for (uint32_t a = 0; a < len && ok; a += PAGE)
        {
            uint32_t n = len - a < PAGE ? len - a : PAGE;
            memset(m->page, 0xFF, PAGE);
            memcpy(m->page, img + a, n);
            ok = program(m, a, (n + 1u) & ~1u) == kStatus_Success;
        }
        FLEXSPI_SoftwareReset(m->base);
        if (ok && same(img, len))
        {
            uart_say("\r\nmtd: kernel written and verified, restarting\r\n");
            NVIC_SystemReset();
        }
        uart_say("\r\nmtd: the kernel image did not verify, again\r\n");
    }
    for (;;)
    {
        uart_say("\r\n*** mtd: writing the kernel failed - the board needs \"crtos flash\" (debug probe)\r\n");
        for (volatile uint32_t d = 0; d < 200000000u; d++)
        {
        }
    }
}

static int kernel_update_ioctl(struct mtd_part *p, const struct mtd_kernel_update *ku)
{
    if (!capable(CAP_SYS))
        return -EPERM;
    const uint8_t *uimg = (const uint8_t *)(uintptr_t)ku->image;
    uint32_t len = ku->len;
    if (!p->kernel || len < IVT_OFFSET + 32u || len > p->size)
        return -EINVAL;
    if (!uaccess_ok(uimg, len, 0))
        return -EFAULT;
    uint8_t *img = kmalloc(len, KM_LARGE);
    if (!img)
        return -ENOMEM;
    memcpy(img, uimg, len);
    uint32_t tag;
    memcpy(&tag, img, 4);
    if (tag != IMAGE_TAG_FCFB || img[IVT_OFFSET] != IVT_TAG || crc32(0, img, len) != ku->crc32)
    {
        kfree(img);
        return -EINVAL; /* not a kernel image for this board, or damaged on the way */
    }
    printk("mtd: writing a new kernel (%lu bytes), the board restarts\n", (unsigned long)len);
    task_sleep_ms(300); /* the log reaches the console first */
    mutex_lock(&p->m->lock, WAIT_FOREVER);
    kernel_update(p->m, img, len);
}

/* ---- /dev/mtd<n> ------------------------------------------------------------------------------ */

static int mtd_open(struct file *f)
{
    struct opened *o = kzalloc(sizeof(*o), KM_ANY);
    if (!o)
        return -ENOMEM;
    o->p = f->dev;
    f->priv = o;
    return 0;
}

static int mtd_close(struct file *f)
{
    kfree(f->priv);
    return 0;
}

static int64_t mtd_lseek(struct file *f, int64_t off, int whence)
{
    struct opened *o = f->priv;
    int64_t pos = whence == VFS_SEEK_SET ? off : whence == VFS_SEEK_CUR ? (int64_t)o->pos + off
                                                                        : (int64_t)o->p->size + off;
    if (pos < 0 || pos > (int64_t)o->p->size)
        return -EINVAL;
    o->pos = (uint32_t)pos;
    return pos;
}

static int mtd_read(struct file *f, void *buf, size_t len)
{
    struct opened *o = f->priv;
    struct mtd_part *p = o->p;
    if (o->pos >= p->size)
        return 0;
    uint32_t n = p->size - o->pos < len ? p->size - o->pos : (uint32_t)len;
    if (mutex_lock(&p->m->lock, WAIT_FOREVER))
        return -EINTR;
    memcpy(buf, (const void *)(AMBA + p->off + o->pos), n);
    mutex_unlock(&p->m->lock);
    o->pos += n;
    return (int)n;
}

/* @total bytes to flash address @addr (from the start of the flash), page by page; the lock
 * is held. The bytes written so far, or <0 when nothing was. */
static int program_range(struct mtd *m, uint32_t addr, const uint8_t *buf, uint32_t total)
{
    uint32_t done = 0;
    int r = 0;
    while (done < total && !r)
    {
        /* one page at most, from an even address to an even end: 0xFF around what is
         * written changes no bit */
        uint32_t a = addr + done;
        uint32_t start = a & ~1u, page_end = (a & ~(PAGE - 1u)) + PAGE;
        uint32_t n = total - done < page_end - a ? total - done : page_end - a;
        uint32_t end = (a + n + 1u) & ~1u;
        memset(m->page, 0xFF, PAGE);
        memcpy((uint8_t *)m->page + (a - start), buf + done, n);
        struct quiet q;
        quiet_enter(&q);
        status_t st = program(m, start, end - start);
        quiet_leave(&q);
        r = to_errno(st);
        if (!r)
            done += n;
    }
    return done ? (int)done : r;
}

static int mtd_write(struct file *f, const void *buf, size_t len)
{
    struct opened *o = f->priv;
    struct mtd_part *p = o->p;
    struct mtd *m = p->m;
    if (p->ro)
        return -EROFS;
    if (p->claimed)
        return -EBUSY;
    if (o->pos >= p->size)
        return len ? -ENOSPC : 0;
    uint32_t total = p->size - o->pos < len ? p->size - o->pos : (uint32_t)len;
    if (mutex_lock(&m->lock, WAIT_FOREVER))
        return -EINTR;
    int r = program_range(m, p->off + o->pos, (const uint8_t *)buf, total);
    mutex_unlock(&m->lock);
    if (r > 0)
        o->pos += (uint32_t)r;
    return r;
}

static int mtd_ioctl(struct file *f, unsigned cmd, void *arg)
{
    struct mtd_part *p = ((struct opened *)f->priv)->p;
    switch (cmd)
    {
    case MEMGETINFO:
    {
        struct mtd_info_user *mi = arg;
        memset(mi, 0, sizeof(*mi));
        mi->type = MTD_NORFLASH;
        mi->flags = p->ro ? 0 : MTD_WRITEABLE | MTD_BIT_WRITEABLE;
        mi->size = p->size;
        mi->erasesize = SECTOR;
        mi->writesize = 1;
        return 0;
    }
    case MEMERASE:
    {
        const struct erase_info_user *e = arg;
        if (p->ro)
            return -EROFS;
        if (p->claimed)
            return -EBUSY;
        if ((e->start | e->length) % SECTOR || !e->length || e->start > p->size || e->length > p->size - e->start)
            return -EINVAL;
        if (mutex_lock(&p->m->lock, WAIT_FOREVER))
            return -EINTR;
        int r = 0;
        for (uint32_t s = 0; s < e->length && !r; s += SECTOR)
        {
            struct quiet q;
            quiet_enter(&q);
            status_t st = erase_sector(p->m, p->off + e->start + s);
            quiet_leave(&q);
            r = to_errno(st);
        }
        mutex_unlock(&p->m->lock);
        return r;
    }
    case MTD_IOC_KERNEL_UPDATE:
        return kernel_update_ioctl(p, arg);
    default:
        return -ENOTTY;
    }
}

static int mtd_fstat(struct file *f, struct vfs_stat *st)
{
    struct mtd_part *p = ((struct opened *)f->priv)->p;
    memset(st, 0, sizeof(*st));
    st->mode = VFS_S_IFCHR;
    st->size = p->size;
    return 0;
}

static const struct file_ops mtd_ops = {
    .open = mtd_open,
    .read = mtd_read,
    .write = mtd_write,
    .lseek = mtd_lseek,
    .ioctl = mtd_ioctl,
    .fstat = mtd_fstat,
    .close = mtd_close,
};

/* ---- for other modules (crtos/mtd.h) ------------------------------------------------------------- */

static struct mtd *s_mtd; /* the one flash (a module using it keeps this one loaded) */

struct mtd_part *mtd_part_of_node(const struct device_node *np)
{
    struct mtd *m = s_mtd;
    for (int i = 0; m && i < m->nparts; i++)
        if (m->parts[i].np == np)
            return &m->parts[i];
    return NULL;
}
EXPORT_SYMBOL(mtd_part_of_node);

int mtd_part_geometry(struct mtd_part *p, struct mtd_geometry *g)
{
    g->size = p->size;
    g->erasesize = SECTOR;
    g->mapped = (const uint8_t *)(AMBA + p->off);
    return 0;
}
EXPORT_SYMBOL(mtd_part_geometry);

int mtd_part_claim(struct mtd_part *p, const char *owner)
{
    if (mutex_lock(&p->m->lock, WAIT_FOREVER))
        return -EINTR;
    int r = p->claimed ? -EBUSY : p->ro ? -EROFS
                                        : 0;
    if (!r)
        p->claimed = owner;
    mutex_unlock(&p->m->lock);
    return r;
}
EXPORT_SYMBOL(mtd_part_claim);

void mtd_part_release(struct mtd_part *p)
{
    mutex_lock(&p->m->lock, WAIT_FOREVER);
    p->claimed = NULL;
    mutex_unlock(&p->m->lock);
}
EXPORT_SYMBOL(mtd_part_release);

int mtd_part_program(struct mtd_part *p, uint32_t off, const void *buf, size_t len)
{
    if (p->ro)
        return -EROFS;
    if (off > p->size || len > p->size - off)
        return -EINVAL;
    if (!len)
        return 0;
    if (mutex_lock(&p->m->lock, WAIT_FOREVER))
        return -EINTR;
    int r = program_range(p->m, p->off + off, (const uint8_t *)buf, (uint32_t)len);
    mutex_unlock(&p->m->lock);
    return r == (int)len ? 0 : r < 0 ? r
                                     : -EIO;
}
EXPORT_SYMBOL(mtd_part_program);

int mtd_part_erase(struct mtd_part *p, uint32_t off, uint32_t len)
{
    if (p->ro)
        return -EROFS;
    if ((off | len) % SECTOR || !len || off > p->size || len > p->size - off)
        return -EINVAL;
    if (mutex_lock(&p->m->lock, WAIT_FOREVER))
        return -EINTR;
    int r = 0;
    for (uint32_t s = 0; s < len && !r; s += SECTOR)
    {
        struct quiet q;
        quiet_enter(&q);
        status_t st = erase_sector(p->m, p->off + off + s);
        quiet_leave(&q);
        r = to_errno(st);
    }
    mutex_unlock(&p->m->lock);
    return r;
}
EXPORT_SYMBOL(mtd_part_erase);

/* ---- the device --------------------------------------------------------------------------------- */

static void add_part(struct mtd *m, const struct device_node *np, const char *label, uint32_t off, uint32_t size,
                     bool ro)
{
    if (m->nparts >= MAX_PARTS)
        return;
    struct mtd_part *p = &m->parts[m->nparts];
    p->m = m;
    p->np = np;
    p->off = off;
    p->size = size;
    p->kernel = !strcmp(label, "kernel");
    p->ro = ro || p->kernel || off < SECTOR; /* the running kernel is never written directly */
    strncpy(p->label, label, sizeof(p->label) - 1);
    ksnprintf(p->name, sizeof(p->name), "mtd%d", m->nparts);
    if (devfs_register(p->name, &mtd_ops, p) == 0)
        m->nparts++;
}

static int mtd_probe(struct device *dev)
{
    struct device_node *flash = NULL, *np;
    for_each_child_of_node(dev->of_node, np) if (of_device_is_compatible(np, "cypress,s26ks512s") && of_device_is_available(np))
        flash = np;
    if (!flash)
        return -ENODEV;
    struct mtd *m = devm_kzalloc(dev, sizeof(*m), 0);
    if (!m)
        return -ENOMEM;
    m->dev = dev;
    m->base = device_map(dev, 0);
    if (!m->base)
        return -ENODEV;
    uint32_t size = 0x4000000u;
    of_property_read_u32(flash, "size", &size);
    mutex_init(&m->lock);

    /* the clock the boot ROM set, and a quarter of it for programming */
    uint32_t src = CLOCK_GetFreq(kCLOCK_Usb1PllPfd0Clk);
    m->podf = CLOCK_GetDiv(kCLOCK_FlexspiDiv);
    m->root_hz = src / (m->podf + 1u);
    m->slow_podf = m->podf + 1u < 4u ? 3u : m->podf;
    m->slow_hz = src / (m->slow_podf + 1u);
    struct quiet q;
    quiet_enter(&q);
    FLEXSPI_UpdateLUT(m->base, 4u * SEQ_FIRST, s_lut, sizeof(s_lut) / sizeof(s_lut[0]));
    quiet_leave(&q);

    dev_set_drvdata(dev, m);
    s_mtd = m;
    struct device_node *parts = of_get_child_by_name(flash, "partitions");
    if (parts)
    {
        for_each_child_of_node(parts, np)
        {
            uint32_t reg[2];
            const char *label = np->name;
            if (of_property_read_u32_array(np, "reg", reg, 2) || reg[0] >= size || reg[1] > size - reg[0])
                continue;
            of_property_read_string(np, "label", &label);
            add_part(m, np, label, reg[0], reg[1], of_property_read_bool(np, "read-only"));
        }
    }
    if (!m->nparts)
        add_part(m, flash, "flash", 0, size, false);
    for (int i = 0; i < m->nparts; i++)
        dev_info(dev, "/dev/%s: '%s' %lu KB at %08lx%s\n", m->parts[i].name, m->parts[i].label,
                 (unsigned long)(m->parts[i].size / 1024u), (unsigned long)(AMBA + m->parts[i].off),
                 m->parts[i].ro ? " (read-only)" : "");
    dev_info(dev, "HyperFlash %lu MB, FlexSPI %lu MHz (%lu MHz while programming)\n", (unsigned long)(size >> 20),
             (unsigned long)(m->root_hz / 2000000u), (unsigned long)(m->slow_hz / 2000000u));
    return 0;
}

static void mtd_remove(struct device *dev)
{
    struct mtd *m = dev_get_drvdata(dev);
    s_mtd = NULL;
    for (int i = 0; i < m->nparts; i++)
        devfs_unregister(m->parts[i].name);
}

static const struct of_device_id mtd_ids[] = {
    {"fsl,imxrt1050-flexspi", NULL},
    {NULL, NULL},
};

static struct driver mtd_driver = {
    .name = "flexspi-mtd",
    .of_match_table = mtd_ids,
    .probe = mtd_probe,
    .remove = mtd_remove,
};

static int init(void)
{
    return driver_register(&mtd_driver);
}

static void fini(void)
{
    driver_unregister(&mtd_driver);
}

MODULE("flexspi-mtd", "HyperFlash on the FlexSPI as /dev/mtd*; kernel updates", init, fini);
