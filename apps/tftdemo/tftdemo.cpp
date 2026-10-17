/*
 * tftdemo - a clock drawn with TFTLIB, the drawing library of the old firmware, in a window:
 * smooth (anti-aliased) circles, arcs and tapered hands, a gradient and FreeFont text on a
 * 32-bit XRGB8888 surface. Tap it to switch between a ticking and a sweeping second hand
 * (not the tap that gives the window the focus). The window can be resized: the dial follows.
 *
 *     tftdemo [-v]    -v: frames per second, drawing and waiting times every 5 s
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern "C"
{
#include <crtos.h>
}

#include "TFTLIB_8BIT.hpp"
#include "tftdemo_win.h"

static int W = 220, H = 230;

static TFTLIB_8BIT tft;

static uint32_t rgb(uint32_t r, uint32_t g, uint32_t b)
{
    return (r << 16) | (g << 8) | b;
}

static void load_tz(void)
{
    char tz[64] = "CET-1CEST,M3.5.0,M10.5.0/3";
    FILE *f = fopen("/sd/crtos/etc/timezone", "r");
    if (f)
    {
        if (fgets(tz, sizeof(tz), f))
            tz[strcspn(tz, "\r\n")] = 0;
        fclose(f);
    }
    setenv("TZ", tz, 1);
    tzset();
}

/* Point on the dial: angle in degrees clockwise from 12 o'clock */
static void polar(float cx, float cy, float r, float deg, float *x, float *y)
{
    float a = (deg - 90.0f) * 3.14159265f / 180.0f;
    *x = cx + r * cosf(a);
    *y = cy + r * sinf(a);
}

static void draw(const struct tm *t, float sec)
{
    const float cx = W / 2.0f, cy = (H - 34) / 2.0f + 6.0f;
    const float R = fminf((float)W, (float)(H - 40)) / 2.0f - 10.0f;
    const uint32_t bg = rgb(16, 20, 32), face = rgb(236, 238, 244);
    tft.fillRectVGradient(0, 0, W, H, rgb(30, 40, 70), rgb(8, 10, 16));

    /* seconds ring and dial */
    tft.drawSmoothArc((int32_t)cx, (int32_t)cy, (int32_t)R + 8, (int32_t)R + 3, 0, 360, rgb(40, 50, 80), bg, false);
    /* from 12 o'clock (180: TFTLIB's angles start at 6 o'clock) clockwise; angles above 360
     * come round again: an end below the start is drawn through 6 o'clock */
    int end = 180 + (int)(sec * 6.0f);
    if (end > 180)
        tft.drawSmoothArc((int32_t)cx, (int32_t)cy, (int32_t)R + 8, (int32_t)R + 3, 180, end > 360 ? end - 360 : end,
                          rgb(90, 160, 255), bg, true);
    tft.fillSmoothCircle((int32_t)cx, (int32_t)cy, (int32_t)R, face, bg);

    /* hour and minute marks */
    for (int i = 0; i < 60; i++)
    {
        float x0, y0, x1, y1;
        bool hour = i % 5 == 0;
        polar(cx, cy, R - 4, i * 6.0f, &x0, &y0);
        polar(cx, cy, R - (hour ? 14 : 8), i * 6.0f, &x1, &y1);
        tft.drawWedgeLine(x0, y0, x1, y1, hour ? 1.6f : 0.6f, hour ? 1.6f : 0.6f, hour ? rgb(20, 24, 36) : rgb(120, 126, 140),
                          face);
    }
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(rgb(40, 44, 60), face);
    tft.setTextDatum(MC_DATUM);
    for (int h = 1; h <= 12; h++)
    {
        float x, y;
        polar(cx, cy, R - 26, h * 30.0f, &x, &y);
        char n[4];
        snprintf(n, sizeof(n), "%d", h);
        tft.drawString(n, (int32_t)x, (int32_t)y);
    }

    /* hands */
    float hx, hy;
    float hdeg = (t->tm_hour % 12) * 30.0f + t->tm_min * 0.5f;
    float mdeg = t->tm_min * 6.0f + sec * 0.1f;
    polar(cx, cy, R * 0.52f, hdeg, &hx, &hy);
    tft.drawWedgeLine(cx, cy, hx, hy, 4.5f, 2.0f, rgb(30, 34, 48), face);
    polar(cx, cy, R * 0.78f, mdeg, &hx, &hy);
    tft.drawWedgeLine(cx, cy, hx, hy, 3.5f, 1.5f, rgb(30, 34, 48), face);
    float tx, ty;
    polar(cx, cy, R * 0.86f, sec * 6.0f, &hx, &hy);
    polar(cx, cy, -R * 0.18f, sec * 6.0f, &tx, &ty);
    tft.drawWedgeLine(tx, ty, hx, hy, 1.2f, 0.8f, rgb(220, 50, 50), face);
    tft.fillSmoothCircle((int32_t)cx, (int32_t)cy, 5, rgb(220, 50, 50), face);

    /* digital time and date */
    char line[32];
    strftime(line, sizeof(line), "%H:%M:%S", t);
    tft.setFreeFont(&FreeSansBold12pt7b);
    tft.setTextColor(rgb(235, 240, 255), bg);
    tft.drawString(line, W / 2, H - 18);
}

