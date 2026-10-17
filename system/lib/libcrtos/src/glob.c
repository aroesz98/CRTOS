/*
 * glob.c - glob() and globfree(), which newlib declares but does not have here.
 *
 * A pattern in every part of a path (like src/ab?/x*.c), with the patterns of fnmatch()
 * (fnmatch.c); names starting with "." match only a pattern part starting with ".". Flags:
 * GLOB_APPEND, GLOB_DOOFFS, GLOB_ERR, GLOB_MARK, GLOB_NOCHECK, GLOB_NOESCAPE, GLOB_NOSORT.
 */
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <glob.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PATH_LEN 256

#ifndef GLOB_NOESCAPE
#define GLOB_NOESCAPE 0x2000    /* POSIX; newlib's (BSD) header has only GLOB_QUOTE */
#endif

static bool magic(const char *s, size_t n, bool noescape)
{
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '*' || s[i] == '?' || s[i] == '[')
            return true;
        if (s[i] == '\\' && !noescape && i + 1 < n)
            i++;
    }
    return false;
}

struct acc {
    char **v;
    int n, cap;
    bool nospace;
};

static void add(struct acc *a, const char *path, bool mark)
{
    if (a->nospace)
        return;
    if (a->n == a->cap) {
        int cap = a->cap ? a->cap * 2 : 16;
        char **v = realloc(a->v, (size_t)cap * sizeof(char *));
        if (!v) {
            a->nospace = true;
            return;
        }
        a->v = v;
        a->cap = cap;
    }
    struct stat st;
    bool dir = mark && !stat(path, &st) && S_ISDIR(st.st_mode);
    char *p = malloc(strlen(path) + 2);
    if (!p) {
        a->nospace = true;
        return;
    }
    strcpy(p, path);
    if (dir && p[strlen(p) - 1] != '/')
        strcat(p, "/");
    a->v[a->n++] = p;
}

/* @prefix (a path so far, "" or ending in /) followed by the rest of the pattern @rest */
static int expand(const char *prefix, const char *rest, int flags, int (*errfunc)(const char *, int), struct acc *a)
{
    bool noesc = (flags & GLOB_NOESCAPE) != 0;
    while (*rest == '/')
        rest++;
    if (!*rest) {
        add(a, prefix, (flags & GLOB_MARK) != 0);
        return 0;
    }
    const char *slash = strchr(rest, '/');
    size_t n = slash ? (size_t)(slash - rest) : strlen(rest);
    char path[PATH_LEN];
    if (!magic(rest, n, noesc)) {
        /* a plain part: take it as it is (without its escapes) */
        size_t pl = strlen(prefix), o = pl;
        if (pl + n + 2 > sizeof(path))
            return 0;
        memcpy(path, prefix, pl);
        for (size_t i = 0; i < n; i++) {
            if (rest[i] == '\\' && !noesc && i + 1 < n)
                i++;
            path[o++] = rest[i];
        }
        path[o] = 0;
        struct stat st;
        if (stat(path, &st))
            return 0;
        if (slash) {
            if (!S_ISDIR(st.st_mode))
                return 0;
            path[o++] = '/';
            path[o] = 0;
        }
        return expand(path, rest + n, flags, errfunc, a);
    }
    char part[PATH_LEN];
    if (n >= sizeof(part))
        return 0;
    memcpy(part, rest, n);
    part[n] = 0;
    DIR *d = opendir(*prefix ? prefix : ".");
    if (!d) {
        if ((errfunc && errfunc(*prefix ? prefix : ".", errno)) || (flags & GLOB_ERR))
            return GLOB_ABEND;
        return 0;
    }
    struct dirent *e;
    int r = 0;
    while (!r && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        if (fnmatch(part, e->d_name, FNM_PERIOD | (noesc ? FNM_NOESCAPE : 0)))
            continue;
        if (snprintf(path, sizeof(path), "%s%s%s", prefix, e->d_name, slash ? "/" : "") >= (int)sizeof(path))
            continue;
        if (slash) {
            struct stat st;
            if (stat(path, &st) || !S_ISDIR(st.st_mode))
                continue;
        }
        r = expand(path, rest + n, flags, errfunc, a);
    }
    closedir(d);
    return r;
}

static int cmp(const void *x, const void *y)
{
    return strcmp(*(char *const *)x, *(char *const *)y);
}

int glob(const char *pattern, int flags, int (*errfunc)(const char *, int), glob_t *g)
{
    if (!(flags & GLOB_APPEND)) {
        g->gl_pathc = 0;
        g->gl_pathv = NULL;
        if (!(flags & GLOB_DOOFFS))
            g->gl_offs = 0;
    }
    g->gl_flags = flags;
    struct acc a = { NULL, 0, 0, false };
    int r = expand(pattern[0] == '/' ? "/" : "", pattern, flags, errfunc, &a);
    g->gl_matchc = a.n;
    if (!r && !a.n && !a.nospace) {
        if (flags & GLOB_NOCHECK)
            add(&a, pattern, false);
        else
            r = GLOB_NOMATCH;
    }
    if (a.nospace)
        r = GLOB_NOSPACE;
    if (!(flags & GLOB_NOSORT) && a.n > 1)
        qsort(a.v, (size_t)a.n, sizeof(char *), cmp);
    /* into gl_pathv: offsets, what was there (GLOB_APPEND), the new names, NULL */
    size_t offs = (flags & GLOB_DOOFFS) ? (size_t)g->gl_offs : 0;
    size_t old = (size_t)g->gl_pathc;
    char **v = realloc(g->gl_pathv, (offs + old + (size_t)a.n + 1) * sizeof(char *));
    if (!v) {
        for (int i = 0; i < a.n; i++)
            free(a.v[i]);
        free(a.v);
        return GLOB_NOSPACE;
    }
    if (!g->gl_pathv)
        for (size_t i = 0; i < offs; i++)
            v[i] = NULL;
    for (int i = 0; i < a.n; i++)
        v[offs + old + (size_t)i] = a.v[i];
    v[offs + old + (size_t)a.n] = NULL;
    g->gl_pathv = v;
    g->gl_pathc = (int)(old + (size_t)a.n);
    free(a.v);
    return r;
}

void globfree(glob_t *g)
{
    if (!g->gl_pathv)
        return;
    size_t offs = (g->gl_flags & GLOB_DOOFFS) ? (size_t)g->gl_offs : 0;
    for (int i = 0; i < g->gl_pathc; i++)
        free(g->gl_pathv[offs + (size_t)i]);
    free(g->gl_pathv);
    g->gl_pathv = NULL;
    g->gl_pathc = 0;
}
