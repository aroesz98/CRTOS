/*
 * kernel/os/kmon_cmds.cpp - kernel monitor commands for files, uploads, the device tree,
 * devices, drivers and modules.
 */
#include "kernel.h"
#include "boot/sdcard.h"
#include "../lib/crc32.h"
#include "fsl_common.h"
#include <crtos/device.h>
#include <crtos/module.h>
#include <crtos/of.h>
#include <crtos/vfs.h>
#include <stdlib.h>
#include <string.h>

/* ---- files ------------------------------------------------------------------------------------ */

void kcmd_ls(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/";
    struct file *d;
    int r = vfs_opendir(path, &d);
    if (r) {
        cprintf("ls: %s: %d\n", path, r);
        return;
    }
    struct vfs_dirent *de = (struct vfs_dirent *)kmalloc(sizeof(*de), KM_ANY);
    if (!de) {
        vfs_close(d);
        return;
    }
    int n = 0;
    while ((r = vfs_readdir(d, de)) > 0) {
        if ((de->mode & VFS_S_IFMT) == VFS_S_IFDIR)
            cprintf("  %10s  %s/\n", "<dir>", de->name);
        else if ((de->mode & VFS_S_IFMT) == VFS_S_IFCHR)
            cprintf("  %10s  %s\n", "<dev>", de->name);
        else
            cprintf("  %10lu  %s\n", (unsigned long)de->size, de->name);
        n++;
    }
    if (r < 0)
        cprintf("ls: read error %d\n", r);
    cprintf("%d entries\n", n);
    kfree(de);
    vfs_close(d);
}

void kcmd_cat(int argc, char **argv)
{
    if (argc < 2) {
        cprintf("usage: cat <file>\n");
        return;
    }
    struct file *f;
    int r = vfs_open(argv[1], VFS_O_RDONLY, &f);
    if (r) {
        cprintf("cat: %s: %d\n", argv[1], r);
        return;
    }
    char buf[128];
    while ((r = vfs_read(f, buf, sizeof(buf))) > 0)
        con_write(buf, (size_t)r);
    vfs_close(f);
    cprintf("\n");
}

void kcmd_hexdump(int argc, char **argv)
{
    if (argc < 2) {
        cprintf("usage: hexdump <file> [offset] [length]\n");
        return;
    }
    uint32_t off = argc > 2 ? strtoul(argv[2], nullptr, 0) : 0;
    uint32_t len = argc > 3 ? strtoul(argv[3], nullptr, 0) : 256;
    struct file *f;
    int r = vfs_open(argv[1], VFS_O_RDONLY, &f);
    if (r) {
        cprintf("hexdump: %s: %d\n", argv[1], r);
        return;
    }
    if (off)
        vfs_lseek(f, off, VFS_SEEK_SET);
    uint8_t b[16];
    while (len) {
        r = vfs_read(f, b, len < 16 ? len : 16);
        if (r <= 0)
            break;
        char line[80];
        int n = ksnprintf(line, sizeof(line), "%08lx:", (unsigned long)off);
        for (int i = 0; i < r; i++)
            n += ksnprintf(line + n, sizeof(line) - (size_t)n, " %02x", b[i]);
        cprintf("%-57s |", line);
        for (int i = 0; i < r; i++)
            cprintf("%c", b[i] >= 32 && b[i] < 127 ? b[i] : '.');
        cprintf("|\n");
        off += (uint32_t)r;
        len -= (uint32_t)r;
    }
    vfs_close(f);
}

void kcmd_rm(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        int r = vfs_unlink(argv[i]);
        if (r)
            cprintf("rm: %s: %d\n", argv[i], r);
    }
}

void kcmd_mkdir(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        int r = vfs_mkdir(argv[i]);
        if (r && r != -EEXIST)
            cprintf("mkdir: %s: %d\n", argv[i], r);
    }
}

