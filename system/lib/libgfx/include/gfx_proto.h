/*
 * gfx_proto.h - messages of the graphics server gfxd (layer 2).
 *
 * Clients talk to the port "gfx": msg_call for requests with an answer, msg_send for the
 * rest. A client first says GFX_HELLO and passes a port of its own; gfxd sends it events
 * there (input, frame done, focus). Window contents live in shared memory the client
 * creates and passes with GFX_WIN_CREATE: the client draws into it, reports the changed
 * area with GFX_WIN_DAMAGE, gfxd composes it into the next frame (2D accelerator) and sends
 * GFX_EV_FRAME once that frame is on the screen - the signal to draw the next one.
 *
 * The input service (inputd) sends GFX_INPUT with screen coordinates; gfxd delivers
 * pointer events to the window under the finger (relative coordinates) and keys to the
 * focused window. A client that ends loses its windows automatically.
 *
 * Multi-touch: each finger on the screen has a number (0 to GFX_FINGERS - 1, in the code of
 * its pointer events) from its press to its lift; the first finger pressed while none is
 * down is 0. A finger keeps the window it was pressed on. Finger 0 raises and focuses that
 * window and goes to every window; the other fingers only go to windows created with
 * GFX_WIN_MULTITOUCH (a game's on-screen buttons held together), the others never see them.
 *
 * Window manager: one client may register as the window manager (GFX_WM_REGISTER). It
 * learns about the windows of the other clients (GFX_EV_WM_*) and decorates them: it
 * draws a frame (a window of its own, usually a title bar) and attaches the client window
 * to it (GFX_WM_ATTACH); the two then move, stack and hide together, and pointer input on
 * the frame goes to the manager. While a manager runs, a new window stays hidden until it
 * is attached (at most GFX_WM_WAIT_MS). When the manager ends, its frames go and the
 * windows stay where they are, undecorated, until another manager takes over - so the
 * window manager can be replaced while everything runs.
 *
 * The manager may also attach a window of its own to a frame as a decoration (a resize
 * grip): it moves and hides with the frame and stays above the client window.
 *
 * Size: a window's pixels have a fixed size, so its owner resizes it. The manager asks
 * with GFX_WM_CONFIGURE, the owner of a GFX_WIN_RESIZABLE window gets GFX_EV_CONFIGURE,
 * draws its new content into new shared memory and hands it over with GFX_WIN_RESIZE,
 * which gfxd applies at once (never a half drawn window); the manager then learns the new
 * size (GFX_EV_WM_RESIZE) and fits its frame. Programs may also resize on their own.
 *
 * Keys: the system keys (the Windows keys, KEY_LEFTMETA / KEY_RIGHTMETA) go to the window
 * manager (win 0), and while it asks for them (GFX_WM_KEYS: its programs menu is open) all
 * keys do. A key's release (and its repeats) go where its press went, even when the focus
 * moved meanwhile.
 *
 * Mice: a mouse moves a cursor and its left button is finger 0; its pointer events carry
 * GFX_PTR_MOUSE in flags (touch: 0), so a program can select text with the mouse and scroll
 * with a finger. The wheel turns (GFX_WHEEL, GFX_EV_WHEEL) go to the window under the cursor.
 * A mouse moved without the button (GFX_PTR_HOVER) reaches only windows that ask for it
 * (GFX_WIN_HOVER: a tooltip, a highlight under the cursor): the one under the cursor gets
 * GFX_PTR_HOVER with the position, and GFX_PTR_LEAVE once the cursor is over another window
 * (or it hides). A touch screen has no hover.
 *
 * Clipboard: gfxd keeps one text (up to GFX_CLIP_MAX bytes) for all programs; it is put and
 * read in pieces (GFX_CLIP_PUT / GFX_CLIP_GET, as one message holds at most 512 bytes), and a
 * program that asked (GFX_CLIP_WATCH) learns of every new text (GFX_EV_CLIP).
 *
 * Screen copy (remote desktop, CAP_SYS): a client passes shared memory of the screen's size
 * (GFX_SCREEN_WATCH); gfxd copies what changes into it with the 2D accelerator, in the batch
 * of the frame itself, collects where, sends GFX_EV_SCREEN once when something changed and
 * says where on GFX_SCREEN_TAKE (which starts collecting anew).
 *
 * Appearance (scale, font, colours, transparency, corners, wallpaper): the settings live in
 * /sd/crtos/etc/ui.cfg (gfx_ui.h). A program that changed them sends GFX_SETTINGS; gfxd reads
 * the file, draws the wallpaper anew and sends every client GFX_EV_SETTINGS, on which libgfx
 * makes the theme follow (before the program sees the event: it lays its windows out anew).
 */
