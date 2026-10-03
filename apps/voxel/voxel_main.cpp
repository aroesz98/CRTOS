/*
 * voxel_main.cpp - Voxel: a 3D block world in a window (layer 3).
 *
 * Every start makes a new island (the seed comes from the hardware random generator):
 * hills, mountains with snow, beaches, a desert, caves and ores, forests, a village around a
 * well, towers, a lighthouse and a pyramid. Walk around, fly, dig and build.
 *
 * Touch (one finger):
 *   drag on the left part    walk (up / down) and turn (left / right)
 *   drag on the right part   look around
 *   tap a block              mine it, or build on the side you tapped (MINE / BUILD)
 *   JUMP, FLY (then UP / DOWN), the hotbar (tap a block to build with; > shows more),
 *   HQ: full or half resolution, NEW: another island
 * Keyboard: W A S D walk, arrows turn and look, space jump / up, shift down, F fly,
 *   Q or backspace mine, E or enter build (at the cross), 1-9 hotbar, tab next page,
 *   M mine / build, N new island.
 * Gamepad (DualSense): left stick walk, right stick look around, cross jump / up, circle
 *   down (flying), R2 mine and L2 build at the cross (again and again while held), L1 / R1
 *   the block before / after, triangle fly, square next page, create HQ / LQ, options twice
 *   a new island. The d-pad is not used: the pad makes one of the left stick as well.
 *
 * The 3D view is drawn into a buffer of our own (at half the window resolution by default)
 * while the graphics server shows the previous frame, then copied (scaled) into the window
 * with the controls on top.
 */
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern "C"
{
#include <crtos.h>
#include <crtos/keys.h>
#include "gfx.h"
}

#include "voxel_blocks.h"
#include "voxel_config.h"
#include "voxel_mesh.h"
#include "voxel_player.h"
#include "voxel_render.h"
#include "voxel_world.h"

#define FRAME_ROOM 46 /* the task bar and the window's title */
#define SLOT 24       /* hotbar slot */
#define ICON 18
#define HOTBAR 9
#define BIG 44        /* jump / up / down buttons */
#define TOP_H 22      /* the buttons at the top */
#define DEAD 8        /* joystick dead zone */

static gfx *g;
static gfx_win *w;
static int W, H;
static int s_scale = 2;

static World s_world;
static Meshes s_mesh;
static Renderer s_r;
static Player s_p;

static bool s_build;           /* BUILD mode (else MINE) */
static int s_slot;             /* selected hotbar slot */
static int s_page;             /* hotbar page */
static uint16_t s_icon[B_COUNT][ICON * ICON]; /* RGB565, TEX_HOLE around the cube */

static unsigned s_report;      /* seconds, for the statistics on the console */
static bool s_verbose;         /* -v: the statistics every 5 seconds */
static char s_msg[48];
static uint64_t s_msg_until;

/* keyboard */
static bool k_fwd, k_back, k_left, k_right, k_turn_l, k_turn_r, k_look_u, k_look_d, k_jump, k_down;

/* gamepad */
#define PAD_DEAD 0.14f            /* of a stick's travel around the middle: no movement */
#define PAD_TURN 3.0f             /* radians a second, the right stick at its end */
#define PAD_TILT 2.0f
#define PAD_FIRST_US 350000u      /* R2 / L2 held: mine / build again after this, */
#define PAD_REPEAT_US 250000u     /* then at this pace */
static float p_lx, p_ly, p_rx, p_ry; /* the sticks, -1..1 beyond the dead zone, down positive */
static bool p_jump, p_down, p_mine, p_build;
static uint64_t p_mine_at, p_build_at; /* the next repetition */
static uint64_t p_new_until;      /* OPTIONS once: again before this makes a new island */
static bool p_used;               /* the pad's help was shown */

/* touch */
enum Touch
{
    T_NONE,
    T_PENDING, /* down, not moved yet: a tap or the start of a drag */
    T_DRIVE,
    T_LOOK,
    T_HOLD     /* a button held down */
};
static Touch s_touch = T_NONE;
static int s_tx0, s_ty0, s_tdx, s_tdy;
static uint64_t s_t0;
static float s_yaw0, s_pitch0;
static int s_held;             /* the button held: 1 jump / up, 2 down */

