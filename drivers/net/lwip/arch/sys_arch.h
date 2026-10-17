/*
 * arch/sys_arch.h - lwIP's operating system layer on CRTOS kernel objects (sys_arch.c).
 */
#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

#include <crtos/sched.h>
#include <crtos/sync.h>

struct crtos_mbox;

typedef struct semaphore *sys_sem_t;
typedef struct mutex *sys_mutex_t;
typedef struct crtos_mbox *sys_mbox_t;
typedef task_t *sys_thread_t;
typedef uint32_t sys_prot_t;

#define sys_sem_valid(s) (*(s) != NULL)
#define sys_sem_set_invalid(s) \
    do                         \
    {                          \
        *(s) = NULL;           \
    } while (0)
#define sys_mutex_valid(m) (*(m) != NULL)
#define sys_mutex_set_invalid(m) \
    do                           \
    {                            \
        *(m) = NULL;             \
    } while (0)
#define sys_mbox_valid(b) (*(b) != NULL)
#define sys_mbox_set_invalid(b) \
    do                          \
    {                           \
        *(b) = NULL;            \
    } while (0)

#endif