#ifndef GFX_PROTO_H
#define GFX_PROTO_H

#include <stddef.h>
#include <stdint.h>

#define GFX_PORT_NAME "gfx"
#define GFX_WM_WAIT_MS 500

enum gfx_msg_type {
    GFX_HELLO = 1,          /* call, handle: client's event port        -> struct gfx_hello_rep */
    GFX_WIN_CREATE,         /* call, handle: shared memory of the pixels -> struct gfx_win_rep */
    GFX_WIN_DESTROY,        /* send struct gfx_win_ref */
    GFX_WIN_DAMAGE,         /* send struct gfx_win_damage */
    GFX_WIN_MOVE,           /* send struct gfx_win_move */
    GFX_WIN_RAISE,          /* send struct gfx_win_ref */
    GFX_WIN_SHOW,           /* send struct gfx_win_show */
    GFX_INPUT,              /* send struct gfx_input (input service) */
    GFX_STATS,              /* call -> struct gfx_stats */
    GFX_WIN_TITLE,          /* send struct gfx_win_title */
    GFX_WM_REGISTER,        /* call -> struct gfx_wm_rep: become the window manager */
    GFX_WM_ATTACH,          /* send struct gfx_wm_attach (manager only) */
    GFX_WM_CLOSE,           /* send struct gfx_win_ref: ask the owner to close it (manager only) */
    GFX_WM_FOCUS,           /* send struct gfx_win_ref: raise and focus it (manager only) */
    GFX_WIN_RESIZE,         /* call, handle: new shared memory, struct gfx_win_resize -> struct gfx_status */
    GFX_WM_CONFIGURE,       /* send struct gfx_wm_configure: ask for another size (manager only) */
    GFX_WM_KEYS,            /* send struct gfx_wm_keys: all keys to the manager or not (manager only) */
    GFX_CLIP_PUT,           /* call struct gfx_clip (a piece of a new text) -> struct gfx_status */
    GFX_CLIP_GET,           /* call struct gfx_clip (offset) -> struct gfx_clip (a piece of the text) */
    GFX_CLIP_WATCH,         /* send struct gfx_hdr: GFX_EV_CLIP from now on */
    GFX_SCREEN_WATCH,       /* call, handle: shared memory for the copy, struct gfx_screen_watch
                             * -> struct gfx_status (CAP_SYS; one client at a time) */
    GFX_SCREEN_TAKE,        /* call -> struct gfx_screen_take: where it changed since the last take */
    GFX_SETTINGS,           /* send struct gfx_hdr: the appearance settings changed (see above) */
};

struct gfx_hdr {
    uint16_t type;
    uint16_t size;          /* of the whole message */
};

struct gfx_hello_rep {
    struct gfx_hdr h;
    uint16_t width, height; /* screen */
    uint32_t format;        /* GPU2D_FMT_* of the screen */
    int32_t client;
};

#define GFX_WIN_ALPHA   0x01u   /* ARGB8888 pixels, blended over what is below */
#define GFX_WIN_HIDDEN  0x02u   /* create hidden (GFX_WIN_SHOW later) */
#define GFX_WIN_TOPMOST 0x04u   /* above the normal windows (panels, menus, keyboards) */
#define GFX_WIN_NOFRAME 0x08u   /* not for the window manager: shown as it is */
#define GFX_WIN_NOFOCUS 0x10u   /* touching it does not take the keyboard focus */
#define GFX_WIN_XRGB    0x20u   /* XRGB8888 pixels (opaque, 32 bits) */
#define GFX_WIN_RESIZABLE 0x40u /* its owner follows GFX_EV_CONFIGURE */
#define GFX_WIN_NOINPUT 0x80u   /* pointer input goes through it to what is below (a mouse cursor) */
#define GFX_WIN_MULTITOUCH 0x100u /* all fingers, not only finger 0 (see above) */
#define GFX_WIN_HOVER   0x200u  /* the mouse over it without the button: GFX_PTR_HOVER / GFX_PTR_LEAVE */

struct gfx_win_create {
    struct gfx_hdr h;
    int16_t x, y;           /* x < 0: let gfxd (or the window manager) place it */
    uint16_t width, height;
    uint32_t stride;        /* bytes per line */
    uint32_t format;        /* GPU2D_FMT_RGB565 or GPU2D_FMT_ARGB8888 */
    uint32_t offset;        /* of the pixels in the shared memory */
    uint32_t flags;         /* GFX_WIN_* */
    char title[32];
};

struct gfx_win_rep {
    struct gfx_hdr h;
    int32_t id;             /* or -errno */
    int16_t x, y;           /* where it was placed */
};

