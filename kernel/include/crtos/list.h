/*
 * crtos/list.h - intrusive circular doubly linked lists (no allocation per node)
 */
#ifndef CRTOS_LIST_H
#define CRTOS_LIST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct list_head {
    struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }

static inline void list_init(struct list_head *h)
{
    h->next = h;
    h->prev = h;
}

static inline void list_insert_between(struct list_head *n, struct list_head *prev, struct list_head *next)
{
    next->prev = n;
    n->next = next;
    n->prev = prev;
    prev->next = n;
}

static inline void list_add(struct list_head *n, struct list_head *head)
{
    list_insert_between(n, head, head->next);
}

static inline void list_add_tail(struct list_head *n, struct list_head *head)
{
    list_insert_between(n, head->prev, head);
}

/* Unlink and self-link, so list_del() of an unlinked node is harmless */
static inline void list_del(struct list_head *n)
{
    n->next->prev = n->prev;
    n->prev->next = n->next;
    n->next = n;
    n->prev = n;
}

static inline int list_empty(const struct list_head *h)
{
    return h->next == h;
}

static inline int list_is_singular(const struct list_head *h)
{
    return !list_empty(h) && h->next == h->prev;
}

static inline void list_move_tail(struct list_head *n, struct list_head *head)
{
    list_del(n);
    list_add_tail(n, head);
}

#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define list_entry(ptr, type, member) container_of(ptr, type, member)
#define list_first_entry(head, type, member) list_entry((head)->next, type, member)

#define list_for_each(pos, head) \
    for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)

#define list_for_each_safe(pos, tmp, head) \
    for ((pos) = (head)->next, (tmp) = (pos)->next; (pos) != (head); (pos) = (tmp), (tmp) = (pos)->next)

#ifdef __cplusplus
}
#endif

#endif
