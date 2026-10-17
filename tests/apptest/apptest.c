/*
 * apptest - checks of the user-space interface: files, directories, heap, threads, futexes,
 * POSIX threads, IPC (in one process and between processes), shared memory, poll, memory protection and
 * the process lifecycle, POSIX processes (posix_spawn, waitpid, system, popen), paths and
 * temporary files. The exit code is the number of failed checks.
 *
 * It starts copies of itself for the multi-process checks: "apptest <mode>" with mode
 * server, spin, exit <n>, echo <text>, ppid <pid>, crash-kernel, crash-null, crash-stack, noperm.
 */
#define _POSIX_TIMERS 1 /* clock_gettime() */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <crtos.h>

static int s_fail, s_checks;
static const char *s_self;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        s_checks++;                                                        \
        if (!(cond)) {                                                     \
            printf("    FAIL line %d: ", __LINE__);                         \
            printf(__VA_ARGS__);                                           \
            printf(" (errno %d %s)\n", errno, strerror(errno));            \
            s_fail++;                                                      \
            return;                                                        \
        }                                                                  \
    } while (0)

static int spawn_self(const char *mode, const char *arg, uint32_t caps)
{
    const char *argv[] = { "apptest", mode, arg, NULL };
    struct crtos_spawn sp;
    memset(&sp, 0, sizeof(sp));
    sp.path = s_self;
    sp.argv = argv;
    sp.stdio[0] = sp.stdio[1] = sp.stdio[2] = -1;
    sp.caps = caps;
    return crtos_spawn(&sp);
}

/* ---- files ------------------------------------------------------------------------------------ */

static void t_files_ram(void)
{
    const char *path = "/ram/apptest.txt";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 3, "open for writing: %d", fd);
    for (int i = 0; i < 100; i++)
        CHECK(write(fd, "hello world\n", 12) == 12, "write %d", i);
    CHECK(close(fd) == 0, "close");
    struct stat st;
    CHECK(stat(path, &st) == 0 && st.st_size == 1200, "stat size %ld", (long)st.st_size);
    FILE *f = fopen(path, "r");
    CHECK(f, "fopen");
    char line[32];
    int n = 0;
    while (fgets(line, sizeof(line), f))
        n += !strcmp(line, "hello world\n");
    CHECK(fseek(f, 600, SEEK_SET) == 0 && ftell(f) == 600, "fseek/ftell");
    CHECK(fgets(line, sizeof(line), f) && !strcmp(line, "hello world\n"), "read after seek");
    fclose(f);
    CHECK(n == 100, "read back %d lines", n);
    CHECK(unlink(path) == 0, "unlink");
    CHECK(stat(path, &st) < 0 && errno == ENOENT, "stat after unlink");
}

static uint32_t pattern(uint32_t i)
{
    return i * 2654435761u + 12345u;
}

static void t_files_sd(void)
{
    const char *path = "/sd/crtos/tmp/apptest.bin";
    mkdir("/sd/crtos/tmp", 0755);
    static uint32_t buf[4096];
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0, "create %s", path);
    for (int blk = 0; blk < 16; blk++) {
        for (int i = 0; i < 4096; i++)
            buf[i] = pattern((uint32_t)(blk * 4096 + i));
        CHECK(write(fd, buf, sizeof(buf)) == (int)sizeof(buf), "write block %d", blk);
    }
    CHECK(close(fd) == 0, "close");
    fd = open(path, O_RDONLY);
    CHECK(fd >= 0, "reopen");
    bool ok = true;
    for (int blk = 0; blk < 16 && ok; blk++) {
        CHECK(read(fd, buf, sizeof(buf)) == (int)sizeof(buf), "read block %d", blk);
        for (int i = 0; i < 4096; i++)
            ok &= buf[i] == pattern((uint32_t)(blk * 4096 + i));
    }
    CHECK(lseek(fd, 4096 * 4 * 3 + 40, SEEK_SET) == 4096 * 4 * 3 + 40, "lseek");
    CHECK(read(fd, buf, 4) == 4 && buf[0] == pattern(4096 * 3 + 10), "read after lseek");
    close(fd);
    CHECK(ok, "data mismatch");
    CHECK(unlink(path) == 0, "unlink");
}