struct gfx_win_ref {
    struct gfx_hdr h;
    int32_t id;
};

struct gfx_win_damage {
    struct gfx_hdr h;
    int32_t id;
    int16_t x, y;           /* window coordinates */
    uint16_t w, hgt;
};

struct gfx_win_move {
    struct gfx_hdr h;
    int32_t id;
    int16_t x, y;
};

struct gfx_win_show {
    struct gfx_hdr h;
    int32_t id;
    int32_t visible;
};

struct gfx_win_title {
    struct gfx_hdr h;
    int32_t id;
    char title[32];
};

struct gfx_wm_rep {
    struct gfx_hdr h;
    int32_t status;         /* 0, or -EBUSY: another manager runs */
};

struct gfx_status {
    struct gfx_hdr h;
    int32_t status;         /* 0 or -errno */
};

/* The window's pixels are now in the shared memory passed along (same format rules as
 * GFX_WIN_CREATE); gfxd lets go of the old ones */
struct gfx_win_resize {
    struct gfx_hdr h;
    int32_t id;
    uint16_t width, height;
    uint32_t stride;
    uint32_t format;
    uint32_t offset;
};

struct gfx_wm_configure {
    struct gfx_hdr h;
    int32_t id;
    uint16_t width, height;
};

/* Put client window @id into the manager's window @frame at (dx, dy) relative to it;
 * frame 0: manage it without a frame, at (dx, dy) on the screen. @id may also be another
 * window of the manager: it becomes the frame's decoration. */
struct gfx_wm_attach {
    struct gfx_hdr h;
    int32_t id;
    int32_t frame;
    int16_t dx, dy;
};

/* the window manager's keys: on != 0 every key goes to it (win 0), 0: to the focused window */
struct gfx_wm_keys {
    struct gfx_hdr h;
    int32_t on;
};

/* input kinds (struct gfx_input.kind and struct gfx_event.kind for pointer events; the
 * code of a pointer event is its finger) */
#define GFX_PTR_DOWN    1u
#define GFX_PTR_MOVE    2u
#define GFX_PTR_UP      3u
#define GFX_FINGERS     5u      /* fingers told apart (a mouse is finger 0) */
#define GFX_KEY         4u
#define GFX_STICK       5u      /* a gamepad stick moved (to the focused window) */
#define GFX_WHEEL       6u      /* a wheel turned with the cursor at x/y: code GFX_WHEEL_*, value
                                 * the notches (up / right positive) */
#define GFX_PTR_HOVER   7u      /* the mouse moved to x/y without the button (value GFX_PTR_MOUSE);
                                 * as an event: to a GFX_WIN_HOVER window under the cursor */
#define GFX_PTR_LEAVE   8u      /* event only: the cursor left that window (x/y 0) */

#define GFX_WHEEL_VERTICAL      0u
#define GFX_WHEEL_HORIZONTAL    1u

/* pointer input from a mouse (struct gfx_input.value of GFX_PTR_*, struct gfx_event.flags of
 * GFX_EV_POINTER and GFX_EV_WHEEL); without it the pointer is a finger */
#define GFX_PTR_MOUSE   0x1u

/* gamepad sticks (struct gfx_input.code for GFX_STICK, struct gfx_event.code for
 * GFX_EV_STICK): x and y from -32767 to 32767, right and down positive, 0 at rest; the
 * triggers from 0 (released) to 32767, x the left one, y the right one */
#define GFX_STICK_LEFT      0u
#define GFX_STICK_RIGHT     1u
#define GFX_STICK_TRIGGERS  2u
#define GFX_STICKS          3u

struct gfx_input {
    struct gfx_hdr h;
    uint16_t kind;          /* GFX_PTR_* (not GFX_PTR_LEAVE), GFX_KEY, GFX_STICK or GFX_WHEEL */
    uint16_t code;          /* key code (Linux KEY_*), GFX_STICK_*, the finger (GFX_PTR_*), GFX_WHEEL_* */
    int16_t x, y;           /* screen coordinates, the stick's position */
    int32_t value;          /* key: 1 pressed, 0 released, 2 repeated; pointer: GFX_PTR_MOUSE or 0;
                             * wheel: notches */
    uint32_t time_ms;
};

