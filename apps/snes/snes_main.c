/*
 * snes_main.c - the SNES program: a cartridge browser for the SD card and the game screen of
 * the Snes9x core (core/, snes_core.cpp). Played with the DualSense (through the ESP32
 * Bluetooth bridge: esp32-pad), a keyboard or the touch screen.
 *
 *     snes [-v] [FOLDER | ROM]         the browser (in FOLDER), or the game straight away;
 *                                      -v: the core's messages and timing every 5 s
 *     snes [-v] FOLDER NAME            the first game in FOLDER with NAME in its file name
 *     snes -b FOLDER | ROM [N [NAME]]  speed test without a window: N frames (600) drawn and
 *                                      with sound, prints the rate; in a FOLDER the first game
 *                                      (with NAME in its file name); "-b ... NAME shot": the
 *                                      last picture's address is printed and kept 20 s
 *
 * Pacing: an NTSC game (60.1 Hz) follows the display (58.7 Hz): one emulated frame per shown
 * frame (the game runs 2 % slow, the sound too). Games whose rate is far from the display's
 * (PAL, 50 Hz), and games too slow for every display frame, follow the clock and skip drawing
 * frames when they fall behind.
 * Sound: 32 kHz stereo from the core, written to /dev/audio after each frame; the core's rate
 * control keeps the device's queue near 50 ms.
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

#include "gfx.h"
#include "snes_browser.h"
#include "snes_core.h"
#include "snes_idle.h"
#include "snes_fxjit.h"
#include "snes_draw.h"
#include "snes_font.h"

#define FRAME_ROOM 46             /* the task bar and the window's title */
#define REPEAT_DELAY_US 380000u   /* held direction in the menus: first repeat */
#define REPEAT_US 70000u          /* then */
#define STALE_US 100000u          /* a picture the display did not take by then is dropped */
#define AUTOSAVE_US 5000000u
#define MAX_SKIP 4                /* pictures skipped in a row when behind the clock */
#define MAX_EVERY 3               /* paced by the clock: a picture every 1-3 frames */

static struct gfx *g;
static struct gfx_win *w;
static int W, H;

enum mode { MODE_BROWSER, MODE_GAME, MODE_MENU };
static enum mode s_mode;
static bool s_dirty = true;       /* the browser or the menu needs drawing */

/* ---- the game session ------------------------------------------------------------------------ */

static bool s_loaded;
static struct snes_rom_info s_info;
static char s_save[400];          /* battery RAM file ("" if the game has none) */
static uint32_t s_save_sum;

/* the picture on the window: s_lines lines at (s_gx, s_gy) */
static int s_gx, s_gy, s_lines;
static uint16_t *s_menu_pic;      /* the picture under the pause menu */

/* pacing */
static bool s_lock;               /* one frame per display frame */
static bool s_lockable;           /* the game's rate is close to the display's */
static int s_slow;                /* seconds in a row the lock showed too few frames */
static int s_fast;                /* seconds in a row the clock's frames were quick enough for it */
static uint64_t s_sec_us;         /* emulation time this second */
static unsigned s_sec_frames;
static uint64_t s_period_ns, s_next_ns;
static bool s_have_frame;         /* a finished picture not on the window yet */
static uint64_t s_have_since;
static int s_skipped;
/* paced by the clock: a picture every s_every frames (an even rhythm), from the average times
 * of a drawn frame, of showing it and of a skipped frame */
static unsigned s_every, s_frame_no;
static bool s_direct;             /* the frame drew its picture straight into the window */
static uint32_t s_draw_us, s_show_us, s_skip_us;
/* the display's frame rate, measured with the frame numbers of GFX_EV_FRAME */
static uint32_t s_frame0, s_display_mhz;
static uint64_t s_frame0_us;

/* statistics */
static unsigned s_shown, s_fps, s_fps_shown;
static uint64_t s_fps_at, s_autosave_at;
static bool s_panels_dirty;
static bool s_verbose;
static bool s_autokeys;           /* a test: Start and A now and then, as the speed test's "keys" */
static unsigned s_autoframe;
static unsigned s_v_frames, s_v_shown, s_v_seconds;
static uint64_t s_v_us, s_v_max;

/* ---- input ----------------------------------------------------------------------------------- */

struct binding {
    uint16_t code;                /* KEY_* / BTN_* */
    uint16_t pad;                 /* SNES_* buttons */
    uint8_t nav;                  /* enum nav in the menus */
};

