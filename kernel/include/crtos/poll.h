/*
 * crtos/poll.h - waiting for several objects at once (the poll system call).
 *
 * A pollable object (device file, IPC port, ...) keeps a poll_head. Its poll function
 * returns the current state as a POLL* mask and, when it is given an entry, links it into
 * the head; whenever the state may have changed the object calls poll_notify(), which wakes
 * the tasks sleeping in poll() on it. poll_notify() may be called from interrupts.
 */
#ifndef CRTOS_POLL_H
#define CRTOS_POLL_H

#include <crtos/list.h>

#define POLLIN      0x0001  /* data to read */
#define POLLPRI     0x0002
#define POLLOUT     0x0004  /* writing will not block */
#define POLLERR     0x0008
#define POLLHUP     0x0010  /* peer gone */
#define POLLNVAL    0x0020  /* not a valid handle */

#if !defined(__ASSEMBLER__) && !defined(CRTOS_USER)
#ifdef __cplusplus
extern "C" {
#endif

struct task;

struct poll_entry {
    struct list_head node;
    struct task *task;
};

struct poll_head {
    struct list_head entries;
};

void poll_head_init(struct poll_head *h);
void poll_add(struct poll_head *h, struct poll_entry *e);   /* e may be NULL (state query only) */
void poll_notify(struct poll_head *h);

#ifdef __cplusplus
}
#endif
#endif

#endif