static void t_dirs(void)
{
    CHECK(mkdir("/ram/adir", 0755) == 0, "mkdir");
    for (int i = 0; i < 5; i++) {
        char p[48];
        snprintf(p, sizeof(p), "/ram/adir/f%d", i);
        int fd = open(p, O_WRONLY | O_CREAT, 0644);
        CHECK(fd >= 0, "create %s", p);
        close(fd);
    }
    DIR *d = opendir("/ram/adir");
    CHECK(d, "opendir");
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)))
        n += e->d_type == DT_REG && e->d_name[0] == 'f';
    closedir(d);
    CHECK(n == 5, "readdir found %d files", n);
    CHECK(chdir("/ram/adir") == 0, "chdir");
    char cwd[64];
    CHECK(getcwd(cwd, sizeof(cwd)) && !strcmp(cwd, "/ram/adir"), "getcwd '%s'", cwd);
    struct stat st;
    CHECK(stat("f3", &st) == 0, "relative stat");
    FILE *f = fopen("../adir/./f2", "r");
    CHECK(f, "relative open with .. and .");
    fclose(f);
    CHECK(rename("f4", "g4") == 0, "rename");
    for (int i = 0; i < 4; i++) {
        char p[8];
        snprintf(p, sizeof(p), "f%d", i);
        CHECK(unlink(p) == 0, "unlink %s", p);
    }
    CHECK(unlink("g4") == 0, "unlink g4");
    chdir("/");
    CHECK(rmdir("/ram/adir") == 0, "rmdir");
}

static void t_errors(void)
{
    CHECK(open("/no/such/file", O_RDONLY) < 0 && errno == ENOENT, "missing file");
    CHECK(read(47, NULL, 1) < 0 && errno == EBADF, "bad handle");
    CHECK(write(1, (void *)0x20000000, 16) < 0 && errno == EFAULT, "kernel pointer");
    int pid = spawn_self("noperm", NULL, 0);
    CHECK(pid > 0, "spawn without capabilities");
    int code = -1;
    CHECK(crtos_wait(pid, &code, 5000) == pid && code == 0, "child without CAP_DEV: code %d", code);
}

/* ---- memory --------------------------------------------------------------------------------- */

static void t_heap(void)
{
    enum { N = 200 };
    static void *p[N];
    static size_t sz[N];
    uint32_t x = 1;
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < N; i++) {
            x = x * 1103515245u + 12345u;
            sz[i] = 16 + (x >> 16) % 2000;
            p[i] = malloc(sz[i]);
            CHECK(p[i], "malloc %u", (unsigned)sz[i]);
            memset(p[i], i & 0xFF, sz[i]);
        }
        for (int i = 0; i < N; i++) {
            unsigned char *c = (unsigned char *)p[i];
            CHECK(c[0] == (i & 0xFF) && c[sz[i] - 1] == (i & 0xFF), "block %d corrupted", i);
            free(p[i]);
        }
    }
    void *huge = malloc(64u * 1024u * 1024u);
    CHECK(!huge, "64 MB allocation should fail");
    void *big = malloc(100u * 1024u);
    CHECK(big, "100 KB allocation");
    memset(big, 1, 100u * 1024u);
    free(big);
}

/* ---- threads ---------------------------------------------------------------------------------- */

static crtos_mutex_t s_mx = CRTOS_MUTEX_INIT;
static volatile uint32_t s_counter;

static void *worker(void *arg)
{
    for (int i = 0; i < 20000; i++) {
        crtos_mutex_lock(&s_mx);
        s_counter++;
        crtos_mutex_unlock(&s_mx);
        if ((i & 1023) == 0)
            crtos_yield();
    }
    return arg;
}

static void t_threads(void)
{
    crtos_thread_t *t[4];
    s_counter = 0;
    for (int i = 0; i < 4; i++) {
        t[i] = crtos_thread_start(worker, (void *)(intptr_t)(i + 100), 4096, 0);
        CHECK(t[i], "thread %d", i);
    }
    for (int i = 0; i < 4; i++)
        CHECK(crtos_thread_join(t[i]) == (void *)(intptr_t)(i + 100), "join %d", i);
    CHECK(s_counter == 80000, "counter %lu", (unsigned long)s_counter);
}

static volatile uint32_t s_flag;
static uint64_t s_woken_at;

