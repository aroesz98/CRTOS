/*
 * nes_main.c - the NES program: a cartridge browser for the SD card and the game screen of
 * the emulator core (core/ with the fast PPU of nes_ppu.c). Played with the DualSense
 * (through the ESP32 Bluetooth bridge: esp32-pad), a keyboard or the touch screen.
 *
 *     nes [-i] [FOLDER | ROM]     the browser (in FOLDER), or the game straight away;
 *                                 -i: the 6502 interpreted, not translated (nes_jit.c)
 *     nes [-i] FOLDER NAME        the first game in FOLDER with NAME in its file name
 *     nes -t [N]                  the recompiler against the interpreter, N cases per opcode
 *     nes -b FOLDER | ROM [N [S]] speed test without a window: N frames (300), prints the rate;
 *                                 S 0: without the sound synthesizer; in a FOLDER the first
 *                                 game (with NAME in its file name: -b FOLDER N S NAME);
 *                                 -b FOLDER N S NAME shot: the last picture's address is
 *                                 printed and kept 20 s (to read it through the debug probe)
 *
 * Pacing: an NTSC game (60.1 Hz) follows the display (58.7 Hz): one emulated frame per shown
 * frame, no dropped or doubled pictures (the game runs 2 % slow). Games whose rate is far from
 * the display's (PAL, 50 Hz) follow the clock and skip drawing frames when they fall behind.
 * Sound: nes_sound.c makes 48 kHz samples from the APU, written to /dev/audio after each
 * frame; the device's queue stays near 45 ms (a little more or fewer samples per frame).
 * While the game follows the display the sound is 2 % lower, like the game is 2 % slower.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <crtos.h>
#include <crtos/audio.h>
#include <crtos/keys.h>

#include "controller.h"
#include "gfx.h"
#include "nes_browser.h"
#include "nes_core.h"
#include "nes_draw.h"
#include "nes_font.h"
#include "nes_jit.h"
#include "nes_ppu.h"
#include "nes_sound.h"

#define FRAME_ROOM 46             /* the task bar and the window's title */
#define REPEAT_DELAY_US 380000u   /* held direction in the menus: first repeat */
#define REPEAT_US 70000u          /* then */
#define STALE_US 100000u          /* a picture the display did not take by then is dropped */
#define AUTOSAVE_US 5000000u

static struct gfx *g;
static struct gfx_win *w;
static int W, H;

enum mode { MODE_BROWSER, MODE_GAME, MODE_MENU };
static enum mode s_mode;
static bool s_dirty = true;       /* the browser or the menu needs drawing */

/* ---- the game session ------------------------------------------------------------------------ */

static Emulator s_emu;
static bool s_loaded;
static struct nes_rom_info s_info;
static char s_save[400];          /* battery RAM file ("" if the game has none) */
static uint32_t s_save_sum;

/* the picture on the window: s_lines lines from s_first at (s_gx, s_gy) */
static int s_gx, s_gy, s_first, s_lines;

/* pacing */
static bool s_lock;               /* one frame per display frame */
static int s_slow;                /* seconds in a row the lock showed too few frames */
static uint64_t s_period_ns, s_next_ns;
static bool s_have_frame;         /* a finished picture not on the window yet */
static bool s_restore;            /* show() puts the picture saved for the menu back first */
static uint64_t s_damage_at;      /* when the window's last picture went to the display */
static uint64_t s_have_since;
static int s_skipped;
/* the display's frame rate, measured with the frame numbers of GFX_EV_FRAME */
static uint32_t s_frame0, s_display_mhz;
static uint64_t s_frame0_us;

/* statistics */
static unsigned s_shown, s_fps, s_fps_shown;
static uint64_t s_fps_at, s_autosave_at;
static bool s_panels_dirty;
static bool s_verbose;            /* -v: timing every 5 s */
static unsigned s_v_frames, s_v_shown, s_v_seconds;
static uint64_t s_v_us, s_v_max;

/* ---- input ----------------------------------------------------------------------------------- */

struct binding {
    uint16_t code;                /* KEY_* / BTN_* */
    uint16_t pad;                 /* NES buttons (controller.h) */
    uint8_t nav;                  /* enum nav in the menus */
};

static const struct binding KEYMAP[] = {
    /* DualSense through the ESP32 bridge (esp32-pad): the stick moves the D-pad too */
    {BTN_DPAD_UP, UP, NAV_UP},
    {BTN_DPAD_DOWN, DOWN, NAV_DOWN},
    {BTN_DPAD_LEFT, LEFT, NAV_LEFT},
    {BTN_DPAD_RIGHT, RIGHT, NAV_RIGHT},
    {BTN_SOUTH, BUTTON_B, NAV_OK},              /* cross */
    {BTN_EAST, BUTTON_A, NAV_BACK},             /* circle */
    {BTN_WEST, TURBO_B, NAV_NONE},              /* square */
    {BTN_NORTH, TURBO_A, NAV_NONE},             /* triangle */
    {BTN_TRIGGER_HAPPY5, TURBO_B, NAV_NONE},    /* the back buttons of the Edge */
    {BTN_TRIGGER_HAPPY6, TURBO_A, NAV_NONE},
    {BTN_SELECT, SELECT, NAV_NONE},             /* create */
    {BTN_START, START, NAV_OK},                 /* options */
    {BTN_TL, 0, NAV_PAGE_UP},
    {BTN_TR, 0, NAV_PAGE_DOWN},
    {BTN_MODE, 0, NAV_MENU},                    /* PS */
    {BTN_TRIGGER_HAPPY1, 0, NAV_MENU},          /* touch pad click */
    /* keyboard */
    {KEY_UP, UP, NAV_UP},
    {KEY_DOWN, DOWN, NAV_DOWN},
    {KEY_LEFT, LEFT, NAV_LEFT},
    {KEY_RIGHT, RIGHT, NAV_RIGHT},
    {KEY_X, BUTTON_A, NAV_OK},
    {KEY_Z, BUTTON_B, NAV_BACK},
    {KEY_S, TURBO_A, NAV_NONE},
    {KEY_A, TURBO_B, NAV_NONE},
    {KEY_ENTER, START, NAV_OK},
    {KEY_SPACE, SELECT, NAV_NONE},
    {KEY_TAB, SELECT, NAV_NONE},
    {KEY_RIGHTSHIFT, SELECT, NAV_NONE},
    {KEY_BACKSPACE, 0, NAV_BACK},
    {KEY_ESC, 0, NAV_MENU},
    {KEY_F1, 0, NAV_MENU},
    {KEY_PAGEUP, 0, NAV_PAGE_UP},
    {KEY_PAGEDOWN, 0, NAV_PAGE_DOWN},
    {KEY_HOME, 0, NAV_HOME},
    {KEY_END, 0, NAV_END},
};
#define NBIND ((int)(sizeof(KEYMAP) / sizeof(KEYMAP[0])))

