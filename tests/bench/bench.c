/*
 * bench - how fast the system is from a program's side: system calls, switching between
 * threads and processes, IPC, memory, reading the SD card, starting programs. Each result
 * is one line "bench: NAME VALUE UNIT  what" ("crtos bench" collects them).
 *
 *     bench [NAME...]          (default: all of them)
 *
 * NAMEs: syscall sysfull thread pipe port memcpy memset read spawn
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>

#define SELF        "/sd/crtos/bin/bench.app"
#define SPAWN_PROG  "/sd/crtos/bin/hello.app"
#define BIG_FILE    "/sd/crtos/apps/netsurf.app"
#define BIG_FILE2   "/sd/crtos/drivers/net-lwip.ko"
#define MEM_BIG     (256 * 1024)    /* bigger than the data cache (32 KB) */
#define MEM_SMALL   (16 * 1024)     /* fits it */

/* ---- results ------------------------------------------------------------------------------------ */

/* per operation: @us for @n of them */
static void per_op(const char *name, uint64_t us, uint32_t n, const char *what)
{
    uint64_t ns10 = n ? us * 10000u / n : 0; /* tenths of a ns */
    printf("bench: %-8s %8lu.%lu ns   %s\n", name, (unsigned long)(ns10 / 10u), (unsigned long)(ns10 % 10u), what);
}

static void rate(const char *name, uint64_t bytes, uint64_t us, const char *what)
{
    uint64_t kbs = us ? bytes * 1000000u / 1024u / us : 0;
    printf("bench: %-8s %8lu.%lu MB/s %s\n", name, (unsigned long)(kbs / 1024u),
           (unsigned long)(kbs % 1024u * 10u / 1024u), what);
}

static void per_op_ms(const char *name, uint64_t us, uint32_t n, const char *what)
{
    uint64_t us_per = n ? us * 10u / n / 1000u : 0; /* tenths of a ms */
    printf("bench: %-8s %8lu.%lu ms   %s\n", name, (unsigned long)(us_per / 10u), (unsigned long)(us_per % 10u), what);
}

static void failed(const char *name, const char *why)
{
    printf("bench: %-8s failed: %s (%s)\n", name, why, strerror(errno));
}

static uint64_t now_us(void)
{
    return crtos_time_us();
}

/* ---- system calls, threads ---------------------------------------------------------------------- */

static void t_syscall(void)
{
    const uint32_t n = 100000;
    uint64_t t0 = now_us();
    for (uint32_t i = 0; i < n; i++)
        getpid();
    per_op("syscall", now_us() - t0, n, "getpid() (answered in the SVC handler)");
}

static void t_sysfull(void)
{
    const uint32_t n = 100000;
    uint64_t t0 = now_us();
    for (uint32_t i = 0; i < n; i++)
        close(-1); /* EBADF at once, through the whole preemptible way into the kernel */
    per_op("sysfull", now_us() - t0, n, "close(-1) (the kernel thread way of every other call)");
}

static volatile uint32_t s_turn; /* whose turn: 0 main, 1 the other thread */

static void *pong(void *arg)
{
    uint32_t n = (uint32_t)(uintptr_t)arg;
    for (uint32_t i = 0; i < n; i++) {
        while (s_turn != 1)
            crtos_futex_wait(&s_turn, 0, CRTOS_FOREVER);
        s_turn = 0;
        crtos_futex_wake(&s_turn, 1);
    }
    return NULL;
}

static void t_thread(void)
{
    const uint32_t n = 20000;
    s_turn = 0;
    crtos_thread_t *t = crtos_thread_start(pong, (void *)(uintptr_t)n, 0, 0);
    if (!t) {
        failed("thread", "no thread");
        return;
    }
    uint64_t t0 = now_us();
    for (uint32_t i = 0; i < n; i++) {
        s_turn = 1;
        crtos_futex_wake(&s_turn, 1);
        while (s_turn != 0)
            crtos_futex_wait(&s_turn, 1, CRTOS_FOREVER);
    }
    uint64_t dt = now_us() - t0;
    crtos_thread_join(t);
    per_op("thread", dt, n, "two threads, futex ping-pong (round trip: 2 switches)");
}

