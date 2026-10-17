/*
 * xspi-mtd.ko - the NOR flash of the STM32H7S78-DK (Macronix MX66UW1G45G, 128 MB on XSPI2) as
 * /dev/mtd<n>, one per partition of the device tree (crtos/mtd.h). The same interface as the
 * i.MX RT board's flexspi-mtd.ko: other modules (flashfs) use mtd_part_*().
 *
 * The kernel runs from this flash (XIP, memory-mapped at 0x70000000 by the boot loader), and
 * while the flash erases or programs it cannot be read. Those steps therefore run from RAM (this
 * module lives in AXI SRAM or the PSRAM) with every interrupt masked, both caches off and the
 * flash's MPU region made non-executable for the time (the core then does not even fetch from it
 * speculatively): XSPI2 leaves memory-mapped mode, the commands go out in indirect mode (octal
 * DTR, as the boot loader set the chip up), then the memory-mapped mode comes back with exactly
 * the boot loader's settings. A 256-byte page takes about 0.15 ms; an erase block (here 256 KB,
 * four of the chip's 64 KB blocks, the same unit as the HyperFlash of the other board) up to
 * about a second - the whole system stands still for that long.
 *
 * MTD_IOC_KERNEL_UPDATE on the kernel partition writes a new kernel image (checked first: the
 * header the boot loader checks, board/image.h, and the CRC-32 of the transfer) and restarts the
 * board from RAM: the old kernel's code never runs again in between.
 */
#include <stdbool.h>
#include <string.h>
#include <crtos/arch.h>
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
#include "stm32h7rsxx.h"
#include "image.h"

uint32_t crc32(uint32_t crc, const void *data, size_t len); /* the kernel's */

#define MAPPED 0x70000000u
#define SECTOR 0x40000u     /* the erase unit offered: 256 KB */
#define BLOCK 0x10000u      /* the chip's erase block: 64 KB */
#define PAGE 256u           /* the chip's program page */
#define MAX_PARTS 4
#define BUSY_TIMEOUT_S 5u

/* MX66UW1G45G commands in octal DTR (8D-8D-8D): the instruction and its complement */
#define CMD_WREN        0x06F9u
#define CMD_RDSR        0x05FAu
#define CMD_PP          0x12EDu
#define CMD_BE64K       0xDC23u
#define SR_WIP          0x01u
#define SR_WEL          0x02u
#define DUMMY_RDSR      5u

/* CCR fields */
#define CCR_IMODE_8     (4u << XSPI_CCR_IMODE_Pos)
#define CCR_IDTR        XSPI_CCR_IDTR
#define CCR_ISIZE_16    (1u << XSPI_CCR_ISIZE_Pos)
#define CCR_ADMODE_8    (4u << XSPI_CCR_ADMODE_Pos)
#define CCR_ADDTR       XSPI_CCR_ADDTR
#define CCR_ADSIZE_32   (3u << XSPI_CCR_ADSIZE_Pos)
#define CCR_DMODE_8     (4u << XSPI_CCR_DMODE_Pos)
#define CCR_DDTR        XSPI_CCR_DDTR
#define CCR_DQSE        XSPI_CCR_DQSE
#define CCR_INSTR       (CCR_IMODE_8 | CCR_IDTR | CCR_ISIZE_16)
#define CCR_ADDR        (CCR_ADMODE_8 | CCR_ADDTR | CCR_ADSIZE_32)
#define CCR_DATA        (CCR_DMODE_8 | CCR_DDTR)
#define FMODE_WRITE     (0u << XSPI_CR_FMODE_Pos)
#define FMODE_READ      (1u << XSPI_CR_FMODE_Pos)
#define FMODE_MAPPED    (3u << XSPI_CR_FMODE_Pos)

struct mtd;

struct mtd_part {
    struct mtd *m;
    char name[8];
    char label[16];
    uint32_t off, size;
    bool ro, kernel;
    const struct device_node *np;
    const char *claimed;
};

struct mtd {
    struct device *dev;
    XSPI_TypeDef *x;
    /* the memory-mapped setup of the boot loader, restored after every operation */
    uint32_t cr, ccr, tcr, ir, wccr, wtcr, wir;
    struct mutex lock;
    struct mtd_part parts[MAX_PARTS];
    int nparts;
    uint8_t page[PAGE];
};

