/*
 * heaptest - checks of libcrtosheap (system/lib/libcrtos/src/heap.c), the malloc the compiler on the board
 * links: first a block freed twice (in a child: it stops the program) and a shared memory
 * object larger than one MPU region, which the kernel covers with several; then the C
 * interface, a long random mix of malloc, free, realloc and memalign whose blocks are filled
 * and verified, the consistency of the whole heap after it, and growth beyond the arena into
 * shared memory windows (the program's own heap is 1 MB); then emulated memory (vmemtest.c;
 * "heaptest vmem": only that). The exit code is the number of failed checks. With
 * CRTOS_HEAP_STATS set the heap reports itself at the end.
 */
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <crtos.h>

extern char **environ;

int __crtos_heap_check(void);

static int s_fail, s_checks;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        s_checks++;                                                        \
        if (!(cond)) {                                                     \
            printf("    FAIL line %d: ", __LINE__);                         \
            printf(__VA_ARGS__);                                           \
            printf(" (errno %d)\n", errno);                                \
            s_fail++;                                                      \
            return;                                                        \
        }                                                                  \
    } while (0)

static uint32_t s_rng = 0x12345678u;

static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static inline uint8_t pat(uint32_t seed, size_t i)
{
    return (uint8_t)(seed + i * 7u + (i >> 8));
}

static void fill(uint8_t *p, size_t n, uint32_t seed)
{
    for (size_t i = 0; i < n; i++)
        p[i] = pat(seed, i);
}

/* all of a small block, the ends and a stride of a large one */
static int verify(const uint8_t *p, size_t n, uint32_t seed)
{
    size_t step = n > 4096 ? 61 : 1;
    for (size_t i = 0; i < n; i += step)
        if (p[i] != pat(seed, i))
            return 0;
    for (size_t i = n > 64 ? n - 64 : 0; i < n; i++)
        if (p[i] != pat(seed, i))
            return 0;
    return 1;
}

static void test_interface(void)
{
    printf("interface\n");
    void *z = malloc(0);
    CHECK(z != NULL, "malloc(0)");
    free(z);
    free(NULL);
    for (size_t n = 1; n < 5000; n = n * 3 + 1) {
        uint8_t *p = malloc(n);
        CHECK(p && !((uintptr_t)p & 7) && malloc_usable_size(p) >= n, "malloc(%u) %p", (unsigned)n, p);
        fill(p, n, (uint32_t)n);
        CHECK(verify(p, n, (uint32_t)n), "contents of malloc(%u)", (unsigned)n);
        free(p);
    }
    uint32_t *c = calloc(1000, 4);
    CHECK(c != NULL, "calloc");
    for (int i = 0; i < 1000; i++)
        CHECK(c[i] == 0, "calloc zeroed at %d", i);
    free(c);
    volatile size_t big = 0x10000; /* (not known to the compiler, which would warn) */
    errno = 0;
    CHECK(calloc(big, big + 1) == NULL && errno == ENOMEM, "calloc overflow");
    errno = 0;
    CHECK(malloc(big * 0x7FFF) == NULL && errno == ENOMEM, "huge malloc");

    uint8_t *r = malloc(100);
    fill(r, 100, 5);
    r = realloc(r, 70000);
    CHECK(r && verify(r, 100, 5), "realloc up keeps the data");
    fill(r, 70000, 6);
    r = realloc(r, 3000);
    CHECK(r && verify(r, 3000, 6), "realloc down keeps the data");
    uint8_t *other = malloc(64);
    r = realloc(r, 200000);
    CHECK(r && verify(r, 3000, 6), "realloc moving keeps the data");
    free(other);
    free(r);
    CHECK(realloc(NULL, 10) != NULL, "realloc(NULL)");

    for (size_t a = 16; a <= 65536; a <<= 2) {
        uint8_t *m = memalign(a, 1000);
        CHECK(m && !((uintptr_t)m & (a - 1)), "memalign(%u) %p", (unsigned)a, m);
        fill(m, 1000, 9);
        CHECK(verify(m, 1000, 9), "memalign(%u) contents", (unsigned)a);
        free(m);
    }
    errno = 0;
    CHECK(memalign(48, 100) == NULL && errno == EINVAL, "memalign with a bad alignment");
    CHECK(__crtos_heap_check() == 0, "heap consistent: %d", __crtos_heap_check());
}

#define SLOTS 512

static struct {
    uint8_t *p;
    size_t n;
    uint32_t seed;
} s_slot[SLOTS];

