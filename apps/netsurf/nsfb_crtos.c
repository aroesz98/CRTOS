/*
 * nsfb_crtos.c - a libnsfb surface on a CRTOS window (libgfx): NetSurf's framebuffer front
 * end draws straight into the window's pixels (XRGB8888). Touches become the pointer and
 * mouse button 1, the mouse wheel buttons 4 and 5 (scrolling), keys arrive as Linux key codes,
 * the window manager's size requests become resize events and closing the window quits.
 * The front end's clipboard is the system's (gfxd): crtos_clipboard_set/get.
 *
 * The surface takes the slot of the SDL surface (NSFB_SURFACE_SDL), which is not built here,
 * under the name "crtos": the front end picks it as the default.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libnsfb.h"
#include "libnsfb_event.h"
#include "libnsfb_plot.h"
#include "nsfb.h"
#include "plot.h"
#include "surface.h"

#include <crtos.h>
#include "gfx.h"

struct crtos_fb
{
    struct gfx *g;
    struct gfx_win *w;
    nsfb_bbox_t dirty; /* changed since the last damage sent */
    bool have_dirty;
    nsfb_event_t queued[16]; /* events to hand out before reading new ones */
    int nqueued;
};

static struct crtos_fb s_fb;

/* ---- keys: Linux key codes (input-event-codes.h) to libnsfb's (mostly ASCII) ----------------- */

static const struct
{
    uint16_t linux_code;
    uint16_t nsfb;
} s_keys[] = {
    {1, NSFB_KEY_ESCAPE},
    {2, '1'},
    {3, '2'},
    {4, '3'},
    {5, '4'},
    {6, '5'},
    {7, '6'},
    {8, '7'},
    {9, '8'},
    {10, '9'},
    {11, '0'},
    {12, '-'},
    {13, '='},
    {14, NSFB_KEY_BACKSPACE},
    {15, NSFB_KEY_TAB},
    {16, 'q'},
    {17, 'w'},
    {18, 'e'},
    {19, 'r'},
    {20, 't'},
    {21, 'y'},
    {22, 'u'},
    {23, 'i'},
    {24, 'o'},
    {25, 'p'},
    {26, '['},
    {27, ']'},
    {28, NSFB_KEY_RETURN},
    {29, NSFB_KEY_LCTRL},
    {30, 'a'},
    {31, 's'},
    {32, 'd'},
    {33, 'f'},
    {34, 'g'},
    {35, 'h'},
    {36, 'j'},
    {37, 'k'},
    {38, 'l'},
    {39, ';'},
    {40, '\''},
    {41, '`'},
    {42, NSFB_KEY_LSHIFT},
    {43, '\\'},
    {44, 'z'},
    {45, 'x'},
    {46, 'c'},
    {47, 'v'},
    {48, 'b'},
    {49, 'n'},
    {50, 'm'},
    {51, ','},
    {52, '.'},
    {53, '/'},
    {54, NSFB_KEY_RSHIFT},
    {57, ' '},
    {97, NSFB_KEY_RCTRL},
    {102, NSFB_KEY_HOME},
    {103, NSFB_KEY_UP},
    {104, NSFB_KEY_PAGEUP},
    {105, NSFB_KEY_LEFT},
    {106, NSFB_KEY_RIGHT},
    {107, NSFB_KEY_END},
    {108, NSFB_KEY_DOWN},
    {109, NSFB_KEY_PAGEDOWN},
    {111, NSFB_KEY_DELETE},
};

static enum nsfb_key_code_e map_key(uint16_t code)
{
    for (size_t i = 0; i < sizeof(s_keys) / sizeof(s_keys[0]); i++)
        if (s_keys[i].linux_code == code)
            return (enum nsfb_key_code_e)s_keys[i].nsfb;
    return NSFB_KEY_UNKNOWN;
}

/* ---- the surface ------------------------------------------------------------------------------ */

static void attach_pixels(nsfb_t *nsfb)
{
    nsfb->ptr = (uint8_t *)s_fb.w->s.pix;
    nsfb->linelen = s_fb.w->s.stride;
    nsfb->width = s_fb.w->s.w;
    nsfb->height = s_fb.w->s.h;
}

static int crtos_defaults(nsfb_t *nsfb)
{
    nsfb->width = 480;
    nsfb->height = 240;
    nsfb->format = NSFB_FMT_XRGB8888;
    select_plotters(nsfb);
    return 0;
}