static uint64_t now_us(void)
{
    return crtos_time_us();
}

static void message(const char *text)
{
    snprintf(s_msg, sizeof(s_msg), "%s", text);
    s_msg_until = now_us() + 1800000u;
}

/* ---- drawing helpers on the window --------------------------------------------------------------- */

static uint16_t to565(uint32_t c)
{
    return (uint16_t)(((c >> 8) & 0xF800u) | ((c >> 5) & 0x07E0u) | ((c >> 3) & 0x001Fu));
}

/* colour c (0xAARRGGBB) over the window, a: 0..256 */
static void fill_alpha(int x, int y, int ww, int hh, uint32_t c, uint32_t a)
{
    gfx_surface *s = &w->s;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + ww > s->w ? s->w : x + ww, y1 = y + hh > s->h ? s->h : y + hh;
    const uint32_t M = 0x07E0F81Fu;
    uint32_t a5 = a >> 3, na = 32u - a5, cs = (((uint32_t)to565(c) | ((uint32_t)to565(c) << 16)) & M) * a5;
    for (int yy = y0; yy < y1; yy++)
    {
        uint16_t *p = (uint16_t *)((uint8_t *)s->pix + yy * s->stride);
        for (int xx = x0; xx < x1; xx++)
        {
            uint32_t d = ((uint32_t)p[xx] | ((uint32_t)p[xx] << 16)) & M;
            d = ((d * na + cs) >> 5) & M;
            p[xx] = (uint16_t)(d | (d >> 16));
        }
    }
}

static void text_shadow(int x, int y, const char *t, uint32_t c)
{
    gfx_text(&w->s, &gfx_dejavu_bold11, x + 1, y + 1, t, GFX_RGB(0, 0, 0));
    gfx_text(&w->s, &gfx_dejavu_bold11, x, y, t, c);
}

static void draw_icon(int x, int y, uint8_t b)
{
    gfx_surface *s = &w->s;
    for (int yy = 0; yy < ICON; yy++)
    {
        if (y + yy < 0 || y + yy >= s->h)
            continue;
        uint16_t *p = (uint16_t *)((uint8_t *)s->pix + (y + yy) * s->stride);
        for (int xx = 0; xx < ICON; xx++)
        {
            uint16_t c = s_icon[b][yy * ICON + xx];
            if (c != TEX_HOLE && x + xx >= 0 && x + xx < s->w)
                p[x + xx] = c;
        }
    }
}

/* ---- layout ------------------------------------------------------------------------------------ */

struct Rect
{
    int x, y, w, h;
};