static size_t random_size(void)
{
    uint32_t k = rnd() % 100;
    if (k < 60)
        return 1 + rnd() % 256;
    if (k < 85)
        return 257 + rnd() % 8000;
    if (k < 97)
        return 8192 + rnd() % 120000;
    return 131072 + rnd() % 1400000;
}

static void test_random(void)
{
    printf("random mix\n");
    size_t live = 0, before = mallinfo().uordblks; /* (stdio's buffers stay) */
    for (int op = 0; op < 60000; op++) {
        int i = (int)(rnd() % SLOTS);
        uint32_t k = rnd() % 10;
        if (!s_slot[i].p) {
            size_t n = random_size();
            if (live + n > (6u << 20))
                continue;
            uint32_t seed = rnd();
            uint8_t *p = k < 2 ? memalign((size_t)16 << (rnd() % 9), n) : malloc(n);
            CHECK(p != NULL, "allocation of %u with %u live", (unsigned)n, (unsigned)live);
            fill(p, n, seed);
            s_slot[i].p = p;
            s_slot[i].n = n;
            s_slot[i].seed = seed;
            live += n;
        } else if (k < 5) {
            CHECK(verify(s_slot[i].p, s_slot[i].n, s_slot[i].seed), "block %d (%u bytes) intact", i,
                  (unsigned)s_slot[i].n);
            free(s_slot[i].p);
            live -= s_slot[i].n;
            s_slot[i].p = NULL;
        } else if (k < 8) {
            size_t n = k == 5 ? random_size() : s_slot[i].n + 1 + rnd() % 20000;
            if (live - s_slot[i].n + n > (6u << 20))
                continue;
            uint8_t *p = realloc(s_slot[i].p, n);
            CHECK(p != NULL, "realloc to %u", (unsigned)n);
            size_t keep = n < s_slot[i].n ? n : s_slot[i].n;
            CHECK(verify(p, keep, s_slot[i].seed), "realloc of block %d kept %u bytes", i, (unsigned)keep);
            uint32_t seed = rnd();
            fill(p, n, seed);
            live += n - s_slot[i].n;
            s_slot[i].p = p;
            s_slot[i].n = n;
            s_slot[i].seed = seed;
        } else {
            CHECK(verify(s_slot[i].p, s_slot[i].n, s_slot[i].seed), "block %d intact", i);
        }
        if (op % 5000 == 0) {
            int e = __crtos_heap_check();
            CHECK(e == 0, "heap consistent after %d operations: %d", op, e);
        }
    }
    for (int i = 0; i < SLOTS; i++) {
        if (s_slot[i].p) {
            CHECK(verify(s_slot[i].p, s_slot[i].n, s_slot[i].seed), "block %d intact at the end", i);
            free(s_slot[i].p);
            s_slot[i].p = NULL;
        }
    }
    struct mallinfo mi = mallinfo();
    CHECK(mi.uordblks == before, "back to %u bytes in use after freeing everything: %u", (unsigned)before,
          (unsigned)mi.uordblks);
    CHECK(__crtos_heap_check() == 0, "heap consistent at the end");
}

/* More than the arena's heap: 256 KB blocks up to 8 MB (or what the system has) */
static void test_beyond(void)
{
    printf("beyond the arena\n");
    enum { BLOCK = 256 * 1024, MAX = 32 };
    static uint8_t *blk[MAX];
    int n = 0;
    while (n < MAX && (blk[n] = malloc(BLOCK)) != NULL) {
        fill(blk[n], BLOCK, (uint32_t)n);
        n++;
    }
    struct mallinfo mi = mallinfo();
    printf("    %d KB in blocks, %u windows of %u KB\n", n * BLOCK / 1024, (unsigned)mi.hblks,
           (unsigned)(mi.hblkhd / 1024));
    CHECK(n * BLOCK > (2 << 20), "more than the 1 MB arena heap: %d blocks", n);
    CHECK(mi.hblks >= 1, "a shared memory window was taken");
    for (int i = 0; i < n; i++)
        CHECK(verify(blk[i], BLOCK, (uint32_t)i), "block %d intact", i);
    CHECK(__crtos_heap_check() == 0, "heap consistent");
    for (int i = 0; i < n; i++)
        free(blk[i]);
    /* one piece that needs a window of its own */
    uint8_t *big = malloc(3u << 20);
    if (big) {
        fill(big, 3u << 20, 77);
        CHECK(verify(big, 3u << 20, 77), "3 MB block intact");
        free(big);
    }
    CHECK(__crtos_heap_check() == 0, "heap consistent at the end");
}