struct opened {
    struct mtd_part *p;
    uint32_t pos;
};

/* ---- flash operations: RAM only, interrupts masked, caches off ---------------------------- */

struct quiet {
    uint32_t primask;
    bool dc, ic;
    int region;             /* the flash's MPU region, made XN meanwhile (-1: none) */
    uint32_t rasr;
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
    q->region = -1;
    for (int r = 1; r < 8; r++) {
        MPU->RNR = (uint32_t)r;
        if ((MPU->RBAR & ~31u) == MAPPED && (MPU->RASR & MPU_RASR_ENABLE_Msk)) {
            q->region = r;
            q->rasr = MPU->RASR;
            MPU->RASR = q->rasr | MPU_RASR_XN_Msk;
            break;
        }
    }
    __DSB();
    __ISB();
}

static void quiet_leave(struct quiet *q)
{
    if (q->region >= 0) {
        MPU->RNR = (uint32_t)q->region;
        MPU->RASR = q->rasr;
        __DSB();
        __ISB();
    }
    if (q->dc)
        SCB_EnableDCache(); /* (invalidates it: no stale flash data) */
    if (q->ic)
        SCB_EnableICache();
    __set_PRIMASK(q->primask);
}

static int wait_flag(XSPI_TypeDef *x, uint32_t flag)
{
    uint32_t t0 = cpu_cycles(), limit = SystemCoreClock / 100u;
    while (!(x->SR & flag))
        if (cpu_cycles() - t0 > limit)
            return -ETIMEDOUT;
    return 0;
}

static int wait_idle(XSPI_TypeDef *x)
{
    uint32_t t0 = cpu_cycles(), limit = SystemCoreClock / 100u;
    while (x->SR & XSPI_SR_BUSY)
        if (cpu_cycles() - t0 > limit)
            return -ETIMEDOUT;
    return 0;
}

/* Stop what the controller does (a memory-mapped read keeps the chip selected, prefetching):
 * ABORT, then wait until the bit clears and the controller is idle. CR is written back only
 * with ABORT clear: read back while the abort still runs, it would start another one, and the
 * controller ignores mode changes while it is busy. */
static int stop(XSPI_TypeDef *x)
{
    if (!(x->SR & XSPI_SR_BUSY))
        return 0;
    x->CR = (x->CR & ~XSPI_CR_ABORT) | XSPI_CR_ABORT;
    uint32_t t0 = cpu_cycles(), limit = SystemCoreClock / 100u;
    while ((x->CR & XSPI_CR_ABORT) || (x->SR & XSPI_SR_BUSY))
        if (cpu_cycles() - t0 > limit)
            return -ETIMEDOUT;
    x->FCR = XSPI_FCR_CTEF | XSPI_FCR_CTCF | XSPI_FCR_CSMF | XSPI_FCR_CTOF;
    return 0;
}

/* the functional mode (and FIFO threshold) of an idle controller */
static void set_mode(XSPI_TypeDef *x, uint32_t fmode)
{
    x->CR = (x->CR & ~(XSPI_CR_ABORT | XSPI_CR_FMODE | XSPI_CR_FTHRES)) | fmode;
}

/* out of memory-mapped mode, into indirect mode */
static int leave_mapped(struct mtd *m)
{
    int r = stop(m->x);
    if (!r)
        set_mode(m->x, FMODE_WRITE);
    return r;
}

/* back to the boot loader's memory-mapped mode (also after a command that went wrong) */
static void enter_mapped(struct mtd *m)
{
    XSPI_TypeDef *x = m->x;
    if (wait_idle(x))
        stop(x);
    x->FCR = XSPI_FCR_CTEF | XSPI_FCR_CTCF | XSPI_FCR_CSMF | XSPI_FCR_CTOF;
    x->CR = m->cr & ~(XSPI_CR_ABORT | XSPI_CR_FMODE);
    x->WCCR = m->wccr;
    x->WTCR = m->wtcr;
    x->WIR = m->wir;
    x->CCR = m->ccr;
    x->TCR = m->tcr;
    x->CR = m->cr & ~XSPI_CR_ABORT;     /* FMODE = memory-mapped */
    x->IR = m->ir;
}

