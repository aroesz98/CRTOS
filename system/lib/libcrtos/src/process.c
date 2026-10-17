/*
 * process.c - POSIX process functions on crtos_spawn()/crtos_wait(): posix_spawn(p) with file
 * actions, waitpid() with the usual status encoding, system(), popen()/pclose().
 *
 * CRTOS has no fork or exec: a child is started from a program file with its argument and
 * environment vectors, and inherits only standard input, output and error (handles 0-2) and
 * the current directory. The file actions of posix_spawn are therefore applied to a picture
 * of the child's handles 0-2 kept here: dup2 onto 0-2 chooses which of our handles the child
 * gets there, close of 0-2 gives it /dev/null, open onto 0-2 opens the file here and passes
 * it on, chdir changes our directory around the start. Actions on other handles concern
 * nothing the child would get and are ignored.
 *
 * Exit status: a normal exit is (code & 0xff) << 8 (WIFEXITED); a process the kernel ended
 * (memory fault: -EFAULT, Ctrl-C: -EINTR, crtos_kill with a negative code) counts as ended by
 * a signal (WIFSIGNALED: SIGSEGV, SIGINT, SIGKILL).
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <crtos.h>

extern char **environ;

#define DEFAULT_SHELL "/sd/crtos/bin/sh.app"

/* ---- file actions and attributes ---------------------------------------------------------- */

enum { FA_DUP2, FA_CLOSE, FA_OPEN, FA_CHDIR };

struct fa {
    int kind, fd, newfd, oflag;
    mode_t mode;
    char *path;
};

struct __posix_spawn_file_actions {
    int n, cap;
    struct fa *a;
};

struct __posix_spawnattr {
    short flags;
    pid_t pgroup;
};

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *fa)
{
    *fa = calloc(1, sizeof(**fa));
    return *fa ? 0 : ENOMEM;
}

int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *fa)
{
    if (!fa || !*fa)
        return EINVAL;
    for (int i = 0; i < (*fa)->n; i++)
        free((*fa)->a[i].path);
    free((*fa)->a);
    free(*fa);
    *fa = NULL;
    return 0;
}

static struct fa *fa_add(posix_spawn_file_actions_t *fa, int kind)
{
    struct __posix_spawn_file_actions *f = *fa;
    if (f->n == f->cap) {
        int cap = f->cap ? f->cap * 2 : 4;
        struct fa *a = realloc(f->a, (size_t)cap * sizeof(*a));
        if (!a)
            return NULL;
        f->a = a;
        f->cap = cap;
    }
    struct fa *x = &f->a[f->n++];
    memset(x, 0, sizeof(*x));
    x->kind = kind;
    return x;
}

int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *fa, int fd, int newfd)
{
    if (fd < 0 || newfd < 0)
        return EBADF;
    struct fa *x = fa_add(fa, FA_DUP2);
    if (!x)
        return ENOMEM;
    x->fd = fd;
    x->newfd = newfd;
    return 0;
}

int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *fa, int fd)
{
    if (fd < 0)
        return EBADF;
    struct fa *x = fa_add(fa, FA_CLOSE);
    if (!x)
        return ENOMEM;
    x->fd = fd;
    return 0;
}

int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *fa, int fd, const char *path, int oflag, mode_t mode)
{
    if (fd < 0)
        return EBADF;
    char *p = strdup(path);
    struct fa *x = p ? fa_add(fa, FA_OPEN) : NULL;
    if (!x) {
        free(p);
        return ENOMEM;
    }
    x->fd = fd;
    x->path = p;
    x->oflag = oflag;
    x->mode = mode;
    return 0;
}

int posix_spawn_file_actions_addchdir_np(posix_spawn_file_actions_t *fa, const char *path)
{
    char *p = strdup(path);
    struct fa *x = p ? fa_add(fa, FA_CHDIR) : NULL;
    if (!x) {
        free(p);
        return ENOMEM;
    }
    x->path = p;
    return 0;
}

int posix_spawn_file_actions_addchdir(posix_spawn_file_actions_t *fa, const char *path)
{
    return posix_spawn_file_actions_addchdir_np(fa, path);
}

int posix_spawnattr_init(posix_spawnattr_t *at)
{
    *at = calloc(1, sizeof(**at));
    return *at ? 0 : ENOMEM;
}

int posix_spawnattr_destroy(posix_spawnattr_t *at)
{
    free(*at);
    *at = NULL;
    return 0;
}