/* A block freed twice stops the program (abort: exit code 134) with a message that says where
 * free was called from - in a child of ours ("heaptest badfree"), whose error output we read. */
static void test_bad_free(const char *self)
{
    printf("block freed twice\n");
    int fds[2];
    CHECK(pipe(fds) == 0, "pipe");
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 2);
    char *args[] = { (char *)self, "badfree", NULL };
    pid_t pid;
    int err = posix_spawnp(&pid, self, &fa, NULL, args, environ);
    if (err)
        err = posix_spawn(&pid, "/flash0/bin/heaptest.app", &fa, NULL, args, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    CHECK(err == 0, "child started (%s: %d)", self, err);
    char msg[200];
    int n = 0, r;
    while (n < (int)sizeof(msg) - 1 && (r = read(fds[0], msg + n, sizeof(msg) - 1 - n)) > 0)
        n += r;
    msg[n] = 0;
    close(fds[0]);
    int st = 0;
    CHECK(waitpid(pid, &st, 0) == pid, "waitpid");
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 134, "ended by abort, status %#x", st);
    CHECK(strstr(msg, "heap: bad pointer 0x") && strstr(msg, "called from 0x") &&
              strstr(msg, "(arena 0x"), "message: %s", msg);
}

/* A shared memory object larger than one MPU region can be (the kernel covers it with several,
 * one window each): all of it usable, and system calls take pointers into all of it but not
 * past its end. The kernel may round the size up: its end is where write() stops accepting. */
static void check_shm_large(uint8_t *m, size_t asked, int fd)
{
    size_t lo = asked, hi = asked + (4u << 20); /* (at most 1/8 of a 32 MB region more) */
    CHECK(write(fd, m, lo) == (ssize_t)lo, "write of the %u KB asked for", (unsigned)(lo >> 10));
    while (hi - lo > 1) {
        size_t mid = lo + (hi - lo) / 2;
        if (write(fd, m, mid) == (ssize_t)mid)
            lo = mid;
        else
            hi = mid;
    }
    size_t size = lo;
    printf("    %u KB at %p (%u KB asked for)\n", (unsigned)(size >> 10), m, (unsigned)(asked >> 10));
    CHECK(size % 4096 == 0, "the object ends on a page: %u bytes", (unsigned)size);
    errno = 0;
    CHECK(write(fd, m + size - 2048, 4096) < 0 && errno == EFAULT, "write past the end refused");
    for (size_t off = 0; off < size; off += 256u << 10)
        fill(m + off, 4096, (uint32_t)off);
    fill(m + size - 4096, 4096, 3);
    for (size_t off = 0; off + 4096 < size; off += 256u << 10)
        CHECK(verify(m + off, 4096, (uint32_t)off), "contents at +%u KB", (unsigned)(off >> 10));
    CHECK(verify(m + size - 4096, 4096, 3), "contents at the end");
    /* every 1 MB boundary (where pieces meet) inside one buffer */
    for (size_t off = 1u << 20; off < size; off += 1u << 20)
        CHECK(write(fd, m + off - 2048, 4096) == 4096, "write across +%u KB", (unsigned)(off >> 10));
}

static void test_shm_large(void)
{
    printf("shared memory larger than a region\n");
    size_t size = 24u << 20;
    int h = -1;
    while (size >= (12u << 20) && (h = crtos_shm_create(size, 0)) < 0)
        size -= 1u << 20;
    if (h < 0) {
        printf("    (skipped: no 12 MB free in one piece)\n");
        return;
    }
    uint8_t *m = crtos_shm_map(h);
    close(h);
    CHECK(m != NULL, "map of %u KB", (unsigned)(size >> 10));
    int fd = open("/dev/null", O_WRONLY);
    if (fd >= 0) {
        check_shm_large(m, size, fd);
        close(fd);
    }
    CHECK(fd >= 0, "open /dev/null");
    CHECK(crtos_shm_unmap(m) == 0, "unmap");
}

void test_vmem(int *checks, int *fails); /* vmemtest.c */

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "badfree")) {
        char *volatile p = malloc(64);
        free(p);
        free(p); /* the heap stops the program here */
        return 0;
    }
    bool all = argc < 2 || strcmp(argv[1], "vmem");
    if (all) {
        test_bad_free(argv[0]); /* first: the heap keeps its windows, a child would find no room */
        test_shm_large();
        test_interface();
        test_random();
        test_beyond();
    }
    test_vmem(&s_checks, &s_fail);
    printf("heaptest: %d checks, %d failed\n", s_checks, s_fail);
    return s_fail;
}