void kcmd_mv(int argc, char **argv)
{
    if (argc < 3) {
        cprintf("usage: mv <from> <to>\n");
        return;
    }
    int r = vfs_rename(argv[1], argv[2]);
    if (r)
        cprintf("mv: %d\n", r);
}

void kcmd_df(int, char **)
{
    uint64_t total = 0, free = 0;
    int r = vfs_statfs("/sd", &total, &free);
    if (r) {
        cprintf("df: /sd: %d\n", r);
        return;
    }
    cprintf("/sd  %s  %lu MB total, %lu MB free, %lu KB clusters\n", fat_type_name(), (unsigned long)(total >> 20),
            (unsigned long)(free >> 20), (unsigned long)(fat_cluster_size() / 1024u));
}

/* sd [trace | reinit | clock <kHz>] */
void kcmd_sd(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "trace")) {
        sdcard_dump_trace(cprintf);
        return;
    }
    if (argc > 1 && !strcmp(argv[1], "reinit")) {
        cprintf("sd: reinit %d\n", sdcard_reinit());
        return;
    }
    if (argc > 4 && !strcmp(argv[1], "pads")) {
        sdcard_set_pads(strtoul(argv[2], nullptr, 0), strtoul(argv[3], nullptr, 0), strtoul(argv[4], nullptr, 0));
        cprintf("sd: pads speed %s dse %s slew %s\n", argv[2], argv[3], argv[4]);
        return;
    }
    if (argc > 2 && !strcmp(argv[1], "clock")) {
        cprintf("sd: clock %lu Hz\n", (unsigned long)sdcard_set_clock(strtoul(argv[2], nullptr, 0) * 1000u));
        return;
    }
    if (argc > 1 && !strcmp(argv[1], "hist")) {
        sdcard_dump_hist(cprintf);
        return;
    }
    if (argc > 2 && !strcmp(argv[1], "cmd23")) {
        int on = !strcmp(argv[2], "on");
        sdcard_set_cmd23(on);
        struct sdcard_info i;
        sdcard_get_info(&i);
        cprintf("sd: CMD23 %s\n", on ? "on (if the card has it)" : "off");
        return;
    }
    if (argc > 2 && !strcmp(argv[1], "mode")) {
        static const char *const modes[] = { "ds", "hs", "sdr50", "sdr104" };
        for (int m = 0; m < 4; m++)
            if (!strcmp(argv[2], modes[m])) {
                cprintf("sd: %s\n", sdcard_mode_name(sdcard_set_mode(m)));
                return;
            }
        cprintf("sd mode: ds, hs, sdr50 or sdr104\n");
        return;
    }
    struct sdcard_info i;
    sdcard_get_info(&i);
    cprintf("present %d ready %d %s blocks %lu (%lu MB) bus %d-bit @ %lu Hz, %s, %s signalling, %lu tunings\n",
            i.present, i.ready, i.high_capacity ? "SDHC/SDXC" : "SDSC", (unsigned long)i.blocks,
            (unsigned long)(i.blocks / 2048u), i.bus_width, (unsigned long)i.clock_hz, sdcard_mode_name(i.mode),
            i.v18 ? "1.8 V" : "3.3 V", (unsigned long)i.tunings);
    /* CID as stored by the host driver: cid[3] = CID[127:96] ... (CRC dropped) */
    char oid[3] = { (char)(i.cid[3] >> 16), (char)(i.cid[3] >> 8), 0 };
    char pnm[6] = { (char)i.cid[3], (char)(i.cid[2] >> 24), (char)(i.cid[2] >> 16), (char)(i.cid[2] >> 8),
                    (char)i.cid[2], 0 };
    for (char *p = oid; *p; p++)
        if (*p < ' ' || *p > '~')
            *p = '?';
    for (char *p = pnm; *p; p++)
        if (*p < ' ' || *p > '~')
            *p = '?';
    uint32_t mdt = (i.cid[0] >> 8) & 0xFFFu;
    cprintf("card: manufacturer %02lx oem %s product %s rev %lu.%lu serial %08lx made %lu/%lu\n",
            (unsigned long)(i.cid[3] >> 24), oid, pnm, (unsigned long)((i.cid[1] >> 28) & 0xFu),
            (unsigned long)((i.cid[1] >> 24) & 0xFu), (unsigned long)((i.cid[1] << 8) | (i.cid[0] >> 24)),
            (unsigned long)(mdt & 0xFu), (unsigned long)(2000u + (mdt >> 4)));
    cprintf("blocks read %lu written %lu; failed transfers %lu, retries %lu, re-inits %lu; "
            "slow transfers %lu, slowest %lu us\n",
            (unsigned long)i.reads, (unsigned long)i.writes, (unsigned long)i.errors, (unsigned long)i.retries,
            (unsigned long)i.reinits, (unsigned long)i.slow, (unsigned long)i.max_us);
}