int posix_spawnattr_setflags(posix_spawnattr_t *at, short flags)
{
    (*at)->flags = flags;
    return 0;
}

int posix_spawnattr_getflags(const posix_spawnattr_t *at, short *flags)
{
    *flags = (*at)->flags;
    return 0;
}

int posix_spawnattr_setpgroup(posix_spawnattr_t *at, pid_t pg)
{
    (*at)->pgroup = pg;
    return 0;
}

int posix_spawnattr_getpgroup(const posix_spawnattr_t *at, pid_t *pg)
{
    *pg = (*at)->pgroup;
    return 0;
}

/* signal masks and scheduling do not exist here: accepted, without effect */
int posix_spawnattr_setsigmask(posix_spawnattr_t *at, const sigset_t *s)
{
    (void)at;
    (void)s;
    return 0;
}

int posix_spawnattr_setsigdefault(posix_spawnattr_t *at, const sigset_t *s)
{
    (void)at;
    (void)s;
    return 0;
}

/* ---- starting ----------------------------------------------------------------------------- */

/* the child's handles 0-2 as our handles: -1 ours, -2 /dev/null */
struct child_fds {
    int fd[3];
    int opened[8], nopened;     /* handles opened here for the child, closed after the start */
    char *cwd;                  /* our directory, when a chdir action changed it */
};

static int our_fd(const struct child_fds *c, int fd)
{
    if (fd < 3)
        return c->fd[fd] == -1 ? fd : c->fd[fd];
    return fd;
}

/* errno value, or 0 */
static int apply_actions(const posix_spawn_file_actions_t *fa, struct child_fds *c)
{
    if (!fa || !*fa)
        return 0;
    for (int i = 0; i < (*fa)->n; i++) {
        const struct fa *x = &(*fa)->a[i];
        switch (x->kind) {
        case FA_DUP2:
            if (x->newfd < 3)
                c->fd[x->newfd] = our_fd(c, x->fd);
            break;
        case FA_CLOSE:
            if (x->fd < 3)
                c->fd[x->fd] = -2;
            break;
        case FA_OPEN:
            if (x->fd < 3) {
                if (c->nopened == (int)(sizeof(c->opened) / sizeof(c->opened[0])))
                    return EMFILE;
                int h = open(x->path, x->oflag, x->mode);
                if (h < 0)
                    return errno;
                c->opened[c->nopened++] = h;
                c->fd[x->fd] = h;
            }
            break;
        case FA_CHDIR:
            if (!c->cwd) {
                c->cwd = malloc(256);
                if (!c->cwd)
                    return ENOMEM;
                if (!getcwd(c->cwd, 256)) {
                    free(c->cwd);
                    c->cwd = NULL;
                    return errno;
                }
            }
            if (chdir(x->path))
                return errno;
            break;
        }
    }
    return 0;
}

static void undo_actions(struct child_fds *c)
{
    for (int i = 0; i < c->nopened; i++)
        close(c->opened[i]);
    if (c->cwd) {
        chdir(c->cwd);
        free(c->cwd);
    }
}

static int start(pid_t *pid, const char *path, const posix_spawn_file_actions_t *fa, char *const argv[],
                 char *const envp[])
{
    struct child_fds c;
    memset(&c, 0, sizeof(c));
    c.fd[0] = c.fd[1] = c.fd[2] = -1;
    int err = apply_actions(fa, &c);
    int null = -1;
    if (!err) {
        for (int i = 0; i < 3; i++) {
            if (c.fd[i] == -2) {
                if (null < 0)
                    null = open("/dev/null", O_RDWR);
                if (null < 0) {
                    err = errno;
                    break;
                }
                c.fd[i] = null;
            }
        }
    }
    if (!err) {
        struct crtos_spawn sp;
        memset(&sp, 0, sizeof(sp));
        sp.path = path;
        sp.argv = (const char *const *)argv;
        sp.envp = (const char *const *)(envp ? envp : environ);
        for (int i = 0; i < 3; i++)
            sp.stdio[i] = c.fd[i];
        sp.caps = CAP_ALL; /* at most ours: the kernel cuts it down */
        int r = crtos_spawn(&sp);
        if (r < 0)
            err = errno;
        else if (pid)
            *pid = r;
    }
    if (null >= 0)
        close(null);
    undo_actions(&c);
    return err;
}

int posix_spawn(pid_t *pid, const char *path, const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *at,
                char *const argv[], char *const envp[])
{
    (void)at;
    return start(pid, path, fa, argv, envp);
}