/* events gfxd -> client */
enum gfx_event_type {
    GFX_EV_POINTER = 100,   /* kind GFX_PTR_*, code the finger, x/y relative to the window,
                             * value: screen position (GFX_EV_SX/SY), flags GFX_PTR_MOUSE or 0
                             * (GFX_EVENT_PTR long); the manager gets touches of the desktop
                             * with win 0 */
    GFX_EV_KEY,             /* code, value (the manager's keys: win 0) */
    GFX_EV_FRAME,           /* the last damage is on the screen, value = frame number */
    GFX_EV_FOCUS,           /* value 1 focused, 0 not */
    GFX_EV_CLOSE,           /* please close the window */
    GFX_EV_CONFIGURE,       /* the window manager asks for the size w x hgt (long event) */
    GFX_EV_STICK,           /* a gamepad stick: code GFX_STICK_*, x/y its position (to the
                             * focused window; with the focus come the sticks not at rest) */
    GFX_EV_WHEEL,           /* a wheel turned over the window: code GFX_WHEEL_*, value the
                             * notches (up / right positive), x/y the cursor relative to the
                             * window, flags GFX_PTR_MOUSE (GFX_EVENT_PTR long) */
    GFX_EV_CLIP,            /* the clipboard has a new text (GFX_CLIP_WATCH): value its serial */
    GFX_EV_SCREEN,          /* the screen changed (GFX_SCREEN_WATCH): take it (GFX_SCREEN_TAKE) */
    GFX_EV_SETTINGS,        /* the appearance settings changed: the theme follows them already
                             * (libgfx); lay the windows out and draw them anew. value: a serial */
    /* to the window manager; win is the client window */
    GFX_EV_WM_CREATE = 120, /* a window to manage: pid, x/y (wanted, x < 0: any), w/hgt,
                             * flags, title */
    GFX_EV_WM_DESTROY,      /* it is gone (a frame of it is useless now) */
    GFX_EV_WM_TITLE,        /* title changed */
    GFX_EV_WM_MAP,          /* value 1: its owner shows it, 0: hides it */
    GFX_EV_WM_FOCUS,        /* value 1: it got the keyboard focus, 0: lost it */
    GFX_EV_WM_RESIZE,       /* its owner gave it the size w x hgt */
};

#define GFX_EV_SX(ev) ((int16_t)((ev)->value & 0xFFFF))
#define GFX_EV_SY(ev) ((int16_t)((uint32_t)(ev)->value >> 16))

struct gfx_event {
    struct gfx_hdr h;
    int32_t win;
    uint16_t kind;
    uint16_t code;
    int16_t x, y;
    int32_t value;
    uint32_t time_ms;
    /* only in GFX_EV_WM_* and GFX_EV_CONFIGURE events (the others end here) */
    int32_t pid;
    uint16_t w, hgt;
    uint32_t flags;
    char title[32];
};

#define GFX_EVENT_SHORT ((int)offsetof(struct gfx_event, pid))  /* size of the other events */
/* size of pointer and wheel events: up to flags (pid, w and hgt are 0) */
#define GFX_EVENT_PTR ((int)(offsetof(struct gfx_event, flags) + sizeof(uint32_t)))

/* ---- clipboard ---- */
#define GFX_CLIP_MAX    65536u  /* bytes of text the clipboard holds */
#define GFX_CLIP_PIECE  480u    /* bytes of it per message */

/* GFX_CLIP_PUT: a piece of a new text (offset 0 starts it, total its length; the text becomes
 * the clipboard with its last piece). GFX_CLIP_GET asks for the piece at offset; the answer
 * has the text's length and serial too (a reader that sees the serial change starts again). */
struct gfx_clip {
    struct gfx_hdr h;
    uint32_t total;
    uint32_t offset;
    uint32_t serial;
    uint16_t len;           /* bytes in data */
    uint16_t pad;
    char data[GFX_CLIP_PIECE];
};

/* ---- screen copy ---- */
/* the shared memory passed along: the screen's width x height in RGB565, stride bytes a line */
struct gfx_screen_watch {
    struct gfx_hdr h;
    uint32_t stride;
};

#define GFX_SCREEN_RECTS 16
struct gfx_screen_rect {
    int16_t x, y;
    uint16_t w, h;
};

struct gfx_screen_take {
    struct gfx_hdr h;
    uint16_t n;             /* rectangles that changed (n = 0: none) */
    uint16_t pad;
    struct gfx_screen_rect r[GFX_SCREEN_RECTS];
};

struct gfx_stats {
    struct gfx_hdr h;
    uint32_t frames;        /* frames shown by the display */
    uint32_t composed;      /* frames composed by gfxd */
    uint32_t clients, windows;
    uint32_t last_us, max_us, avg_us;  /* composition time */
    uint32_t flips;         /* frames shown after a composition */
    uint32_t missed;        /* of them, shown one or more display frames late */
    uint32_t lag_us;        /* frame start -> composition start (moving average) */
};

#endif
