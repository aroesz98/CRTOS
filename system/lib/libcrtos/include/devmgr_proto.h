/*
 * devmgr_proto.h - messages of the device manager (devmgr, layer 2).
 *
 * A service that needs devices of some kind (inputd: "event") calls DEVMGR_SUBSCRIBE on
 * the port "devmgr" with a name prefix and a port of its own; devmgr then sends it a
 * DEVMGR_EV_ADD for every matching device that exists and every one that appears later,
 * and DEVMGR_EV_REMOVE when one goes away.
 */
#ifndef DEVMGR_PROTO_H
#define DEVMGR_PROTO_H

#include <stdint.h>

#define DEVMGR_PORT_NAME "devmgr"

enum {
    DEVMGR_SUBSCRIBE = 1,   /* call, handle: event port; struct devmgr_subscribe -> int32 result */
    DEVMGR_LIST,            /* call; struct devmgr_subscribe (prefix) -> names separated by '\n' */
    DEVMGR_EV_ADD = 100,
    DEVMGR_EV_REMOVE,
};

struct devmgr_subscribe {
    uint16_t type;
    uint16_t size;
    char prefix[24];        /* "" for everything */
};

struct devmgr_event {
    uint16_t type;          /* DEVMGR_EV_* */
    uint16_t size;
    char name[24];          /* device node under /dev */
};

#endif