/*
 * put <path> <size> <crc32>: receive a file over the console.
 * The host waits for "READY", then sends the data in 1 KiB chunks; after each chunk has been
 * written the board answers "ACK <bytes so far>" (the host may keep one more chunk in flight).
 * Finally "OK <crc>" or "ERR <reason>". Parent directories are created as needed.
 */
#define PUT_CHUNK 1024u

static void make_parents(const char *path)
{
    char dir[VFS_PATH_MAX];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = 0;
    for (char *p = dir + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            vfs_mkdir(dir);
            *p = '/';
        }
    }
}

static void put_file(const char *path, const char *tmp, uint32_t size, uint32_t want);

void kcmd_put(int argc, char **argv)
{
    if (argc < 4) {
        cprintf("usage: put <path> <size> <crc32-hex>\n");
        return;
    }
    char *tmp = (char *)kmalloc(VFS_PATH_MAX + 8, KM_ANY);
    if (!tmp) {
        cprintf("ERR nomem\n");
        return;
    }
    ksnprintf(tmp, VFS_PATH_MAX + 8, "%s.part", argv[1]);
    put_file(argv[1], tmp, strtoul(argv[2], nullptr, 0), strtoul(argv[3], nullptr, 16));
    kfree(tmp);
}

static void put_file(const char *path, const char *tmp, uint32_t size, uint32_t want)
{
    make_parents(path);
    struct file *f;
    int r = vfs_open(tmp, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, &f);
    if (r) {
        cprintf("ERR open %d\n", r);
        return;
    }
    uint8_t *buf = (uint8_t *)kmalloc_aligned(PUT_CHUNK, 32, KM_ANY);
    if (!buf) {
        vfs_close(f);
        cprintf("ERR nomem\n");
        return;
    }
    cprintf("READY\n");
    uint32_t got = 0, crc = 0;
    uint64_t t0 = time_us();
    const char *err = nullptr;
    while (got < size && !err) {
        uint32_t n = size - got < PUT_CHUNK ? size - got : PUT_CHUNK;
        for (uint32_t i = 0; i < n;) {
            int c = console_read(buf + i, n - i, 3000);
            if (c < 0) {
                err = "timeout";
                break;
            }
            i += (uint32_t)c;
        }
        if (err)
            break;
        if (vfs_write(f, buf, n) != (int)n) {
            err = "write";
            break;
        }
        crc = crc32(crc, buf, n);
        got += n;
        cprintf("ACK %lu\n", (unsigned long)got);
    }
    kfree(buf);
    vfs_close(f);
    if (!err && crc != want)
        err = "crc";
    if (err) {
        vfs_unlink(tmp);
        cprintf("ERR %s (%lu of %lu bytes, crc %08lx)\n", err, (unsigned long)got, (unsigned long)size,
                (unsigned long)crc);
        return;
    }
    vfs_unlink(path);
    r = vfs_rename(tmp, path);
    uint32_t ms = (uint32_t)((time_us() - t0) / 1000u);
    if (r)
        cprintf("ERR rename %d\n", r);
    else
        cprintf("OK %08lx %lu bytes in %lu ms\n", (unsigned long)crc, (unsigned long)size, (unsigned long)ms);
}

