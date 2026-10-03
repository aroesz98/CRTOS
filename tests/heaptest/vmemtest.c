/*
 * vmemtest.c - heaptest's checks of emulated memory (kernel/os/vmem.cpp, arch/emulate.cpp):
 * every kind of load and store the kernel carries out there (compiled C of all widths, signed,
 * unaligned, floating point, atomics, and hand-written LDM/STM, LDRD/STRD, indexed forms, an
 * IT block, VLDM/VSTM, a load into the PC), a sweep over several times the page cache (pages
 * written back and read again), system calls with paths and buffers in emulated memory, two
 * threads at once, and malloc going on into emulated memory when memory runs out.
 */
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>

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

/* ---- the instructions ------------------------------------------------------------------------ */

/* 16-bit STMIA r0!, LDMIA; 32-bit STMDB r8!, LDMDB */
static int t_multiple(uint32_t *p)
{
    register uint32_t *b0 __asm("r0") = p;
    register uint32_t v1 __asm("r1") = 0x11111111u, v2 __asm("r2") = 0x22222222u, v3 __asm("r3") = 0x33333333u;
    __asm volatile("stmia r0!, {r1, r2, r3}" : "+r"(b0) : "r"(v1), "r"(v2), "r"(v3) : "memory");
    if (b0 != p + 3 || p[0] != v1 || p[1] != v2 || p[2] != v3)
        return 1;
    register uint32_t *b1 __asm("r0") = p;
    register uint32_t w1 __asm("r4"), w2 __asm("r5"), w3 __asm("r6");
    __asm volatile("ldmia.w r0, {r4, r5, r6}" : "=r"(w1), "=r"(w2), "=r"(w3) : "r"(b1) : "memory");
    if (w1 != v1 || w2 != v2 || w3 != v3)
        return 2;
    register uint32_t *b8 __asm("r8") = p + 8;
    __asm volatile("stmdb r8!, {r1, r2, r3}" : "+r"(b8) : "r"(v1), "r"(v2), "r"(v3) : "memory");
    if (b8 != p + 5 || p[5] != v1 || p[6] != v2 || p[7] != v3)
        return 3;
    register uint32_t *b9 __asm("r10") = p + 8; /* (r9 is the GOT of a program in place) */
    __asm volatile("ldmdb r10, {r4, r5, r6}" : "=r"(w1), "=r"(w2), "=r"(w3) : "r"(b9) : "memory");
    if (w1 != v1 || w2 != v2 || w3 != v3)
        return 4;
    return 0;
}

/* LDRD/STRD pre-indexed with write-back and post-indexed; LDR/STR post- and pre-indexed */
static int t_indexed(uint32_t *p)
{
    uint32_t *b = p, x, y;
    uint32_t lo = 0xAAAA5555u, hi = 0x0F0F0F0Fu;
    __asm volatile("strd %1, %2, [%0, #8]!" : "+r"(b) : "r"(lo), "r"(hi) : "memory");
    if (b != p + 2 || p[2] != lo || p[3] != hi)
        return 1;
    __asm volatile("ldrd %1, %2, [%0], #-8" : "+r"(b), "=&r"(x), "=&r"(y) : : "memory");
    if (b != p || x != lo || y != hi)
        return 2;
    __asm volatile("str %1, [%0], #4" : "+r"(b) : "r"(0x12345678u) : "memory");
    if (b != p + 1 || p[0] != 0x12345678u)
        return 3;
    __asm volatile("ldr %1, [%0, #-4]!" : "+r"(b), "=r"(x) : : "memory");
    if (b != p || x != 0x12345678u)
        return 4;
    uint32_t i = 5;
    __asm volatile("str.w %1, [%0, %2, lsl #2]" : : "r"(p), "r"(0xCAFEBABEu), "r"(i) : "memory");
    if (p[5] != 0xCAFEBABEu)
        return 5;
    uint16_t h;
    i = 11; /* halfword 11 = the upper half of word 5 */
    __asm volatile("ldrh.w %0, [%1, %2, lsl #1]" : "=r"(h) : "r"(p), "r"(i) : "memory");
    if (h != 0xCAFE)
        return 6;
    return 0;
}