/* an instruction alone */
static int command(struct mtd *m, uint32_t instr)
{
    XSPI_TypeDef *x = m->x;
    if (wait_idle(x))
        return -ETIMEDOUT;
    set_mode(x, FMODE_WRITE);
    x->CCR = CCR_INSTR;
    x->TCR = 0;
    x->IR = instr;
    int r = wait_flag(x, XSPI_SR_TCF);
    x->FCR = XSPI_FCR_CTCF;
    return r;
}

static int read_status(struct mtd *m, uint8_t *sr)
{
    XSPI_TypeDef *x = m->x;
    if (wait_idle(x))
        return -ETIMEDOUT;
    set_mode(x, FMODE_READ);
    x->DLR = 1;                                         /* 2 bytes: DTR repeats the register */
    x->CCR = CCR_INSTR | CCR_ADDR | CCR_DATA | CCR_DQSE;
    x->TCR = DUMMY_RDSR;
    x->IR = CMD_RDSR;
    x->AR = 0;
    if (wait_flag(x, XSPI_SR_TCF)) {
        stop(x);
        return -ETIMEDOUT;
    }
    *sr = *(volatile uint8_t *)&x->DR;
    (void)*(volatile uint8_t *)&x->DR;
    x->FCR = XSPI_FCR_CTCF;
    return 0;
}

static int wait_ready(struct mtd *m)
{
    uint32_t t0 = cpu_cycles(), limit = SystemCoreClock * BUSY_TIMEOUT_S;
    for (;;) {
        uint8_t sr;
        int r = read_status(m, &sr);
        if (r)
            return r;
        if (!(sr & SR_WIP))
            return 0;
        if (cpu_cycles() - t0 > limit)
            return -ETIMEDOUT;
    }
}

static int write_enable(struct mtd *m)
{
    int r = command(m, CMD_WREN);
    for (int i = 0; !r && i < 100; i++) {
        uint8_t sr;
        r = read_status(m, &sr);
        if (!r && (sr & SR_WEL))
            return 0;
    }
    return r ? r : -EIO;
}

static int erase_block(struct mtd *m, uint32_t addr)
{
    int r = write_enable(m);
    if (!r) {
        XSPI_TypeDef *x = m->x;
        set_mode(x, FMODE_WRITE);
        x->CCR = CCR_INSTR | CCR_ADDR;
        x->TCR = 0;
        x->IR = CMD_BE64K;
        x->AR = addr;
        r = wait_flag(x, XSPI_SR_TCF);
        x->FCR = XSPI_FCR_CTCF;
    }
    if (!r)
        r = wait_ready(m);
    return r;
}

/* @len bytes (even, within one page, from an even address) of m->page */
static int program_page(struct mtd *m, uint32_t addr, uint32_t len)
{
    int r = write_enable(m);
    if (r)
        return r;
    XSPI_TypeDef *x = m->x;
    set_mode(x, FMODE_WRITE);
    x->DLR = len - 1u;
    x->CCR = CCR_INSTR | CCR_ADDR | CCR_DATA;
    x->TCR = 0;
    x->IR = CMD_PP;
    x->AR = addr;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t t0 = cpu_cycles();
        while (!(x->SR & XSPI_SR_FTF))
            if (cpu_cycles() - t0 > SystemCoreClock / 100u)
                return -ETIMEDOUT;
        *(volatile uint8_t *)&x->DR = m->page[i];
    }
    r = wait_flag(x, XSPI_SR_TCF);
    x->FCR = XSPI_FCR_CTCF;
    return r ? r : wait_ready(m);
}

/* one 256 KB unit: four 64 KB blocks; quiet held by the caller */
static int erase_sector(struct mtd *m, uint32_t addr)
{
    int r = leave_mapped(m);
    for (uint32_t b = 0; b < SECTOR && !r; b += BLOCK)
        r = erase_block(m, addr + b);
    enter_mapped(m);
    return r;
}

static int program(struct mtd *m, uint32_t addr, uint32_t len)
{
    int r = leave_mapped(m);
    if (!r)
        r = program_page(m, addr, len);
    enter_mapped(m);
    return r;
}

/* ---- the kernel update: from RAM to the restart ----------------------------------------------- */