/* ---- device tree ---------------------------------------------------------------------------------- */

static void print_prop(const struct property *pp, int indent)
{
    const uint8_t *v = (const uint8_t *)pp->value;
    uint32_t len = pp->length;
    cprintf("%*s%s", indent, "", pp->name);
    if (!len) {
        cprintf(";\n");
        return;
    }
    /* strings? */
    bool text = v[len - 1] == 0;
    for (uint32_t i = 0; i < len && text; i++)
        if (v[i] && (v[i] < 32 || v[i] > 126))
            text = false;
    if (text && v[0]) {
        cprintf(" = ");
        for (uint32_t i = 0; i < len;) {
            cprintf("%s\"%s\"", i ? ", " : "", (const char *)v + i);
            i += (uint32_t)strlen((const char *)v + i) + 1;
        }
        cprintf(";\n");
    } else if (!(len & 3)) {
        cprintf(" = <");
        for (uint32_t i = 0; i < len; i += 4)
            cprintf("%s0x%lx", i ? " " : "", (unsigned long)of_be32(v + i));
        cprintf(">;\n");
    } else {
        cprintf(" = [");
        for (uint32_t i = 0; i < len; i++)
            cprintf("%s%02x", i ? " " : "", v[i]);
        cprintf("];\n");
    }
}

static void print_node(const struct device_node *np, int depth, int max_depth)
{
    cprintf("%*s%s {\n", depth * 2, "", np->parent ? np->name : "/");
    for (const struct property *pp = np->properties; pp; pp = pp->next)
        print_prop(pp, depth * 2 + 2);
    if (depth < max_depth) {
        struct device_node *c;
        for_each_child_of_node(np, c)
            print_node(c, depth + 1, max_depth);
    } else if (np->child) {
        cprintf("%*s...\n", depth * 2 + 2, "");
    }
    cprintf("%*s};\n", depth * 2, "");
}

void kcmd_dt(int argc, char **argv)
{
    if (!of_root()) {
        cprintf("no device tree\n");
        return;
    }
    const struct device_node *np = of_find_node_by_path(argc > 1 ? argv[1] : "/");
    if (!np) {
        cprintf("dt: no node %s\n", argv[1]);
        return;
    }
    int depth = argc > 2 ? atoi(argv[2]) : (argc > 1 ? 8 : 1);
    print_node(np, 0, depth);
}

static const char *state_str(int s)
{
    switch (s) {
    case DEV_BOUND: return "bound";
    case DEV_DEFERRED: return "deferred";
    case DEV_FAILED: return "failed";
    default: return "-";
    }
}

static void dev_line(struct device *dev, void *)
{
    const char *compat = "";
    if (dev->of_node)
        of_property_read_string(dev->of_node, "compatible", &compat);
    cprintf("  %-28s %-26s %-9s %s\n", dev->name, compat, state_str(dev->state),
            dev->driver ? dev->driver->name : "");
}

void kcmd_dtload(int argc, char **argv)
{
    if (argc < 2) {
        cprintf("usage: dtload <dtb> [driver dir]\n");
        return;
    }
    int r = boot_setup_devices(argv[1], argc > 2 ? argv[2] : nullptr);
    if (r)
        cprintf("dtload: %d\n", r);
}

void kcmd_devices(int, char **)
{
    cprintf("  %-28s %-26s %-9s %s\n", "DEVICE", "COMPATIBLE", "STATE", "DRIVER");
    device_foreach(dev_line, nullptr);
}

static void drv_line(struct driver *drv, void *)
{
    cprintf("  %-24s %s\n", drv->name, module_name(drv->owner));
}

void kcmd_drivers(int, char **)
{
    cprintf("  %-24s %s\n", "DRIVER", "MODULE");
    driver_foreach(drv_line, nullptr);
}

