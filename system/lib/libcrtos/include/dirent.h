/*
 * dirent.h - directory reading (newlib has no implementation for bare-metal targets).
 */
#ifndef _CRTOS_DIRENT_H
#define _CRTOS_DIRENT_H

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DT_UNKNOWN  0
#define DT_CHR      2
#define DT_DIR      4
#define DT_REG      8

struct dirent {
    unsigned char d_type;
    long long d_size;           /* CRTOS extension: file size */
    char d_name[256];
};

typedef struct __crtos_dir DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);
void rewinddir(DIR *dir);

#ifdef __cplusplus
}
#endif

#endif