static void *futex_waiter(void *arg)
{
    (void)arg;
    while (!s_flag)
        crtos_futex_wait(&s_flag, 0, CRTOS_FOREVER);
    s_woken_at = crtos_time_us();
    return NULL;
}

static void t_futex(void)
{
    s_flag = 0;
    crtos_thread_t *t = crtos_thread_start(futex_waiter, NULL, 2048, 0);
    CHECK(t, "thread");
    crtos_sleep_ms(20);
    uint64_t t0 = crtos_time_us();
    s_flag = 1;
    crtos_futex_wake(&s_flag, 1);
    crtos_thread_join(t);
    printf("    futex wake-up latency %lu us\n", (unsigned long)(s_woken_at - t0));
    CHECK(s_woken_at >= t0, "woken before the wake");
    uint32_t v = 5;
    CHECK(crtos_futex_wait(&v, 6, 10) < 0 && errno == EAGAIN, "value changed: EAGAIN");
    uint64_t a = crtos_time_us();
    CHECK(crtos_futex_wait(&v, 5, 30) < 0 && errno == ETIMEDOUT, "timeout");
    CHECK(crtos_time_us() - a >= 29000, "timeout too short");
}

/* ---- POSIX threads ---------------------------------------------------------------------------- */

static pthread_mutex_t s_pmx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_pcond = PTHREAD_COND_INITIALIZER;
static int s_pready, s_pwoken, s_once_runs;
static pthread_t s_pself;
static pthread_once_t s_once = PTHREAD_ONCE_INIT;

static void *p_counter(void *arg)
{
    for (int i = 0; i < 10000; i++) {
        pthread_mutex_lock(&s_pmx);
        s_counter++;
        pthread_mutex_unlock(&s_pmx);
    }
    return arg;
}

static void *p_exit(void *arg)
{
    (void)arg;
    s_pself = pthread_self();
    pthread_exit((void *)42);
}

static void *p_waiter(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&s_pmx);
    while (!s_pready)
        pthread_cond_wait(&s_pcond, &s_pmx);
    s_pwoken++;
    pthread_mutex_unlock(&s_pmx);
    return NULL;
}

static void once_fn(void)
{
    s_once_runs++;
    crtos_sleep_ms(5); /* the other callers must wait for it */
}

static void *p_once(void *arg)
{
    pthread_once(&s_once, once_fn);
    return arg;
}

static void *p_short(void *arg)
{
    return arg;
}

static void *p_hold(void *arg)
{
    pthread_mutex_lock(&s_pmx);
    crtos_sleep_ms(30);
    pthread_mutex_unlock(&s_pmx);
    return arg;
}