/* stores and a load inside an IT block */
static int t_it(uint32_t *p)
{
    uint32_t r = 0;
    p[0] = 0;
    __asm volatile("cmp %1, #0\n ite eq\n streq %2, [%0]\n strne %3, [%0]" : : "r"(p), "r"(1), "r"(7), "r"(9) : "cc", "memory");
    if (p[0] != 9)
        return 1;
    __asm volatile("cmp %2, #1\n itt eq\n ldreq %0, [%1]\n addeq %0, %0, #1" : "+r"(r) : "r"(p), "r"(1) : "cc", "memory");
    if (r != 10)
        return 2;
    return 0;
}

/* VSTMIA with write-back, VLDMIA, VSTR/VLDR of singles */
static int t_vfp(uint32_t *p)
{
    volatile double src[4] = { 1.5, -2.25, 3.0e10, 0.125 };
    double out[4];
    uint32_t *b = p;
    __asm volatile("vldmia %1, {d0-d3}\n vstmia %0!, {d0-d3}" : "+r"(b) : "r"(src) : "d0", "d1", "d2", "d3", "memory");
    if (b != p + 8)
        return 1;
    __asm volatile("vldmia %1, {d0-d3}\n vstmia %0, {d0-d3}" : : "r"(out), "r"(p) : "d0", "d1", "d2", "d3", "memory");
    for (int i = 0; i < 4; i++)
        if (out[i] != src[i])
            return 2;
    volatile float *f = (volatile float *)(p + 16);
    f[0] = 2.5f;
    f[1] = f[0] * 4.0f;
    if (f[1] != 10.0f)
        return 3;
    volatile double *d = (volatile double *)(p + 20);
    d[0] = 1.0 / 3.0;
    if (d[0] * 3.0 != 1.0)
        return 4;
    return 0;
}

static int t_ldr_pc(uint32_t *slot)
{
    int r;
    __asm volatile("adr r2, 1f\n"
                   "orr r2, r2, #1\n"
                   "str r2, [%1]\n"
                   "mov %0, #0\n"
                   "ldr pc, [%1]\n"
                   "mov %0, #1\n"
                   ".align 2\n"
                   "1: add %0, %0, #2\n"
                   : "=&r"(r) : "r"(slot) : "r2", "memory");
    return r == 2 ? 0 : 1;
}