/* ---- modules ---------------------------------------------------------------------------------------- */

static void mod_line(const char *name, const char *desc, void *base, uint32_t size, uint32_t refs, void *)
{
    cprintf("  %-16s %08lx %7lu %4lu  %s\n", name, (unsigned long)(uintptr_t)base, (unsigned long)size,
            (unsigned long)refs, desc);
}

void kcmd_lsmod(int, char **)
{
    cprintf("  %-16s %-8s %7s %4s  %s\n", "MODULE", "ADDRESS", "SIZE", "USED", "DESCRIPTION");
    module_foreach(mod_line, nullptr);
    cprintf("  (kernel exports %u symbols)\n", ksym_kernel_count());
}

void kcmd_insmod(int argc, char **argv)
{
    if (argc < 2) {
        cprintf("usage: insmod <name|path>\n");
        return;
    }
    char path[VFS_PATH_MAX];
    if (argv[1][0] == '/')
        ksnprintf(path, sizeof(path), "%s", argv[1]);
    else
        ksnprintf(path, sizeof(path), "%s/%s.ko", CRTOS_DRIVER_DIR, argv[1]);
    int r = module_load(path, nullptr);
    if (r)
        cprintf("insmod: %s: %d\n", path, r);
}

void kcmd_rmmod(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        int r = module_unload(argv[i]);
        if (r)
            cprintf("rmmod: %s: %d\n", argv[i], r);
    }
}

/* ---- storage checks ---------------------------------------------------------------------------- */

void kcmd_crc32(int argc, char **argv)
{
    if (argc < 2) {
        cprintf("usage: crc32 <file>\n");
        return;
    }
    struct file *f;
    int r = vfs_open(argv[1], VFS_O_RDONLY, &f);
    if (r) {
        cprintf("crc32: %s: %d\n", argv[1], r);
        return;
    }
    uint8_t *buf = (uint8_t *)kmalloc_aligned(4096, 32, KM_ANY);
    if (!buf) {
        vfs_close(f);
        return;
    }
    uint32_t crc = 0, n = 0;
    while ((r = vfs_read(f, buf, 4096)) > 0) {
        crc = crc32(crc, buf, (size_t)r);
        n += (uint32_t)r;
    }
    kfree(buf);
    vfs_close(f);
    if (r < 0)
        cprintf("crc32: read error %d\n", r);
    else
        cprintf("%08lx  %lu bytes  %s\n", (unsigned long)crc, (unsigned long)n, argv[1]);
}

/* sdbench [MB] [chunk KB]: sequential file write/read through FAT, raw sequential reads of
 * several sizes, raw single-block latency */