static void uart_say(const char *s)
{
    for (; *s; s++) {
        while (!(UART4->ISR & USART_ISR_TXE_TXFNF)) {
        }
        UART4->TDR = (uint8_t)*s;
    }
}

static bool same(const uint8_t *img, uint32_t len)
{
    const volatile uint8_t *f = (const volatile uint8_t *)MAPPED;
    for (uint32_t i = 0; i < len; i++)
        if (f[i] != img[i])
            return false;
    return true;
}

static void __attribute__((noreturn)) kernel_update(struct mtd *m, const uint8_t *img, uint32_t len)
{
    struct quiet q;
    quiet_enter(&q); /* for good: this kernel does not run again */
    for (int attempt = 0; attempt < 3; attempt++) {
        bool ok = true;
        for (uint32_t s = 0; s < len && ok; s += SECTOR)
            ok = erase_sector(m, s) == 0;
        for (uint32_t a = 0; a < len && ok; a += PAGE) {
            uint32_t n = len - a < PAGE ? len - a : PAGE;
            memset(m->page, 0xFF, PAGE); /* (the kernel's, in ITCM: they run with the flash busy) */
            memcpy(m->page, img + a, n);
            ok = program(m, a, (n + 1u) & ~1u) == 0;
        }
        if (ok && same(img, len)) {
            uart_say("\r\nmtd: kernel written and verified, restarting\r\n");
            NVIC_SystemReset();
        }
        uart_say("\r\nmtd: the kernel image did not verify, again\r\n");
    }
    for (;;) {
        uart_say("\r\n*** mtd: writing the kernel failed - the board needs \"crtos flash\" (debug probe)\r\n");
        for (volatile uint32_t d = 0; d < 200000000u; d++) {
        }
    }
}

/* the boot loader's own check (board/image.h, tools/imagehdr.py) */
static bool image_ok(const uint8_t *img, uint32_t len)
{
    struct crtos_image_header h;
    if (len < CRTOS_IMAGE_HDR_OFF + sizeof(h))
        return false;
    memcpy(&h, img + CRTOS_IMAGE_HDR_OFF, sizeof(h));
    if (h.magic != CRTOS_IMAGE_MAGIC || h.header_size != sizeof(h) || h.image_size != len || len > CRTOS_IMAGE_MAX)
        return false;
    static const uint8_t zero[4];
    uint32_t at = CRTOS_IMAGE_HDR_OFF + 12u;   /* offsetof(crc32) */
    uint32_t c = crc32(0, img, at);
    c = crc32(c, zero, 4);
    c = crc32(c, img + at + 4, len - at - 4);
    return c == h.crc32;
}

