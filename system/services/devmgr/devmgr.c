/*
 * devmgr - device manager (layer 2).
 *
 * Follows the kernel's device notifications (/dev/uevent) and keeps the list of device
 * nodes; services subscribe on the port "devmgr" to the devices they handle and get told
 * when such a device appears or goes away (devmgr_proto.h). Driver modules for devices
 * found at boot are loaded by the kernel itself; modules for devices that appear later
 * (hot plug) will be loaded here from modules.alias.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include "devmgr_proto.h"

#define MAX_DEV     64
#define MAX_SUBS    16

struct sub {
    bool used;
    int port;
    int pid;
    char prefix[24];
};

static char s_dev[MAX_DEV][24];
static int s_ndev;
static struct sub s_sub[MAX_SUBS];

static bool match(const struct sub *s, const char *name)
{
    return !strncmp(name, s->prefix, strlen(s->prefix));
}

static void notify(struct sub *s, uint16_t type, const char *name)
{
    struct devmgr_event ev = { type, sizeof(ev), "" };
    strncpy(ev.name, name, sizeof(ev.name) - 1);
    if (crtos_msg_send(s->port, &ev, sizeof(ev), -1, 500) < 0 && errno == EPIPE) {
        close(s->port);
        s->used = false;
    }
}

static void device_event(bool add, const char *name)
{
    int found = -1;
    for (int i = 0; i < s_ndev; i++)
        if (!strcmp(s_dev[i], name))
            found = i;
    if (add) {
        if (found >= 0 || s_ndev >= MAX_DEV)
            return;
        strncpy(s_dev[s_ndev++], name, sizeof(s_dev[0]) - 1);
    } else {
        if (found < 0)
            return;
        memmove(s_dev[found], s_dev[found + 1], (size_t)(s_ndev - found - 1) * sizeof(s_dev[0]));
        s_ndev--;
    }
    for (int i = 0; i < MAX_SUBS; i++)
        if (s_sub[i].used && match(&s_sub[i], name))
            notify(&s_sub[i], add ? DEVMGR_EV_ADD : DEVMGR_EV_REMOVE, name);
}

/* One batch of notification lines; the number of bytes, <= 0 when there are none */
static int read_uevents(int fd)
{
    static char buf[512];
    int n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0)
        return n;
    buf[n] = 0;
    for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
        char *name = strchr(line, ' ');
        if (!name)
            continue;
        *name++ = 0;
        device_event(!strcmp(line, "add"), name);
    }
    return n;
}

static void subscribe(const struct crtos_msginfo *mi, const struct devmgr_subscribe *q)
{
    int32_t r = -ENOSPC;
    struct sub *s = NULL;
    for (int i = 0; i < MAX_SUBS && !s; i++)
        if (!s_sub[i].used)
            s = &s_sub[i];
    if (s && mi->handle >= 0) {
        s->used = true;
        s->port = mi->handle;
        s->pid = mi->pid;
        memcpy(s->prefix, q->prefix, sizeof(s->prefix));
        s->prefix[sizeof(s->prefix) - 1] = 0;
        r = 0;
    } else if (mi->handle >= 0) {
        close(mi->handle);
    }
    crtos_msg_reply(mi->token, &r, sizeof(r), -1);
    if (!r) /* what is there already */
        for (int i = 0; i < s_ndev && s->used; i++)
            if (match(s, s_dev[i]))
                notify(s, DEVMGR_EV_ADD, s_dev[i]);
}

static void list(const struct crtos_msginfo *mi, const struct devmgr_subscribe *q)
{
    static char out[MSG_MAX];
    size_t n = 0;
    struct sub tmp;
    memcpy(tmp.prefix, q->prefix, sizeof(tmp.prefix));
    tmp.prefix[sizeof(tmp.prefix) - 1] = 0;
    for (int i = 0; i < s_ndev; i++) {
        size_t l = strlen(s_dev[i]);
        if (match(&tmp, s_dev[i]) && n + l + 1 < sizeof(out)) {
            memcpy(out + n, s_dev[i], l);
            n += l;
            out[n++] = '\n';
        }
    }
    crtos_msg_reply(mi->token, out, n, -1);
}

int main(void)
{
    int uev = open("/dev/uevent", O_RDONLY | O_NONBLOCK);
    if (uev < 0) {
        printf("devmgr: /dev/uevent: %s\n", strerror(errno));
        return 1;
    }
    int port = crtos_port_create(DEVMGR_PORT_NAME);
    if (port < 0) {
        printf("devmgr: port: %s\n", strerror(errno));
        return 1;
    }
    while (read_uevents(uev) > 0) /* the devices that are already there */
        ;
    for (;;) {
        struct pollfd pf[2 + MAX_SUBS];
        int map[MAX_SUBS], n = 2;
        pf[0].fd = port;
        pf[0].events = POLLIN;
        pf[1].fd = uev;
        pf[1].events = POLLIN;
        for (int i = 0; i < MAX_SUBS; i++)
            if (s_sub[i].used) {
                pf[n].fd = s_sub[i].port;
                pf[n].events = 0;
                map[n - 2] = i;
                n++;
            }
        if (poll(pf, (nfds_t)n, -1) < 0)
            continue;
        if (pf[1].revents & POLLIN)
            while (read_uevents(uev) > 0)
                ;
        for (int k = 2; k < n; k++)
            if (pf[k].revents & (POLLHUP | POLLNVAL)) {
                close(s_sub[map[k - 2]].port);
                s_sub[map[k - 2]].used = false;
            }
        for (;;) {
            char buf[64];
            struct crtos_msginfo mi;
            int len = crtos_msg_recv(port, buf, sizeof(buf), &mi, 0);
            if (len < 0)
                break;
            const struct devmgr_subscribe *q = (const struct devmgr_subscribe *)buf;
            if (len >= (int)sizeof(*q) && q->type == DEVMGR_SUBSCRIBE) {
                subscribe(&mi, q);
            } else if (len >= (int)sizeof(*q) && q->type == DEVMGR_LIST) {
                list(&mi, q);
            } else {
                if (mi.token)
                    crtos_msg_reply(mi.token, NULL, 0, -1);
                if (mi.handle >= 0)
                    close(mi.handle);
            }
        }
    }
}
