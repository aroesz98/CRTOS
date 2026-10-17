/*
 * libgen.c - the POSIX basename() and dirname() of <libgen.h>, which newlib does not have here.
 */
#include <string.h>

/* the POSIX versions: they may change the string */
char *basename(char *path)
{
    static char dot[] = ".";
    if (!path || !*path)
        return dot;
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/')
        path[--n] = 0;
    char *s = strrchr(path, '/');
    return s && s[1] ? s + 1 : path;
}

char *dirname(char *path)
{
    static char dot[] = ".";
    if (!path || !*path)
        return dot;
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/')
        n--;
    while (n > 0 && path[n - 1] != '/')
        n--;
    if (!n)
        return dot;
    while (n > 1 && path[n - 1] == '/')
        n--;
    path[n] = 0;
    return path;
}