void kcmd_sdbench(int argc, char **argv)
{
    uint32_t mb = argc > 1 ? strtoul(argv[1], nullptr, 0) : 4;
    uint32_t chunk_kb = argc > 2 ? strtoul(argv[2], nullptr, 0) : 32;
    if (chunk_kb < 1 || chunk_kb > 1024)
        chunk_kb = 32;
    const uint32_t chunk = chunk_kb * 1024u;
    const char *path = CRTOS_ROOT "/tmp/sdbench.bin";
    vfs_mkdir(CRTOS_ROOT "/tmp");
    uint8_t *buf = (uint8_t *)kmalloc_aligned(chunk, 32, KM_LARGE);
    if (!buf) {
        cprintf("sdbench: no memory\n");
        return;
    }
    struct file *f;
    int r = vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, &f);
    if (r) {
        cprintf("sdbench: %s: %d\n", path, r);
        kfree(buf);
        return;
    }
    uint32_t total = mb * 1024u * 1024u, done = 0;
    uint64_t t0 = time_us();
    while (done < total && r >= 0) {
        for (uint32_t i = 0; i < chunk / 4; i++)
            ((uint32_t *)buf)[i] = (done / 4 + i) * 2654435761u;
        r = vfs_write(f, buf, chunk);
        done += chunk;
    }
    vfs_sync(f);
    vfs_close(f);
    uint32_t wus = (uint32_t)(time_us() - t0);
    if (r < 0) {
        cprintf("sdbench: write error %d\n", r);
        kfree(buf);
        return;
    }
    cprintf("write %lu KB: %lu ms, %lu KB/s\n", (unsigned long)(total / 1024u), (unsigned long)(wus / 1000u),
            (unsigned long)((uint64_t)total * 1000000u / 1024u / (wus ? wus : 1)));

    r = vfs_open(path, VFS_O_RDONLY, &f);
    if (r) {
        cprintf("sdbench: reopen %d\n", r);
        kfree(buf);
        return;
    }
    done = 0;
    int bad = 0;
    t0 = time_us();
    while (done < total) {
        r = vfs_read(f, buf, chunk);
        if (r != (int)chunk)
            break;
        for (uint32_t i = 0; i < chunk / 4 && !bad; i++)
            if (((uint32_t *)buf)[i] != (done / 4 + i) * 2654435761u)
                bad = 1;
        done += chunk;
    }
    uint32_t rus = (uint32_t)(time_us() - t0);
    uint32_t ext_lba = 0, ext_blocks = 0;
    bool have_ext = fat_file_extent(f, &ext_lba, &ext_blocks) == 0;
    vfs_close(f);
    cprintf("read  %lu KB: %lu ms, %lu KB/s, data %s\n", (unsigned long)(done / 1024u), (unsigned long)(rus / 1000u),
            (unsigned long)((uint64_t)done * 1000000u / 1024u / (rus ? rus : 1)), bad || done != total ? "BAD" : "ok");
    /* raw writes over the test file's own blocks (it is contiguous): card speed per command size */
    if (have_ext) {
        static const uint32_t wsizes[] = { 64, 256, 2048 };
        for (unsigned t = 0; t < sizeof(wsizes) / sizeof(wsizes[0]); t++) {
            uint32_t blocks = wsizes[t];
            if (blocks * SDCARD_BLOCK_SIZE > chunk)
                continue;
            uint32_t n = ext_blocks / blocks;
            if (n * blocks > 32768u) /* 16 MB at most */
                n = 32768u / blocks;
            uint64_t tw = time_us();
            uint32_t k = 0;
            for (; k < n; k++)
                if (sdcard_write(ext_lba + k * blocks, buf, blocks))
                    break;
            uint32_t us = (uint32_t)(time_us() - tw);
            uint64_t bytes = (uint64_t)k * blocks * SDCARD_BLOCK_SIZE;
            cprintf("raw write %4lu blocks/cmd: %6lu KB/s (%lu us/cmd)\n", (unsigned long)blocks,
                    (unsigned long)(bytes * 1000000u / 1024u / (us ? us : 1)), (unsigned long)(k ? us / k : 0));
        }
        fat_cache_invalidate(); /* the file system's read-ahead may hold these blocks */
    }
    vfs_unlink(path);

    /* raw sequential reads (no file system): what the bus and the card give per command size,
     * and per destination memory (the DMA engine writes it) */
    auto raw = [](const char *where, uint8_t *dst, uint32_t blocks) {
        uint32_t n = (4u * 1024u * 1024u / SDCARD_BLOCK_SIZE) / blocks; /* 4 MB each */
        uint32_t c, d, tot, cnt;
        sdcard_timing(&c, &d, &tot, &cnt);
        uint64_t t = time_us();
        uint32_t k = 0;
        for (; k < n; k++)
            if (sdcard_read(1000000u + k * blocks, dst, blocks))
                break;
        uint32_t us = (uint32_t)(time_us() - t);
        sdcard_timing(&c, &d, &tot, &cnt);
        uint64_t bytes = (uint64_t)k * blocks * SDCARD_BLOCK_SIZE;
        cprintf("raw read %4lu blocks/cmd to %-10s %6lu KB/s (%lu us/cmd: command %lu, data %lu)\n",
                (unsigned long)blocks, where, (unsigned long)(bytes * 1000000u / 1024u / (us ? us : 1)),
                (unsigned long)(k ? us / k : 0), (unsigned long)c, (unsigned long)d);
    };
    static const uint32_t sizes[] = { 8, 64, 256, 2048 };
    for (unsigned t = 0; t < sizeof(sizes) / sizeof(sizes[0]); t++)
        if (sizes[t] * SDCARD_BLOCK_SIZE <= chunk)
            raw("SDRAM", buf, sizes[t]);
    static const struct {
        const char *name;
        uint32_t flags, blocks;
    } mems[] = { { "SDRAM nc", KM_NOCACHE | KM_DMA, 64 }, { "OCRAM", KM_DMA, 64 }, { "DTCM", KM_FAST, 16 } };
    for (unsigned t = 0; t < sizeof(mems) / sizeof(mems[0]); t++) {
        uint8_t *b = (uint8_t *)kmalloc_aligned(mems[t].blocks * SDCARD_BLOCK_SIZE, 32, mems[t].flags);
        if (!b)
            continue;
        raw(mems[t].name, b, mems[t].blocks);
        kfree(b);
    }

    /* raw reads: what FAT metadata access costs */
    static const struct {
        const char *what;
        uint32_t stride, blocks;
    } tests[] = { { "1 block, consecutive", 1, 1 }, { "1 block, 4 KB apart", 8, 1 }, { "1 block, 1 MB apart", 2048, 1 },
                  { "8 blocks, consecutive", 8, 8 } };
    for (unsigned t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
        const int n = 200;
        uint32_t c, d, tot, cnt;
        sdcard_timing(&c, &d, &tot, &cnt);
        t0 = time_us();
        for (int i = 0; i < n; i++)
            if (sdcard_read(4096u + (uint32_t)i * tests[t].stride, buf, tests[t].blocks))
                break;
        uint32_t us = (uint32_t)(time_us() - t0);
        sdcard_timing(&c, &d, &tot, &cnt);
        cprintf("%-24s %5lu us/op (cmd irq %lu us, data irq %lu us, waiter %lu us)\n", tests[t].what,
                (unsigned long)(us / n), (unsigned long)c, (unsigned long)d, (unsigned long)tot);
    }
    kfree(buf);
}

