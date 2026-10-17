/*
 * osk_proto.h - the port "osk" of the on-screen keyboard (system/services/osk): the window manager,
 * or any program, shows and hides it. Every request is a crtos_msg_call() of struct osk_req,
 * answered with struct osk_state:
 *
 *     int port = crtos_port_connect(OSK_PORT_NAME, 0);
 *     struct osk_req req = { OSK_TOGGLE };
 *     struct osk_state st;
 *     crtos_msg_call(port, &req, sizeof(req), &st, sizeof(st), 500);
 */
#ifndef OSK_PROTO_H
#define OSK_PROTO_H
#include <stdint.h>

#define OSK_PORT_NAME "osk"

enum osk_op {
    OSK_STATE = 1,      /* only the state */
    OSK_SHOW,
    OSK_HIDE,
    OSK_TOGGLE,
};

struct osk_req {
    uint32_t op;
};

struct osk_state {
    uint8_t available;  /* no keyboard is connected, so the on-screen one is offered */
    uint8_t visible;
    uint16_t height;    /* of the keyboard, which sits right above the task bar */
};

#endif