enum held { RELEASED, HELD, IGNORED /* held since before the game went on */ };
static uint8_t s_held[NBIND];
static uint16_t s_touch;          /* NES buttons held on the touch screen (all fingers) */
static uint16_t s_finger_touch[GFX_FINGERS]; /* those of each finger */
static unsigned s_touch_game;     /* the fingers that went down on the game screen, a bit each */
static int s_repeat = -1;         /* binding repeating its direction in the menus */
static uint64_t s_repeat_at;

static void menu_open(void);
static void stop_game(void);

/* the NES controller from the held keys, buttons and touches */
static void update_pad(void)
{
    uint16_t want = 0;
    if (s_mode == MODE_GAME)
    {
        for (int i = 0; i < NBIND; i++)
            if (s_held[i] == HELD)
                want |= KEYMAP[i].pad;
        want |= s_touch;
    }
    /* a real D-pad cannot press opposite directions (some games break on it) */
    if ((want & (LEFT | RIGHT)) == (LEFT | RIGHT))
        want &= (uint16_t)~(LEFT | RIGHT);
    if ((want & (UP | DOWN)) == (UP | DOWN))
        want &= (uint16_t)~(UP | DOWN);
    if (!s_loaded)
        return;
    JoyPad *pad = &s_emu.mem.joy1;
    /* turbo_trigger() toggles A and B while their turbo is held: keep the phase */
    pad->status = (uint16_t)(want | (pad->status & (want >> 8) & 3));
}

/* keys that are down now mean nothing to the game until they are pressed again */
static void ignore_held(void)
{
    for (int i = 0; i < NBIND; i++)
        if (s_held[i])
            s_held[i] = IGNORED;
    s_touch = 0;
    memset(s_finger_touch, 0, sizeof(s_finger_touch));
    s_touch_game = 0;
    s_repeat = -1;
    update_pad();
}

static void release_all(void)
{
    memset(s_held, 0, sizeof(s_held));
    s_touch = 0;
    memset(s_finger_touch, 0, sizeof(s_finger_touch));
    s_touch_game = 0;
    s_repeat = -1;
    update_pad();
}

/* ---- battery saves --------------------------------------------------------------------------- */

static uint32_t checksum(const uint8_t *p, size_t n)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < n; i++)
        sum = sum * 31u + p[i];
    return sum;
}

static void battery_load(const char *rom)
{
    Mapper *m = &s_emu.mapper;
    s_save[0] = 0;
    if (!s_info.battery || !m->PRG_RAM || !m->RAM_size)
        return;
    const char *base = strrchr(rom, '/');
    base = base ? base + 1 : rom;
    const char *dot = strrchr(base, '.');
    int len = dot ? (int)(dot - base) : (int)strlen(base);
    snprintf(s_save, sizeof(s_save), NES_VAR_DIR "/%.*s.sav", len, base);
    FILE *f = fopen(s_save, "rb");
    if (f)
    {
        size_t got = fread(m->PRG_RAM, 1, m->RAM_size, f);
        fclose(f);
        printf("nes: %u bytes of save RAM from %s\n", (unsigned)got, s_save);
    }
    s_save_sum = checksum(m->PRG_RAM, m->RAM_size);
}

/* writes the save RAM if the game changed it */
static void battery_save(void)
{
    Mapper *m = &s_emu.mapper;
    if (!s_loaded || !s_save[0] || !m->PRG_RAM)
        return;
    uint32_t sum = checksum(m->PRG_RAM, m->RAM_size);
    if (sum == s_save_sum)
        return;
    mkdir("/sd/crtos/var", 0755);
    mkdir(NES_VAR_DIR, 0755);
    FILE *f = fopen(s_save, "wb");
    if (!f)
    {
        printf("nes: cannot write %s\n", s_save);
        return;
    }
    size_t put = fwrite(m->PRG_RAM, 1, m->RAM_size, f);
    if (fclose(f) == 0 && put == m->RAM_size)
        s_save_sum = sum;
}

/* ---- the game screen ------------------------------------------------------------------------- */

struct rect {
    int x, y, w, h;
};

static struct rect r_menu, r_select, r_start;
static int s_dpad_x, s_dpad_y;    /* centre of the D-pad */
static int s_b_x, s_b_y, s_a_x, s_a_y;
#define DPAD_ARM 26
#define BUTTON_R 21