/* ---- two processes -------------------------------------------------------------------------------- */

static int spawn_self(const char *mode, const char *arg, int in, int out)
{
    const char *argv[] = { "bench", mode, arg, NULL };
    struct crtos_spawn sp;
    memset(&sp, 0, sizeof(sp));
    sp.path = SELF;
    sp.argv = argv;
    sp.stdio[0] = in;
    sp.stdio[1] = out;
    sp.stdio[2] = -1;
    return crtos_spawn(&sp);
}

/* child: bytes from stdin back to stdout until the end */
static int child_pipe(void)
{
    char c;
    while (read(0, &c, 1) == 1)
        if (write(1, &c, 1) != 1)
            break;
    return 0;
}

static void t_pipe(void)
{
    const uint32_t n = 5000;
    int to[2], from[2];
    if (crtos_pipe(to, 0) || crtos_pipe(from, 0)) {
        failed("pipe", "pipe");
        return;
    }
    int pid = spawn_self("child-pipe", NULL, to[0], from[1]);
    close(to[0]);
    close(from[1]);
    if (pid < 0) {
        failed("pipe", "spawn");
        close(to[1]);
        close(from[0]);
        return;
    }
    char c = 'x';
    uint64_t t0 = now_us();
    uint32_t i;
    for (i = 0; i < n; i++)
        if (write(to[1], &c, 1) != 1 || read(from[0], &c, 1) != 1)
            break;
    uint64_t dt = now_us() - t0;
    close(to[1]);
    close(from[0]);
    int status;
    crtos_wait(pid, &status, 2000);
    if (i < n)
        failed("pipe", "short");
    else
        per_op("pipe", dt, n, "two processes, 1 byte there and back through pipes");
}

/* child: answer every call with its own request */
static int child_port(const char *name)
{
    int port = crtos_port_create(name);
    if (port < 0)
        return 1;
    char buf[64];
    struct crtos_msginfo mi;
    for (;;) {
        int n = crtos_msg_recv(port, buf, sizeof(buf), &mi, CRTOS_FOREVER);
        if (n < 0)
            return 1;
        if (mi.token)
            crtos_msg_reply(mi.token, buf, (size_t)n, -1);
        if (n == 4 && !memcmp(buf, "quit", 4))
            return 0;
    }
}

static void t_port(void)
{
    const uint32_t n = 5000;
    char name[24];
    snprintf(name, sizeof(name), "bench.%d", getpid());
    int pid = spawn_self("child-port", name, -1, -1);
    if (pid < 0) {
        failed("port", "spawn");
        return;
    }
    int port = crtos_port_connect(name, 3000);
    if (port < 0) {
        failed("port", "connect");
        crtos_kill(pid, 0);
        return;
    }
    uint32_t req = 0, rep = 0;
    uint64_t t0 = now_us();
    uint32_t i;
    for (i = 0; i < n; i++) {
        req = i;
        if (crtos_msg_call(port, &req, sizeof(req), &rep, sizeof(rep), 3000) != (int)sizeof(rep) || rep != i)
            break;
    }
    uint64_t dt = now_us() - t0;
    char quit[4] = { 'q', 'u', 'i', 't' };
    crtos_msg_call(port, quit, sizeof(quit), &rep, sizeof(rep), 1000);
    close(port);
    int status;
    if (crtos_wait(pid, &status, 1000) < 0)
        crtos_kill(pid, 0);
    if (i < n)
        failed("port", "call");
    else
        per_op("port", dt, n, "two processes, IPC call and reply (4 bytes)");
}

/* ---- memory ------------------------------------------------------------------------------------------ */

