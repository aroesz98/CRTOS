/*
 * flashfs - the flash file system /flash0 (flashfs.ko) through its control device /dev/flashfs0.
 *
 *     flashfs info            state, space, files, the record log
 *     flashfs ls [DIR]        files with their place in the flash (erase blocks, address, CRC)
 *     flashfs check           every file's data against the CRC-32 written with it
 *     flashfs format yes      a new, empty file system - every file on /flash0 is lost
 *
 * Needs the dev capability (the device); format also sys.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include <crtos/flashfs.h>

#define DEV     "/dev/flashfs0"
#define MOUNT   "/flash0"

static int open_dev(void)
{
    int fd = open(DEV, O_RDONLY);
    if (fd < 0)
        fprintf(stderr, "flashfs: %s: %s (is flashfs.ko loaded? the dev capability?)\n", DEV, strerror(errno));
    return fd;
}

static int info(int fd)
{
    struct flashfs_info in;
    if (ioctl(fd, FLASHFS_IOC_INFO, &in)) {
        fprintf(stderr, "flashfs: %s\n", strerror(errno));
        return 1;
    }
    unsigned kb = in.erasesize / 1024u;
    printf("state:        %s\n", in.state == FLASHFS_STATE_MOUNTED ? "mounted on " MOUNT
                                 : "not formatted (flashfs format yes)");
    printf("space:        %lu KB free of %lu KB (%lu of %lu blocks of %u KB)\n",
           (unsigned long)in.free_blocks * kb, (unsigned long)in.blocks * kb, (unsigned long)in.free_blocks,
           (unsigned long)in.blocks, kb);
    printf("largest file: %lu KB (the longest free run)\n", (unsigned long)in.largest_free * kb);
    printf("entries:      %lu files, %lu directories, %lu open\n", (unsigned long)in.files, (unsigned long)in.dirs,
           (unsigned long)in.open_files);
    printf("record log:   generation %lu, %lu of %lu slots used\n", (unsigned long)in.log_seq,
           (unsigned long)in.log_used, (unsigned long)in.log_slots);
    return 0;
}

static int list(int fd, const char *dir)
{
    char path[256];
    snprintf(path, sizeof(path), "%s%s", MOUNT, strcmp(dir, "/") ? dir : "");
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "flashfs: %s: %s\n", path, strerror(errno));
        return 1;
    }
    printf("%-32s %10s %6s %5s  %-10s %-8s\n", "name", "bytes", "block", "count", "address", "crc32");
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_type == DT_DIR) {
            printf("%s/\n", e->d_name);
            continue;
        }
        struct flashfs_file ff;
        memset(&ff, 0, sizeof(ff));
        if (snprintf(ff.path, sizeof(ff.path), "%s/%s", strcmp(dir, "/") ? dir : "", e->d_name) >=
            (int)sizeof(ff.path)) {
            printf("%-32s  (path too long)\n", e->d_name);
            continue;
        }
        if (ioctl(fd, FLASHFS_IOC_FILE, &ff)) {
            printf("%-32s  (%s)\n", e->d_name, strerror(errno));
            continue;
        }
        printf("%-32s %10lu %6lu %5lu  0x%08lx %08lx%s\n", e->d_name, (unsigned long)ff.size, (unsigned long)ff.block,
               (unsigned long)ff.blocks, (unsigned long)ff.address, (unsigned long)ff.crc32,
               ff.opens ? "  (open)" : "");
    }
    closedir(d);
    return 0;
}

int main(int argc, char **argv)
{
    const char *cmd = argc > 1 ? argv[1] : "info";
    if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help") || !strcmp(cmd, "help")) {
        printf("usage: flashfs info | ls [DIR] | check | format yes\n");
        return 0;
    }
    int fd = open_dev();
    if (fd < 0)
        return 1;
    int r = 0;
    if (!strcmp(cmd, "info")) {
        r = info(fd);
    } else if (!strcmp(cmd, "ls")) {
        r = list(fd, argc > 2 ? argv[2] : "/");
    } else if (!strcmp(cmd, "check")) {
        int bad = ioctl(fd, FLASHFS_IOC_CHECK, 0);
        if (bad < 0) {
            fprintf(stderr, "flashfs: %s\n", strerror(errno));
            r = 1;
        } else {
            printf("flashfs: %d damaged file(s)%s\n", bad, bad ? " - the names are in the kernel log" : "");
            r = bad ? 1 : 0;
        }
    } else if (!strcmp(cmd, "format")) {
        if (argc < 3 || strcmp(argv[2], "yes")) {
            fprintf(stderr, "flashfs: format erases every file on %s - type \"flashfs format yes\"\n", MOUNT);
            r = 2;
        } else if (ioctl(fd, FLASHFS_IOC_FORMAT, 0)) {
            fprintf(stderr, "flashfs: format: %s\n", strerror(errno));
            r = 1;
        } else {
            printf("flashfs: %s is empty\n", MOUNT);
            r = info(fd);
        }
    } else {
        fprintf(stderr, "flashfs: unknown command '%s' (info, ls, check, format)\n", cmd);
        r = 2;
    }
    close(fd);
    return r;
}