static void t_pthreads(void)
{
    pthread_t t[4];
    void *ret;
    s_counter = 0;
    for (int i = 0; i < 4; i++)
        CHECK(pthread_create(&t[i], NULL, p_counter, (void *)(intptr_t)(i + 1)) == 0, "create %d", i);
    for (int i = 0; i < 4; i++)
        CHECK(pthread_join(t[i], &ret) == 0 && ret == (void *)(intptr_t)(i + 1), "join %d", i);
    CHECK(s_counter == 40000, "counter %lu under a static mutex", (unsigned long)s_counter);

    pthread_t e;
    CHECK(pthread_create(&e, NULL, p_exit, NULL) == 0, "create");
    CHECK(pthread_join(e, &ret) == 0 && ret == (void *)42, "pthread_exit value");
    CHECK(pthread_equal(s_pself, e), "pthread_self in the thread");
    CHECK(!pthread_equal(pthread_self(), e), "pthread_self in main");

    pthread_t h;
    CHECK(pthread_create(&h, NULL, p_hold, NULL) == 0, "create holder");
    crtos_sleep_ms(10);
    CHECK(pthread_mutex_trylock(&s_pmx) == EBUSY, "trylock of a held mutex");
    CHECK(pthread_join(h, NULL) == 0, "join holder");
    CHECK(pthread_mutex_trylock(&s_pmx) == 0, "trylock of a free mutex");
    pthread_mutex_unlock(&s_pmx);

    /* signal wakes one, broadcast the rest */
    s_pready = 0;
    s_pwoken = 0;
    pthread_t w[3];
    for (int i = 0; i < 3; i++)
        CHECK(pthread_create(&w[i], NULL, p_waiter, NULL) == 0, "create waiter %d", i);
    crtos_sleep_ms(20);
    CHECK(s_pwoken == 0, "a waiter ran without the condition");
    pthread_mutex_lock(&s_pmx);
    s_pready = 1;
    pthread_cond_signal(&s_pcond);
    pthread_mutex_unlock(&s_pmx);
    crtos_sleep_ms(20);
    pthread_mutex_lock(&s_pmx);
    int after_signal = s_pwoken;
    pthread_cond_broadcast(&s_pcond);
    pthread_mutex_unlock(&s_pmx);
    for (int i = 0; i < 3; i++)
        CHECK(pthread_join(w[i], NULL) == 0, "join waiter %d", i);
    CHECK(after_signal >= 1 && s_pwoken == 3, "woken %d after signal, %d after broadcast", after_signal, s_pwoken);

    /* a timed wait nobody signals */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += 30 * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    uint64_t a = crtos_time_us();
    pthread_mutex_lock(&s_pmx);
    int r = pthread_cond_timedwait(&s_pcond, &s_pmx, &ts);
    pthread_mutex_unlock(&s_pmx);
    CHECK(r == ETIMEDOUT && crtos_time_us() - a >= 25000, "timedwait: %d after %lu us", r,
          (unsigned long)(crtos_time_us() - a));

    s_once_runs = 0;
    for (int i = 0; i < 4; i++)
        CHECK(pthread_create(&t[i], NULL, p_once, NULL) == 0, "create once %d", i);
    for (int i = 0; i < 4; i++)
        CHECK(pthread_join(t[i], NULL) == 0, "join once %d", i);
    CHECK(s_once_runs == 1, "once ran %d times", s_once_runs);

    /* ended detached threads are reaped: 40 of these would not fit in the heap at once */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 8192);
    for (int i = 0; i < 40; i++) {
        pthread_t d;
        CHECK(pthread_create(&d, &attr, p_short, NULL) == 0, "detached thread %d", i);
        crtos_sleep_ms(3);
    }
    pthread_attr_destroy(&attr);
}

/* ---- IPC -------------------------------------------------------------------------------------- */

static int s_echo_port;

static void *echo_server(void *arg)
{
    (void)arg;
    char buf[MSG_MAX];
    struct crtos_msginfo mi;
    for (;;) {
        int n = crtos_msg_recv(s_echo_port, buf, sizeof(buf), &mi, CRTOS_FOREVER);
        if (n < 0)
            return NULL;
        if (n == 4 && !memcmp(buf, "quit", 4)) {
            crtos_msg_reply(mi.token, "bye", 3, -1);
            return NULL;
        }
        for (int i = 0; i < n; i++)
            if (buf[i] >= 'a' && buf[i] <= 'z')
                buf[i] -= 32;
        if (mi.token)
            crtos_msg_reply(mi.token, buf, (size_t)n, -1);
    }
}

static void t_ipc_local(void)
{
    s_echo_port = crtos_port_create("apptest.echo");
    CHECK(s_echo_port >= 0, "port_create");
    CHECK(crtos_port_create("apptest.echo") < 0 && errno == EEXIST, "duplicate name");
    crtos_thread_t *t = crtos_thread_start(echo_server, NULL, 4096, 0);
    CHECK(t, "server thread");
    int c = crtos_port_connect("apptest.echo", 1000);
    CHECK(c >= 0, "connect");
    char rep[64];
    uint64_t t0 = crtos_time_us();
    for (int i = 0; i < 200; i++) {
        int n = crtos_msg_call(c, "hello ipc", 9, rep, sizeof(rep), 1000);
        CHECK(n == 9 && !memcmp(rep, "HELLO IPC", 9), "call %d: %d", i, n);
    }
    uint64_t us = crtos_time_us() - t0;
    printf("    call/reply round trip %lu.%lu us\n", (unsigned long)(us / 200), (unsigned long)(us * 10 / 200 % 10));
    CHECK(crtos_msg_call(c, "quit", 4, rep, sizeof(rep), 1000) == 3, "quit");
    crtos_thread_join(t);
    close(c);
    close(s_echo_port);
    CHECK(crtos_port_connect("apptest.echo", 0) < 0 && errno == ENOENT, "name gone after close");
}