static void t_mem(bool copy)
{
    uint8_t *a = malloc(MEM_BIG), *b = malloc(MEM_BIG);
    if (!a || !b) {
        failed(copy ? "memcpy" : "memset", "no memory");
        free(a);
        free(b);
        return;
    }
    memset(a, 1, MEM_BIG);
    memset(b, 2, MEM_BIG);
    static const struct {
        uint32_t size;
        const char *what;
    } sizes[] = {
        { MEM_SMALL, "16 KB (in the data cache)" },
        { MEM_BIG, "256 KB (SDRAM)" },
    };
    for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        uint32_t size = sizes[s].size, rounds = 0;
        uint64_t t0 = now_us(), dt;
        do {
            for (int k = 0; k < 16; k++) {
                if (copy)
                    memcpy(b, a, size);
                else
                    memset(b, k, size);
            }
            rounds += 16;
            dt = now_us() - t0;
        } while (dt < 200000u);
        rate(copy ? "memcpy" : "memset", (uint64_t)size * rounds, dt, sizes[s].what);
    }
    free(a);
    free(b);
}

/* ---- files, programs -------------------------------------------------------------------------------- */

static void t_read(void)
{
    const char *path = BIG_FILE;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        path = BIG_FILE2;
        fd = open(path, O_RDONLY);
    }
    if (fd < 0) {
        failed("read", "no file to read");
        return;
    }
    const size_t chunk = 32 * 1024;
    uint8_t *buf = malloc(chunk);
    if (!buf) {
        close(fd);
        failed("read", "no memory");
        return;
    }
    uint64_t total = 0, t0 = now_us();
    for (;;) {
        int n = (int)read(fd, buf, chunk);
        if (n <= 0)
            break;
        total += (uint64_t)n;
    }
    uint64_t dt = now_us() - t0;
    close(fd);
    free(buf);
    char what[80];
    snprintf(what, sizeof(what), "%s (%lu KB, 32 KB reads)", strrchr(path, '/') + 1, (unsigned long)(total / 1024u));
    rate("read", total, dt, what);
}

static void t_spawn(void)
{
    const uint32_t n = 10;
    int null = open("/dev/null", O_WRONLY);
    const char *argv[] = { "hello", NULL };
    struct crtos_spawn sp;
    memset(&sp, 0, sizeof(sp));
    sp.path = SPAWN_PROG;
    sp.argv = argv;
    sp.stdio[0] = -1;
    sp.stdio[1] = null;
    sp.stdio[2] = null;
    uint64_t t0 = now_us();
    uint32_t i;
    for (i = 0; i < n; i++) {
        int pid = crtos_spawn(&sp);
        int status;
        if (pid < 0 || crtos_wait(pid, &status, 5000) < 0)
            break;
    }
    uint64_t dt = now_us() - t0;
    if (null >= 0)
        close(null);
    if (i < n)
        failed("spawn", SPAWN_PROG);
    else
        per_op_ms("spawn", dt, n, "hello.app: load, run, exit, wait");
}

/* ---- main ------------------------------------------------------------------------------------------ */

static void t_memcpy(void)
{
    t_mem(true);
}

static void t_memset(void)
{
    t_mem(false);
}

static const struct {
    const char *name;
    void (*fn)(void);
} s_tests[] = {
    { "syscall", t_syscall }, { "sysfull", t_sysfull }, { "thread", t_thread }, { "pipe", t_pipe }, { "port", t_port },
    { "memcpy", t_memcpy },   { "memset", t_memset }, { "read", t_read }, { "spawn", t_spawn },
};

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "child-pipe"))
        return child_pipe();
    if (argc > 2 && !strcmp(argv[1], "child-port"))
        return child_port(argv[2]);
    int ran = 0;
    for (unsigned i = 0; i < sizeof(s_tests) / sizeof(s_tests[0]); i++) {
        bool want = argc < 2;
        for (int a = 1; a < argc; a++)
            if (!strcmp(argv[a], s_tests[i].name))
                want = true;
        if (want) {
            s_tests[i].fn();
            fflush(stdout);
            ran++;
        }
    }
    if (!ran) {
        printf("usage: bench [syscall|sysfull|thread|pipe|port|memcpy|memset|read|spawn ...]\n");
        return 2;
    }
    return 0;
}
