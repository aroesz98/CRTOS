/*
 * evtest - print the events of an input device: evtest [/dev/eventN] [seconds]
 *
 * Every open file of an input device gets all its events, so this runs next to inputd.
 * Needs the "dev" capability.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include <crtos/input.h>

static const char *type_name(unsigned t)
{
    switch (t) {
    case EV_SYN: return "SYN";
    case EV_KEY: return "KEY";
    case EV_REL: return "REL";
    case EV_ABS: return "ABS";
    default: return "?";
    }
}

static const char *code_name(unsigned t, unsigned c)
{
    if (t == EV_KEY && c == BTN_TOUCH)
        return "BTN_TOUCH";
    if (t == EV_ABS) {
        switch (c) {
        case ABS_X: return "X";
        case ABS_Y: return "Y";
        case ABS_MT_SLOT: return "MT_SLOT";
        case ABS_MT_POSITION_X: return "MT_X";
        case ABS_MT_POSITION_Y: return "MT_Y";
        case ABS_MT_TRACKING_ID: return "MT_ID";
        default: break;
        }
    }
    return "";
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/dev/event1";
    int seconds = argc > 2 ? atoi(argv[2]) : 10;
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        printf("evtest: %s: %s\n", path, strerror(errno));
        return 1;
    }
    printf("evtest: %s for %d s\n", path, seconds);
    uint64_t end = crtos_time_us() + (uint64_t)seconds * 1000000u;
    uint32_t syns = 0;
    while (crtos_time_us() < end) {
        struct pollfd pf = { fd, POLLIN, 0 };
        if (poll(&pf, 1, 200) <= 0)
            continue;
        struct input_event ev[16];
        int n = read(fd, ev, sizeof(ev));
        for (int i = 0; i < n / (int)sizeof(ev[0]); i++) {
            const struct input_event *e = &ev[i];
            unsigned ms = (unsigned)(e->sec % 1000u) * 1000u + e->usec / 1000u;
            if (e->type == EV_SYN) {
                syns++;
                printf("%6u.%03u ---- SYN %lu\n", ms / 1000u, ms % 1000u, (unsigned long)syns);
            } else {
                printf("%6u.%03u %s %-9s %ld\n", ms / 1000u, ms % 1000u, type_name(e->type), code_name(e->type, e->code),
                       (long)e->value);
            }
        }
    }
    close(fd);
    return 0;
}
