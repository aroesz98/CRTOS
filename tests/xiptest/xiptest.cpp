/*
 * xiptest - checks of a program that runs in place (XIP): built with crtos_app(... XIP), it
 * lies on /flash0, and its code stays there (only its data is in the arena). Also run from a
 * copy on the card, where the kernel loads the code into memory instead.
 *
 *     xiptest          the checks; the exit code is the number of failed ones
 *     xiptest crash    a NULL write: the fault report shows the text and GOT addresses
 *
 * What it looks at: code addresses (in the flash or the arena), constructors in priority
 * order, tables of pointers (relocated by the loader), virtual calls, globals and bss, a
 * second thread (r9 in every thread), the heap, printf with floating point.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <crtos.h>

static int s_fail, s_checks;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        s_checks++;                                                   \
        if (!(cond)) {                                                \
            printf("  FAIL line %d: ", __LINE__);                     \
            printf(__VA_ARGS__);                                      \
            printf("\n");                                             \
            s_fail++;                                                 \
        }                                                             \
    } while (0)

/* constructors: run in priority order before main */
static int s_order[4], s_norder;

struct Mark {
    explicit Mark(int id) { s_order[s_norder++] = id; }
};
static Mark s_late(3);
static Mark s_second __attribute__((init_priority(300)))(2);
static Mark s_first __attribute__((init_priority(200)))(1);

/* pointers in tables (.data.rel.ro) and in data */
static int twice(int x) { return 2 * x; }
static int square(int x) { return x * x; }
static int (*const s_ops[])(int) = { twice, square };
static const char *const s_names[] = { "zero", "one", "two" };
static const char *s_greeting = "hello from flash";
int g_counter = 41;
int g_zero[16];

struct Shape {
    virtual ~Shape() {}
    virtual int area() const = 0;
};
struct Rect : Shape {
    int w, h;
    Rect(int w_, int h_) : w(w_), h(h_) {}
    int area() const override { return w * h; }
};
struct Square : Shape {
    int s;
    explicit Square(int s_) : s(s_) {}
    int area() const override { return s * s; }
};

static volatile int s_thread_saw;

static void *worker(void *arg)
{
    /* globals through the GOT: r9 must be this program's in a new thread too */
    s_thread_saw = g_counter + (int)(intptr_t)arg;
    return (void *)(intptr_t)s_ops[1](7);
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "crash")) {
        printf("xiptest: writing to NULL\n");
        fflush(stdout);
        *(volatile int *)0 = 1;
        return 99;
    }
    uintptr_t code = (uintptr_t)&main;
    printf("xiptest: code at %08lx (%s), data at %08lx\n", (unsigned long)code,
           code >= 0x60000000u && code < 0x64000000u ? "in place, flash" : "in memory",
           (unsigned long)(uintptr_t)&g_counter);

    CHECK(s_norder == 3 && s_order[0] == 1 && s_order[1] == 2 && s_order[2] == 3, "constructor order %d %d %d (%d)",
          s_order[0], s_order[1], s_order[2], s_norder);
    CHECK(s_ops[0](21) == 42 && s_ops[1](9) == 81, "function table");
    CHECK(!strcmp(s_names[2], "two") && !strcmp(s_greeting, "hello from flash"), "string pointers");
    CHECK(g_counter == 41, "initialised global %d", g_counter);
    int zeros = 0;
    for (int i = 0; i < 16; i++)
        zeros += g_zero[i] == 0;
    CHECK(zeros == 16, "bss not zero");
    g_counter++;
    CHECK(g_counter == 42, "global write");

    Shape *shapes[2] = { new Rect(3, 4), new Square(5) };
    CHECK(shapes[0]->area() == 12 && shapes[1]->area() == 25, "virtual calls");
    delete shapes[0];
    delete shapes[1];

    char *big = (char *)malloc(100000);
    CHECK(big != NULL, "malloc");
    if (big) {
        memset(big, 0x5A, 100000);
        CHECK(big[99999] == 0x5A, "heap memory");
        free(big);
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%.3f %lld", 3.14159, (long long)1234567890123LL);
    CHECK(!strcmp(buf, "3.142 1234567890123"), "printf '%s'", buf);

    crtos_thread_t *t = crtos_thread_start(worker, (void *)(intptr_t)100, 0, 0);
    CHECK(t != NULL, "thread start");
    if (t) {
        void *res = crtos_thread_join(t);
        CHECK(s_thread_saw == 142 && (intptr_t)res == 49, "thread: saw %d, result %d", s_thread_saw,
              (int)(intptr_t)res);
    }
    printf("xiptest: %d checks, %d failed\n", s_checks, s_fail);
    return s_fail;
}