static int crtos_initialise(nsfb_t *nsfb)
{
    if (s_fb.g)
        return -1;
    s_fb.g = gfx_open();
    if (!s_fb.g)
        return -1;
    int sw = gfx_screen_width(s_fb.g), sh = gfx_screen_height(s_fb.g);
    int w = nsfb->width < sw ? nsfb->width : sw;
    int h = nsfb->height < sh ? nsfb->height : sh;
    s_fb.w = gfx_win_create(s_fb.g, -1, -1, w, h, GFX_WIN_XRGB | GFX_WIN_RESIZABLE, "NetSurf");
    if (!s_fb.w)
    {
        gfx_close(s_fb.g);
        s_fb.g = NULL;
        return -1;
    }
    if (nsfb->format != NSFB_FMT_XRGB8888)
    {
        nsfb->format = NSFB_FMT_XRGB8888;
        select_plotters(nsfb);
    }
    attach_pixels(nsfb);
    return 0;
}

static int crtos_finalise(nsfb_t *nsfb)
{
    (void)nsfb;
    if (s_fb.w)
        gfx_win_destroy(s_fb.w);
    if (s_fb.g)
        gfx_close(s_fb.g);
    memset(&s_fb, 0, sizeof(s_fb));
    return 0;
}

static int crtos_set_geometry(nsfb_t *nsfb, int width, int height, enum nsfb_format_e format)
{
    (void)format; /* always XRGB8888: the window's pixels */
    nsfb->format = NSFB_FMT_XRGB8888;
    nsfb->width = width;
    nsfb->height = height;
    select_plotters(nsfb);
    if (s_fb.w)
    {
        if (gfx_win_resize(s_fb.w, width, height) < 0)
            return -1;
        attach_pixels(nsfb);
        s_fb.have_dirty = false;
    }
    return 0;
}

/* Changed pixels go to the graphics server in one damage message per event loop turn */
static void flush_damage(void)
{
    if (!s_fb.have_dirty || !s_fb.w)
        return;
    nsfb_bbox_t *b = &s_fb.dirty;
    gfx_damage(s_fb.w, b->x0, b->y0, b->x1 - b->x0, b->y1 - b->y0);
    s_fb.have_dirty = false;
}

static int crtos_update(nsfb_t *nsfb, nsfb_bbox_t *box)
{
    (void)nsfb;
    if (box->x1 <= box->x0 || box->y1 <= box->y0)
        return 0;
    if (!s_fb.have_dirty)
    {
        s_fb.dirty = *box;
        s_fb.have_dirty = true;
    }
    else
    {
        nsfb_bbox_t *d = &s_fb.dirty;
        d->x0 = box->x0 < d->x0 ? box->x0 : d->x0;
        d->y0 = box->y0 < d->y0 ? box->y0 : d->y0;
        d->x1 = box->x1 > d->x1 ? box->x1 : d->x1;
        d->y1 = box->y1 > d->y1 ? box->y1 : d->y1;
    }
    return 0;
}

static int crtos_claim(nsfb_t *nsfb, nsfb_bbox_t *box)
{
    (void)nsfb;
    (void)box;
    return 0; /* no pointer is drawn on a touch screen */
}

static void queue(enum nsfb_event_type_e type, int keycode)
{
    if (s_fb.nqueued >= (int)(sizeof(s_fb.queued) / sizeof(s_fb.queued[0])))
        return;
    nsfb_event_t *e = &s_fb.queued[s_fb.nqueued++];
    e->type = type;
    e->value.keycode = (enum nsfb_key_code_e)keycode;
}

static bool dequeue(nsfb_event_t *event)
{
    if (!s_fb.nqueued)
        return false;
    *event = s_fb.queued[0];
    memmove(&s_fb.queued[0], &s_fb.queued[1], (size_t)(s_fb.nqueued - 1) * sizeof(s_fb.queued[0]));
    s_fb.nqueued--;
    return true;
}

