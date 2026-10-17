/*
 * where - where a program is: every match in the directories of $PATH, in the order the shell
 * looks at them (<dir>/<name>.app, then <dir>/<name>), like "where" of Windows and "which -a":
 *
 *     where [-q] [-t] NAME...              NAME may be a pattern: * ? [...] (quote it for sh)
 *     where [-q] [-t] -r DIR NAME...       any file under DIR (and below) with that name
 *
 *     -q   nothing printed, only the exit code: 0 each NAME found, 1 something not, 2 misuse
 *     -t   the size and the time of each file too
 *
 * A pattern matches a program's name with or without ".app" (where 'ed*' finds edit.app).
 * The shell's own commands (cd, ls...) are no files: "which" in the shell names them.
 */
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <crtos.h>

#define PATH_LEN 256

static bool s_quiet, s_times;

static bool is_pattern(const char *s)
{
    return strpbrk(s, "*?[") != NULL;
}

/* @entry is what @name means: the same, or the same with ".app" (for patterns: matching) */
static bool matches(const char *name, const char *entry)
{
    char bare[PATH_LEN];
    size_t n = strlen(entry);
    snprintf(bare, sizeof(bare), "%s", entry);
    if (n > 4 && !strcmp(entry + n - 4, ".app"))
        bare[n - 4] = 0;
    if (is_pattern(name))
        return !fnmatch(name, entry, 0) || !fnmatch(name, bare, 0);
    return !strcmp(name, entry) || !strcmp(name, bare);
}

static void show(const char *path, const struct stat *st)
{
    if (s_quiet)
        return;
    if (!s_times) {
        printf("%s\n", path);
        return;
    }
    char when[24] = "?";
    time_t t = st->st_mtime;
    struct tm *tm = localtime(&t);
    if (tm)
        strftime(when, sizeof(when), "%Y-%m-%d %H:%M", tm);
    printf("%10lu  %s  %s\n", (unsigned long)st->st_size, when, path);
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The matches of @name in @dir, sorted (programs first in the order the shell tries them);
 * with @deep also in the directories below */
static int search_dir(const char *dir, const char *name, bool deep)
{
    int found = 0;
    if (!is_pattern(name) && !deep) { /* a program name: NAME.app before NAME, as the shell runs them */
        char path[PATH_LEN];
        struct stat st;
        snprintf(path, sizeof(path), "%s/%s.app", dir, name);
        if (!stat(path, &st) && !S_ISDIR(st.st_mode)) {
            show(path, &st);
            found++;
        }
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        if (!stat(path, &st) && !S_ISDIR(st.st_mode)) {
            show(path, &st);
            found++;
        }
        return found;
    }
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    char *names[256];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < 256) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        names[n] = strdup(e->d_name);
        if (names[n])
            n++;
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof(names[0]), by_name);
    for (int i = 0; i < n; i++) {
        char path[PATH_LEN];
        struct stat st;
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        bool isdir = !stat(path, &st) && S_ISDIR(st.st_mode);
        if (matches(name, names[i]) && (deep || !isdir)) {
            show(path, &st);
            found++;
        }
        if (deep && isdir)
            found += search_dir(path, name, true);
        free(names[i]);
    }
    return found;
}

static int usage(void)
{
    fprintf(stderr, "usage: where [-q] [-t] NAME...\n"
                    "       where [-q] [-t] -r DIR NAME...    (NAME may be a pattern: * ? [...])\n");
    return 2;
}

int main(int argc, char **argv)
{
    const char *root = NULL;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "-q"))
            s_quiet = true;
        else if (!strcmp(argv[i], "-t"))
            s_times = true;
        else if (!strcmp(argv[i], "-r") && i + 1 < argc)
            root = argv[++i];
        else
            return usage();
    }
    if (i >= argc)
        return usage();
    const char *path = getenv("PATH");
    int status = 0;
    for (; i < argc; i++) {
        const char *name = argv[i];
        int found = 0;
        if (root) {
            size_t n = strlen(root);
            char top[PATH_LEN];
            snprintf(top, sizeof(top), "%.*s", (int)(n > 1 && root[n - 1] == '/' ? n - 1 : n), root);
            found = search_dir(top, name, true);
        } else if (strchr(name, '/')) { /* a path: the shell runs it as it is */
            struct stat st;
            if (!stat(name, &st) && !S_ISDIR(st.st_mode)) {
                show(name, &st);
                found = 1;
            }
        } else {
            char dirs[PATH_LEN * 2];
            snprintf(dirs, sizeof(dirs), "%s", path ? path : "/sd/crtos/bin");
            for (char *dir = strtok(dirs, ":"); dir; dir = strtok(NULL, ":"))
                found += search_dir(dir, name, false);
        }
        if (!found) {
            if (!s_quiet)
                fprintf(stderr, "where: %s: not found%s\n", name, root ? "" : " in PATH");
            status = 1;
        }
    }
    return status;
}
