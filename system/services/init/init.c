/*
 * init - the first user process (started by the kernel with every capability).
 *
 * Reads /sd/crtos/etc/init.cfg and starts what it lists, in order:
 *
 *     # comment
 *     service <name> <policy> [caps=a,b,...] <program> [args...]
 *     console [caps=...] <program> [args...]
 *
 * policy: respawn (restart when it ends), once (start and forget), wait (run to the end
 * before going on). caps: spawn, kill, module, sys, dev or all (default: none). The console
 * program gets the serial terminal (and the input focus from kmon) and is restarted when it
 * ends. A service that keeps dying right after its start is restarted with a growing delay.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>

extern char **environ;

#define CONFIG      "/sd/crtos/etc/init.cfg"
#define MAX_SVC     16
#define MAX_ARGS    12

enum { P_ONCE, P_RESPAWN, P_WAIT, P_CONSOLE };

struct service {
    char name[16];
    int policy;
    uint32_t caps;
    char *argv[MAX_ARGS + 1];
    int pid;
    uint64_t started_us;
    uint32_t delay_ms;          /* before the next restart */
    uint64_t restart_at_us;
};

static struct service s_svc[MAX_SVC];
static int s_nsvc;

static uint32_t parse_caps(const char *list)
{
    static const struct {
        const char *name;
        uint32_t cap;
    } names[] = { { "spawn", CAP_SPAWN }, { "kill", CAP_KILL }, { "module", CAP_MODULE },
                  { "sys", CAP_SYS }, { "dev", CAP_DEV }, { "all", CAP_ALL } };
    uint32_t caps = 0;
    char buf[64];
    strncpy(buf, list, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
        size_t i;
        for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
            if (!strcmp(t, names[i].name))
                break;
        if (i < sizeof(names) / sizeof(names[0]))
            caps |= names[i].cap;
        else
            printf("init: unknown capability '%s'\n", t);
    }
    return caps;
}

static void parse_line(char *line)
{
    char *tok[MAX_ARGS + 4];
    int n = 0;
    for (char *t = strtok(line, " \t\r\n"); t && n < MAX_ARGS + 4; t = strtok(NULL, " \t\r\n"))
        tok[n++] = t;
    if (!n || tok[0][0] == '#')
        return;
    if (s_nsvc >= MAX_SVC) {
        printf("init: too many services\n");
        return;
    }
    struct service *s = &s_svc[s_nsvc];
    memset(s, 0, sizeof(*s));
    int i;
    if (!strcmp(tok[0], "service") && n >= 4) {
        strncpy(s->name, tok[1], sizeof(s->name) - 1);
        s->policy = !strcmp(tok[2], "respawn") ? P_RESPAWN : !strcmp(tok[2], "wait") ? P_WAIT : P_ONCE;
        i = 3;
    } else if (!strcmp(tok[0], "console") && n >= 2) {
        strcpy(s->name, "console");
        s->policy = P_CONSOLE;
        i = 1;
    } else {
        printf("init: bad line starting with '%s'\n", tok[0]);
        return;
    }
    if (i < n && !strncmp(tok[i], "caps=", 5))
        s->caps = parse_caps(tok[i++] + 5);
    if (i >= n) {
        printf("init: %s: no program\n", s->name);
        return;
    }
    int a = 0;
    for (; i < n && a < MAX_ARGS; i++)
        s->argv[a++] = strdup(tok[i]);
    s->argv[a] = NULL;
    s_nsvc++;
}

static int start(struct service *s)
{
    struct crtos_spawn sp;
    memset(&sp, 0, sizeof(sp));
    sp.path = s->argv[0];
    sp.argv = (const char *const *)s->argv;
    sp.envp = (const char *const *)environ;
    sp.stdio[0] = sp.stdio[1] = sp.stdio[2] = -1;
    sp.caps = s->caps;
    int pid = crtos_spawn(&sp);
    if (pid < 0) {
        printf("init: cannot start %s (%s): %s\n", s->name, s->argv[0], strerror(errno));
        return -1;
    }
    s->pid = pid;
    s->started_us = crtos_time_us();
    if (s->policy == P_CONSOLE) {
        ioctl(0, TTY_IOC_SET_FG, 0);
        ioctl(0, TTY_IOC_FOCUS, 0);
    }
    return pid;
}

static struct service *find(int pid)
{
    for (int i = 0; i < s_nsvc; i++)
        if (s_svc[i].pid == pid)
            return &s_svc[i];
    return NULL;
}

int main(void)
{
    FILE *f = fopen(CONFIG, "r");
    if (!f) {
        printf("init: %s: %s - starting the shell only\n", CONFIG, strerror(errno));
        char line[] = "console caps=all /sd/crtos/bin/sh.app";
        parse_line(line);
    } else {
        char line[160];
        while (fgets(line, sizeof(line), f))
            parse_line(line);
        fclose(f);
    }
    chdir("/sd/crtos");

    for (int i = 0; i < s_nsvc; i++) {
        struct service *s = &s_svc[i];
        if (start(s) > 0 && s->policy == P_WAIT) {
            int code = 0;
            crtos_wait(s->pid, &code, CRTOS_FOREVER);
            if (code)
                printf("init: %s ended with %d\n", s->name, code);
            s->pid = 0;
        }
    }

    for (;;) {
        /* sleep until a child ends or the next delayed restart is due */
        uint64_t now = crtos_time_us(), next = 0;
        for (int i = 0; i < s_nsvc; i++)
            if (s_svc[i].restart_at_us && (!next || s_svc[i].restart_at_us < next))
                next = s_svc[i].restart_at_us;
        uint32_t timeout = CRTOS_FOREVER;
        if (next)
            timeout = next > now ? (uint32_t)((next - now) / 1000u) + 1u : 0u;
        int code = 0;
        int pid = crtos_wait(-1, &code, timeout);
        now = crtos_time_us();
        if (pid > 0) {
            struct service *s = find(pid);
            if (!s)
                continue; /* not ours (a shell's child we inherited cannot happen) */
            s->pid = 0;
            if (s->policy == P_RESPAWN || s->policy == P_CONSOLE) {
                bool quick = now - s->started_us < 2000000u;
                s->delay_ms = quick ? (s->delay_ms ? s->delay_ms * 2u : 500u) : 0u;
                if (s->delay_ms > 30000u)
                    s->delay_ms = 30000u;
                if (code || quick)
                    printf("init: %s ended with %d, restarting%s\n", s->name, code, s->delay_ms ? " later" : "");
                s->restart_at_us = now + (uint64_t)s->delay_ms * 1000u + 1u;
            } else if (code) {
                printf("init: %s ended with %d\n", s->name, code);
            }
        }
        for (int i = 0; i < s_nsvc; i++) {
            struct service *s = &s_svc[i];
            if (s->restart_at_us && s->restart_at_us <= now) {
                s->restart_at_us = 0;
                start(s);
            }
        }
    }
}