static void t_ipc_process(void)
{
    int pid = spawn_self("server", NULL, 0);
    CHECK(pid > 0, "spawn server");
    int c = crtos_port_connect("apptest.srv", 3000);
    CHECK(c >= 0, "connect to the child");
    char rep[64];
    int n = crtos_msg_call(c, "add 40 2", 8, rep, sizeof(rep), 1000);
    CHECK(n > 0 && atoi(rep) == 42, "add: %d '%.*s'", n, n > 0 ? n : 0, rep);

    /* shared memory: the child fills it, we check */
    int shm = crtos_shm_create(64 * 1024, 0);
    CHECK(shm >= 0, "shm_create");
    uint32_t *mem = (uint32_t *)crtos_shm_map(shm);
    CHECK(mem, "shm_map");
    struct crtos_call call = { "fill", 4, shm, rep, sizeof(rep), -1 };
    n = crtos_msg_call2(c, &call, 2000);
    CHECK(n > 0, "fill call %d", n);
    bool ok = true;
    for (int i = 0; i < 16384; i++)
        ok &= mem[i] == pattern((uint32_t)i);
    CHECK(ok, "shared memory content");
    CHECK(crtos_shm_unmap(mem) == 0, "unmap");
    close(shm);

    /* poll: a port with nothing for us, then an anonymous port the child answers on */
    int mine = crtos_port_create(NULL);
    CHECK(mine >= 0, "anonymous port");
    struct pollfd pf = { mine, POLLIN, 0 };
    uint64_t t0 = crtos_time_us();
    CHECK(poll(&pf, 1, 50) == 0, "poll should time out");
    uint64_t waited = crtos_time_us() - t0;
    CHECK(waited >= 49000 && waited < 80000, "poll waited %lu us", (unsigned long)waited);
    CHECK(crtos_msg_send(c, "ping", 4, mine, 1000) == 0, "send with a port handle");
    CHECK(poll(&pf, 1, 2000) == 1 && (pf.revents & POLLIN), "poll for the answer: revents %x", pf.revents);
    struct crtos_msginfo mi;
    n = crtos_msg_recv(mine, rep, sizeof(rep), &mi, 0);
    CHECK(n == 4 && !memcmp(rep, "pong", 4) && mi.pid == pid, "pong from the child");
    close(mine);

    CHECK(crtos_msg_call(c, "quit", 4, rep, sizeof(rep), 1000) >= 0, "quit");
    int code = -1;
    CHECK(crtos_wait(pid, &code, 3000) == pid && code == 0, "server exit code %d", code);
    CHECK(crtos_msg_call(c, "add 1 1", 7, rep, sizeof(rep), 100) < 0 && errno == EPIPE, "dead port: EPIPE");
    close(c);
}

static int server_main(void)
{
    int port = crtos_port_create("apptest.srv");
    if (port < 0)
        return 1;
    char buf[MSG_MAX];
    struct crtos_msginfo mi;
    for (;;) {
        int n = crtos_msg_recv(port, buf, sizeof(buf) - 1, &mi, CRTOS_FOREVER);
        if (n < 0)
            return 2;
        buf[n] = 0;
        if (!strncmp(buf, "add ", 4)) {
            char r[16];
            int a = 0, b = 0;
            sscanf(buf + 4, "%d %d", &a, &b);
            int k = snprintf(r, sizeof(r), "%d", a + b);
            crtos_msg_reply(mi.token, r, (size_t)k + 1, -1);
        } else if (!strcmp(buf, "fill")) {
            uint32_t *mem = mi.handle >= 0 ? (uint32_t *)crtos_shm_map(mi.handle) : NULL;
            if (mem) {
                for (int i = 0; i < 16384; i++)
                    mem[i] = pattern((uint32_t)i);
                crtos_shm_unmap(mem);
            }
            close(mi.handle);
            crtos_msg_reply(mi.token, mem ? "ok" : "no", 2, -1);
        } else if (!strcmp(buf, "ping")) {
            crtos_msg_send(mi.handle, "pong", 4, -1, 1000);
            close(mi.handle);
        } else if (!strcmp(buf, "quit")) {
            crtos_msg_reply(mi.token, "", 0, -1);
            return 0;
        }
    }
}