static bool inside(const Rect &r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

enum Button
{
    BUT_MODE,
    BUT_FLY,
    BUT_HQ,
    BUT_NEW,
    BUT_JUMP,
    BUT_DOWN,
    BUT_PAGE,
    BUT_COUNT
};

static Rect button_rect(int b)
{
    switch (b)
    {
    case BUT_NEW: return {W - 44, 4, 40, TOP_H};
    case BUT_HQ: return {W - 44 - 38, 4, 34, TOP_H};
    case BUT_FLY: return {W - 44 - 38 - 44, 4, 40, TOP_H};
    case BUT_MODE: return {W - 44 - 38 - 44 - 62, 4, 58, TOP_H};
    case BUT_JUMP: return {W - BIG - 6, H - BIG - 6, BIG, BIG};
    case BUT_DOWN: return s_p.flying ? Rect{W - BIG - 6, H - 2 * BIG - 12, BIG, BIG} : Rect{0, 0, 0, 0};
    default: /* page: right of the hotbar */
    {
        int x0 = (W - (HOTBAR * SLOT + 4 + SLOT)) / 2;
        return {x0 + HOTBAR * SLOT + 4, H - SLOT - 4, SLOT, SLOT};
    }
    }
}

static Rect slot_rect(int i)
{
    int x0 = (W - (HOTBAR * SLOT + 4 + SLOT)) / 2;
    return {x0 + i * SLOT, H - SLOT - 4, SLOT, SLOT};
}

static int pages(void)
{
    return (g_buildable_count + HOTBAR - 1) / HOTBAR;
}

static uint8_t selected_block(void)
{
    int i = s_page * HOTBAR + s_slot;
    return i < g_buildable_count ? g_buildable[i] : g_buildable[0];
}

static void select_slot(int i)
{
    if (s_page * HOTBAR + i >= g_buildable_count)
        return;
    s_slot = i;
    s_build = true;
    message(g_blocks[selected_block()].name);
}

static void next_page(void)
{
    s_page = (s_page + 1) % pages();
    if (s_page * HOTBAR + s_slot >= g_buildable_count)
        s_slot = 0;
    message(g_blocks[selected_block()].name);
}

/* ---- the world ---------------------------------------------------------------------------------- */

static void loading(int percent, const char *what)
{
    gfx_surface *s = &w->s;
    gfx_fill(s, 0, 0, W, H, GFX_RGB(24, 30, 42));
    const char *title = "Voxel";
    gfx_text(s, &gfx_sans_bold12, (W - gfx_text_width(&gfx_sans_bold12, title)) / 2, H / 2 - 24, title, GFX_RGB(230, 236, 250));
    gfx_text(s, &gfx_dejavu11, (W - gfx_text_width(&gfx_dejavu11, what)) / 2, H / 2 - 2, what, GFX_RGB(170, 186, 210));
    int bw = W * 2 / 3;
    gfx_rect(s, (W - bw) / 2 - 1, H / 2 + 10, bw + 2, 10, GFX_RGB(90, 110, 140));
    gfx_fill(s, (W - bw) / 2, H / 2 + 11, bw * percent / 100, 8, GFX_RGB(110, 190, 90));
    gfx_present(w);
    gfx_wait_frame(w, 200);
}

static uint32_t s_seed_arg; /* the seed from the command line (the first island only) */

static uint32_t random_seed(void)
{
    uint32_t seed = s_seed_arg;
    s_seed_arg = 0;
    if (seed)
        return seed;
    int fd = open("/dev/random", O_RDONLY);
    if (fd >= 0)
    {
        if (read(fd, &seed, sizeof(seed)) != (int)sizeof(seed))
            seed = 0;
        close(fd);
    }
    return seed ? seed : (uint32_t)now_us() ^ 0x9E3779B9u;
}

static void new_world(void)
{
    uint64_t t0 = now_us();
    mesh_free(&s_mesh); /* the old island's faces first: the new ones reuse that memory */
    world_generate(&s_world, random_seed(), loading);
    loading(92, "building the meshes");
    uint64_t t1 = now_us();
    mesh_mark_all(&s_mesh);
    mesh_update(&s_mesh, &s_world);
    player_spawn(&s_p, &s_world);
    p_jump = p_down = p_mine = p_build = false;
    printf("voxel: island %08lx made in %lu ms (meshes %lu ms), %d faces\n", (unsigned long)s_world.seed,
           (unsigned long)((now_us() - t0) / 1000u), (unsigned long)((now_us() - t1) / 1000u), s_mesh.faces);
    message("a new island");
}

static void edit(const Hit *h, bool build)
{
    if (!h->hit)
        return;
    int x = h->x, y = h->y, z = h->z;
    uint8_t b = B_AIR;
    if (build)
    {
        x += h->nx;
        y += h->ny;
        z += h->nz;
        uint8_t old = world_get(&s_world, x, y, z);
        if (!world_inside(x, y, z) || (old != B_AIR && old != B_WATER))
            return;
        b = selected_block();
        if (block_solid(b) && player_overlaps(&s_p, x, y, z))
        {
            message("you are standing there");
            return;
        }
    }
    else
    {
        if (g_blocks[world_get(&s_world, x, y, z)].flags & BF_UNBREAKABLE)
        {
            message("bedrock cannot be broken");
            return;
        }
        /* water flows into a hole next to it (one block, it does not spread further) */
        for (int k = 0; k < 6 && b == B_AIR; k++)
        {
            if (k == 2)
                continue; /* not from below */
            const FaceDef *f = &g_faces[k];
            if (world_get(&s_world, x + f->n[0], y + f->n[1], z + f->n[2]) == B_WATER)
                b = B_WATER;
        }
    }
    bool column = world_set(&s_world, x, y, z, b);
    mesh_mark(&s_mesh, x, y, z, column);
}

static void eye(float o[3])
{
    float bob = (s_p.on_ground && !s_p.flying) ? sinf(s_p.step * 3.1416f * 0.9f) * 0.04f : 0.0f;
    o[0] = s_p.x;
    o[1] = player_eye(&s_p) + bob;
    o[2] = s_p.z;
}

static Hit aim(float px, float py, bool center, float reach = REACH)
{
    float o[3], d[3];
    eye(o);
    Camera cam = {o[0], o[1], o[2], s_p.yaw, s_p.pitch};
    if (center)
        camera_forward(&cam, d);
    else
        render_ray(&s_r, &cam, px / (float)s_scale, py / (float)s_scale, d);
    return world_raycast(&s_world, o, d, reach);
}

/* mine or build at a pixel of the window (or at the cross) */
static void act(float px, float py, bool center, bool build)
{
    Hit h = aim(px, py, center);
    if (h.hit)
        edit(&h, build);
    else if (aim(px, py, center, 64.0f).hit)
        message("too far - come closer");
}

/* ---- input ---------------------------------------------------------------------------------------- */

/* a stick axis (-32767..32767) as -1..1, nothing within the dead zone */
static float pad_axis(int v)
{
    float f = (float)v / 32767.0f, a = fabsf(f);
    if (a < PAD_DEAD)
        return 0.0f;
    a = (a - PAD_DEAD) / (1.0f - PAD_DEAD);
    a = a > 1.0f ? 1.0f : a;
    return f < 0 ? -a : a;
}

static void pad_help(void)
{
    if (p_used)
        return;
    p_used = true;
    message("R2 mine  L2 build  L1/R1 block  X jump");
}

static void stick(const gfx_event *ev)
{
    if (ev->code == GFX_STICK_LEFT)
    {
        p_lx = pad_axis(ev->x);
        p_ly = pad_axis(ev->y);
    }
    else if (ev->code == GFX_STICK_RIGHT)
    {
        p_rx = pad_axis(ev->x);
        p_ry = pad_axis(ev->y);
    }
    if (p_lx != 0.0f || p_ly != 0.0f || p_rx != 0.0f || p_ry != 0.0f)
        pad_help();
}

/* the block @d places before / after the selected one, over the pages */
static void hotbar_step(int d)
{
    int n = g_buildable_count, i = s_page * HOTBAR + s_slot;
    i = (i + d + n) % n;
    s_page = i / HOTBAR;
    s_slot = i % HOTBAR;
    s_build = true;
    message(g_blocks[selected_block()].name);
}

/* the window lost the focus: its keys and sticks will not come any more */
static void release_all(void)
{
    k_fwd = k_back = k_left = k_right = k_turn_l = k_turn_r = k_look_u = k_look_d = k_jump = k_down = false;
    p_lx = p_ly = p_rx = p_ry = 0.0f;
    p_jump = p_down = p_mine = p_build = false;
}

static void press(int b)
{
    switch (b)
    {
    case BUT_MODE:
        s_build = !s_build;
        message(s_build ? "build" : "mine");
        break;
    case BUT_FLY:
        s_p.flying = !s_p.flying;
        s_p.vy = 0;
        message(s_p.flying ? "flying" : "walking");
        break;
    case BUT_HQ:
        s_scale = s_scale == 1 ? 2 : 1;
        if (!render_init(&s_r, (W + s_scale - 1) / s_scale, (H + s_scale - 1) / s_scale))
            s_scale = 2;
        message(s_scale == 1 ? "full resolution" : "half resolution (faster)");
        break;
    case BUT_NEW: new_world(); break;
    case BUT_PAGE: next_page(); break;
    default: break;
    }
}

static void pointer(const gfx_event *ev)
{
    int x = ev->x, y = ev->y;
    switch (ev->kind)
    {
    case GFX_PTR_DOWN:
        for (int b = 0; b < BUT_COUNT; b++)
        {
            if (!inside(button_rect(b), x, y))
                continue;
            s_touch = T_HOLD;
            if (b == BUT_JUMP)
                s_held = 1;
            else if (b == BUT_DOWN)
                s_held = 2;
            else
                press(b);
            return;
        }
        for (int i = 0; i < HOTBAR; i++)
            if (inside(slot_rect(i), x, y))
            {
                s_touch = T_HOLD;
                select_slot(i);
                return;
            }
        s_touch = T_PENDING;
        s_tx0 = x;
        s_ty0 = y;
        s_tdx = s_tdy = 0;
        s_t0 = now_us();
        s_yaw0 = s_p.yaw;
        s_pitch0 = s_p.pitch;
        break;
    case GFX_PTR_MOVE:
        if (s_touch == T_PENDING && (abs(x - s_tx0) > 10 || abs(y - s_ty0) > 10))
            s_touch = s_tx0 < W * 2 / 5 ? T_DRIVE : T_LOOK;
        s_tdx = x - s_tx0;
        s_tdy = y - s_ty0;
        if (s_touch == T_LOOK)
        {
            float k = 2.4f / (float)W; /* a drag across the window: about 140 degrees */
            s_p.yaw = s_yaw0 + (float)s_tdx * k;
            float p = s_pitch0 - (float)s_tdy * k;
            s_p.pitch = p > 1.5f ? 1.5f : p < -1.5f ? -1.5f : p;
        }
        break;
    case GFX_PTR_UP:
        if (s_touch == T_PENDING && now_us() - s_t0 < 600000u)
        {
            act((float)x, (float)y, false, s_build);
        }
        s_touch = T_NONE;
        s_held = 0;
        s_tdx = s_tdy = 0;
        break;
    default: break;
    }
}

static void key(const gfx_event *ev)
{
    bool down = ev->value != 0;
    switch (ev->code)
    {
    case KEY_W: k_fwd = down; break;
    case KEY_S: k_back = down; break;
    case KEY_A: k_left = down; break;
    case KEY_D: k_right = down; break;
    case KEY_LEFT: k_turn_l = down; break;
    case KEY_RIGHT: k_turn_r = down; break;
    case KEY_UP: k_look_u = down; break;
    case KEY_DOWN: k_look_d = down; break;
    case KEY_SPACE: k_jump = down; break;
    case KEY_LEFTSHIFT: k_down = down; break;
    case BTN_SOUTH: p_jump = down; break;
    case BTN_EAST: p_down = down; break;
    case BTN_TR2: p_mine = down; break;
    case BTN_TL2: p_build = down; break;
    default: break;
    }
    if (ev->value != 1) /* the rest on the press only */
        return;
    if (ev->code >= BTN_SOUTH && ev->code <= BTN_THUMBR)
        pad_help();
    switch (ev->code)
    {
    case BTN_TR2:
        act(0, 0, true, false);
        p_mine_at = now_us() + PAD_FIRST_US;
        break;
    case BTN_TL2:
        act(0, 0, true, true);
        p_build_at = now_us() + PAD_FIRST_US;
        break;
    case BTN_TL: hotbar_step(-1); break;
    case BTN_TR: hotbar_step(1); break;
    case BTN_NORTH: press(BUT_FLY); break;
    case BTN_WEST: next_page(); break;
    case BTN_SELECT: press(BUT_HQ); break;
    case BTN_START:
        if (now_us() < p_new_until)
        {
            p_new_until = 0;
            press(BUT_NEW);
        }
        else
        {
            p_new_until = now_us() + 2000000u;
            message("OPTIONS again: a new island");
        }
        break;
    case KEY_F: press(BUT_FLY); break;
    case KEY_M: press(BUT_MODE); break;
    case KEY_N: press(BUT_NEW); break;
    case KEY_H: press(BUT_HQ); break;
    case KEY_TAB: next_page(); break;
    case KEY_Q:
    case KEY_BACKSPACE:
        act(0, 0, true, false);
        break;
    case KEY_E:
    case KEY_ENTER:
        act(0, 0, true, true);
        break;
    default:
        if (ev->code >= KEY_1 && ev->code <= KEY_9)
            select_slot(ev->code - KEY_1);
        break;
    }
}

static void steer(float dt, Controls *c)
{
    c->forward = (k_fwd ? 1.0f : 0.0f) - (k_back ? 1.0f : 0.0f);
    c->strafe = (k_right ? 1.0f : 0.0f) - (k_left ? 1.0f : 0.0f);
    c->jump = k_jump || s_held == 1 || p_jump;
    c->down = k_down || s_held == 2 || p_down;
    float turn = (k_turn_r ? 1.0f : 0.0f) - (k_turn_l ? 1.0f : 0.0f);
    float look = (k_look_u ? 1.0f : 0.0f) - (k_look_d ? 1.0f : 0.0f);

    /* the pad: the left stick walks (as far as it is pushed), the right one looks - slowly
     * near the middle for aiming at a block (the square of the push) */
    if (p_lx != 0.0f || p_ly != 0.0f)
    {
        c->forward = -p_ly;
        c->strafe = p_lx;
    }
    s_p.yaw += p_rx * fabsf(p_rx) * PAD_TURN * dt;
    s_p.pitch -= p_ry * fabsf(p_ry) * PAD_TILT * dt;
    if (s_touch == T_DRIVE)
    {
        int dy = s_tdy, dx = s_tdx;
        if (abs(dy) > DEAD)
        {
            float f = (float)(-dy + (dy > 0 ? DEAD : -DEAD)) / 40.0f;
            c->forward = f > 1 ? 1 : f < -1 ? -1 : f;
        }
        if (abs(dx) > DEAD)
        {
            float t = (float)(dx - (dx > 0 ? DEAD : -DEAD)) / 60.0f;
            turn = t > 1 ? 1 : t < -1 ? -1 : t;
        }
    }
    s_p.yaw += turn * 2.2f * dt;
    s_p.pitch += look * 1.6f * dt;
    s_p.pitch = s_p.pitch > 1.5f ? 1.5f : s_p.pitch < -1.5f ? -1.5f : s_p.pitch;
}

/* ---- the picture ----------------------------------------------------------------------------------- */

static void blit(void)
{
    gfx_surface *s = &w->s;
    int ww = s->w < W ? s->w : W, hh = s->h < H ? s->h : H;
    for (int y = 0; y < hh; y++)
    {
        uint16_t *d = (uint16_t *)((uint8_t *)s->pix + y * s->stride);
        if (s_scale == 1)
        {
            memcpy(d, s_r.color + y * s_r.w, sizeof(uint16_t) * (size_t)ww);
            continue;
        }
        if (y & 1)
        {
            memcpy(d, (uint8_t *)d - s->stride, sizeof(uint16_t) * (size_t)ww);
            continue;
        }
        /* every pixel twice: one 32-bit store per source pixel (rows start 4-byte aligned) */
        const uint16_t *src = s_r.color + (y >> 1) * s_r.w;
        uint32_t *d32 = (uint32_t *)d;
        int n = ww >> 1;
        for (int x = 0; x < n; x++)
            d32[x] = (uint32_t)src[x] * 0x00010001u;
        if (ww & 1)
            d[ww - 1] = src[n];
    }
}

static void button(int b, const char *label, bool on)
{
    Rect r = button_rect(b);
    if (!r.w)
        return;
    bool held = (b == BUT_JUMP && s_held == 1) || (b == BUT_DOWN && s_held == 2);
    fill_alpha(r.x, r.y, r.w, r.h, on || held ? GFX_RGB(70, 140, 230) : GFX_RGB(10, 14, 22), on || held ? 190u : 120u);
    gfx_rect(&w->s, r.x, r.y, r.w, r.h, GFX_RGB(220, 228, 240));
    const GFXfont *f = &gfx_dejavu_bold11;
    gfx_text(&w->s, f, r.x + (r.w - gfx_text_width(f, label)) / 2, r.y + r.h / 2 + 4, label, GFX_RGB(255, 255, 255));
}

static void hud(unsigned fps, unsigned ms)
{
    gfx_surface *s = &w->s;
    /* cross in the middle */
    int cx = W / 2, cy = H / 2;
    fill_alpha(cx - 7, cy - 1, 15, 2, GFX_RGB(255, 255, 255), 200);
    fill_alpha(cx - 1, cy - 7, 2, 6, GFX_RGB(255, 255, 255), 200);
    fill_alpha(cx - 1, cy + 1, 2, 6, GFX_RGB(255, 255, 255), 200);

    char line[64];
    snprintf(line, sizeof(line), "%u fps %u ms", fps, ms);
    text_shadow(5, 14, line, GFX_RGB(255, 255, 255));
    snprintf(line, sizeof(line), "%d %d %d", (int)floorf(s_p.x), (int)floorf(s_p.y), (int)floorf(s_p.z));
    text_shadow(5, 28, line, GFX_RGB(200, 210, 225));

    button(BUT_MODE, s_build ? "BUILD" : "MINE", s_build);
    button(BUT_FLY, "FLY", s_p.flying);
    button(BUT_HQ, s_scale == 1 ? "HQ" : "LQ", s_scale == 1);
    button(BUT_NEW, "NEW", false);
    button(BUT_JUMP, s_p.flying ? "UP" : "JUMP", false);
    button(BUT_DOWN, "DOWN", false);

    /* hotbar */
    Rect first = slot_rect(0);
    fill_alpha(first.x - 2, first.y - 2, HOTBAR * SLOT + 4, SLOT + 4, GFX_RGB(0, 0, 0), 130);
    for (int i = 0; i < HOTBAR; i++)
    {
        int bi = s_page * HOTBAR + i;
        if (bi >= g_buildable_count)
            break;
        Rect r = slot_rect(i);
        draw_icon(r.x + (SLOT - ICON) / 2, r.y + (SLOT - ICON) / 2, g_buildable[bi]);
        if (i == s_slot && s_build)
        {
            gfx_rect(s, r.x, r.y, r.w, r.h, GFX_RGB(255, 255, 255));
            gfx_rect(s, r.x + 1, r.y + 1, r.w - 2, r.h - 2, GFX_RGB(40, 40, 40));
        }
    }
    Rect pg = button_rect(BUT_PAGE);
    fill_alpha(pg.x, pg.y, pg.w, pg.h, GFX_RGB(0, 0, 0), 130);
    snprintf(line, sizeof(line), "%d", s_page + 1);
    text_shadow(pg.x + 8, pg.y + 16, line, GFX_RGB(255, 255, 255));

    /* where the finger started walking */
    if (s_touch == T_DRIVE)
    {
        gfx_circle(s, s_tx0, s_ty0, 24, GFX_RGB(255, 255, 255));
        int dx = s_tdx > 24 ? 24 : s_tdx < -24 ? -24 : s_tdx, dy = s_tdy > 24 ? 24 : s_tdy < -24 ? -24 : s_tdy;
        gfx_fill_circle(s, s_tx0 + dx, s_ty0 + dy, 7, GFX_RGB(255, 230, 120));
    }
    if (now_us() < s_msg_until)
    {
        int tw = gfx_text_width(&gfx_dejavu_bold11, s_msg);
        fill_alpha((W - tw) / 2 - 6, H - SLOT - 30, tw + 12, 18, GFX_RGB(0, 0, 0), 150);
        text_shadow((W - tw) / 2, H - SLOT - 17, s_msg, GFX_RGB(255, 255, 255));
    }
}

/* ---- main ------------------------------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    bool bench = false; /* -b: turn around for 12 s at each quality, print the frame rates, end */
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-v"))
            s_verbose = true;
        else if (!strcmp(argv[i], "-b"))
            bench = true;
        else
            s_seed_arg = (uint32_t)strtoul(argv[i], nullptr, 16);
    }
    g = gfx_open();
    if (!g)
    {
        printf("voxel: no graphics server\n");
        return 1;
    }
    W = gfx_screen_width(g);
    H = gfx_screen_height(g) - FRAME_ROOM;
    w = gfx_win_create(g, -1, -1, W, H, GFX_WIN_RESIZABLE, "Voxel"); /* RGB565 */
    if (!w)
    {
        printf("voxel: no window\n");
        return 1;
    }
    blocks_init();
    for (int b = 1; b < B_COUNT; b++)
    {
        static uint32_t icon[ICON * ICON];
        memset(icon, 0, sizeof(icon));
        block_icon(icon, ICON, ICON, (uint8_t)b);
        for (int i = 0; i < ICON * ICON; i++)
            s_icon[b][i] = (icon[i] >> 24) ? to565(icon[i]) : (uint16_t)TEX_HOLE;
    }
    mesh_init(&s_mesh);
    if (!world_alloc(&s_world) || !render_init(&s_r, (W + 1) / 2, (H + 1) / 2))
    {
        printf("voxel: not enough memory\n");
        return 1;
    }
    new_world();

    uint64_t last = now_us(), t_fps = last;
    unsigned frames = 0, fps = 0, ms = 0;
    uint64_t busy = 0;
    uint64_t bench_t0 = last;
    unsigned bench_frames[2] = {0, 0};
    uint64_t bench_busy[2] = {0, 0};
    for (;;)
    {
        gfx_event ev;
        while (gfx_next_event(g, &ev, 0) == 0)
        {
            if (ev.h.type == GFX_EV_CLOSE)
            {
                gfx_close(g);
                return 0;
            }
            if (ev.h.type == GFX_EV_POINTER)
            {
                pointer(&ev);
            }
            else if (ev.h.type == GFX_EV_KEY)
            {
                key(&ev);
            }
            else if (ev.h.type == GFX_EV_STICK)
            {
                stick(&ev);
            }
            else if (ev.h.type == GFX_EV_FOCUS && !ev.value)
            {
                release_all();
            }
            else if (ev.h.type == GFX_EV_CONFIGURE)
            {
                int nw = ev.w < 240 ? 240 : ev.w, nh = ev.hgt < 160 ? 160 : ev.hgt;
                if (gfx_win_resize(w, nw, nh) == 0)
                {
                    W = nw;
                    H = nh;
                    render_init(&s_r, (W + s_scale - 1) / s_scale, (H + s_scale - 1) / s_scale);
                }
            }
        }
        uint64_t now = now_us();
        float dt = (float)(now - last) * 1e-6f;
        last = now;

        /* R2 / L2 held: mine / build on */
        if (p_mine && now >= p_mine_at)
        {
            act(0, 0, true, false);
            p_mine_at = now + PAD_REPEAT_US;
        }
        if (p_build && now >= p_build_at)
        {
            act(0, 0, true, true);
            p_build_at = now + PAD_REPEAT_US;
        }

        Controls c;
        steer(dt, &c);
        if (bench)
        {
            float t = (float)(now - bench_t0) * 1e-6f;
            int phase = t < 12.0f ? 0 : 1;
            if (phase == 1 && s_scale == 2)
                press(BUT_HQ);
            if (t >= 24.0f)
            {
                for (int i = 0; i < 2; i++)
                    printf("voxel: benchmark %s: %u fps, %lu ms per frame drawn\n", i ? "full resolution" : "half resolution",
                           bench_frames[i] / 12u, (unsigned long)(bench_busy[i] / (bench_frames[i] ? bench_frames[i] : 1) / 1000u));
                gfx_close(g);
                return 0;
            }
            s_p.yaw = t * 6.2832f / 12.0f;
            s_p.pitch = -0.12f;
            bench_frames[phase]++;
        }
        player_update(&s_p, &s_world, &c, dt);
        mesh_update(&s_mesh, &s_world);

        float o[3];
        eye(o);
        Camera cam = {o[0], o[1], o[2], s_p.yaw, s_p.pitch};
        Hit h = aim(0, 0, true);
        int target[3] = {h.x, h.y, h.z};
        render_frame(&s_r, &s_world, &s_mesh, &cam, h.hit ? target : nullptr);
        busy += now_us() - now;
        if (bench)
            bench_busy[s_scale == 1] += now_us() - now;

        gfx_wait_frame(w, 100); /* the window's pixels are free again */
        blit();
        hud(fps, ms);
        gfx_present(w);

        frames++;
        if (now - t_fps >= 1000000u)
        {
            fps = (unsigned)(frames * 1000000ull / (now - t_fps));
            ms = frames ? (unsigned)(busy / frames / 1000u) : 0;
            if (s_verbose && ++s_report % 5 == 0)
                printf("voxel: %u fps, frame %u ms (sky %lu, solid %lu, water %lu us), %d sections, %d faces, %d triangles, %d rows, %d pixels\n",
                       fps, ms, (unsigned long)s_r.us_sky, (unsigned long)s_r.us_solid, (unsigned long)s_r.us_water,
                       s_r.sections, s_r.quads, s_r.triangles, s_r.rows, s_r.pixels);
            frames = 0;
            busy = 0;
            t_fps = now;
        }
    }
}
