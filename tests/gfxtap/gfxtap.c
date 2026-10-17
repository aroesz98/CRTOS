/*
 * gfxtap - synthetic input for the graphics server, for tests and remote control:
 *
 *     gfxtap tap X Y [N]                   touch and release (N times, 150 ms apart)
 *     gfxtap drag X0 Y0 X1 Y1 [steps]      touch, move in steps (16 ms apart), release
 *     gfxtap hold X0 Y0 X1 Y1 [MS]         two fingers (0 and 1) down together for MS ms (1000)
 *     gfxtap mclick X Y [N]                the mouse's left button: click (N times, a double
 *                                          click with N = 2)
 *     gfxtap mdrag X0 Y0 X1 Y1 [steps]     the mouse's left button held while it moves
 *     gfxtap wheel X Y N [h]               the mouse wheel at X, Y: N notches (up positive;
 *                                          h: the horizontal one, right positive)
 *     gfxtap hover X Y [X1 Y1 [steps]]     the mouse moved without the button to X, Y (or from
 *                                          there to X1, Y1 in steps 16 ms apart): tooltips
 *     gfxtap key CODE[+CODE...]            press the keys in order, release them backwards
 *                                          (Linux KEY_* codes: 29+46 is Ctrl-C)
 *     gfxtap type TEXT                     type the text (US layout; "\n" Enter, "\t" Tab,
 *                                          "\e" Esc, "\\" a backslash)
 *     gfxtap stick N X Y [MS]              gamepad stick N (0 left, 1 right, 2 triggers) at
 *                                          X, Y (-32767..32767) for MS ms (500), then at rest
 *
 * It sends GFX_INPUT like inputd does (the mouse ones marked GFX_PTR_MOUSE, as from a mouse).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include <crtos/keys.h>
#include "gfx.h"
#include "gfx_ui.h"

static int s_port;

static void send(uint16_t kind, int x, int y, uint16_t code, int32_t value)
{
    struct gfx_input in = { { GFX_INPUT, sizeof(in) }, kind, code, (int16_t)x, (int16_t)y, value,
                            (uint32_t)(crtos_time_us() / 1000u) };
    crtos_msg_send(s_port, &in, sizeof(in), -1, 1000);
}

static void drag(int x0, int y0, int x1, int y1, int steps, int32_t flags)
{
    if (steps < 1)
        steps = 1;
    send(GFX_PTR_DOWN, x0, y0, 0, flags);
    for (int i = 1; i <= steps; i++) {
        crtos_sleep_ms(16);
        send(GFX_PTR_MOVE, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, 0, flags);
    }
    crtos_sleep_ms(16);
    send(GFX_PTR_UP, x1, y1, 0, flags);
}

static void key(uint16_t code, int32_t value)
{
    send(GFX_KEY, 0, 0, code, value);
    crtos_sleep_ms(15);
}

/* One character as its key, with Shift or Ctrl held around it */
static int type_char(int c)
{
    uint16_t code;
    unsigned mods;
    if (ui_char_key(c, &code, &mods))
        return -1;
    if (mods & UI_MOD_CTRL)
        key(KEY_LEFTCTRL, 1);
    if (mods & UI_MOD_SHIFT)
        key(KEY_LEFTSHIFT, 1);
    key(code, 1);
    key(code, 0);
    if (mods & UI_MOD_SHIFT)
        key(KEY_LEFTSHIFT, 0);
    if (mods & UI_MOD_CTRL)
        key(KEY_LEFTCTRL, 0);
    return 0;
}

