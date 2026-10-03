/*
 * mtd - the flash memory partitions (/dev/mtd*).
 *
 *     mtd info                         the partitions: size, erase block, read-only
 *     mtd dump DEV OFF [LEN]           hex dump (256 bytes by default)
 *     mtd read DEV OFF LEN FILE        flash -> file
 *     mtd erase DEV OFF LEN            erase whole blocks
 *     mtd write DEV OFF FILE           file -> flash (the blocks must be erased)
 *     mtd test DEV OFF                 erase one block, write a pattern, read it back; its old
 *                                      contents go back afterwards
 *     mtd kernel FILE                  a new kernel into the kernel partition, then a restart
 *
 * DEV: mtd1 or /dev/mtd1; OFF and LEN in bytes (0x... hex), within the partition. While the
 * flash erases or programs, the whole system stands still (the kernel runs from it).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include <crtos/mtd.h>

static uint32_t crc32_of(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    while (len--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static int open_dev(const char *name, int flags, struct mtd_info_user *mi)
{
    char path[32];
    if (strncmp(name, "/dev/", 5))
        snprintf(path, sizeof(path), "/dev/%s", name);
    else
        snprintf(path, sizeof(path), "%s", name);
    int fd = open(path, flags);
    if (fd < 0) {
        printf("mtd: %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (ioctl(fd, MEMGETINFO, mi) < 0) {
        printf("mtd: %s: not a flash device\n", path);
        close(fd);
        return -1;
    }
    return fd;
}

static int info(void)
{
    for (int i = 0; i < 8; i++) {
        char name[16];
        struct mtd_info_user mi;
        snprintf(name, sizeof(name), "mtd%d", i);
        char path[24];
        snprintf(path, sizeof(path), "/dev/%s", name);
        int fd = open(path, O_RDONLY);
        if (fd < 0)
            break;
        if (!ioctl(fd, MEMGETINFO, &mi))
            printf("%-5s %6lu KB, erase block %lu KB%s\n", name, (unsigned long)(mi.size / 1024u),
                   (unsigned long)(mi.erasesize / 1024u), (mi.flags & MTD_WRITEABLE) ? "" : ", read-only");
        close(fd);
    }
    return 0;
}

static int dump(const char *dev, uint32_t off, uint32_t len)
{
    struct mtd_info_user mi;
    int fd = open_dev(dev, O_RDONLY, &mi);
    if (fd < 0)
        return 1;
    uint8_t buf[16];
    lseek(fd, (off_t)off, SEEK_SET);
    for (uint32_t done = 0; done < len; done += 16) {
        int n = (int)read(fd, buf, 16);
        if (n <= 0)
            break;
        printf("%08lx ", (unsigned long)(off + done));
        for (int k = 0; k < n; k++)
            printf(" %02x", buf[k]);
        printf("\n");
    }
    close(fd);
    return 0;
}

static int copy_out(const char *dev, uint32_t off, uint32_t len, const char *file)
{
    struct mtd_info_user mi;
    int fd = open_dev(dev, O_RDONLY, &mi);
    if (fd < 0)
        return 1;
    FILE *f = fopen(file, "wb");
    if (!f) {
        printf("mtd: %s: %s\n", file, strerror(errno));
        close(fd);
        return 1;
    }
    static uint8_t buf[4096];
    lseek(fd, (off_t)off, SEEK_SET);
    uint32_t done = 0;
    while (done < len) {
        int n = (int)read(fd, buf, len - done < sizeof(buf) ? len - done : sizeof(buf));
        if (n <= 0 || fwrite(buf, 1, (size_t)n, f) != (size_t)n)
            break;
        done += (uint32_t)n;
    }
    fclose(f);
    close(fd);
    printf("%lu byte(s) -> %s\n", (unsigned long)done, file);
    return done == len ? 0 : 1;
}

static int erase(const char *dev, uint32_t off, uint32_t len)
{
    struct mtd_info_user mi;
    int fd = open_dev(dev, O_RDWR, &mi);
    if (fd < 0)
        return 1;
    struct erase_info_user e = { off, len };
    uint64_t t0 = crtos_time_us();
    int r = ioctl(fd, MEMERASE, &e);
    close(fd);
    if (r < 0) {
        printf("mtd: erase: %s\n", strerror(errno));
        return 1;
    }
    printf("erased %lu KB in %lu ms\n", (unsigned long)(len / 1024u), (unsigned long)((crtos_time_us() - t0) / 1000u));
    return 0;
}

static uint8_t *load(const char *file, uint32_t *len)
{
    FILE *f = fopen(file, "rb");
    if (!f) {
        printf("mtd: %s: %s\n", file, strerror(errno));
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = size > 0 ? malloc((size_t)size) : NULL;
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        printf("mtd: %s: cannot read it\n", file);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (uint32_t)size;
    return buf;
}

static int write_file(const char *dev, uint32_t off, const char *file)
{
    uint32_t len;
    uint8_t *buf = load(file, &len);
    if (!buf)
        return 1;
    struct mtd_info_user mi;
    int fd = open_dev(dev, O_RDWR, &mi);
    if (fd < 0) {
        free(buf);
        return 1;
    }
    lseek(fd, (off_t)off, SEEK_SET);
    uint64_t t0 = crtos_time_us();
    int n = (int)write(fd, buf, len);
    close(fd);
    free(buf);
    if (n != (int)len) {
        printf("mtd: write: %s\n", n < 0 ? strerror(errno) : "short");
        return 1;
    }
    printf("wrote %lu byte(s) in %lu ms\n", (unsigned long)len, (unsigned long)((crtos_time_us() - t0) / 1000u));
    return 0;
}

/* erase a block, write a pattern, compare; then the block gets its old contents back */
static int test(const char *dev, uint32_t off)
{
    struct mtd_info_user mi;
    int fd = open_dev(dev, O_RDWR, &mi);
    if (fd < 0)
        return 1;
    uint32_t len = mi.erasesize;
    uint8_t *pat = malloc(len), *back = malloc(len), *keep = malloc(len);
    int bad = 1;
    if (!pat || !back || !keep) {
        printf("mtd: no memory\n");
        goto out;
    }
    lseek(fd, (off_t)off, SEEK_SET);
    if (read(fd, keep, len) != (int)len) {
        printf("mtd: cannot read the block\n");
        goto out;
    }
    uint32_t used = 0;
    for (uint32_t i = 0; i < len; i++)
        used += keep[i] != 0xFF;
    for (uint32_t i = 0; i < len; i++)
        pat[i] = (uint8_t)(i * 7u + (i >> 8));
    struct erase_info_user e = { off, len };
    uint64_t t0 = crtos_time_us();
    if (ioctl(fd, MEMERASE, &e) < 0) {
        printf("mtd: erase: %s\n", strerror(errno));
        goto out;
    }
    uint64_t t_erase = crtos_time_us() - t0;
    lseek(fd, (off_t)off, SEEK_SET);
    read(fd, back, len);
    uint32_t not_ff = 0;
    for (uint32_t i = 0; i < len; i++)
        not_ff += back[i] != 0xFF;
    lseek(fd, (off_t)off, SEEK_SET);
    t0 = crtos_time_us();
    int n = (int)write(fd, pat, len);
    uint64_t t_write = crtos_time_us() - t0;
    lseek(fd, (off_t)off, SEEK_SET);
    read(fd, back, len);
    uint32_t diff = 0;
    for (uint32_t i = 0; i < len; i++)
        diff += back[i] != pat[i];
    /* the old contents back */
    ioctl(fd, MEMERASE, &e);
    uint32_t lost = 0;
    if (used) {
        lseek(fd, (off_t)off, SEEK_SET);
        write(fd, keep, len);
        lseek(fd, (off_t)off, SEEK_SET);
        read(fd, back, len);
        for (uint32_t i = 0; i < len; i++)
            lost += back[i] != keep[i];
    }
    printf("block %08lx: erase %lu ms (%lu byte(s) not erased), write %d byte(s) in %lu ms, %lu byte(s) differ\n",
           (unsigned long)off, (unsigned long)(t_erase / 1000u), (unsigned long)not_ff, n,
           (unsigned long)(t_write / 1000u), (unsigned long)diff);
    printf("its %lu byte(s) of data put back%s\n", (unsigned long)used, lost ? " - NOT ALL" : "");
    bad = not_ff || n != (int)len || diff || lost;
    printf("test: %s\n", bad ? "FAILED" : "ok");
out:
    free(pat);
    free(back);
    free(keep);
    close(fd);
    return bad;
}