static bool crtos_input(nsfb_t *nsfb, nsfb_event_t *event, int timeout)
{
    (void)nsfb;
    flush_damage();
    if (dequeue(event))
        return true;
    uint64_t end = timeout > 0 ? crtos_time_us() + (uint64_t)timeout * 1000u : 0;
    for (;;)
    {
        uint32_t wait = CRTOS_FOREVER;
        if (timeout == 0)
        {
            wait = 0;
        }
        else if (timeout > 0)
        {
            uint64_t now = crtos_time_us();
            wait = now < end ? (uint32_t)((end - now + 999u) / 1000u) : 0;
        }
        struct gfx_event ev;
        if (gfx_next_event(s_fb.g, &ev, wait))
        {
            if (timeout > 0)
            { /* the time is up: the scheduler runs */
                event->type = NSFB_EVENT_CONTROL;
                event->value.controlcode = NSFB_CONTROL_TIMEOUT;
                return true;
            }
            return false;
        }
        switch (ev.h.type)
        {
        case GFX_EV_POINTER:
            /* the pointer moves there first, the button follows */
            event->type = NSFB_EVENT_MOVE_ABSOLUTE;
            event->value.vector.x = ev.x;
            event->value.vector.y = ev.y;
            event->value.vector.z = 0;
            if (ev.kind == GFX_PTR_DOWN)
                queue(NSFB_EVENT_KEY_DOWN, NSFB_KEY_MOUSE_1);
            else if (ev.kind == GFX_PTR_UP)
                queue(NSFB_EVENT_KEY_UP, NSFB_KEY_MOUSE_1);
            return true;
        case GFX_EV_WHEEL:
        {
            if (ev.code != GFX_WHEEL_VERTICAL || !ev.value)
                break;
            /* the pointer there, then a click of button 4 (up) or 5 (down) a notch */
            event->type = NSFB_EVENT_MOVE_ABSOLUTE;
            event->value.vector.x = ev.x;
            event->value.vector.y = ev.y;
            event->value.vector.z = 0;
            int button = ev.value > 0 ? NSFB_KEY_MOUSE_4 : NSFB_KEY_MOUSE_5;
            for (int n = ev.value > 0 ? ev.value : -ev.value; n > 0 && s_fb.nqueued < 14; n--)
            {
                queue(NSFB_EVENT_KEY_DOWN, button);
                queue(NSFB_EVENT_KEY_UP, button);
            }
            return true;
        }
        case GFX_EV_KEY:
        {
            enum nsfb_key_code_e k = map_key(ev.code);
            if (k == NSFB_KEY_UNKNOWN)
                break;
            event->type = ev.value ? NSFB_EVENT_KEY_DOWN : NSFB_EVENT_KEY_UP;
            event->value.keycode = k;
            return true;
        }
        case GFX_EV_CONFIGURE:
            event->type = NSFB_EVENT_RESIZE;
            event->value.resize.w = ev.w;
            event->value.resize.h = ev.hgt;
            return true;
        case GFX_EV_CLOSE:
            event->type = NSFB_EVENT_CONTROL;
            event->value.controlcode = NSFB_CONTROL_QUIT;
            return true;
        default: /* frames shown, focus: nothing to do */
            break;
        }
        if (timeout == 0)
            return false;
    }
}

/* ---- the system clipboard (gfxd), for the front end's copy and paste ----------------------- */

/* The text (UTF-8) becomes the clipboard of all programs: 0, or -1 without a window yet */
int crtos_clipboard_set(const char *text, size_t len)
{
    if (!s_fb.g)
        return -1;
    return gfx_clip_set(s_fb.g, text, len);
}

/* The clipboard's text, malloc'd with a 0 after it (*len without it), or NULL when it is
 * empty or cannot be read */
char *crtos_clipboard_get(size_t *len)
{
    *len = 0;
    if (!s_fb.g)
        return NULL;
    char probe[1];
    int total = gfx_clip_get(s_fb.g, probe, sizeof(probe));
    if (total <= 0)
        return NULL;
    char *text = malloc((size_t)total + 1);
    if (!text)
        return NULL;
    int n = gfx_clip_get(s_fb.g, text, (size_t)total + 1);
    if (n <= 0)
    {
        free(text);
        return NULL;
    }
    *len = strlen(text); /* (a newer, shorter text may have come meanwhile) */
    return text;
}

static const nsfb_surface_rtns_t crtos_rtns = {
    .defaults = crtos_defaults,
    .initialise = crtos_initialise,
    .finalise = crtos_finalise,
    .geometry = crtos_set_geometry,
    .input = crtos_input,
    .claim = crtos_claim,
    .update = crtos_update,
};

NSFB_SURFACE_DEF(crtos, NSFB_SURFACE_SDL, &crtos_rtns)