static int is_file(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* @file in the PATH (also as file.app, the name of programs here); 0 or an errno value */
static int search_path(const char *file, char *out, size_t size)
{
    const char *path = getenv("PATH");
    if (!path || !*path)
        path = "/sd/crtos/bin";
    for (const char *s = path; *s;) {
        const char *e = strchr(s, ':');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        if (n && n + strlen(file) + 6 < size) {
            memcpy(out, s, n);
            out[n] = '/';
            strcpy(out + n + 1, file);
            if (is_file(out))
                return 0;
            strcat(out, ".app");
            if (is_file(out))
                return 0;
        }
        s += n;
        if (*s == ':')
            s++;
    }
    return ENOENT;
}

int posix_spawnp(pid_t *pid, const char *file, const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *at,
                 char *const argv[], char *const envp[])
{
    (void)at;
    if (strchr(file, '/'))
        return start(pid, file, fa, argv, envp);
    char path[256];
    int err = search_path(file, path, sizeof(path));
    return err ? err : start(pid, path, fa, argv, envp);
}

/* ---- waiting ------------------------------------------------------------------------------ */

static int encode_status(int code)
{
    if (code >= 0)
        return (code & 0xff) << 8;
    if (code == -EFAULT)
        return SIGSEGV;
    if (code == -EINTR)
        return SIGINT;
    return SIGKILL;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    int code = 0;
    int r = crtos_wait(pid > 0 ? pid : -1, &code, (options & WNOHANG) ? 0 : CRTOS_FOREVER);
    if (r < 0) {
        if ((options & WNOHANG) && errno == ETIMEDOUT)
            return 0;
        return -1;
    }
    if (status)
        *status = encode_status(code);
    return r;
}

int _wait(int *status)
{
    return waitpid(-1, status, 0);
}

/* ---- the shell ---------------------------------------------------------------------------- */

static const char *shell(void)
{
    const char *s = getenv("SHELL");
    return s && *s ? s : DEFAULT_SHELL;
}

int system(const char *cmd)
{
    if (!cmd)
        return is_file(shell());
    char *argv[] = { "sh", "-c", (char *)cmd, NULL };
    pid_t pid;
    int err = posix_spawn(&pid, shell(), NULL, NULL, argv, environ);
    if (err) {
        errno = err;
        return -1;
    }
    int status;
    return waitpid(pid, &status, 0) < 0 ? -1 : status;
}

struct popened {
    FILE *f;
    pid_t pid;
};
static struct popened s_popened[8];

FILE *popen(const char *cmd, const char *mode)
{
    int writing = mode[0] == 'w';
    if ((mode[0] != 'r' && !writing) || (mode[1] && mode[1] != 'e')) {
        errno = EINVAL;
        return NULL;
    }
    struct popened *slot = NULL;
    for (size_t i = 0; i < sizeof(s_popened) / sizeof(s_popened[0]); i++)
        if (!s_popened[i].f)
            slot = &s_popened[i];
    if (!slot) {
        errno = EMFILE;
        return NULL;
    }
    int fds[2];
    if (pipe(fds))
        return NULL;
    posix_spawn_file_actions_t fa;
    int err = posix_spawn_file_actions_init(&fa);
    if (!err)
        err = posix_spawn_file_actions_adddup2(&fa, writing ? fds[0] : fds[1], writing ? 0 : 1);
    pid_t pid = 0;
    char *argv[] = { "sh", "-c", (char *)cmd, NULL };
    if (!err)
        err = posix_spawn(&pid, shell(), &fa, NULL, argv, environ);
    if (fa)
        posix_spawn_file_actions_destroy(&fa);
    close(writing ? fds[0] : fds[1]); /* the child's end: it has its own copy */
    int ours = writing ? fds[1] : fds[0];
    if (err) {
        close(ours);
        errno = err;
        return NULL;
    }
    FILE *f = fdopen(ours, writing ? "w" : "r");
    if (!f) {
        close(ours);
        return NULL;
    }
    slot->f = f;
    slot->pid = pid;
    return f;
}

int pclose(FILE *f)
{
    for (size_t i = 0; i < sizeof(s_popened) / sizeof(s_popened[0]); i++) {
        if (s_popened[i].f == f) {
            pid_t pid = s_popened[i].pid;
            s_popened[i].f = NULL;
            fclose(f);
            int status;
            return waitpid(pid, &status, 0) < 0 ? -1 : status;
        }
    }
    errno = EINVAL;
    return -1;
}
