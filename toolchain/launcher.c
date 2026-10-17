/*
 * launcher.c - arm-crtos-gcc, arm-crtos-g++ and the other arm-crtos-* commands of the CRTOS
 * toolchain (on the PC: Windows, Linux, macOS).
 *
 * One program under many names: arm-crtos-<tool> runs the Arm GNU Toolchain's
 * arm-none-eabi-<tool> with the same arguments. For the compiler drivers (gcc, g++, c++,
 * cpp) it adds what makes a CRTOS program out of a source file:
 *
 *   -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard   (each unless given)
 *   -specs=<root>/lib/crtos.specs -B<root>/lib/ -L<root>/lib -isystem <root>/include
 *
 * where <root> is the toolchain directory this program lies in (<root>/bin); with -mxip (a
 * program that runs in place) also -L<root>/lib/xip first. crtos.specs holds the rules
 * (partial link, start-up objects, libraries), so
 *
 *   arm-crtos-gcc -O2 hello.c -o hello.app
 *
 * gives a program the board runs. The Arm toolchain is found from, in this order: the
 * environment variable CRTOS_GCC_BIN (its bin directory), <root>/lib/gcc-bin.txt (written by
 * "crtos toolchain"), the PATH, and the usual install directories.
 *
 * Arguments go to the real program unchanged (no shell or cmd.exe in between).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define EXE ".exe"
#define SEP '\\'
#else
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#define EXE ""
#define SEP '/'
#endif

#define PREFIX "arm-crtos-"
#define TARGET "arm-none-eabi-"
#define PATH_LEN 4096

static const char *const cpu_flags[][2] = {
    /* option given by the user      what we add otherwise */
    { "-mcpu=",       "-mcpu=cortex-m7" },
    { "-mthumb",      "-mthumb" },
    { "-mfpu=",       "-mfpu=fpv5-d16" },
    { "-mfloat-abi=", "-mfloat-abi=hard" },
};

static void die(const char *what, const char *arg)
{
    fprintf(stderr, PREFIX "toolchain: %s%s%s\n", what, arg ? ": " : "", arg ? arg : "");
    exit(127);
}

static char *xstrdup(const char *s)
{
    char *p = malloc(strlen(s) + 1);
    if (!p)
        die("out of memory", NULL);
    return strcpy(p, s);
}

static char *join(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    char *p = malloc(la + lb + 1);
    if (!p)
        die("out of memory", NULL);
    memcpy(p, a, la);
    memcpy(p + la, b, lb + 1);
    return p;
}

/* a/b */
static char *path2(const char *a, const char *b)
{
    const char sep[2] = { SEP, 0 };
    char *t = join(a, sep);
    char *r = join(t, b);
    free(t);
    return r;
}

static int is_sep(char c)
{
#ifdef _WIN32
    return c == '\\' || c == '/';
#else
    return c == '/';
#endif
}

/* the directory part of a path (without the separator) */
static char *dir_of(const char *path)
{
    char *d = xstrdup(path);
    char *s = d + strlen(d);
    while (s > d && !is_sep(s[-1]))
        s--;
    while (s > d && is_sep(s[-1]))
        s--;
    *s = 0;
    return d;
}

static const char *base_of(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (is_sep(*p))
            b = p + 1;
    return b;
}

static int file_exists(const char *path)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

/* the path of this program */
static char *self_path(const char *argv0)
{
    char buf[PATH_LEN];
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf))
        return xstrdup(buf);
#elif defined(__APPLE__)
    uint32_t size = sizeof(buf);
    char real[PATH_MAX];
    if (_NSGetExecutablePath(buf, &size) == 0 && realpath(buf, real))
        return xstrdup(real);
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        return xstrdup(buf);
    }
    char real[PATH_MAX];
    if (realpath(argv0, real))
        return xstrdup(real);
#endif
    return xstrdup(argv0);
}

