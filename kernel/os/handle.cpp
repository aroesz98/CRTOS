/*
 * kernel/os/handle.cpp - per-process handle tables.
 *
 * A handle is an index into the process' table; the slot holds the object type, the
 * rights and one reference to the object. Slots change under irq_lock() (short), objects are
 * released outside of it because closing may block (a file system flushing a file).
 */
#include "kernel.h"
#include <crtos/vfs.h>

void obj_get(uint8_t type, void *obj)
{
    switch (type) {
    case H_FILE: vfs_file_get((struct file *)obj); break;
    case H_PORT: port_get((struct port *)obj); break;
    case H_SHM: shm_get((struct shm *)obj); break;
    default: break;
    }
}

void obj_put(uint8_t type, void *obj)
{
    switch (type) {
    case H_FILE: vfs_close((struct file *)obj); break;
    case H_PORT: port_put((struct port *)obj); break;
    case H_SHM: shm_put((struct shm *)obj); break;
    default: break;
    }
}

int obj_poll(uint8_t type, uint8_t rights, void *obj, struct poll_entry *e)
{
    switch (type) {
    case H_FILE: return vfs_poll((struct file *)obj, e);
    case H_PORT: return port_poll((struct port *)obj, rights, e);
    default: return 0;
    }
}

/* Release what a slot held (outside irq_lock) */
static void slot_release(struct handle h)
{
    if (h.type == H_PORT && (h.rights & HR_RECV))
        port_close_recv((struct port *)h.obj);
    obj_put(h.type, h.obj);
}

int handle_install(struct proc *p, uint8_t type, uint8_t rights, void *obj, int min)
{
    if (!p->htab || min < 0)
        return -EBADF;
    uint32_t key = irq_lock();
    for (int i = min; i < CONFIG_MAX_HANDLES; i++) {
        if (p->htab[i].type == H_FREE) {
            p->htab[i].type = type;
            p->htab[i].rights = rights;
            p->htab[i].obj = obj;
            irq_unlock(key);
            return i;
        }
    }
    irq_unlock(key);
    return -EMFILE;
}

int handle_install_at(struct proc *p, int h, uint8_t type, uint8_t rights, void *obj)
{
    if (!p->htab || h < 0 || h >= CONFIG_MAX_HANDLES)
        return -EBADF;
    uint32_t key = irq_lock();
    struct handle old = p->htab[h];
    p->htab[h].type = type;
    p->htab[h].rights = rights;
    p->htab[h].obj = obj;
    irq_unlock(key);
    if (old.type != H_FREE)
        slot_release(old);
    return h;
}

void *handle_ref(struct proc *p, int h, uint8_t *type, uint8_t *rights)
{
    if (!p || !p->htab || h < 0 || h >= CONFIG_MAX_HANDLES)
        return nullptr;
    uint8_t want = *type;
    uint32_t key = irq_lock();
    struct handle s = p->htab[h];
    if (s.type == H_FREE || (want != H_FREE && s.type != want)) {
        irq_unlock(key);
        return nullptr;
    }
    obj_get(s.type, s.obj); /* only counters: safe under the lock */
    irq_unlock(key);
    *type = s.type;
    if (rights)
        *rights = s.rights;
    return s.obj;
}

int handle_close(struct proc *p, int h)
{
    if (!p || !p->htab || h < 0 || h >= CONFIG_MAX_HANDLES)
        return -EBADF;
    uint32_t key = irq_lock();
    struct handle old = p->htab[h];
    p->htab[h].type = H_FREE;
    p->htab[h].obj = nullptr;
    irq_unlock(key);
    if (old.type == H_FREE)
        return -EBADF;
    slot_release(old);
    return 0;
}

void handle_close_all(struct proc *p)
{
    if (!p->htab)
        return;
    for (int i = 0; i < CONFIG_MAX_HANDLES; i++)
        if (p->htab[i].type != H_FREE)
            handle_close(p, i);
}

int handle_count(struct proc *p)
{
    int n = 0;
    if (p->htab)
        for (int i = 0; i < CONFIG_MAX_HANDLES; i++)
            n += p->htab[i].type != H_FREE;
    return n;
}