static int kernel(const char *file)
{
    uint32_t len;
    uint8_t *img = load(file, &len);
    if (!img)
        return 1;
    struct mtd_info_user mi;
    int fd = open_dev("mtd0", O_RDONLY, &mi);
    if (fd < 0) {
        free(img);
        return 1;
    }
    struct mtd_kernel_update ku = { (uintptr_t)img, len, crc32_of(img, len) };
    printf("writing the kernel %s (%lu bytes, crc %08lx), the board restarts...\n", file, (unsigned long)len,
           (unsigned long)ku.crc32);
    fflush(stdout);
    crtos_sleep_ms(100);
    int r = ioctl(fd, MTD_IOC_KERNEL_UPDATE, &ku); /* (returns only when refused) */
    printf("mtd: kernel: %s\n", strerror(errno));
    close(fd);
    free(img);
    return r ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "info"))
        return info();
    if (argc >= 4 && !strcmp(argv[1], "dump"))
        return dump(argv[2], (uint32_t)strtoul(argv[3], NULL, 0), argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 0) : 256u);
    if (argc >= 6 && !strcmp(argv[1], "read"))
        return copy_out(argv[2], (uint32_t)strtoul(argv[3], NULL, 0), (uint32_t)strtoul(argv[4], NULL, 0), argv[5]);
    if (argc >= 5 && !strcmp(argv[1], "erase"))
        return erase(argv[2], (uint32_t)strtoul(argv[3], NULL, 0), (uint32_t)strtoul(argv[4], NULL, 0));
    if (argc >= 5 && !strcmp(argv[1], "write"))
        return write_file(argv[2], (uint32_t)strtoul(argv[3], NULL, 0), argv[4]);
    if (argc >= 4 && !strcmp(argv[1], "test"))
        return test(argv[2], (uint32_t)strtoul(argv[3], NULL, 0));
    if (argc >= 3 && !strcmp(argv[1], "kernel"))
        return kernel(argv[2]);
    printf("usage: mtd info | dump DEV OFF [LEN] | read DEV OFF LEN FILE | erase DEV OFF LEN\n"
           "           | write DEV OFF FILE | test DEV OFF | kernel FILE\n");
    return 2;
}