static const struct binding KEYMAP[] = {
    /* DualSense through the ESP32 bridge (esp32-pad): its face buttons sit where the SNES's
     * do (cross = B below, circle = A right, square = Y left, triangle = X above); the stick
     * moves the D-pad too */
    {BTN_DPAD_UP, SNES_UP, NAV_UP},
    {BTN_DPAD_DOWN, SNES_DOWN, NAV_DOWN},
    {BTN_DPAD_LEFT, SNES_LEFT, NAV_LEFT},
    {BTN_DPAD_RIGHT, SNES_RIGHT, NAV_RIGHT},
    {BTN_SOUTH, SNES_B, NAV_OK},                /* cross */
    {BTN_EAST, SNES_A, NAV_BACK},               /* circle */
    {BTN_WEST, SNES_Y, NAV_NONE},               /* square */
    {BTN_NORTH, SNES_X, NAV_NONE},              /* triangle */
    {BTN_TL, SNES_L, NAV_PAGE_UP},
    {BTN_TR, SNES_R, NAV_PAGE_DOWN},
    {BTN_TL2, SNES_L, NAV_NONE},
    {BTN_TR2, SNES_R, NAV_NONE},
    {BTN_SELECT, SNES_SELECT, NAV_NONE},        /* create */
    {BTN_START, SNES_START, NAV_OK},            /* options */
    {BTN_MODE, 0, NAV_MENU},                    /* PS */
    {BTN_TRIGGER_HAPPY1, 0, NAV_MENU},          /* touch pad click */
    /* keyboard */
    {KEY_UP, SNES_UP, NAV_UP},
    {KEY_DOWN, SNES_DOWN, NAV_DOWN},
    {KEY_LEFT, SNES_LEFT, NAV_LEFT},
    {KEY_RIGHT, SNES_RIGHT, NAV_RIGHT},
    {KEY_X, SNES_A, NAV_OK},
    {KEY_Z, SNES_B, NAV_BACK},
    {KEY_S, SNES_X, NAV_NONE},
    {KEY_A, SNES_Y, NAV_NONE},
    {KEY_Q, SNES_L, NAV_NONE},
    {KEY_W, SNES_R, NAV_NONE},
    {KEY_ENTER, SNES_START, NAV_OK},
    {KEY_SPACE, SNES_SELECT, NAV_NONE},
    {KEY_TAB, SNES_SELECT, NAV_NONE},
    {KEY_RIGHTSHIFT, SNES_SELECT, NAV_NONE},
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
static uint16_t s_touch;          /* SNES buttons held on the touch screen (all fingers) */
static uint16_t s_finger_touch[GFX_FINGERS]; /* those of each finger */
static unsigned s_touch_game;     /* the fingers that went down on the game screen, a bit each */
static int s_repeat = -1;         /* binding repeating its direction in the menus */
static uint64_t s_repeat_at;

static void menu_open(void);
static void stop_game(void);

/* the SNES joypad from the held keys, buttons and touches */
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
    if ((want & (SNES_LEFT | SNES_RIGHT)) == (SNES_LEFT | SNES_RIGHT))
        want &= (uint16_t)~(SNES_LEFT | SNES_RIGHT);
    if ((want & (SNES_UP | SNES_DOWN)) == (SNES_UP | SNES_DOWN))
        want &= (uint16_t)~(SNES_UP | SNES_DOWN);
    snes_input_set(0, want);
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
    size_t size;
    uint8_t *sram = snes_sram(&size);
    s_save[0] = 0;
    if (!sram)
        return;
    const char *base = strrchr(rom, '/');
    base = base ? base + 1 : rom;
    const char *dot = strrchr(base, '.');
    int len = dot ? (int)(dot - base) : (int)strlen(base);
    snprintf(s_save, sizeof(s_save), SNES_VAR_DIR "/%.*s.srm", len, base);
    FILE *f = fopen(s_save, "rb");
    if (f)
    {
        size_t got = fread(sram, 1, size, f);
        fclose(f);
        printf("snes: %u bytes of save RAM from %s\n", (unsigned)got, s_save);
    }
    s_save_sum = checksum(sram, size);
}

/* writes the save RAM if the game changed it */
static void battery_save(void)
{
    size_t size;
    uint8_t *sram = snes_sram(&size);
    if (!s_loaded || !s_save[0] || !sram)
        return;
    uint32_t sum = checksum(sram, size);
    if (sum == s_save_sum)
        return;
    mkdir("/sd/crtos/var", 0755);
    mkdir(SNES_VAR_DIR, 0755);
    FILE *f = fopen(s_save, "wb");
    if (!f)
    {
        printf("snes: cannot write %s\n", s_save);
        return;
    }
    size_t put = fwrite(sram, 1, size, f);
    if (fclose(f) == 0 && put == size)
        s_save_sum = sum;
}

/* ---- the game screen ------------------------------------------------------------------------- */

struct rect {
    int x, y, w, h;
};

static struct rect r_menu, r_select, r_start, r_l, r_r;
static int s_dpad_x, s_dpad_y;    /* centre of the D-pad */
static int s_face_x, s_face_y;    /* centre of the four face buttons */
#define DPAD_ARM 26
#define FACE_R 15                 /* radius of a face button */
#define FACE_D 29                 /* from the centre of the four */