static int usage(void)
{
    printf("usage: gfxtap tap X Y [N] | drag X0 Y0 X1 Y1 [steps] | hold X0 Y0 X1 Y1 [MS] |\n"
           "              mclick X Y [N] | mdrag X0 Y0 X1 Y1 [steps] | wheel X Y N [h] |\n"
           "              hover X Y [X1 Y1 [steps]] |\n"
           "              key CODE[+CODE...] | type TEXT | stick N X Y [MS]\n");
    return 2;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return usage();
    s_port = crtos_port_connect(GFX_PORT_NAME, 2000);
    if (s_port < 0) {
        printf("gfxtap: no graphics server\n");
        return 1;
    }
    int r = 0;
    if ((!strcmp(argv[1], "tap") || !strcmp(argv[1], "mclick")) && argc >= 4) {
        int x = atoi(argv[2]), y = atoi(argv[3]), n = argc > 4 ? atoi(argv[4]) : 1;
        int32_t flags = argv[1][0] == 'm' ? GFX_PTR_MOUSE : 0;
        for (int i = 0; i < n; i++) {
            if (i)
                crtos_sleep_ms(flags ? 60 : 90);
            send(GFX_PTR_DOWN, x, y, 0, flags);
            crtos_sleep_ms(60);
            send(GFX_PTR_UP, x, y, 0, flags);
        }
    } else if ((!strcmp(argv[1], "drag") || !strcmp(argv[1], "mdrag")) && argc >= 6) {
        drag(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), argc > 6 ? atoi(argv[6]) : 10,
             argv[1][0] == 'm' ? GFX_PTR_MOUSE : 0);
    } else if (!strcmp(argv[1], "hold") && argc >= 6) {
        int x0 = atoi(argv[2]), y0 = atoi(argv[3]), x1 = atoi(argv[4]), y1 = atoi(argv[5]);
        send(GFX_PTR_DOWN, x0, y0, 0, 0);
        crtos_sleep_ms(30);
        send(GFX_PTR_DOWN, x1, y1, 1, 0);
        crtos_sleep_ms(argc > 6 ? (uint32_t)atoi(argv[6]) : 1000u);
        send(GFX_PTR_UP, x1, y1, 1, 0);
        send(GFX_PTR_UP, x0, y0, 0, 0);
    } else if (!strcmp(argv[1], "wheel") && argc >= 5) {
        uint16_t axis = argc > 5 && argv[5][0] == 'h' ? GFX_WHEEL_HORIZONTAL : GFX_WHEEL_VERTICAL;
        send(GFX_WHEEL, atoi(argv[2]), atoi(argv[3]), axis, atoi(argv[4]));
    } else if (!strcmp(argv[1], "hover") && argc >= 4) {
        int x0 = atoi(argv[2]), y0 = atoi(argv[3]);
        int x1 = argc >= 6 ? atoi(argv[4]) : x0, y1 = argc >= 6 ? atoi(argv[5]) : y0;
        int steps = argc >= 6 ? (argc > 6 ? atoi(argv[6]) : 10) : 0;
        if (steps < 0)
            steps = 0;
        send(GFX_PTR_HOVER, x0, y0, 0, GFX_PTR_MOUSE);
        for (int i = 1; i <= steps; i++) {
            crtos_sleep_ms(16);
            send(GFX_PTR_HOVER, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, 0, GFX_PTR_MOUSE);
        }
    } else if (!strcmp(argv[1], "key") && argc >= 3) {
        uint16_t codes[8];
        int n = 0;
        for (char *p = argv[2]; *p && n < 8; p++) {
            codes[n++] = (uint16_t)strtoul(p, &p, 10);
            if (*p != '+')
                break;
        }
        for (int i = 0; i < n; i++)
            key(codes[i], 1);
        crtos_sleep_ms(15);
        for (int i = n - 1; i >= 0; i--)
            key(codes[i], 0);
    } else if (!strcmp(argv[1], "type") && argc >= 3) {
        for (int a = 2; a < argc; a++) {
            if (a > 2)
                type_char(' ');
            for (const char *p = argv[a]; *p; p++) {
                int c = (unsigned char)*p;
                if (c == '\\' && p[1]) {
                    p++;
                    c = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p == 'e' ? 27 : (unsigned char)*p;
                }
                if (type_char(c)) {
                    printf("gfxtap: no key types '%c'\n", c);
                    r = 1;
                }
            }
        }
    } else if (!strcmp(argv[1], "stick") && argc >= 5) {
        uint16_t n = (uint16_t)atoi(argv[2]);
        send(GFX_STICK, atoi(argv[3]), atoi(argv[4]), n, 0);
        crtos_sleep_ms(argc > 5 ? (uint32_t)atoi(argv[5]) : 500u);
        send(GFX_STICK, 0, 0, n, 0);
    } else {
        close(s_port);
        return usage();
    }
    close(s_port);
    return r;
}