/* <dir>/arm-none-eabi-gcc exists: the Arm toolchain's bin directory */
static int is_gcc_bin(const char *dir)
{
    char *q = path2(dir, TARGET "gcc" EXE);
    int ok = file_exists(q);
    free(q);
    return ok;
}

static char *trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

static char *from_path(void)
{
    const char *path = getenv("PATH");
    if (!path)
        return NULL;
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    char *copy = xstrdup(path);
    for (char *s = copy, *e; s; s = e) {
        e = strchr(s, sep);
        if (e)
            *e++ = 0;
        if (*s && is_gcc_bin(s)) {
            char *r = xstrdup(s);
            free(copy);
            return r;
        }
    }
    free(copy);
    return NULL;
}

/* the newest <pattern-dir>/<version>/bin that holds the compiler */
static char *newest_in(const char *parent, const char *tail)
{
    char *best = NULL, *best_name = NULL;
#ifdef _WIN32
    char *pat = join(parent, "\\*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    free(pat);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    do {
        const char *name = fd.cFileName;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || name[0] == '.')
            continue;
#else
    DIR *d = opendir(parent);
    if (!d)
        return NULL;
    struct dirent *de;
    while ((de = readdir(d))) {
        const char *name = de->d_name;
        if (name[0] == '.')
            continue;
#endif
        char *b = path2(parent, name);
        char *c = join(b, tail);
        free(b);
        if (is_gcc_bin(c) && (!best_name || strcmp(name, best_name) > 0)) {
            free(best);
            free(best_name);
            best = c;
            best_name = xstrdup(name);
        } else {
            free(c);
        }
#ifdef _WIN32
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    }
    closedir(d);
#endif
    free(best_name);
    return best;
}

static char *find_gcc_bin(const char *root)
{
    const char *env = getenv("CRTOS_GCC_BIN");
    if (env && *env) {
        if (!is_gcc_bin(env))
            die("CRTOS_GCC_BIN does not hold " TARGET "gcc", env);
        return xstrdup(env);
    }
    char *lib = path2(root, "lib");
    char *file = path2(lib, "gcc-bin.txt");
    free(lib);
    FILE *fp = fopen(file, "r");
    free(file);
    if (fp) {
        char line[PATH_LEN];
        char *r = NULL;
        if (fgets(line, sizeof(line), fp)) {
            char *t = trim(line);
            if (*t && is_gcc_bin(t))
                r = xstrdup(t);
        }
        fclose(fp);
        if (r)
            return r;
    }
    char *r = from_path();
    if (r)
        return r;
#ifdef _WIN32
    static const char *const places[][2] = {
        { "C:\\Program Files (x86)\\Arm GNU Toolchain arm-none-eabi", "\\bin" },
        { "C:\\Program Files\\Arm GNU Toolchain arm-none-eabi", "\\bin" },
        { "C:\\Program Files (x86)\\GNU Arm Embedded Toolchain", "\\bin" },
        { "C:\\nxp", "\\ide\\tools\\bin" },
    };
#else
    static const char *const places[][2] = {
        { "/opt", "/bin" },
        { "/usr/local", "/bin" },
        { "/Applications/ArmGNUToolchain", "/arm-none-eabi/bin" },
    };
#endif
    for (size_t i = 0; i < sizeof(places) / sizeof(places[0]); i++) {
        r = newest_in(places[i][0], places[i][1]);
        if (r)
            return r;
    }
    return NULL;
}