static bool inside(const struct rect *r, int x, int y)
{
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

static void layout(void)
{
    s_lines = H >= SNES_H ? SNES_H : H;
    s_gx = (W - SNES_W) / 2;
    s_gy = (H - s_lines) / 2;
    int pw = s_gx, rx = s_gx + SNES_W;
    r_menu = (struct rect){8, 8, pw - 16, 20};
    r_l = (struct rect){8, 34, pw - 16, 20};
    r_select = (struct rect){8, H - 28, pw - 16, 20};
    r_r = (struct rect){rx + 8, 34, W - rx - 16, 20};
    r_start = (struct rect){rx + 8, H - 28, W - rx - 16, 20};
    s_dpad_x = pw / 2;
    s_dpad_y = H / 2 + 8;
    s_face_x = rx + (W - rx) / 2;
    s_face_y = H / 2 + 8;
}

static int dist2(int x0, int y0, int x1, int y1)
{
    return (x0 - x1) * (x0 - x1) + (y0 - y1) * (y0 - y1);
}

/* the four face buttons: where they are around their centre, their colour and letter */
static const struct {
    int dx, dy;
    uint16_t button;
    const char *label;
} FACE[4] = {{0, -FACE_D, SNES_X, "X"}, {FACE_D, 0, SNES_A, "A"}, {0, FACE_D, SNES_B, "B"}, {-FACE_D, 0, SNES_Y, "Y"}};

static uint16_t face_colour(uint16_t button)
{
    return button == SNES_A ? C_RED : button == SNES_B ? C_GOLD : button == SNES_X ? C_XBLUE : C_GREEN;
}

/* the SNES buttons under the finger; *menu: the MENU button */
static uint16_t touch_at(int x, int y, bool *menu)
{
    *menu = inside(&r_menu, x, y);
    if (*menu)
        return 0;
    if (inside(&r_select, x, y))
        return SNES_SELECT;
    if (inside(&r_start, x, y))
        return SNES_START;
    if (inside(&r_l, x, y))
        return SNES_L;
    if (inside(&r_r, x, y))
        return SNES_R;
    int dx = x - s_dpad_x, dy = y - s_dpad_y;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (x < s_gx && ax <= 2 * DPAD_ARM + 4 && ay <= 2 * DPAD_ARM + 4)
    {
        /* eight directions: a diagonal where both offsets are alike */
        uint16_t b = 0;
        if (ax > 8 && ax * 2 > ay)
            b |= dx > 0 ? SNES_RIGHT : SNES_LEFT;
        if (ay > 8 && ay * 2 > ax)
            b |= dy > 0 ? SNES_DOWN : SNES_UP;
        return b;
    }
    if (x >= s_gx + SNES_W)
    {
        /* the nearest face button, if the finger is near enough */
        int best = -1, best_d = (FACE_R + 9) * (FACE_R + 9);
        for (int i = 0; i < 4; i++)
        {
            int d = dist2(x, y, s_face_x + FACE[i].dx, s_face_y + FACE[i].dy);
            if (d <= best_d)
            {
                best_d = d;
                best = i;
            }
        }
        if (best >= 0)
            return FACE[best].button;
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
    draw_text(s, r->x + (r->w - text_width(label, 1)) / 2, r->y + (r->h - 8) / 2, label, on ? C_WHITE : C_SKY, 1);
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

static void draw_panels(void)
{
    const struct gfx_surface *s = &w->s;
    uint16_t panel = RGB565(44, 42, 52), on = s_touch;
    draw_fill(s, 0, 0, s_gx, H, panel);
    draw_fill(s, s_gx + SNES_W, 0, W - s_gx - SNES_W, H, panel);
    draw_fill(s, s_gx, 0, SNES_W, s_gy, C_BLACK);
    draw_fill(s, s_gx, s_gy + s_lines, SNES_W, H - s_gy - s_lines, C_BLACK);
    draw_button(s, &r_menu, "MENU", s_mode == MODE_MENU);
    draw_button(s, &r_l, "L", on & SNES_L);
    draw_button(s, &r_r, "R", on & SNES_R);
    draw_button(s, &r_select, "SELECT", on & SNES_SELECT);
    draw_button(s, &r_start, "START", on & SNES_START);

    /* the D-pad */
    int cx = s_dpad_x, cy = s_dpad_y, a = DPAD_ARM, h = DPAD_ARM / 2;
    draw_fill(s, cx - h - 2, cy - 3 * h - 2, a + 4, 3 * a + 4, C_DARK);
    draw_fill(s, cx - 3 * h - 2, cy - h - 2, 3 * a + 4, a + 4, C_DARK);
    draw_fill(s, cx - h, cy - 3 * h, a, 3 * a, C_BLACK);
    draw_fill(s, cx - 3 * h, cy - h, 3 * a, a, C_BLACK);
    if (on & SNES_UP)
        draw_fill(s, cx - h, cy - 3 * h, a, a, C_BLUE);
    if (on & SNES_DOWN)
        draw_fill(s, cx - h, cy + h, a, a, C_BLUE);
    if (on & SNES_LEFT)
        draw_fill(s, cx - 3 * h, cy - h, a, a, C_BLUE);
    if (on & SNES_RIGHT)
        draw_fill(s, cx + h, cy - h, a, a, C_BLUE);
    draw_arrow(s, cx, cy - 3 * h + 7, 0, -1, C_GREY);
    draw_arrow(s, cx, cy + 3 * h - 8, 0, 1, C_GREY);
    draw_arrow(s, cx - 3 * h + 7, cy, -1, 0, C_GREY);
    draw_arrow(s, cx + 3 * h - 8, cy, 1, 0, C_GREY);

    /* the four face buttons in their colours */
    for (int i = 0; i < 4; i++)
    {
        int x = s_face_x + FACE[i].dx, y = s_face_y + FACE[i].dy;
        bool pressed = on & FACE[i].button;
        gfx_fill_circle(s, x, y, FACE_R + 2, GFX_RGB(0, 0, 0));
        gfx_fill_circle(s, x, y, FACE_R, pressed ? GFX_RGB(248, 248, 248) : GFX_RGB(0, 0, 0));
        uint16_t c = face_colour(FACE[i].button);
        if (!pressed)
        {
            /* the button's colour, drawn as a disc */
            int r = FACE_R - 1;
            for (int yy = -r; yy <= r; yy++)
            {
                int xx = 0;
                while ((xx + 1) * (xx + 1) + yy * yy <= r * r)
                    xx++;
                draw_fill(s, x - xx, y + yy, 2 * xx + 1, 1, c);
            }
        }
        draw_text_shadow(s, x - 3, y - 3, FACE[i].label, pressed ? c : C_WHITE, 1);
    }

    /* frames per second on the screen */
    char fps[16];
    snprintf(fps, sizeof(fps), "%u FPS", s_fps);
    int rx = s_gx + SNES_W;
    unsigned full = s_info.pal ? 50u : 60u;
    draw_text(s, rx + ((W - rx) - text_width(fps, 1)) / 2, 14, fps,
              s_fps * 100u >= full * 92u ? C_GREEN : s_fps * 100u >= full * 66u ? C_GOLD : C_RED, 1);
    s_fps_shown = s_fps;
}

/*
 * The core's last picture onto the window: 256 x 224 as it is; 512 wide (hi-res) with the
 * two pixels of each pair averaged; interlaced (twice the lines) every second line; the
 * middle 224 lines of a picture with overscan (239).
 */
static void blit_picture(void)
{
    int width, height, pitch;
    const uint16_t *src = snes_screen(&width, &height, &pitch);
    int step = height > 300 ? 2 : 1;
    int lines = height / step;
    int first = lines > s_lines ? (lines - s_lines) / 2 : 0;
    for (int y = 0; y < s_lines; y++)
    {
        uint16_t *d = (uint16_t *)((uint8_t *)w->s.pix + (s_gy + y) * w->s.stride) + s_gx;
        if (y >= lines)
        {
            memset(d, 0, SNES_W * 2);
            continue;
        }
        const uint16_t *line = src + (size_t)(first + y) * step * pitch;
        if (width <= SNES_W)
        {
            memcpy(d, line, SNES_W * 2);
            continue;
        }
        for (int x = 0; x < SNES_W; x++)
        {
            uint32_t a = line[2 * x], b = line[2 * x + 1];
            d[x] = (uint16_t)(((a & 0xf7deu) >> 1) + ((b & 0xf7deu) >> 1) + (a & b & 0x0821u));
        }
    }
}

/* the picture under the pause menu, kept and put back */
static void save_picture(void)
{
    if (!s_menu_pic)
        s_menu_pic = malloc(SNES_W * SNES_H * 2);
    if (!s_menu_pic)
        return;
    for (int y = 0; y < s_lines; y++)
        memcpy(s_menu_pic + y * SNES_W, (uint8_t *)w->s.pix + (s_gy + y) * w->s.stride + s_gx * 2, SNES_W * 2);
}

static void restore_picture(void)
{
    if (!s_menu_pic)
        return;
    for (int y = 0; y < s_lines; y++)
        memcpy((uint8_t *)w->s.pix + (s_gy + y) * w->s.stride + s_gx * 2, s_menu_pic + y * SNES_W, SNES_W * 2);
}

/* a running average (of microseconds) */
static uint32_t average(uint32_t avg, uint64_t us)
{
    return avg ? (uint32_t)((avg * 7u + us) / 8u) : (uint32_t)us;
}

static void show(void)
{
    uint64_t t0 = crtos_time_us();
    if (!s_direct)
        blit_picture();
    s_direct = false;
    if (s_panels_dirty || s_fps != s_fps_shown)
    {
        draw_panels();
        s_panels_dirty = false;
        gfx_present(w);
    }
    else
    {
        gfx_damage(w, s_gx, s_gy, SNES_W, s_lines);
    }
    s_have_frame = false;
    s_shown++;
    s_show_us = average(s_show_us, crtos_time_us() - t0);
}

/* ---- sound ----------------------------------------------------------------------------------- */

#define SOUND_RATE 32000u
#define SOUND_QUEUE (SOUND_RATE * 50u / 1000u) /* stereo samples kept in the device: 50 ms */

static int s_audio = -1;          /* /dev/audio, or -1: no sound */
static uint32_t s_volume = 80;
static int16_t s_last[2];         /* the last stereo sample written */

static void sound_open(void)
{
    s_audio = open(AUDIO_DEVICE, O_WRONLY | O_NONBLOCK);
    if (s_audio < 0)
    {
        printf("snes: no sound (%s: %s)\n", AUDIO_DEVICE, strerror(errno));
        return;
    }
    if (ioctl(s_audio, AUDIO_IOC_SET_RATE, SOUND_RATE) < 0 || ioctl(s_audio, AUDIO_IOC_SET_CHANNELS, 2) < 0)
    {
        printf("snes: no sound (%s: %s)\n", AUDIO_DEVICE, strerror(errno));
        close(s_audio);
        s_audio = -1;
        return;
    }
    ioctl(s_audio, AUDIO_IOC_GET_VOLUME, &s_volume);
    s_last[0] = s_last[1] = 0;
}

static void sound_close(void)
{
    if (s_audio >= 0)
    {
        close(s_audio);
        s_audio = -1;
    }
}

/* The samples of the last frame to the device */
static void sound_output(void)
{
    static int16_t buf[2 * 1024];
    int n = snes_sound_take(buf, 1024);
    if (s_audio < 0)
        return;
    uint32_t queued = 0;
    ioctl(s_audio, AUDIO_IOC_GET_QUEUED, &queued);
    if (queued < SOUND_QUEUE / 2)
    {
        /* the start, after the menu, a slow frame: the last level up to the target (a device
         * that runs dry clicks) */
        static int16_t pad[2 * 128];
        for (int i = 0; i < 128; i++)
        {
            pad[2 * i] = s_last[0];
            pad[2 * i + 1] = s_last[1];
        }
        uint32_t fill = SOUND_QUEUE - queued;
        while (fill)
        {
            uint32_t k = fill < 128u ? fill : 128u;
            if (write(s_audio, pad, k * 4u) <= 0)
                break;
            fill -= k;
        }
        queued = SOUND_QUEUE;
    }
    if (n > 0)
    {
        write(s_audio, buf, (size_t)n * 4u);
        s_last[0] = buf[2 * n - 2];
        s_last[1] = buf[2 * n - 1];
    }
    /* the clocks of the display, the CPU and the sound drift apart: the core's resampler goes
     * a little faster or slower to keep the queue where it should be */
    snes_sound_pace((int)queued, (int)SOUND_QUEUE);
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
    s_have_frame = true; /* the last picture, without the menu */
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
        snes_reset();
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
    restore_picture();
    draw_panels();
    draw_dim(s, s_gx, s_gy, SNES_W, s_lines);
    int bw = 160, bh = 30 + MENU_ITEMS * 16 + 6;
    int bx = s_gx + (SNES_W - bw) / 2, by = s_gy + (s_lines - bh) / 2;
    draw_frame(s, bx, by, bw, bh, C_WHITE, C_NAVY);
    draw_text_shadow(s, bx + (bw - text_width("PAUSE", 1)) / 2, by + 9, "PAUSE", C_SKY, 1);
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
    uint32_t game_mhz = snes_frame_rate_mhz();
    uint32_t diff = s_display_mhz > game_mhz ? s_display_mhz - game_mhz : game_mhz - s_display_mhz;
    s_lock = s_lockable = diff * 100u < game_mhz * 4u;
    printf("snes: display %lu.%02lu Hz, game %lu.%02lu Hz: %s\n", (unsigned long)(s_display_mhz / 1000u),
           (unsigned long)(s_display_mhz % 1000u / 10u), (unsigned long)(game_mhz / 1000u),
           (unsigned long)(game_mhz % 1000u / 10u), s_lock ? "one frame per display frame" : "paced by the clock");
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

    const char *problem = snes_rom_info(path, &s_info);
    if (problem || !snes_open(path, err, sizeof(err)))
    {
        browser_popup("CANNOT PLAY THIS GAME", problem ? problem : err, false);
        s_dirty = true;
        return;
    }
    browser_popup_close();
    s_loaded = true;
    s_info.pal = snes_is_pal();
    battery_load(path);
    printf("snes: %s (%s, %uK, %s)\n", name, s_info.map, s_info.rom_kb, s_info.pal ? "PAL" : "NTSC");

    char title[64];
    const char *dot = strrchr(name, '.');
    snprintf(title, sizeof(title), "SNES - %.*s", dot ? (int)(dot - name) : (int)strlen(name), name);
    gfx_win_set_title(w, title);
    choose_pacing();
    s_period_ns = 1000000000000ull / snes_frame_rate_mhz();
    sound_open();
    s_mode = MODE_GAME;
    ignore_held();
    s_next_ns = 0;
    s_have_frame = false;
    s_skipped = 0;
    s_every = 1;
    s_frame_no = 0;
    s_draw_us = s_show_us = s_skip_us = 0;
    s_slow = s_fast = 0;
    s_lockable = false;
    s_fps = 0;
    s_shown = 0;
    s_fps_at = crtos_time_us() + 1000000u;
    s_autosave_at = crtos_time_us() + AUTOSAVE_US;
    gfx_wait_frame(w, 50);
    draw_panels();
    draw_fill(&w->s, s_gx, s_gy, SNES_W, s_lines, C_BLACK);
    gfx_present(w);
}

static void stop_game(void)
{
    if (!s_loaded)
        return;
    battery_save();
    sound_close();
    snes_close();
    s_loaded = false;
    s_mode = MODE_BROWSER;
    release_all();
    gfx_win_set_title(w, "SNES");
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
            /* in the game, L1/R1 and the like are only the joypad's */
            if (s_mode == MODE_GAME && nav != NAV_MENU)
                nav = NAV_NONE;
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
        if (!s_next_ns || (ns > s_next_ns && ns - s_next_ns > 8 * s_period_ns))
            s_next_ns = ns; /* just started, or far behind (a pause, too slow): go on from now */
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
        /* a finished picture still waits for the display to take the last one: a few ms for
         * that first, as the next frame would draw over it */
        if (s_have_frame && w->frame_pending && ns - s_next_ns < 4000000u)
        {
            pump(1);
            return;
        }
    }
    if (!due)
    {
        pump(4); /* the next GFX_EV_FRAME */
        return;
    }

    /* paced by the clock: a picture every s_every frames; behind the clock by more than a
     * frame, the next one is not drawn either */
    bool skip = !s_lock && ((s_every > 1 && s_frame_no % s_every) ||
                            (now * 1000u - s_next_ns > s_period_ns && s_skipped < MAX_SKIP));
    s_frame_no++;
    /* a frame to draw while the window's last picture is on the screen already: its lines go
     * straight into the window, strip by strip (no copy of the whole picture afterwards) */
    bool direct = false;
    if (!skip && !w->frame_pending && !w->resize_pending)
    {
        int width, height, pitch;
        snes_screen(&width, &height, &pitch);
        int first = height > s_lines ? (height - s_lines) / 2 : 0;
        if (width == SNES_W && height - first >= s_lines)
        {
            snes_output((uint16_t *)((uint8_t *)w->s.pix + s_gy * w->s.stride) + s_gx, w->s.stride / 2, first,
                        s_lines, height);
            direct = true;
        }
    }
    if (!direct)
        snes_output(NULL, 0, 0, 0, 0);
    uint64_t t0 = crtos_time_us();
    if (s_autokeys)
    {
        unsigned f = s_autoframe++ % 120u;
        snes_input_set(0, f < 4 ? SNES_START : f >= 60 && f < 64 ? SNES_A : 0);
    }
    snes_frame(!skip);
    uint64_t took = crtos_time_us() - t0;
    s_v_us += took;
    s_v_frames++;
    s_sec_us += took;
    s_sec_frames++;
    if (took > s_v_max)
        s_v_max = took;
    sound_output();
    s_next_ns += s_period_ns;
    if (skip)
    {
        s_skipped++;
        s_skip_us = average(s_skip_us, took);
    }
    else
    {
        s_skipped = 0;
        s_draw_us = average(s_draw_us, took);
        s_direct = direct && snes_output_done();
        s_have_frame = true;
        s_have_since = crtos_time_us();
        if (!w->frame_pending)
            show();
    }

    now = crtos_time_us();
    if (now >= s_fps_at)
    {
        s_fps = s_shown;
        /* a game too slow for one frame per display frame is better off with the clock and
         * skipped pictures */
        s_slow = s_lock && s_fps * 1000u < s_display_mhz * 93u / 100u ? s_slow + 1 : 0;
        if (s_slow >= 2)
        {
            printf("snes: %u frames per second: paced by the clock\n", s_fps);
            s_lock = false;
            s_next_ns = 0;
            s_every = 2; /* (a picture every frame did not keep up) */
        }
        /* paced by the clock, but drawn frames quick enough for the display again: back to it */
        uint64_t display_us = s_display_mhz ? 1000000000ull / s_display_mhz : 0;
        s_fast = !s_lock && s_lockable && s_draw_us && s_draw_us + s_show_us < display_us * 80u / 100u
                     ? s_fast + 1
                     : 0;
        if (!s_lock && s_draw_us && s_skip_us)
        {
            /* the rhythm: one picture more often only when the time is well enough (a drawn
             * frame costs more after skipped ones: its data are out of the caches), one less
             * often when the frames of the rhythm do not fit (a margin for the other programs) */
            uint32_t budget = (uint32_t)(s_period_ns / 1000u) * 90u / 100u;
            uint32_t need = (s_draw_us + s_show_us + (s_every - 1) * s_skip_us) / s_every;
            unsigned n = s_every;
            if (need > budget && n < MAX_EVERY)
                n++;
            else if (n > 1 && (s_draw_us + s_show_us + (n - 2) * s_skip_us) / (n - 1) < budget * 85u / 100u)
                n--;
            if (n != s_every && s_verbose)
                printf("snes: a picture every %u frames (drawn %lu us, shown %lu us, skipped %lu us)\n", n,
                       (unsigned long)s_draw_us, (unsigned long)s_show_us, (unsigned long)s_skip_us);
            s_every = n;
        }
        if (s_fast >= 3)
        {
            printf("snes: frames of %lu us: one frame per display frame again\n",
                   (unsigned long)(s_draw_us + s_show_us));
            s_lock = true;
            s_fast = s_slow = 0;
            s_have_frame = false;
        }
        s_sec_us = 0;
        s_sec_frames = 0;
        s_v_shown += s_shown;
        s_shown = 0;
        if (s_verbose && ++s_v_seconds == 5)
        {
            printf("snes: %u shown, %u emulated per s, frame %lu us (max %lu)\n", s_v_shown / 5u, s_v_frames / 5u,
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

static bool is_rom_name(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".sfc") || !strcasecmp(dot, ".smc") || !strcasecmp(dot, ".swc") ||
                   !strcasecmp(dot, ".fig"));
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
        if (is_rom_name(de->d_name) && (!part || strstr(de->d_name, part)))
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
    find_game(argv[2], argc > 4 ? argv[4] : NULL, path, sizeof(path));
    printf("snes: %s\n", path);
    const char *problem = snes_rom_info(path, &s_info);
    if (problem || !snes_open(path, err, sizeof(err)))
    {
        printf("snes: %s\n", problem ? problem : err);
        return 1;
    }
    bool keys = false;
    int verify = 0, verify_bank = -1;
    bool trace = false;
    uint16_t hold = 0;
    int warm = 0, draw_every = 1;
    for (int i = 5; i < argc; i++)
    {
        if (!strncmp(argv[i], "warm", 4))
            warm = atoi(argv[i] + 4); /* frames before the measurement (into the game first) */
        if (!strcmp(argv[i], "noidle"))
            snes_idle_enable(0); /* waiting emulated step by step, to compare with */
        if (!strcmp(argv[i], "keys"))
            keys = true; /* Start and A now and then: through the menus into the game */
        if (!strcmp(argv[i], "nofx"))
            fxjit_enable(0); /* the Super FX interpreted, to compare with */
        if (!strncmp(argv[i], "fxoff", 5))
            fxjit_kind_off(atoi(argv[i] + 5)); /* an instruction kind not translated (a bug hunt) */
        if (!strncmp(argv[i], "fxverify", 8))
            verify = !strcmp(argv[i], "fxverifyram") ? 2 : 1; /* the Super FX translations checked
                                                               * against the interpreter (ram: slow) */
        if (!strncmp(argv[i], "fxbank", 6))
            verify_bank = (int)strtol(argv[i] + 6, NULL, 16); /* only this Super FX program bank */
        if (!strcmp(argv[i], "trace"))
            trace = true; /* every 300 frames: the 65816's PC and the picture's hash */
        if (!strcmp(argv[i], "save"))
            battery_load(path); /* the game's battery save (read only: the test never writes it) */
        if (!strcmp(argv[i], "right"))
            hold = SNES_RIGHT; /* right held while measuring (into the enemies) */
        if (!strncmp(argv[i], "draw", 4) && atoi(argv[i] + 4) > 0)
            draw_every = atoi(argv[i] + 4); /* one picture per N frames (the others skipped) */
    }
    int frames = argc > 3 ? atoi(argv[3]) : 600;
    static int16_t buf[2 * 1024];
    unsigned long samples = 0;
    /* FNV-1a of the sound, to see that a change keeps it: of the first 500 sample pairs per
     * frame, as the last frame's samples may come a little earlier or later */
    uint32_t sound_hash = 2166136261u;
    unsigned long hashed = (unsigned long)frames * 500u * 2u;
    for (int i = 0; i < warm; i++)
    {
        if (keys)
            snes_input_set(0, i % 120 < 4 ? SNES_START : i % 120 >= 60 && i % 120 < 64 ? SNES_A : 0);
        snes_frame(true);
        snes_sound_take(buf, 1024);
    }
    fxjit_verify(verify, verify_bank);
    uint64_t t0 = crtos_time_us(), c0 = own_cycles();
    for (int i = 0; i < frames; i++)
    {
        int f = warm + i;
        uint16_t pad = hold;
        if (keys)
            pad |= f % 120 < 4 ? SNES_START : f % 120 >= 60 && f % 120 < 64 ? SNES_A : 0;
        if (keys || hold)
            snes_input_set(0, pad);
        snes_frame(i % draw_every == draw_every - 1);
        if (trace && f % 300 == 299)
        {
            int tw, th, tp;
            const uint16_t *tpic = snes_screen(&tw, &th, &tp);
            uint32_t th_ = 2166136261u;
            for (int y = 0; y < th; y++)
                for (int x = 0; x < tw; x++)
                    th_ = (th_ ^ tpic[y * tp + x]) * 16777619u;
            printf("snes: frame %d pc %06lx picture %08lx\n", f + 1, (unsigned long)snes_cpu_pc(), (unsigned long)th_);
        }
        int n = snes_sound_take(buf, 1024);
        for (int k = 0; k < 2 * n && samples * 2u + (unsigned long)k < hashed; k++)
            sound_hash = (sound_hash ^ (uint16_t)buf[k]) * 16777619u;
        samples += (unsigned long)n;
    }
    uint64_t us = crtos_time_us() - t0;
    struct crtos_sysinfo si;
    crtos_sys_info(&si);
    uint64_t cpu_us = (own_cycles() - c0) / (si.cpu_hz / 1000000u);
    if (!cpu_us)
        cpu_us = 1;
    printf("snes: %d frames in %lu ms (CPU time %lu ms): %lu.%lu fps, %lu us per frame, %lu samples\n", frames,
           (unsigned long)(us / 1000u), (unsigned long)(cpu_us / 1000u), (unsigned long)(frames * 1000000ull / cpu_us),
           (unsigned long)(frames * 10000000ull / cpu_us % 10u), (unsigned long)(cpu_us / (uint64_t)frames), samples);
    int width, height, pitch;
    const uint16_t *pic = snes_screen(&width, &height, &pitch);
    uint32_t picture_hash = 2166136261u;
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            picture_hash = (picture_hash ^ pic[y * pitch + x]) * 16777619u;
    printf("snes: sound %08lx, picture %08lx (%dx%d)\n", (unsigned long)sound_hash, (unsigned long)picture_hash, width,
           height);
    const struct snes_idle_stats *is = snes_idle_stats();
    uint64_t frame_cycles = (uint64_t)frames * (snes_is_pal() ? 425568u : 357368u);
    printf("snes: waiting fast-forwarded: %lu loops %lu times (%lu%% of the time), WAI %lu%%\n",
           (unsigned long)is->loops, (unsigned long)is->skips, (unsigned long)(is->loop_cycles * 100u / frame_cycles),
           (unsigned long)(is->wai_cycles * 100u / frame_cycles));
    if (verify)
        printf("snes: Super FX checked against the interpreter: %lu runs, %lu different\n",
               (unsigned long)fxjit_verify_stats()->checked, (unsigned long)fxjit_verify_stats()->bad);
    const struct fxjit_stats *fs = fxjit_stats();
    if (fs->runs)
        printf("snes: Super FX: %lu blocks (%lu KB), %lu runs, %lu steps interpreted, left: budget %lu, static %lu, "
               "dynamic %lu, end %lu, odd %lu; %lu compiled, %lu linked, %lu caches, %lu flushes, %lu bugs\n",
               (unsigned long)fs->blocks, (unsigned long)(fs->code_bytes / 1024u), (unsigned long)fs->runs,
               (unsigned long)fs->interp, (unsigned long)fs->x_budget, (unsigned long)fs->x_static,
               (unsigned long)fs->x_dyn, (unsigned long)fs->x_end, (unsigned long)fs->x_odd,
               (unsigned long)fs->compiled, (unsigned long)fs->linked, (unsigned long)fs->slots,
               (unsigned long)fs->flushes, (unsigned long)fs->bugs);
    if (fs->runs)
        printf("snes: Super FX code at %p\n", fs->code);
    if (argc > 5 && !strcmp(argv[5], "shot"))
    {
        /* the last picture (RGB565, pitch in pixels) stays 20 s for the debug probe to read */
        printf("snes: screen at %p %dx%d pitch %d\n", (const void *)pic, width, height, pitch);
        crtos_sleep_ms(20000);
    }
    snes_close();
    return 0;
}

/* ---- main ------------------------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "-v"))
    {
        s_verbose = g_snes_verbose = true;
        argc--;
        argv++;
    }
    if (argc > 1 && !strcmp(argv[1], "-b"))
        return bench(argc, argv);
    const char *arg = argc > 1 ? argv[1] : NULL;

    g = gfx_open();
    if (!g)
    {
        printf("snes: no graphics server\n");
        return 1;
    }
    gfx_on_server_lost(on_server_lost);
    W = gfx_screen_width(g);
    H = gfx_screen_height(g) - FRAME_ROOM;
    w = gfx_win_create(g, -1, -1, W, H, GFX_WIN_MULTITOUCH, "SNES"); /* RGB565 */
    if (!w)
    {
        printf("snes: no window\n");
        return 1;
    }
    layout();

    /* a folder to browse, or a game to start */
    DIR *d = arg ? opendir(arg) : NULL;
    if (d)
        closedir(d);
    browser_init(d ? arg : NULL);
    if (!snes_init())
        browser_popup("SORRY", "NOT ENOUGH MEMORY FOR THE EMULATOR", false);
    else if (arg && !d)
        start_game(arg);
    else if (d && argc > 2)
    {
        static char path[400];
        find_game(arg, argv[2], path, sizeof(path));
        s_autokeys = argc > 3 && !strcmp(argv[3], "keys");
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
