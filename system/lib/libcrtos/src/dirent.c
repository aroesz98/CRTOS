/*
 * dirent.c - opendir/readdir on top of SYS_OPEN(O_DIRECTORY) and SYS_READDIR.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>

#ifndef O_DIRECTORY
#define O_DIRECTORY 0x200000
#endif

struct __crtos_dir {
    int fd;
    char *path;
    struct dirent ent;
    struct crtos_dirent raw;
};

DIR *opendir(const char *path)
{
    int fd = (int)crtos_sys(SYS_OPEN, (long)path, O_RDONLY | O_DIRECTORY, 0, 0);
    if (fd < 0)
        return NULL;
    DIR *d = (DIR *)calloc(1, sizeof(*d));
    if (d)
        d->path = strdup(path);
    if (!d || !d->path) {
        free(d);
        close(fd);
        errno = ENOMEM;
        return NULL;
    }
    d->fd = fd;
    return d;
}

struct dirent *readdir(DIR *d)
{
    long r = crtos_sys(SYS_READDIR, d->fd, (long)&d->raw, 0, 0);
    if (r <= 0)
        return NULL;
    uint32_t type = d->raw.mode & CRTOS_S_IFMT;
    d->ent.d_type = type == CRTOS_S_IFDIR ? DT_DIR : type == CRTOS_S_IFCHR ? DT_CHR : DT_REG;
    d->ent.d_size = d->raw.size;
    memcpy(d->ent.d_name, d->raw.name, sizeof(d->ent.d_name));
    return &d->ent;
}

void rewinddir(DIR *d)
{
    int fd = (int)crtos_sys(SYS_OPEN, (long)d->path, O_RDONLY | O_DIRECTORY, 0, 0);
    if (fd >= 0) {
        close(d->fd);
        d->fd = fd;
    }
}

int closedir(DIR *d)
{
    int r = close(d->fd);
    free(d->path);
    free(d);
    return r;
}