static int starts(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

#ifdef _WIN32
/* one argument quoted the way the C runtime splits command lines */
static void append_quoted(char **buf, size_t *len, size_t *cap, const char *arg)
{
    size_t need = *len + 2 * strlen(arg) + 4;
    if (need > *cap) {
        *cap = need * 2;
        *buf = realloc(*buf, *cap);
        if (!*buf)
            die("out of memory", NULL);
    }
    char *o = *buf + *len;
    if (*len)
        *o++ = ' ';
    if (*arg && !strpbrk(arg, " \t\n\v\"")) {
        strcpy(o, arg);
        o += strlen(arg);
    } else {
        *o++ = '"';
        for (const char *p = arg;; p++) {
            size_t bs = 0;
            while (*p == '\\') {
                p++;
                bs++;
            }
            if (!*p) {
                for (size_t k = 0; k < bs * 2; k++)
                    *o++ = '\\';
                break;
            }
            if (*p == '"') {
                for (size_t k = 0; k < bs * 2 + 1; k++)
                    *o++ = '\\';
            } else {
                for (size_t k = 0; k < bs; k++)
                    *o++ = '\\';
            }
            *o++ = *p;
        }
        *o++ = '"';
    }
    *o = 0;
    *len = (size_t)(o - *buf);
}

static int run(const char *prog, char **argv)
{
    size_t len = 0, cap = 256;
    char *cmd = malloc(cap);
    if (!cmd)
        die("out of memory", NULL);
    cmd[0] = 0;
    for (int i = 0; argv[i]; i++)
        append_quoted(&cmd, &len, &cap, argv[i]);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    /* Ctrl-C reaches the child as well; this process waits for it to end */
    SetConsoleCtrlHandler(NULL, TRUE);
    if (!CreateProcessA(prog, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        die("cannot start", prog);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}
#else
static int run(const char *prog, char **argv)
{
    execv(prog, argv);
    die("cannot start", prog);
    return 127;
}
#endif

int main(int argc, char **argv)
{
    char *self = self_path(argv[0]);
    char *bin = dir_of(self);
    char *root = dir_of(bin);
    char *name = xstrdup(base_of(self));
    size_t nl = strlen(name);
    if (nl > 4 && (!strcmp(name + nl - 4, ".exe") || !strcmp(name + nl - 4, ".EXE")))
        name[nl - 4] = 0;
    if (!starts(name, PREFIX))
        die("this program must be called " PREFIX "<tool>, not", name);
    const char *tool = name + strlen(PREFIX);

    char *gcc_bin = find_gcc_bin(root);
    if (!gcc_bin)
        die("no Arm GNU Toolchain (" TARGET "gcc) found - install it (crtos setup), put it on the PATH or set CRTOS_GCC_BIN", NULL);
    char *t = join(TARGET, tool);
    char *prog = join(path2(gcc_bin, t), EXE);
    if (!file_exists(prog))
        die("no such tool", prog);

    int driver = !strcmp(tool, "gcc") || !strcmp(tool, "g++") || !strcmp(tool, "c++") || !strcmp(tool, "cpp");
    char **nargv = calloc((size_t)argc + 16, sizeof(char *));
    if (!nargv)
        die("out of memory", NULL);
    int n = 0;
    nargv[n++] = prog;
    if (driver) {
        for (size_t f = 0; f < sizeof(cpu_flags) / sizeof(cpu_flags[0]); f++) {
            int given = 0;
            for (int i = 1; i < argc; i++) {
                if (starts(argv[i], cpu_flags[f][0]) ||
                    (f == 0 && starts(argv[i], "-march=")) || (f == 1 && !strcmp(argv[i], "-marm")))
                    given = 1;
            }
            if (!given)
                nargv[n++] = (char *)cpu_flags[f][1];
        }
        char *lib = path2(root, "lib");
        nargv[n++] = join("-specs=", path2(lib, "crtos.specs"));
        nargv[n++] = join("-B", path2(lib, ""));
        /* a program that runs in place: the position-independent libraries first */
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-mxip")) {
                nargv[n++] = join("-L", path2(lib, "xip"));
                break;
            }
        }
        nargv[n++] = join("-L", lib);
        nargv[n++] = "-isystem";
        nargv[n++] = path2(root, "include");
        /* for crtos.specs (%:getenv) */
#ifdef _WIN32
        SetEnvironmentVariableA("CRTOS_SYSROOT", root);
#else
        setenv("CRTOS_SYSROOT", root, 1);
#endif
    }
    for (int i = 1; i < argc; i++)
        nargv[n++] = argv[i];
    nargv[n] = NULL;
    return run(prog, nargv);
}