static bool inside(const struct rect *r, int x, int y)
{
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

static void layout(void)
{
    s_lines = H >= NES_H ? NES_H : H >= 224 ? 224 : H;
    s_first = (NES_H - s_lines) / 2;
    s_gx = (W - NES_W) / 2;
    s_gy = (H - s_lines) / 2;
    int pw = s_gx, rx = s_gx + NES_W;
    r_menu = (struct rect){8, 8, pw - 16, 20};
    r_select = (struct rect){8, H - 28, pw - 16, 20};
    r_start = (struct rect){rx + 8, H - 28, W - rx - 16, 20};
    s_dpad_x = pw / 2;
    s_dpad_y = H / 2;
    s_b_x = rx + (W - rx) / 2 - 26;
    s_b_y = H / 2 + 8;
    s_a_x = rx + (W - rx) / 2 + 26;
    s_a_y = H / 2 - 8;
}

static int dist2(int x0, int y0, int x1, int y1)
{
    return (x0 - x1) * (x0 - x1) + (y0 - y1) * (y0 - y1);
}

/* the NES buttons under the finger; *menu: the MENU button */
static uint16_t touch_at(int x, int y, bool *menu)
{
    *menu = inside(&r_menu, x, y);
    if (*menu)
        return 0;
    if (inside(&r_select, x, y))
        return SELECT;
    if (inside(&r_start, x, y))
        return START;
    int dx = x - s_dpad_x, dy = y - s_dpad_y;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (x < s_gx && ax <= 2 * DPAD_ARM + 4 && ay <= 2 * DPAD_ARM + 4)
    {
        /* eight directions: a diagonal where both offsets are alike */
        uint16_t b = 0;
        if (ax > 8 && ax * 2 > ay)
            b |= dx > 0 ? RIGHT : LEFT;
        if (ay > 8 && ay * 2 > ax)
            b |= dy > 0 ? DOWN : UP;
        return b;
    }
    if (x >= s_gx + NES_W)
    {
        int db = dist2(x, y, s_b_x, s_b_y), da = dist2(x, y, s_a_x, s_a_y);
        int r = BUTTON_R + 10;
        if (da <= r * r || db <= r * r)
            return da < db ? BUTTON_A : BUTTON_B;
    }
    return 0;
}

static void draw_button(const struct gfx_surface *s, const struct rect *r, const char *label, bool on)
{
    draw_fill(s, r->x, r->y, r->w, r->h, on ? C_BLUE : C_BLACK);
    draw_fill(s, r->x, r->y, r->w, 1, C_DARK);
    draw_fill(s, r->x, r->y + r->h - 1, r->w, 1, C_DARK);
    draw_fill(s, r->x, r->y, 1, r->h, C_DARK);
    draw_fill(s, r->x + r->w - 1, r->y, 1, r->h, C_DARK);
    draw_text(s, r->x + (r->w - text_width(label, 1)) / 2, r->y + (r->h - 8) / 2, label, on ? C_WHITE : C_RED, 1);
}

/* a small filled triangle pointing dx, dy (one of them 0) with its tip at x, y */
static void draw_arrow(const struct gfx_surface *s, int x, int y, int dx, int dy, uint16_t c)
{
    for (int k = 0; k < 5; k++)
    {
        if (dy)
            draw_fill(s, x - k, y - dy * k, 2 * k + 1, 1, c);
        else
            draw_fill(s, x - dx * k, y - k, 1, 2 * k + 1, c);
    }
}

static void draw_round_button(const struct gfx_surface *s, int x, int y, const char *label, bool on)
{
    gfx_fill_circle(s, x, y, BUTTON_R + 2, GFX_RGB(0, 0, 0));
    gfx_fill_circle(s, x, y, BUTTON_R, on ? GFX_RGB(252, 160, 68) : GFX_RGB(200, 20, 16));
    draw_text_shadow(s, x - 7, y - 7, label, C_WHITE, 2);
}

static void draw_panels(void)
{
    const struct gfx_surface *s = &w->s;
    uint16_t panel = RGB565(40, 40, 48), on = s_touch;
    draw_fill(s, 0, 0, s_gx, H, panel);
    draw_fill(s, s_gx + NES_W, 0, W - s_gx - NES_W, H, panel);
    draw_fill(s, s_gx, 0, NES_W, s_gy, C_BLACK);
    draw_fill(s, s_gx, s_gy + s_lines, NES_W, H - s_gy - s_lines, C_BLACK);
    draw_button(s, &r_menu, "MENU", s_mode == MODE_MENU);
    draw_button(s, &r_select, "SELECT", on & SELECT);
    draw_button(s, &r_start, "START", on & START);

    /* the D-pad */
    int cx = s_dpad_x, cy = s_dpad_y, a = DPAD_ARM, h = DPAD_ARM / 2;
    draw_fill(s, cx - h - 2, cy - 3 * h - 2, a + 4, 3 * a + 4, C_DARK);
    draw_fill(s, cx - 3 * h - 2, cy - h - 2, 3 * a + 4, a + 4, C_DARK);
    draw_fill(s, cx - h, cy - 3 * h, a, 3 * a, C_BLACK);
    draw_fill(s, cx - 3 * h, cy - h, 3 * a, a, C_BLACK);
    if (on & UP)
        draw_fill(s, cx - h, cy - 3 * h, a, a, C_BLUE);
    if (on & DOWN)
        draw_fill(s, cx - h, cy + h, a, a, C_BLUE);
    if (on & LEFT)
        draw_fill(s, cx - 3 * h, cy - h, a, a, C_BLUE);
    if (on & RIGHT)
        draw_fill(s, cx + h, cy - h, a, a, C_BLUE);
    draw_arrow(s, cx, cy - 3 * h + 7, 0, -1, C_GREY);
    draw_arrow(s, cx, cy + 3 * h - 8, 0, 1, C_GREY);
    draw_arrow(s, cx - 3 * h + 7, cy, -1, 0, C_GREY);
    draw_arrow(s, cx + 3 * h - 8, cy, 1, 0, C_GREY);

    draw_round_button(s, s_b_x, s_b_y, "B", on & BUTTON_B);
    draw_round_button(s, s_a_x, s_a_y, "A", on & BUTTON_A);

    /* frames per second on the screen */
    char fps[16];
    snprintf(fps, sizeof(fps), "%u FPS", s_fps);
    int rx = s_gx + NES_W;
    draw_text(s, rx + ((W - rx) - text_width(fps, 1)) / 2, 14, fps, s_fps >= 55 ? C_GREEN : s_fps >= 40 ? C_GOLD : C_RED, 1);
    s_fps_shown = s_fps;
}

/*
 * The emulator draws its lines straight into the window (the SDRAM's bandwidth is what
 * drawing costs most: no picture of its own, no copy). The next frame starts only after the
 * display has taken the last one (window_busy), else the picture would tear.
 */
static void game_output(void)
{
    uint16_t *pix = (uint16_t *)((uint8_t *)w->s.pix + s_gy * w->s.stride) + s_gx;
    nes_set_output(&s_emu.ppu, pix, (int)(w->s.stride / 2u), s_first, s_lines);
}

static bool window_busy(uint64_t now)
{
    /* (a display that does not take our pictures at all, a hidden window: not waited for) */
    return w->frame_pending && now - s_damage_at < STALE_US;
}

/* the picture into the PPU's own buffer (under the menu), and back */
static void save_picture(void)
{
    uint16_t *buf = nes_screen(&s_emu.ppu);
    for (int y = 0; y < s_lines; y++)
        memcpy(buf + y * NES_W, (uint8_t *)w->s.pix + (s_gy + y) * w->s.stride + s_gx * 2, NES_W * 2);
}

static void blit(void)
{
    const uint16_t *buf = nes_screen(&s_emu.ppu);
    for (int y = 0; y < s_lines; y++)
        memcpy((uint8_t *)w->s.pix + (s_gy + y) * w->s.stride + s_gx * 2, buf + y * NES_W, NES_W * 2);
}

static void show(void)
{
    if (s_restore)
    {
        blit();
        s_restore = false;
    }
    if (s_panels_dirty || s_fps != s_fps_shown)
    {
        draw_panels();
        s_panels_dirty = false;
        gfx_present(w);
    }
    else
    {
        gfx_damage(w, s_gx, s_gy, NES_W, s_lines);
    }
    s_damage_at = crtos_time_us();
    s_have_frame = false;
    s_shown++;
}

/* ---- sound ----------------------------------------------------------------------------------- */

#define SOUND_RATE 48000u
#define SOUND_QUEUE (SOUND_RATE * 45u / 1000u) /* samples kept in the device: 45 ms */

static int s_audio = -1;          /* /dev/audio, or -1: no sound */
static uint32_t s_sound_base;     /* CPU cycles per sample (1/65536) at the game's pace */
static uint32_t s_volume = 80;
static int16_t s_last_sample;

/* CPU cycles per sample: the game's cycles per second as it runs now - its frames at the
 * display's pace (the sound is lower then, as the game is slower) or at its own */
static void sound_pace(void)
{
    uint32_t fps_mhz = s_lock && s_display_mhz ? s_display_mhz : nes_frame_rate_mhz(&s_emu);
    uint64_t cycles_x2 = s_emu.type == PAL ? 66495u : 59561u; /* CPU cycles per frame, twice */
    s_sound_base = (uint32_t)(cycles_x2 * fps_mhz * 65536u / (2000ull * SOUND_RATE));
    nes_sound_set_step(s_sound_base);
}

static void sound_open(void)
{
    s_audio = open(AUDIO_DEVICE, O_WRONLY | O_NONBLOCK);
    if (s_audio < 0)
    {
        printf("nes: no sound (%s: %s)\n", AUDIO_DEVICE, strerror(errno));
        return;
    }
    if (ioctl(s_audio, AUDIO_IOC_SET_RATE, SOUND_RATE) < 0 || ioctl(s_audio, AUDIO_IOC_SET_CHANNELS, 1) < 0)
    {
        printf("nes: no sound (%s: %s)\n", AUDIO_DEVICE, strerror(errno));
        close(s_audio);
        s_audio = -1;
        return;
    }
    ioctl(s_audio, AUDIO_IOC_GET_VOLUME, &s_volume);
    nes_sound_start(&s_emu.apu, SOUND_RATE);
    sound_pace();
    s_last_sample = 0;
}

static void sound_close(void)
{
    nes_sound_stop();
    if (s_audio >= 0)
    {
        close(s_audio);
        s_audio = -1;
    }
}

/* The samples of the last frame to the device */
static void sound_output(void)
{
    static int16_t buf[2048];
    int n = nes_sound_take(buf, 2048);
    if (s_audio < 0)
        return;
    uint32_t queued = 0;
    ioctl(s_audio, AUDIO_IOC_GET_QUEUED, &queued);
    if (queued < SOUND_QUEUE / 2)
    {
        /* the start, after the menu, a slow frame: the last level up to the target (a device
         * that runs dry clicks) */
        static int16_t pad[256];
        for (int i = 0; i < 256; i++)
            pad[i] = s_last_sample;
        uint32_t fill = SOUND_QUEUE - queued;
        while (fill)
        {
            uint32_t k = fill < 256u ? fill : 256u;
            if (write(s_audio, pad, k * 2u) <= 0)
                break;
            fill -= k;
        }
        queued = SOUND_QUEUE;
    }
    if (n > 0)
    {
        write(s_audio, buf, (size_t)n * 2u);
        s_last_sample = buf[n - 1];
    }
    /* the clocks of the display, the CPU and the sound drift apart: a little more or fewer
     * samples per frame (at most 1 %) keep the queue where it should be */
    int32_t err = (int32_t)queued - (int32_t)SOUND_QUEUE;
    int64_t adj = (int64_t)s_sound_base * err / (int32_t)SOUND_QUEUE / 100;
    int64_t lim = s_sound_base / 100u;
    if (adj > lim)
        adj = lim;
    if (adj < -lim)
        adj = -lim;
    nes_sound_set_step((uint32_t)((int64_t)s_sound_base + adj));
}

static void sound_volume(int volume)
{
    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;
    if (s_audio >= 0 && ioctl(s_audio, AUDIO_IOC_SET_VOLUME, (uint32_t)volume) == 0)
        s_volume = (uint32_t)volume;
}

/* ---- the pause menu -------------------------------------------------------------------------- */

enum { ITEM_CONTINUE, ITEM_RESET, ITEM_VOLUME, ITEM_QUIT, MENU_ITEMS };
static const char *const MENU[MENU_ITEMS] = {"CONTINUE", "RESET", "VOLUME", "QUIT GAME"};
static int s_menu_sel;
static struct rect s_menu_rect[MENU_ITEMS];

static void resume(void)
{
    s_mode = MODE_GAME;
    ignore_held();
    s_next_ns = 0; /* the clock starts again */
    s_panels_dirty = true;
    s_have_frame = true; /* the picture without the menu */
    s_restore = true;
    s_have_since = crtos_time_us();
}

static void menu_open(void)
{
    save_picture();
    s_mode = MODE_MENU;
    s_menu_sel = 0;
    release_all();
    battery_save();
    s_dirty = true;
}

static void menu_choose(void)
{
    switch (s_menu_sel)
    {
    case ITEM_CONTINUE:
        resume();
        break;
    case ITEM_RESET:
        nes_reset(&s_emu);
        resume();
        break;
    case ITEM_VOLUME:
        sound_volume(s_volume >= 100 ? 0 : (int)s_volume + 10);
        s_dirty = true;
        break;
    default:
        stop_game();
        break;
    }
}

static void menu_nav(enum nav nav)
{
    switch (nav)
    {
    case NAV_UP:
        s_menu_sel = (s_menu_sel + MENU_ITEMS - 1) % MENU_ITEMS;
        break;
    case NAV_DOWN:
        s_menu_sel = (s_menu_sel + 1) % MENU_ITEMS;
        break;
    case NAV_LEFT:
    case NAV_RIGHT:
        if (s_menu_sel != ITEM_VOLUME)
            return;
        sound_volume((int)s_volume + (nav == NAV_LEFT ? -10 : 10));
        break;
    case NAV_OK:
        menu_choose();
        return;
    case NAV_BACK:
    case NAV_MENU:
        resume();
        return;
    default:
        return;
    }
    s_dirty = true;
}

static void draw_menu(uint64_t now)
{
    const struct gfx_surface *s = &w->s;
    blit();
    draw_panels();
    draw_dim(s, s_gx, s_gy, NES_W, s_lines);
    int bw = 160, bh = 30 + MENU_ITEMS * 16 + 6;
    int bx = s_gx + (NES_W - bw) / 2, by = s_gy + (s_lines - bh) / 2;
    draw_frame(s, bx, by, bw, bh, C_WHITE, C_NAVY);
    draw_text_shadow(s, bx + (bw - text_width("PAUSE", 1)) / 2, by + 9, "PAUSE", C_GOLD, 1);
    bool blink = (now / 400000u) & 1u;
    for (int i = 0; i < MENU_ITEMS; i++)
    {
        int y = by + 28 + i * 16;
        s_menu_rect[i] = (struct rect){bx + 4, y - 4, bw - 8, 16};
        if (i == s_menu_sel && !blink)
            draw_char(s, bx + 16, y, GLYPH_CURSOR, C_WHITE, 1);
        char text[24];
        if (i == ITEM_VOLUME && s_audio < 0)
            snprintf(text, sizeof(text), "NO SOUND");
        else if (i == ITEM_VOLUME)
            snprintf(text, sizeof(text), "VOLUME < %lu >", (unsigned long)s_volume);
        else
            snprintf(text, sizeof(text), "%s", MENU[i]);
        draw_text(s, bx + 30, y, text, i == s_menu_sel ? C_WHITE : C_GREY, 1);
    }
}

/* ---- starting and ending a game -------------------------------------------------------------- */

static void draw_ui(uint64_t now)
{
    gfx_wait_frame(w, 50); /* gfxd is done with the pixels */
    if (s_mode == MODE_MENU)
        draw_menu(now);
    else
        browser_draw(&w->s, now);
    gfx_present(w);
    s_dirty = false;
}

/* Follows the display if the game's rate is close to it (known after the first seconds) */
static void choose_pacing(void)
{
    if (!s_loaded || !s_display_mhz)
    {
        s_lock = false;
        return;
    }
    uint32_t game_mhz = nes_frame_rate_mhz(&s_emu);
    uint32_t diff = s_display_mhz > game_mhz ? s_display_mhz - game_mhz : game_mhz - s_display_mhz;
    s_lock = diff * 100u < game_mhz * 4u;
    printf("nes: display %lu.%02lu Hz, game %lu.%02lu Hz: %s\n", (unsigned long)(s_display_mhz / 1000u),
           (unsigned long)(s_display_mhz % 1000u / 10u), (unsigned long)(game_mhz / 1000u),
           (unsigned long)(game_mhz % 1000u / 10u), s_lock ? "one frame per display frame" : "paced by the clock");
    sound_pace();
}

/* a picture of ours is on the screen, @frame is the display's frame number: after 1.5 s of
 * them the display's frame rate is known */
static void frame_shown(uint32_t frame)
{
    uint64_t now = crtos_time_us();
    if (!s_frame0_us)
    {
        s_frame0 = frame;
        s_frame0_us = now;
        return;
    }
    if (s_display_mhz || now - s_frame0_us < 1500000u)
        return;
    s_display_mhz = (uint32_t)((uint64_t)(frame - s_frame0) * 1000000000ull / (now - s_frame0_us));
    choose_pacing();
}

static void start_game(const char *path)
{
    char err[160];
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    browser_remember();
    browser_popup("LOADING", name, true);
    s_mode = MODE_BROWSER;
    draw_ui(crtos_time_us());
    gfx_wait_frame(w, 100);

    const char *problem = nes_rom_info(path, &s_info);
    if (problem || !nes_open(&s_emu, path, err, sizeof(err)))
    {
        browser_popup("CANNOT PLAY THIS GAME", problem ? problem : err, false);
        s_dirty = true;
        return;
    }
    browser_popup_close();
    s_loaded = true;
    battery_load(path);
    printf("nes: %s (mapper %d %s, %s)\n", name, s_info.mapper, s_info.mapper_name, s_info.pal ? "PAL" : "NTSC");

    char title[64];
    snprintf(title, sizeof(title), "NES - %.*s", (int)(strlen(name) > 4 ? strlen(name) - 4 : strlen(name)), name);
    gfx_win_set_title(w, title);
    choose_pacing();
    s_period_ns = 1000000000000ull / nes_frame_rate_mhz(&s_emu);
    game_output();
    sound_open();
    s_mode = MODE_GAME;
    ignore_held();
    s_next_ns = 0;
    s_have_frame = false;
    s_restore = false;
    s_skipped = 0;
    s_slow = 0;
    s_fps = 0;
    s_shown = 0;
    s_fps_at = crtos_time_us() + 1000000u;
    s_autosave_at = crtos_time_us() + AUTOSAVE_US;
    g_ppu_skip = 0;
    gfx_wait_frame(w, 50);
    draw_panels();
    draw_fill(&w->s, s_gx, s_gy, NES_W, s_lines, C_BLACK);
    gfx_present(w);
}

static void stop_game(void)
{
    if (!s_loaded)
        return;
    battery_save();
    sound_close();
    nes_close(&s_emu);
    s_loaded = false;
    s_mode = MODE_BROWSER;
    release_all();
    gfx_win_set_title(w, "NES");
    s_dirty = true;
}

static void finish(void)
{
    stop_game();
    browser_remember();
}

static void on_server_lost(void)
{
    battery_save();
}

/* ---- events ---------------------------------------------------------------------------------- */

static void do_nav(enum nav nav, uint16_t code)
{
    switch (s_mode)
    {
    case MODE_BROWSER:
    {
        if (code == KEY_ESC)
            nav = NAV_BACK; /* Esc leaves a folder here */
        enum browser_result r = browser_nav(nav);
        if (r == BROWSER_REDRAW)
            s_dirty = true;
        else if (r == BROWSER_OPEN)
            start_game(browser_path());
        break;
    }
    case MODE_GAME:
        if (nav == NAV_MENU)
            menu_open();
        break;
    case MODE_MENU:
        menu_nav(nav);
        break;
    }
}

static bool repeats(enum nav nav)
{
    return nav >= NAV_UP && nav <= NAV_PAGE_DOWN;
}

static void on_key(uint16_t code, int32_t value)
{
    if (value == 2)
        return; /* the keyboard's own repeat: the menus repeat by themselves */
    for (int i = 0; i < NBIND; i++)
    {
        if (KEYMAP[i].code != code)
            continue;
        if (!value)
        {
            s_held[i] = RELEASED;
            if (s_repeat == i)
                s_repeat = -1;
        }
        else if (s_held[i] == RELEASED)
        {
            s_held[i] = HELD;
            enum nav nav = (enum nav)KEYMAP[i].nav;
            if (repeats(nav))
            {
                s_repeat = i;
                s_repeat_at = crtos_time_us() + REPEAT_DELAY_US;
            }
            do_nav(nav, code);
        }
        break;
    }
    update_pad();
}

static void repeat_tick(uint64_t now)
{
    if (s_repeat < 0 || s_mode == MODE_GAME || now < s_repeat_at)
        return;
    s_repeat_at = now + REPEAT_US;
    do_nav((enum nav)KEYMAP[s_repeat].nav, KEYMAP[s_repeat].code);
}

/* a finger (the window takes all of them: the buttons can be held together) */
static void on_pointer(int kind, unsigned finger, int x, int y)
{
    if (finger >= GFX_FINGERS || (s_mode != MODE_GAME && finger))
        return; /* (the browser and the menu: one finger) */
    if (s_mode == MODE_BROWSER)
    {
        enum browser_result r = browser_pointer(kind, x, y);
        if (r == BROWSER_REDRAW)
            s_dirty = true;
        else if (r == BROWSER_OPEN)
            start_game(browser_path());
        return;
    }
    if (s_mode == MODE_MENU)
    {
        if (kind != GFX_PTR_DOWN)
            return;
        if (inside(&r_menu, x, y))
        {
            resume();
            return;
        }
        for (int i = 0; i < MENU_ITEMS; i++)
            if (inside(&s_menu_rect[i], x, y))
            {
                s_menu_sel = i;
                menu_choose();
                return;
            }
        return;
    }
    bool menu = false;
    unsigned bit = 1u << finger;
    uint16_t b = s_finger_touch[finger];
    if (kind == GFX_PTR_DOWN)
    {
        b = touch_at(x, y, &menu);
        s_touch_game |= bit;
    }
    else if (kind == GFX_PTR_MOVE && (s_touch_game & bit))
    {
        b = touch_at(x, y, &menu);
        menu = false; /* only a press opens the menu */
    }
    else if (kind == GFX_PTR_UP)
    {
        b = 0;
        s_touch_game &= ~bit;
    }
    if (menu)
    {
        menu_open();
        return;
    }
    s_finger_touch[finger] = b;
    uint16_t t = 0;
    for (unsigned i = 0; i < GFX_FINGERS; i++)
        t |= s_finger_touch[i];
    if (t != s_touch)
    {
        s_touch = t;
        s_panels_dirty = true;
        update_pad();
    }
}

static void handle(const struct gfx_event *ev)
{
    switch (ev->h.type)
    {
    case GFX_EV_CLOSE:
        finish();
        gfx_close(g);
        exit(0);
    case GFX_EV_KEY:
        on_key(ev->code, ev->value);
        break;
    case GFX_EV_POINTER:
        on_pointer(ev->kind, ev->code, ev->x, ev->y);
        break;
    case GFX_EV_WHEEL: /* the mouse wheel moves through the lists as the arrows (not in a game) */
        if (s_mode != MODE_GAME && ev->code == GFX_WHEEL_VERTICAL)
            for (int n = ev->value > 0 ? ev->value : -ev->value; n > 0; n--)
            {
                on_key(ev->value > 0 ? KEY_UP : KEY_DOWN, 1);
                on_key(ev->value > 0 ? KEY_UP : KEY_DOWN, 0);
            }
        break;
    case GFX_EV_FRAME:
        frame_shown((uint32_t)ev->value);
        break;
    case GFX_EV_FOCUS:
        if (!ev->value)
        {
            release_all();
            if (s_mode == MODE_GAME)
                menu_open(); /* someone else has the keyboard: pause */
        }
        break;
    default:
        break;
    }
}

/* handles the events that come within @timeout ms (0: the queued ones) */
static void pump(uint32_t timeout)
{
    struct gfx_event ev;
    while (gfx_next_event(g, &ev, timeout) == 0)
    {
        handle(&ev);
        timeout = 0;
    }
}

/* ---- the game loop --------------------------------------------------------------------------- */

static void run_game(void)
{
    uint64_t now = crtos_time_us();
    if (s_have_frame && !w->frame_pending)
        show();
    if (s_have_frame && now - s_have_since > STALE_US)
        s_have_frame = false; /* the display does not take it (hidden window?) */

    bool due;
    if (s_lock)
    {
        due = !s_have_frame;
    }
    else
    {
        uint64_t ns = now * 1000u;
        if (!s_next_ns || (ns > s_next_ns && ns - s_next_ns > 4 * s_period_ns))
            s_next_ns = ns; /* just started, or far behind (a pause, the SD card): go on from now */
        due = ns >= s_next_ns;
        if (!due)
        {
            uint64_t left_us = (s_next_ns - ns) / 1000u;
            if (left_us >= 2000u)
                pump((uint32_t)(left_us / 1000u - 1u));
            else
                crtos_yield();
            return;
        }
    }
    if (!due)
    {
        pump(4); /* the next GFX_EV_FRAME */
        return;
    }

    /* behind the clock by more than a frame: emulate the next one without drawing it */
    bool skip = !s_lock && now * 1000u - s_next_ns > s_period_ns && s_skipped < 3;
    if (!skip && window_busy(now))
    {
        pump(4); /* the next GFX_EV_FRAME */
        return;
    }
    g_ppu_skip = skip;
    uint64_t t0 = crtos_time_us();
    nes_frame(&s_emu);
    uint64_t took = crtos_time_us() - t0;
    s_v_us += took;
    s_v_frames++;
    if (took > s_v_max)
        s_v_max = took;
    sound_output();
    s_next_ns += s_period_ns;
    if (skip)
    {
        s_skipped++;
    }
    else
    {
        s_skipped = 0;
        s_have_frame = true;
        s_have_since = crtos_time_us();
        if (!w->frame_pending)
            show();
    }

    now = crtos_time_us();
    if (now >= s_fps_at)
    {
        s_fps = s_shown;
        /* a game too slow for one frame per display frame gets every second one: the clock
         * with skipped pictures is better than that */
        s_slow = s_lock && s_fps * 1000u < s_display_mhz * 85u / 100u ? s_slow + 1 : 0;
        if (s_slow >= 3)
        {
            printf("nes: %u frames per second: paced by the clock from now on\n", s_fps);
            s_lock = false;
            s_next_ns = 0;
            sound_pace();
        }
        s_v_shown += s_shown;
        s_shown = 0;
        if (s_verbose && ++s_v_seconds == 5)
        {
            printf("nes: %u shown, %u emulated per s, frame %lu us (max %lu)\n", s_v_shown / 5u, s_v_frames / 5u,
                   (unsigned long)(s_v_us / (s_v_frames ? s_v_frames : 1u)), (unsigned long)s_v_max);
            s_v_seconds = s_v_frames = s_v_shown = 0;
            s_v_us = s_v_max = 0;
        }
        s_fps_at += 1000000u;
        if (now > s_fps_at)
            s_fps_at = now + 1000000u;
    }
    if (now >= s_autosave_at)
    {
        battery_save();
        s_autosave_at = now + AUTOSAVE_US;
    }
}

/* ---- speed test ------------------------------------------------------------------------------ */

/* CPU time of this process in CPU cycles (other programs may run next to the test) */
static uint64_t own_cycles(void)
{
    struct crtos_procinfo pi;
    int pid = getpid();
    for (int i = 0; crtos_proc_info(i, &pi) == 0; i++)
        if (pi.pid == pid)
            return pi.cycles;
    return 0;
}

/* the first game in folder @dir (with @part in its file name) into @path; @dir itself if it
 * is no folder */
static void find_game(const char *dir, const char *part, char *path, size_t size)
{
    snprintf(path, size, "%s", dir);
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *de;
    while ((de = readdir(d)))
    {
        size_t n = strlen(de->d_name);
        if (n > 4 && !strcasecmp(de->d_name + n - 4, ".nes") && (!part || strstr(de->d_name, part)))
        {
            snprintf(path, size, "%s/%s", dir, de->d_name);
            break;
        }
    }
    closedir(d);
}

static int bench(int argc, char **argv)
{
    char err[160], path[400];
    if (argc < 3)
        return 2;
    find_game(argv[2], argc > 5 ? argv[5] : NULL, path, sizeof(path));
    printf("nes: %s\n", path);
    if (!nes_open(&s_emu, path, err, sizeof(err)))
    {
        printf("nes: %s\n", err);
        return 1;
    }
    int frames = argc > 3 ? atoi(argv[3]) : 300;
    bool sound = argc <= 4 || atoi(argv[4]) != 0; /* the synthesizer too (not played) */
    if (sound)
        nes_sound_start(&s_emu.apu, SOUND_RATE);
    static int16_t buf[2048];
    unsigned long samples = 0;
    uint64_t t0 = crtos_time_us(), c0 = own_cycles();
    for (int i = 0; i < frames; i++)
    {
        nes_frame(&s_emu);
        samples += (unsigned long)nes_sound_take(buf, 2048);
    }
    uint64_t us = crtos_time_us() - t0;
    /* the CPU time the test itself used: the speed even if other programs ran too */
    struct crtos_sysinfo si;
    crtos_sys_info(&si);
    uint64_t cpu_us = (own_cycles() - c0) / (si.cpu_hz / 1000000u);
    if (!cpu_us)
        cpu_us = 1;
    nes_sound_stop();
    printf("nes: %d frames in %lu ms (CPU time %lu ms): %lu.%lu fps, %lu us per frame, %lu samples\n", frames,
           (unsigned long)(us / 1000u), (unsigned long)(cpu_us / 1000u), (unsigned long)(frames * 1000000ull / cpu_us),
           (unsigned long)(frames * 10000000ull / cpu_us % 10u), (unsigned long)(cpu_us / (uint64_t)frames), samples);
    if (g_nes_jit)
    {
        const struct jit_stats *js = jit_stats();
        printf("nes: jit: %lu blocks (%lu KB), %lu flushes, %lu links, %lu entries, %lu instructions interpreted\n",
               (unsigned long)js->blocks, (unsigned long)(js->code_bytes / 1024u), (unsigned long)js->flushes,
               (unsigned long)js->linked, (unsigned long)js->runs, (unsigned long)js->interpreted);
        printf("nes: jit: interpreted at checkpoints %lu, interrupts/DMA %lu, outside ROM %lu"
               " (%lu %lu %lu %lu %lu %lu %lu %lu by 4 KB), not translated %lu\n",
               (unsigned long)js->why_budget, (unsigned long)js->why_irq, (unsigned long)js->why_ram,
               (unsigned long)js->ram_pc[0], (unsigned long)js->ram_pc[1], (unsigned long)js->ram_pc[2],
               (unsigned long)js->ram_pc[3], (unsigned long)js->ram_pc[4], (unsigned long)js->ram_pc[5],
               (unsigned long)js->ram_pc[6], (unsigned long)js->ram_pc[7], (unsigned long)js->why_op);
    }
    if (argc > 6 && !strcmp(argv[6], "shot"))
    {
        /* the last picture (256x240 RGB565) stays 20 s for the debug probe to read */
        printf("nes: screen at %p\n", (void *)nes_screen(&s_emu.ppu));
        crtos_sleep_ms(20000);
    }
    nes_close(&s_emu);
    return 0;
}

/* ---- main ------------------------------------------------------------------------------------ */

int main(int argc, char **argv)
{

    for (;;)
    {
        if (argc > 1 && !strcmp(argv[1], "-v"))
            s_verbose = true;
        else if (argc > 1 && !strcmp(argv[1], "-i"))
            g_nes_jit = 0; /* the interpreter alone */
        else
            break;
        argc--;
        argv++;
    }
    if (argc > 1 && !strcmp(argv[1], "-t"))
        return jit_selftest(argc > 2 ? atoi(argv[2]) : 200) ? 1 : 0;
    if (argc > 1 && !strcmp(argv[1], "-b"))
        return bench(argc, argv);
    const char *arg = argc > 1 ? argv[1] : NULL;

    g = gfx_open();
    if (!g)
    {
        printf("nes: no graphics server\n");
        return 1;
    }
    gfx_on_server_lost(on_server_lost);
    W = gfx_screen_width(g);
    H = gfx_screen_height(g) - FRAME_ROOM;
    w = gfx_win_create(g, -1, -1, W, H, GFX_WIN_MULTITOUCH, "NES"); /* RGB565 */
    if (!w)
    {
        printf("nes: no window\n");
        return 1;
    }
    layout();

    /* a folder to browse, or a game to start */
    DIR *d = arg ? opendir(arg) : NULL;
    if (d)
        closedir(d);
    browser_init(d ? arg : NULL);
    if (arg && !d)
    {
        start_game(arg);
    }
    else if (d && argc > 2)
    {
        static char path[400];
        find_game(arg, argv[2], path, sizeof(path));
        start_game(path);
    }

    uint64_t blink = 0;
    for (;;)
    {
        pump(0);
        uint64_t now = crtos_time_us();
        repeat_tick(now);
        if (s_mode == MODE_GAME)
        {
            run_game();
            continue;
        }
        /* the browser and the menu: the cursor blinks */
        if (now / 400000u != blink)
        {
            blink = now / 400000u;
            s_dirty = true;
        }
        if (s_dirty)
            draw_ui(now);
        pump(s_repeat >= 0 ? 10 : 50);
    }
}