static int kernel_update_ioctl(struct mtd_part *p, const struct mtd_kernel_update *ku)
{
    if (!capable(CAP_SYS))
        return -EPERM;
    const uint8_t *uimg = (const uint8_t *)(uintptr_t)ku->image;
    uint32_t len = ku->len;
    if (!p->kernel || len > p->size)
        return -EINVAL;
    if (!uaccess_ok(uimg, len, 0))
        return -EFAULT;
    uint8_t *img = kmalloc(len, KM_LARGE);
    if (!img)
        return -ENOMEM;
    memcpy(img, uimg, len);
    if (crc32(0, img, len) != ku->crc32 || !image_ok(img, len)) {
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
    memcpy(buf, (const void *)(MAPPED + p->off + o->pos), n);
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
    while (done < total && !r) {
        /* one page at most, from an even address to an even end: 0xFF around what is
         * written changes no bit */
        uint32_t a = addr + done;
        uint32_t start = a & ~1u, page_end = (a & ~(PAGE - 1u)) + PAGE;
        uint32_t n = total - done < page_end - a ? total - done : page_end - a;
        uint32_t end = (a + n + 1u) & ~1u;
        memset(m->page, 0xFF, PAGE);
        memcpy(m->page + (a - start), buf + done, n);
        struct quiet q;
        quiet_enter(&q);
        r = program(m, start, end - start);
        quiet_leave(&q);
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

static int erase_range(struct mtd_part *p, uint32_t off, uint32_t len)
{
    if (mutex_lock(&p->m->lock, WAIT_FOREVER))
        return -EINTR;
    int r = 0;
    for (uint32_t s = 0; s < len && !r; s += SECTOR) {
        struct quiet q;
        quiet_enter(&q);
        r = erase_sector(p->m, p->off + off + s);
        quiet_leave(&q);
    }
    mutex_unlock(&p->m->lock);
    return r;
}

static int mtd_ioctl(struct file *f, unsigned cmd, void *arg)
{
    struct mtd_part *p = ((struct opened *)f->priv)->p;
    switch (cmd) {
    case MEMGETINFO: {
        struct mtd_info_user *mi = arg;
        memset(mi, 0, sizeof(*mi));
        mi->type = MTD_NORFLASH;
        mi->flags = p->ro ? 0 : MTD_WRITEABLE | MTD_BIT_WRITEABLE;
        mi->size = p->size;
        mi->erasesize = SECTOR;
        mi->writesize = 1;
        return 0;
    }
    case MEMERASE: {
        const struct erase_info_user *e = arg;
        if (p->ro)
            return -EROFS;
        if (p->claimed)
            return -EBUSY;
        if ((e->start | e->length) % SECTOR || !e->length || e->start > p->size || e->length > p->size - e->start)
            return -EINVAL;
        return erase_range(p, e->start, e->length);
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

static struct mtd *s_mtd;

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
    g->mapped = (const uint8_t *)(MAPPED + p->off);
    return 0;
}
EXPORT_SYMBOL(mtd_part_geometry);

int mtd_part_claim(struct mtd_part *p, const char *owner)
{
    if (mutex_lock(&p->m->lock, WAIT_FOREVER))
        return -EINTR;
    int r = p->claimed ? -EBUSY : p->ro ? -EROFS : 0;
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
    return r == (int)len ? 0 : r < 0 ? r : -EIO;
}
EXPORT_SYMBOL(mtd_part_program);

int mtd_part_erase(struct mtd_part *p, uint32_t off, uint32_t len)
{
    if (p->ro)
        return -EROFS;
    if ((off | len) % SECTOR || !len || off > p->size || len > p->size - off)
        return -EINVAL;
    return erase_range(p, off, len);
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
    for_each_child_of_node(dev->of_node, np) if (of_device_is_compatible(np, "mxicy,mx66uw1g45g") && of_device_is_available(np))
        flash = np;
    if (!flash)
        return -ENODEV;
    struct mtd *m = devm_kzalloc(dev, sizeof(*m), 0);
    if (!m)
        return -ENOMEM;
    m->dev = dev;
    m->x = device_map(dev, 0);
    if (!m->x)
        return -ENODEV;
    if ((m->x->CR & XSPI_CR_FMODE) != FMODE_MAPPED) {
        dev_err(dev, "XSPI2 is not in memory-mapped mode (CR %08lx): the boot loader sets it up\n",
                (unsigned long)m->x->CR);
        return -ENODEV;
    }
    uint32_t size = 0x8000000u;
    of_property_read_u32(flash, "size", &size);
    mutex_init(&m->lock);
    /* what the boot loader set up for memory-mapped reads (and writes) */
    m->cr = m->x->CR;
    m->ccr = m->x->CCR;
    m->tcr = m->x->TCR;
    m->ir = m->x->IR;
    m->wccr = m->x->WCCR;
    m->wtcr = m->x->WTCR;
    m->wir = m->x->WIR;

    dev_set_drvdata(dev, m);
    s_mtd = m;
    struct device_node *parts = of_get_child_by_name(flash, "partitions");
    if (parts) {
        for_each_child_of_node(parts, np) {
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
                 (unsigned long)(m->parts[i].size / 1024u), (unsigned long)(MAPPED + m->parts[i].off),
                 m->parts[i].ro ? " (read-only)" : "");
    dev_info(dev, "MX66UW1G45G %lu MB, octal DTR, XSPI2 %lu MHz\n", (unsigned long)(size >> 20),
             (unsigned long)(200u / ((m->x->DCR2 & XSPI_DCR2_PRESCALER) + 1u)));
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
    { "st,stm32-xspi", NULL },
    { NULL, NULL },
};

static struct driver mtd_driver = {
    .name = "xspi-mtd",
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

MODULE("xspi-mtd", "octal NOR flash on XSPI2 as /dev/mtd*; kernel updates", init, fini);