/* sdstress [reads] [blocks] [gap-us]: random reads spread over the whole card (read only) */
void kcmd_sdstress(int argc, char **argv)
{
    uint32_t n = argc > 1 ? strtoul(argv[1], nullptr, 0) : 500;
    uint32_t blocks = argc > 2 ? strtoul(argv[2], nullptr, 0) : 1;
    uint32_t gap_us = argc > 3 ? strtoul(argv[3], nullptr, 0) : 0;
    struct sdcard_info i0, i1;
    sdcard_get_info(&i0);
    if (!i0.ready || !blocks || blocks > 64) {
        cprintf("sdstress: card not ready or bad block count\n");
        return;
    }
    uint8_t *buf = (uint8_t *)kmalloc_aligned(blocks * SDCARD_BLOCK_SIZE, 32, KM_LARGE);
    if (!buf)
        return;
    uint32_t x = 0x12345678u ^ (uint32_t)time_us(), fails = 0, lo = ~0u, hi = 0;
    uint64_t sum = 0, t_start = time_us();
    for (uint32_t k = 0; k < n; k++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        uint32_t lba = x % (i0.blocks - blocks);
        if (gap_us >= 1000u)
            task_sleep_ms(gap_us / 1000u);
        else if (gap_us)
            SDK_DelayAtLeastUs(gap_us, SystemCoreClock);
        uint64_t t0 = time_us();
        int r = sdcard_read(lba, buf, blocks);
        uint32_t us = (uint32_t)(time_us() - t0);
        if (r) {
            fails++;
            cprintf("read %lu at %08lx: %d\n", (unsigned long)k, (unsigned long)lba, r);
            continue;
        }
        sum += us;
        lo = us < lo ? us : lo;
        hi = us > hi ? us : hi;
    }
    uint32_t total_ms = (uint32_t)((time_us() - t_start) / 1000u);
    sdcard_get_info(&i1);
    uint32_t ok = n - fails;
    cprintf("%lu reads of %lu block(s) @ %lu Hz in %lu ms: failed %lu, latency min %lu avg %lu max %lu us; "
            "card errors +%lu, re-inits +%lu\n",
            (unsigned long)n, (unsigned long)blocks, (unsigned long)i1.clock_hz, (unsigned long)total_ms,
            (unsigned long)fails, (unsigned long)(ok ? lo : 0), (unsigned long)(ok ? sum / ok : 0), (unsigned long)hi,
            (unsigned long)(i1.errors - i0.errors), (unsigned long)(i1.reinits - i0.reinits));
    kfree(buf);
}