static void test_instructions(uint8_t *m)
{
    printf("instructions\n");
    volatile uint8_t *b = m;
    volatile uint16_t *h = (volatile uint16_t *)m;
    volatile uint32_t *w = (volatile uint32_t *)m;
    volatile uint64_t *d = (volatile uint64_t *)m;
    for (int i = 0; i < 64; i++)
        b[i] = (uint8_t)(i * 3);
    for (int i = 0; i < 64; i++)
        CHECK(b[i] == (uint8_t)(i * 3), "byte %d", i);
    h[40] = 0xBEEF;
    CHECK(h[40] == 0xBEEF && b[80] == 0xEF && b[81] == 0xBE, "halfword");
    w[30] = 0x01020304u;
    CHECK(w[30] == 0x01020304u && h[60] == 0x0304, "word");
    d[20] = 0x1122334455667788ull;
    CHECK(d[20] == 0x1122334455667788ull && w[40] == 0x55667788u, "doubleword");
    b[200] = 0x80;
    h[101] = 0x8001;
    CHECK(*(volatile int8_t *)&b[200] == -128 && *(volatile int16_t *)&h[101] == -32767, "signed loads");
    *(volatile uint32_t *)(m + 301) = 0xA1B2C3D4u; /* unaligned */
    CHECK(*(volatile uint32_t *)(m + 301) == 0xA1B2C3D4u && b[301] == 0xD4 && b[304] == 0xA1, "unaligned word");
    *(volatile uint16_t *)(m + 4095) = 0x5AA5; /* across two pages */
    CHECK(*(volatile uint16_t *)(m + 4095) == 0x5AA5 && b[4096] == 0x5A, "halfword across pages");
    memcpy((void *)(m + 8000), "emulated memory, copied by memcpy", 34);
    char s[40];
    memcpy(s, (const void *)(m + 8000), 34);
    CHECK(!memcmp(s, "emulated memory, copied by memcpy", 34), "memcpy both ways");
    CHECK(t_multiple((uint32_t *)(m + 10000)) == 0, "LDM/STM: %d", t_multiple((uint32_t *)(m + 10000)));
    CHECK(t_indexed((uint32_t *)(m + 11000)) == 0, "indexed forms: %d", t_indexed((uint32_t *)(m + 11000)));
    CHECK(t_it((uint32_t *)(m + 12000)) == 0, "IT block: %d", t_it((uint32_t *)(m + 12000)));
    CHECK(t_vfp((uint32_t *)(m + 16384 - 40)) == 0, "floating point: %d", t_vfp((uint32_t *)(m + 16384 - 40)));
    CHECK(t_ldr_pc((uint32_t *)(m + 13000)) == 0, "load into the PC");
    volatile uint32_t *cnt = (volatile uint32_t *)(m + 14000);
    volatile uint8_t *c8 = m + 14010;
    *cnt = 5;
    *c8 = 250;
    __atomic_fetch_add(cnt, 3, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(c8, 10, __ATOMIC_SEQ_CST);
    CHECK(*cnt == 8 && *c8 == 4, "atomics: %u %u", (unsigned)*cnt, (unsigned)*c8);
}

/* ---- more than the page cache ---------------------------------------------------------------- */

static void test_sweep(uint8_t *m, uint32_t size)
{
    printf("sweep over %u KB\n", (unsigned)(size >> 10));
    volatile uint32_t *w = (volatile uint32_t *)m;
    uint32_t n = size / 4;
    uint64_t t0 = crtos_time_us();
    for (uint32_t i = 0; i < n; i += 13)
        w[i] = i * 2654435761u;
    uint64_t t1 = crtos_time_us();
    for (uint32_t i = 0; i < n; i += 13)
        CHECK(w[i] == i * 2654435761u, "word %u after the sweep", (unsigned)i);
    uint64_t t2 = crtos_time_us();
    struct crtos_vmeminfo vi;
    crtos_vmem_info(&vi);
    printf("    %u stores in %u ms, %u loads in %u ms; pages read %u, written %u, zeroed %u\n", (unsigned)(n / 13),
           (unsigned)((t1 - t0) / 1000), (unsigned)(n / 13), (unsigned)((t2 - t1) / 1000), (unsigned)vi.pageins,
           (unsigned)vi.pageouts, (unsigned)vi.zerofills);
    CHECK(vi.pageouts > 0 && vi.pageins > 0, "pages went to the card and came back");
    /* the cost of one access to a page that is in the cache */
    volatile uint32_t *hot = w + 7;
    uint32_t sum = 0;
    uint64_t t3 = crtos_time_us();
    for (int i = 0; i < 20000; i++)
        sum += *hot;
    uint64_t t4 = crtos_time_us();
    (void)sum;
    printf("    an access to a cached page: %u ns\n", (unsigned)((t4 - t3) * 1000 / 20000));
}

/* ---- system calls ------------------------------------------------------------------------------ */

static void test_syscalls(uint8_t *m)
{
    printf("system calls\n");
    char *path = (char *)m + 20000;
    strcpy(path, "/ram/vmemtest.bin");
    uint8_t *out = m + 24576, *in = m + 24576 + 70000;
    for (int i = 0; i < 65536; i++)
        out[i] = (uint8_t)(i ^ (i >> 7));
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0, "open with the path in emulated memory");
    CHECK(write(fd, out, 65536) == 65536, "write from emulated memory");
    close(fd);
    fd = open(path, O_RDONLY);
    CHECK(fd >= 0, "open again");
    CHECK(read(fd, in, 65536) == 65536, "read into emulated memory");
    close(fd);
    unlink(path);
    CHECK(!memcmp(in, out, 65536), "the data came back");
    struct crtos_vmeminfo *vi = (struct crtos_vmeminfo *)(m + 200000);
    CHECK(crtos_vmem_info(vi) == 0 && vi->mine >= (4u << 20), "a result written into emulated memory");
    int32_t *fds = (int32_t *)(m + 200200);
    CHECK(crtos_pipe((int *)fds, 0) == 0, "pipe ends into emulated memory");
    close(fds[0]);
    close(fds[1]);
}