/* ---- processes and protection ------------------------------------------------------------------------ */

static void expect_child(const char *mode, const char *arg, int want, const char *what)
{
    int pid = spawn_self(mode, arg, 0);
    CHECK(pid > 0, "spawn %s", mode);
    int code = 12345;
    CHECK(crtos_wait(pid, &code, 5000) == pid, "wait for %s", mode);
    CHECK(code == want, "%s: exit code %d, expected %d", what, code, want);
}

static void t_protection(void)
{
    expect_child("crash-kernel", NULL, -EFAULT, "reading kernel memory");
    expect_child("crash-null", NULL, -EFAULT, "writing through NULL");
    expect_child("crash-stack", NULL, -EFAULT, "stack overflow");
}

/* ---- POSIX processes, paths, temporary files ------------------------------------------------ */

static void t_posix(void)
{
    char *const env[] = { "PATH=/sd/crtos/bin", NULL };
    pid_t pid;
    int st;
    char *argv_exit[] = { "apptest", "exit", "7", NULL };
    CHECK(posix_spawn(&pid, s_self, NULL, NULL, argv_exit, env) == 0, "posix_spawn");
    CHECK(waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 7, "exit status %x", st);
    char *argv_crash[] = { "apptest", "crash-null", NULL };
    CHECK(posix_spawn(&pid, s_self, NULL, NULL, argv_crash, env) == 0, "posix_spawn crash");
    CHECK(waitpid(pid, &st, 0) == pid && WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV, "fault status %x", st);
    char *argv_spin[] = { "apptest", "spin", NULL };
    CHECK(posix_spawnp(&pid, "apptest", NULL, NULL, argv_spin, env) == 0, "posix_spawnp via PATH (+ .app)");
    CHECK(waitpid(pid, &st, WNOHANG) == 0, "WNOHANG on a running child");
    CHECK(crtos_kill(pid, -9) == 0, "kill");
    CHECK(waitpid(pid, &st, 0) == pid && WIFSIGNALED(st), "killed status %x", st);

    /* file actions: stdout to a file, then to a pipe */
    posix_spawn_file_actions_t fa;
    CHECK(posix_spawn_file_actions_init(&fa) == 0, "file actions");
    CHECK(posix_spawn_file_actions_addopen(&fa, 1, "/ram/spawn.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0,
          "addopen");
    char *argv_echo[] = { "apptest", "echo", "hello posix", NULL };
    CHECK(posix_spawn(&pid, s_self, &fa, NULL, argv_echo, env) == 0, "posix_spawn to a file");
    posix_spawn_file_actions_destroy(&fa);
    CHECK(waitpid(pid, &st, 0) == pid && st == 0, "echo status %x", st);
    char buf[64] = { 0 };
    FILE *f = fopen("/ram/spawn.txt", "r");
    CHECK(f && fgets(buf, sizeof(buf), f) && !strcmp(buf, "hello posix\n"), "file content '%s'", buf);
    fclose(f);
    unlink("/ram/spawn.txt");
    int fds[2];
    CHECK(pipe(fds) == 0, "pipe");
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    char *argv_echo2[] = { "apptest", "echo", "through a pipe", NULL };
    CHECK(posix_spawn(&pid, s_self, &fa, NULL, argv_echo2, env) == 0, "posix_spawn to a pipe");
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    int n = 0, r;
    while ((r = (int)read(fds[0], buf + n, sizeof(buf) - 1 - (size_t)n)) > 0)
        n += r;
    buf[n] = 0;
    close(fds[0]);
    CHECK(waitpid(pid, &st, 0) == pid && !strcmp(buf, "through a pipe\n"), "pipe content '%s'", buf);

    /* the shell: system(), popen() */
    st = system("/sd/crtos/bin/apptest.app exit 5");
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 5, "system: %x", st);
    f = popen("/sd/crtos/bin/apptest.app echo popen-ok", "r");
    CHECK(f, "popen");
    memset(buf, 0, sizeof(buf));
    CHECK(fgets(buf, sizeof(buf), f) && !strcmp(buf, "popen-ok\n"), "popen read '%s'", buf);
    CHECK(pclose(f) == 0, "pclose");

    /* paths: realpath, a long path before normalising, getcwd(NULL) */
    int fd = open("/ram/rp.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0, "create");
    close(fd);
    char rp[256];
    CHECK(realpath("/ram/../ram/./rp.txt", rp) && !strcmp(rp, "/ram/rp.txt"), "realpath '%s'", rp);
    CHECK(!realpath("/ram/none", rp) && errno == ENOENT, "realpath of a missing file");
    char longp[600] = "/ram";
    for (int i = 0; i < 60; i++)
        strcat(longp, "/x/..");
    strcat(longp, "/rp.txt");
    struct stat sb;
    CHECK(strlen(longp) > 256 && stat(longp, &sb) == 0, "stat of a %u-character path", (unsigned)strlen(longp));
    char *cwd = getcwd(NULL, 0);
    CHECK(cwd && cwd[0] == '/', "getcwd(NULL)");
    free(cwd);
    CHECK(chmod("/ram/rp.txt", 0755) == 0 && chmod("/ram/none", 0755) < 0 && errno == ENOENT, "chmod");
    unlink("/ram/rp.txt");

    /* the system's values, temporary files */
    CHECK(sysconf(_SC_PAGESIZE) == 4096 && sysconf(_SC_PHYS_PAGES) > 1000, "sysconf");
    struct rusage ru;
    CHECK(getrusage(RUSAGE_SELF, &ru) == 0, "getrusage");
    char me[12];
    snprintf(me, sizeof(me), "%d", (int)getpid());
    char *argv_ppid[] = { "apptest", "ppid", me, NULL };
    CHECK(posix_spawn(&pid, s_self, NULL, NULL, argv_ppid, env) == 0, "posix_spawn ppid");
    CHECK(waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0, "getppid in the child");
    f = tmpfile();
    CHECK(f, "tmpfile");
    CHECK(fputs("temporary\n", f) >= 0 && fseek(f, 0, SEEK_SET) == 0 && fgets(buf, sizeof(buf), f) &&
          !strcmp(buf, "temporary\n"), "tmpfile read back");
    fclose(f);
    char *t = tmpnam(NULL);
    CHECK(t && !strncmp(t, "/ram/", 5), "tmpnam '%s'", t ? t : "(null)");

    /* the time of the last write (FAT: 2 s steps), once the clock knows the date */
    static const char *const stamped[] = { "/ram/mtime.txt", "/sd/crtos/tmp/mtime.txt" };
    time_t now = time(NULL);
    if (now < 1700000000) {
        printf("    (the clock is not set: file times not checked)\n");
        return;
    }
    mkdir("/sd/crtos/tmp", 0755);
    for (int i = 0; i < 2; i++) {
        f = fopen(stamped[i], "w");
        CHECK(f && fputs("x", f) >= 0, "write %s", stamped[i]);
        fflush(f);
        struct stat fs_;
        CHECK(fstat(fileno(f), &fs_) == 0, "fstat");
        fclose(f);
        CHECK(stat(stamped[i], &sb) == 0 && sb.st_mtime > now - 10 && sb.st_mtime < now + 10,
              "%s: time %ld, now %ld", stamped[i], (long)sb.st_mtime, (long)now);
        unlink(stamped[i]);
    }
}

/* Whether the kernel still lists process @pid (also an ended one it has not freed yet) */
static bool proc_listed(int pid)
{
    struct crtos_procinfo pi;
    for (int i = 0; crtos_proc_info(i, &pi) == 0; i++)
        if (pi.pid == pid)
            return true;
    return false;
}

static void t_lifecycle(void)
{
    expect_child("exit", "42", 42, "exit code");
    int pid = spawn_self("spin", NULL, 0);
    CHECK(pid > 0, "spawn spin");
    crtos_sleep_ms(30);
    CHECK(crtos_kill(pid, 77) == 0, "kill");
    int code = 0;
    CHECK(crtos_wait(pid, &code, 2000) == pid && code == 77, "killed child: code %d", code);
    CHECK(crtos_wait(pid, &code, 0) < 0 && errno == ECHILD, "already reaped");

    /* a process that ends itself with kill (as abort does) is freed like any other */
    pid = spawn_self("killself", "55", 0);
    CHECK(pid > 0, "spawn killself");
    CHECK(crtos_wait(pid, &code, 2000) == pid && code == 55, "child killed itself: code %d", code);
    for (int i = 0; i < 20 && proc_listed(pid); i++)
        crtos_sleep_ms(10); /* the kernel frees ended processes in its worker thread */
    CHECK(!proc_listed(pid), "process %d freed after it killed itself", pid);

    /* many at once; memory must come back */
    struct crtos_sysinfo before, after;
    crtos_sys_info(&before);
    int pids[8];
    for (int i = 0; i < 8; i++) {
        char a[8];
        snprintf(a, sizeof(a), "%d", i + 10);
        pids[i] = spawn_self("exit", a, 0);
        CHECK(pids[i] > 0, "spawn %d", i);
    }
    int seen = 0;
    for (int i = 0; i < 8; i++) {
        int c = 0, p = crtos_wait(-1, &c, 5000);
        CHECK(p > 0, "wait any");
        for (int k = 0; k < 8; k++)
            if (pids[k] == p && c == k + 10)
                seen |= 1 << k;
    }
    CHECK(seen == 0xFF, "exit codes of 8 children: %x", seen);
    crtos_sleep_ms(50); /* the kernel frees ended processes in its worker thread */
    crtos_sys_info(&after);
    long diff = (long)before.mem_free - (long)after.mem_free;
    printf("    process memory after 8 runs: %+ld bytes\n", -diff);
    CHECK(diff < 4096 && diff > -4096, "memory leak of %ld bytes", diff);
}

#pragma GCC diagnostic ignored "-Winfinite-recursion"
static int recurse(int n)
{
    volatile char pad[256];
    pad[0] = (char)n;
    return recurse(n + 1) + pad[0];
}

static int child_main(const char *mode, const char *arg)
{
    if (!strcmp(mode, "server"))
        return server_main();
    if (!strcmp(mode, "exit"))
        return arg ? atoi(arg) : 0;
    if (!strcmp(mode, "ppid"))
        return arg && getppid() == atoi(arg) ? 0 : 1;
    if (!strcmp(mode, "echo")) {
        printf("%s\n", arg ? arg : "");
        return 0;
    }
    if (!strcmp(mode, "spin"))
        for (;;) {
        }
    if (!strcmp(mode, "killself")) {
        crtos_kill(getpid(), arg ? atoi(arg) : 1);
        return 98; /* (the kill did not end us) */
    }
    if (!strcmp(mode, "crash-kernel"))
        return *(volatile int *)0x20000000;
    if (!strcmp(mode, "crash-null"))
        return *(volatile int *)0 = 1;
    if (!strcmp(mode, "crash-stack"))
        return recurse(0);
    if (!strcmp(mode, "noperm")) {
        int fd = open("/dev/fb0", O_RDWR);
        if (fd >= 0 || errno != EACCES)
            return 1;
        fd = open("/dev/null", O_WRONLY);
        if (fd < 0)
            return 2;
        close(fd);
        return crtos_spawnv(s_self, NULL) < 0 && errno == EPERM ? 0 : 3;
    }
    return 99;
}

int main(int argc, char **argv)
{
    s_self = "/sd/crtos/bin/apptest.app";
    if (argc > 1)
        return child_main(argv[1], argc > 2 ? argv[2] : NULL);

    static const struct {
        const char *name;
        void (*fn)(void);
    } tests[] = {
        { "files on /ram", t_files_ram }, { "files on /sd", t_files_sd }, { "directories", t_dirs },
        { "errors and permissions", t_errors }, { "heap", t_heap }, { "threads and mutexes", t_threads },
        { "futex", t_futex }, { "POSIX threads", t_pthreads }, { "IPC in a process", t_ipc_local }, { "IPC, shm, poll between processes", t_ipc_process },
        { "memory protection", t_protection }, { "POSIX processes, paths, temporary files", t_posix },
        { "process lifecycle", t_lifecycle },
    };
    int failed_tests = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int f = s_fail;
        uint64_t t0 = crtos_time_us();
        printf("[%s]\n", tests[i].name);
        tests[i].fn();
        uint64_t us = crtos_time_us() - t0;
        printf("  -> %s (%lu ms)\n", f == s_fail ? "ok" : "FAILED", (unsigned long)(us / 1000u));
        failed_tests += f != s_fail;
    }
    printf("apptest: %d checks, %d failed\n", s_checks, s_fail);
    return s_fail;
}