static bool s_sweep = true;
static uint32_t s_focus_ms;     /* when the window got the focus */
static bool s_focus_press;      /* the finger that is down gave the window the focus */

/* One event from the window; false: the window is to be drawn at once (new size) */
static bool handle_event(const struct tdw_event *e)
{
    switch (e->kind)
    {
    case TDW_FOCUS:
        if (e->value)
            s_focus_ms = e->time_ms;
        break;
    case 1: /* down: gfxd sends the focus just before the press that brings it */
        s_focus_press = e->time_ms - s_focus_ms < 50u;
        break;
    case 3: /* up: a tap */
        if (!s_focus_press)
            s_sweep = !s_sweep;
        s_focus_press = false;
        break;
    case TDW_CONFIGURE:
    {
        int nw = e->width < 120 ? 120 : e->width, nh = e->height < 140 ? 140 : e->height;
        uint32_t *p = tdw_resize(nw, nh);
        if (p)
        {
            W = nw;
            H = nh;
            tft.setFramebuffer(p, W, H);
        }
        return false;
    }
    default:
        break;
    }
    return true;
}

int main(int argc, char **argv)
{
    bool verbose = argc > 1 && !strcmp(argv[1], "-v");
    uint32_t *pix = tdw_open(W, H, "TFTLIB clock");
    if (!pix)
    {
        printf("tftdemo: no window\n");
        return 1;
    }
    tft.setFramebuffer(pix, W, H);
    load_tz();
    /* -v: frames, time drawing, time waiting for the display, frames not shown in time */
    uint32_t v_frames = 0, v_late = 0;
    uint64_t v_draw = 0, v_wait = 0, v_at = crtos_time_us() + 5000000u;
    for (;;)
    {
        struct tdw_event e;
        int r;
        while ((r = tdw_event(&e, 0)) > 0)
            handle_event(&e);
        if (r < 0)
            break;
        int64_t us = crtos_wall_us();
        time_t now = (time_t)(us / 1000000);
        struct tm t;
        localtime_r(&now, &t);
        float sec = (float)t.tm_sec + (s_sweep ? (float)(us % 1000000) / 1e6f : 0.0f);
        uint64_t t0 = crtos_time_us();
        draw(&t, sec);
        uint64_t t1 = crtos_time_us();
        if (tdw_present())
            v_late++;
        v_frames++;
        v_draw += t1 - t0;
        v_wait += crtos_time_us() - t1;
        if (verbose && crtos_time_us() >= v_at)
        {
            printf("tftdemo (thread %d): %s, %lu.%lu frames/s, drawing %lu us, waiting for the display %lu us, %lu late\n",
                   crtos_gettid(), s_sweep ? "sweeping" : "ticking", (unsigned long)(v_frames / 5u),
                   (unsigned long)(v_frames * 2u % 10u), (unsigned long)(v_draw / v_frames),
                   (unsigned long)(v_wait / v_frames), (unsigned long)v_late);
            v_frames = v_late = 0;
            v_draw = v_wait = 0;
            v_at += 5000000u;
        }
        /* ticking: nothing moves until the next second; sweeping: 20 frames a second are
         * smooth enough (every frame is drawn in full, anti-aliased, by the CPU) */
        int32_t left = s_sweep ? 50 : (int32_t)(1000 - (crtos_wall_us() / 1000) % 1000);
        uint64_t until = crtos_time_us() + (uint64_t)left * 1000u;
        while ((r = tdw_event(&e, (uint32_t)left)) > 0)
        {
            if (!handle_event(&e))
                break; /* draw the new size at once */
            uint64_t now2 = crtos_time_us();
            if (now2 >= until)
                break;
            left = (int32_t)((until - now2) / 1000u);
        }
        if (r < 0)
            break;
    }
    tdw_close();
    return 0;
}