/* ---- two threads ---------------------------------------------------------------------------- */

struct worker {
    volatile uint32_t *counter;
    volatile uint32_t *area;
    uint32_t words;
    int done;
};

static void *worker(void *arg)
{
    struct worker *w = arg;
    for (int i = 0; i < 5000; i++) {
        __atomic_fetch_add(w->counter, 1, __ATOMIC_SEQ_CST);
        uint32_t k = (uint32_t)(i * 997) % w->words;
        w->area[k] = w->area[k] + 1;
    }
    w->done = 1;
    return NULL;
}

static void test_threads(uint8_t *m)
{
    printf("two threads\n");
    volatile uint32_t *counter = (volatile uint32_t *)(m + 300000);
    *counter = 0;
    struct worker a = { counter, (volatile uint32_t *)(m + (1u << 20)), (1u << 20) / 4, 0 };
    struct worker b = { counter, (volatile uint32_t *)(m + (2u << 20)), (1u << 20) / 4, 0 };
    crtos_thread_t *t = crtos_thread_start(worker, &b, 8192, -1);
    CHECK(t != NULL, "second thread");
    worker(&a);
    crtos_thread_join(t);
    CHECK(*counter == 10000, "atomic count of both threads: %u", (unsigned)*counter);
}

/* ---- malloc into emulated memory ------------------------------------------------------------- */

static void test_malloc(void)
{
    printf("malloc beyond memory\n");
    enum { BLOCK = 512 * 1024, MAX = 96 };
    static uint8_t *blk[MAX];
    int n = 0, emulated = -1;
    while (n < MAX && (blk[n] = malloc(BLOCK)) != NULL) {
        if (CRTOS_IN_VMEM(blk[n]) && emulated < 0)
            emulated = n;
        blk[n][0] = (uint8_t)n;
        blk[n][BLOCK - 1] = (uint8_t)~n;
        n++;
        if (emulated >= 0 && n >= emulated + 4)
            break;
    }
    printf("    %d blocks of 512 KB, the first in emulated memory: %d\n", n, emulated);
    CHECK(emulated > 0, "malloc went on into emulated memory");
    for (int i = 0; i < n; i++)
        CHECK(blk[i][0] == (uint8_t)i && blk[i][BLOCK - 1] == (uint8_t)~i, "block %d intact", i);
    CHECK(__crtos_heap_check() == 0, "heap consistent with emulated memory: %d", __crtos_heap_check());
    for (int i = 0; i < n; i++)
        free(blk[i]);
    CHECK(__crtos_heap_check() == 0, "heap consistent after freeing");
}

void test_vmem(int *checks, int *fails)
{
    struct crtos_vmeminfo vi;
    if (crtos_vmem_info(&vi) || !vi.size) {
        printf("emulated memory: none in this system\n");
        return;
    }
    uint32_t size = 6u << 20;
    uint8_t *m = crtos_vmem_map(size);
    if (!m) {
        printf("    FAIL: no emulated memory of %u KB (errno %d)\n", (unsigned)(size >> 10), errno);
        (*checks)++;
        (*fails)++;
        return;
    }
    s_checks++;
    if (!CRTOS_IN_VMEM(m)) {
        printf("    FAIL: region at %p\n", m);
        s_fail++;
    }
    test_instructions(m);
    test_syscalls(m);
    test_threads(m);
    test_sweep(m, size);
    crtos_vmem_unmap(m);
    test_malloc();
    *checks += s_checks;
    *fails += s_fail;
}
