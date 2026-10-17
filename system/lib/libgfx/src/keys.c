/*
 * keys.c - key events <-> characters, US layout: what a keyboard's keys type (ui_key_char)
 * and which keys type a character (ui_char_key, for on-screen keyboards).
 */
#include <stddef.h>
#include <crtos/keys.h>
#include "gfx_ui.h"

/* the keys of the letters a..z */
static const uint8_t s_letters[26] = {
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
};

/* the other printable characters: the key, without and with Shift */
static const struct {
    uint8_t code;
    char plain, shifted;
} s_chars[] = {
    { KEY_1, '1', '!' }, { KEY_2, '2', '@' }, { KEY_3, '3', '#' }, { KEY_4, '4', '$' },
    { KEY_5, '5', '%' }, { KEY_6, '6', '^' }, { KEY_7, '7', '&' }, { KEY_8, '8', '*' },
    { KEY_9, '9', '(' }, { KEY_0, '0', ')' }, { KEY_MINUS, '-', '_' }, { KEY_EQUAL, '=', '+' },
    { KEY_LEFTBRACE, '[', '{' }, { KEY_RIGHTBRACE, ']', '}' }, { KEY_SEMICOLON, ';', ':' },
    { KEY_APOSTROPHE, '\'', '"' }, { KEY_GRAVE, '`', '~' }, { KEY_BACKSLASH, '\\', '|' },
    { KEY_COMMA, ',', '<' }, { KEY_DOT, '.', '>' }, { KEY_SLASH, '/', '?' }, { KEY_SPACE, ' ', ' ' },
};

/* UI_KEY_UP .. UI_KEY_DELETE */
static const uint16_t s_arrows[9] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END, KEY_PAGEUP,
                                      KEY_PAGEDOWN, KEY_DELETE };

/* the numeric keypad: what its keys type (as with Num Lock on) */
static const struct {
    uint8_t code;
    char c;
} s_keypad[] = {
    { KEY_KP0, '0' }, { KEY_KP1, '1' }, { KEY_KP2, '2' }, { KEY_KP3, '3' }, { KEY_KP4, '4' },
    { KEY_KP5, '5' }, { KEY_KP6, '6' }, { KEY_KP7, '7' }, { KEY_KP8, '8' }, { KEY_KP9, '9' },
    { KEY_KPDOT, '.' }, { KEY_KPSLASH, '/' }, { KEY_KPASTERISK, '*' }, { KEY_KPMINUS, '-' },
    { KEY_KPPLUS, '+' },
};

int ui_key_char(struct ui_keys *ks, const struct gfx_event *ev)
{
    if (ev->h.type != GFX_EV_KEY)
        return -1;
    bool down = ev->value != 0;     /* pressed or repeated */
    switch (ev->code) {
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
        ks->shift = down;
        return -1;
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
        ks->ctrl = down;
        return -1;
    case KEY_CAPSLOCK:
        if (ev->value == 1)
            ks->caps = !ks->caps;
        return -1;
    default:
        break;
    }
    if (!down)
        return -1;
    switch (ev->code) {
    case KEY_BACKSPACE:
        return '\b';
    case KEY_ENTER:
    case KEY_KPENTER:
        return '\n';
    case KEY_TAB:
        return '\t';
    case KEY_ESC:
        return 27;
    default:
        break;
    }
    for (int i = 0; i < 9; i++)
        if (ev->code == s_arrows[i])
            return UI_KEY_UP + i;
    if (ev->code == KEY_INSERT)
        return UI_KEY_INSERT;
    for (int i = 0; i < 26; i++)
        if (ev->code == s_letters[i]) {
            if (ks->ctrl)
                return i + 1;
            return (ks->shift != ks->caps ? 'A' : 'a') + i;
        }
    for (size_t i = 0; i < sizeof(s_chars) / sizeof(s_chars[0]); i++)
        if (ev->code == s_chars[i].code) {
            int c = ks->shift ? s_chars[i].shifted : s_chars[i].plain;
            if (ks->ctrl && c >= '@' && c <= '_')
                c &= 0x1f;
            return c;
        }
    for (size_t i = 0; i < sizeof(s_keypad) / sizeof(s_keypad[0]); i++)
        if (ev->code == s_keypad[i].code)
            return s_keypad[i].c;
    return -1;
}

int ui_char_key(int c, uint16_t *code, unsigned *mods)
{
    *mods = 0;
    switch (c) {
    case '\b':
        *code = KEY_BACKSPACE;
        return 0;
    case '\n':
    case '\r':
        *code = KEY_ENTER;
        return 0;
    case '\t':
        *code = KEY_TAB;
        return 0;
    case 27:
        *code = KEY_ESC;
        return 0;
    default:
        break;
    }
    if (c >= UI_KEY_UP && c <= UI_KEY_DELETE) {
        *code = s_arrows[c - UI_KEY_UP];
        return 0;
    }
    if (c == UI_KEY_INSERT) {
        *code = KEY_INSERT;
        return 0;
    }
    if (c >= 'a' && c <= 'z') {
        *code = s_letters[c - 'a'];
        return 0;
    }
    if (c >= 'A' && c <= 'Z') {
        *code = s_letters[c - 'A'];
        *mods = UI_MOD_SHIFT;
        return 0;
    }
    if (c >= 1 && c <= 26) {        /* Ctrl-A .. Ctrl-Z */
        *code = s_letters[c - 1];
        *mods = UI_MOD_CTRL;
        return 0;
    }
    if (c >= 0 && c < 0x20) {       /* Ctrl-@, Ctrl-\ ... Ctrl-_: Ctrl and the key of c + '@' */
        if (ui_char_key(c + '@', code, mods))
            return -1;
        *mods |= UI_MOD_CTRL;
        return 0;
    }
    for (size_t i = 0; i < sizeof(s_chars) / sizeof(s_chars[0]); i++) {
        if (c == s_chars[i].plain) {
            *code = s_chars[i].code;
            return 0;
        }
        if (c == s_chars[i].shifted) {
            *code = s_chars[i].code;
            *mods = UI_MOD_SHIFT;
            return 0;
        }
    }
    return -1;
}