/* ---- fast uploads through the debug probe ------------------------------------------------------ */

/* The host asks for a buffer (stage), writes the file into it through the debugger while the
 * core runs, then has it saved (savestage). */
static void *s_stage;
static uint32_t s_stage_size;

void kcmd_stage(int argc, char **argv)
{
    uint32_t size = argc > 1 ? strtoul(argv[1], nullptr, 0) : 0;
    kfree(s_stage);
    s_stage = nullptr;
    if (!size || size > 8u * 1024u * 1024u) {
        cprintf("ERR size\n");
        return;
    }
    s_stage = kmalloc_aligned(ALIGN_UP(size, 32u), 32, KM_LARGE);
    if (!s_stage) {
        cprintf("ERR nomem\n");
        return;
    }
    s_stage_size = size;
    /* no dirty line of an earlier user may be written back over what the debugger puts here */
    SCB_CleanInvalidateDCache_by_Addr(s_stage, (int32_t)ALIGN_UP(size, 32u));
    cprintf("STAGE %08lx\n", (unsigned long)(uintptr_t)s_stage);
}

void kcmd_savestage(int argc, char **argv)
{
    if (argc < 4 || !s_stage) {
        cprintf("ERR usage: savestage <path> <size> <crc32-hex> (after stage)\n");
        return;
    }
    uint32_t size = strtoul(argv[2], nullptr, 0), want = strtoul(argv[3], nullptr, 16);
    if (size > s_stage_size) {
        cprintf("ERR size\n");
        return;
    }
    SCB_InvalidateDCache_by_Addr(s_stage, (int32_t)ALIGN_UP(size, 32u));
    uint32_t crc = crc32(0, s_stage, size);
    if (crc != want) {
        cprintf("ERR crc %08lx\n", (unsigned long)crc);
        return;
    }
    char *tmp = (char *)kmalloc(VFS_PATH_MAX + 8, KM_ANY);
    if (!tmp) {
        cprintf("ERR nomem\n");
        return;
    }
    ksnprintf(tmp, VFS_PATH_MAX + 8, "%s.part", argv[1]);
    make_parents(argv[1]);
    uint64_t t0 = time_us();
    struct file *f;
    int r = vfs_open(tmp, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, &f);
    if (!r) {
        r = vfs_write(f, s_stage, size) == (int)size ? 0 : -EIO;
        int c = vfs_close(f);
        if (!r)
            r = c;
    }
    if (!r) {
        vfs_unlink(argv[1]);
        r = vfs_rename(tmp, argv[1]);
    } else {
        vfs_unlink(tmp);
    }
    kfree(tmp);
    kfree(s_stage);
    s_stage = nullptr;
    if (r)
        cprintf("ERR write %d\n", r);
    else
        cprintf("OK %08lx %lu bytes in %lu ms\n", (unsigned long)crc, (unsigned long)size,
                (unsigned long)((time_us() - t0) / 1000u));
}
