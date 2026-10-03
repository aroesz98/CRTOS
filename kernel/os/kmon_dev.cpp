/*
 * kernel/os/kmon_dev.cpp - kernel monitor commands for testing device drivers:
 * i2cdetect, evtest, fb (display mode), fbtest, gpu2dtest.
 */
#include "kernel.h"
#include <crtos/fb.h>
#include <crtos/gpu2d.h>
#include <crtos/i2c.h>
#include <crtos/input.h>
#include <crtos/vfs.h>
#include <stdlib.h>
#include <string.h>

void kcmd_i2cdetect(int argc, char **argv)
{
    int bus = argc > 1 ? atoi(argv[1]) : 0;
    struct i2c_adapter *a = i2c_adapter_get(bus);
    if (!a) {
        cprintf("no i2c bus %d\n", bus);
        return;
    }
    cprintf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    for (int row = 0; row < 0x80; row += 16) {
        cprintf("%02x:", row);
        for (int c = 0; c < 16; c++) {
            int addr = row + c;
            if (addr < 0x08 || addr > 0x77) {
                cprintf("   ");
                continue;
            }
            uint8_t b;
            struct i2c_msg m = { (uint16_t)addr, I2C_M_RD, 1, &b };
            int r = i2c_transfer(a, &m, 1);
            cprintf(r == 1 ? " %02x" : " --", addr);
        }
        cprintf("\n");
    }
}

static const char *ev_type(uint16_t t)
{
    switch (t) {
    case EV_SYN: return "SYN";
    case EV_KEY: return "KEY";
    case EV_ABS: return "ABS";
    case EV_REL: return "REL";
    default: return "?";
    }
}

void kcmd_evtest(int argc, char **argv)
{
    char path[32];
    ksnprintf(path, sizeof(path), "/dev/%s", argc > 1 ? argv[1] : "event0");
    uint32_t seconds = argc > 2 ? strtoul(argv[2], nullptr, 0) : 10;
    struct file *f;
    int r = vfs_open(path, VFS_O_RDONLY | VFS_O_NONBLOCK, &f);
    if (r) {
        cprintf("evtest: %s: %d\n", path, r);
        return;
    }
    char name[32] = "";
    vfs_ioctl(f, INPUT_IOC_GET_NAME, name);
    cprintf("%s (%s): events for %lu s\n", path, name, (unsigned long)seconds);
    uint32_t t0 = tick_get();
    int n = 0;
    while (tick_get() - t0 < seconds * 1000u) {
        struct input_event ev[8];
        r = vfs_read(f, ev, sizeof(ev));
        if (r == -EAGAIN) {
            task_sleep_ms(10);
            continue;
        }
        if (r <= 0)
            break;
        for (int i = 0; i < r / (int)sizeof(ev[0]); i++, n++)
            cprintf("  %lu.%06lu %s code %u value %ld\n", (unsigned long)ev[i].sec, (unsigned long)ev[i].usec,
                    ev_type(ev[i].type), ev[i].code, (long)ev[i].value);
    }
    vfs_close(f);
    cprintf("%d events\n", n);
}

static uint16_t rgb565(uint32_t rgb)
{
    return (uint16_t)(((rgb >> 8) & 0xF800u) | ((rgb >> 5) & 0x07E0u) | ((rgb >> 3) & 0x001Fu));
}

/* fb [WxH]: the display's mode; with a size, change to that mode of its panel - possible only
 * while no program uses the display (fb_suggest_size) */
void kcmd_fb(int argc, char **argv)
{
    if (argc > 1) {
        char *end;
        unsigned long w = strtoul(argv[1], &end, 10), h = *end == 'x' ? strtoul(end + 1, nullptr, 10) : 0;
        if (!w || !h) {
            cprintf("usage: fb [WIDTHxHEIGHT]\n");
            return;
        }
        int r = fb_suggest_size((unsigned)w, (unsigned)h);
        if (r)
            cprintf("fb0: no change to %lux%lu (%s)\n", w, h,
                    r == -EBUSY ? "in use" : r == -ENOENT ? "the panel has no such mode" : "no display");
    }
    struct fb_info fi;
    if (fb_get_info(0, &fi)) {
        cprintf("no fb0\n");
        return;
    }
    cprintf("fb0: %ux%u, %lu buffer(s) of %lu bytes at %08lx, refresh %lu.%03lu Hz\n", fi.width, fi.height,
            (unsigned long)fi.nbuffers, (unsigned long)fi.buffer_size, (unsigned long)fi.buffer[0],
            (unsigned long)(fi.refresh_mhz / 1000u), (unsigned long)(fi.refresh_mhz % 1000u));
}

void kcmd_fbtest(int argc, char **argv)
{
    int idx = argc > 1 ? atoi(argv[1]) : 0;
    struct fb_info fi;
    if (fb_get_info(idx, &fi)) {
        cprintf("no fb%d\n", idx);
        return;
    }
    cprintf("fb%d: %ux%u stride %lu, %lu buffer(s), refresh %lu.%03lu Hz\n", idx, fi.width, fi.height,
            (unsigned long)fi.stride, (unsigned long)fi.nbuffers, (unsigned long)(fi.refresh_mhz / 1000u),
            (unsigned long)(fi.refresh_mhz % 1000u));
    if (fi.format != FB_FMT_RGB565) {
        cprintf("fbtest: only RGB565 frame buffers\n");
        return;
    }
    /* colour bars into buffer 1 (or 0), drawn by the CPU */
    static const uint32_t bars[8] = { 0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000 };
    unsigned target = fi.nbuffers > 1 ? 1 : 0;
    uint16_t *px = (uint16_t *)fi.buffer[target];
    uint32_t t0 = cpu_cycles();
    for (unsigned y = 0; y < fi.height; y++) {
        uint16_t *row = px + y * (fi.stride / 2);
        for (unsigned x = 0; x < fi.width; x++)
            row[x] = y < fi.height * 3 / 4 ? rgb565(bars[x * 8 / fi.width]) : rgb565((x * 255 / fi.width) * 0x010101u);
    }
    uint32_t cpu_us = (cpu_cycles() - t0) / 600u;
    fb_show(idx, target);
    fb_wait_vsync(idx, 100);
    cprintf("colour bars drawn by the CPU in %lu us, shown in buffer %u\n", (unsigned long)cpu_us, target);

    /* measure the refresh rate from frame-done interrupts */
    uint64_t t1 = time_us();
    int frames = 0;
    while (time_us() - t1 < 500000u && fb_wait_vsync(idx, 100) == 0)
        frames++;
    uint32_t dt = (uint32_t)(time_us() - t1);
    cprintf("%d frames in %lu us: %lu.%01lu Hz measured\n", frames, (unsigned long)dt,
            (unsigned long)((uint64_t)frames * 1000000u / dt), (unsigned long)((uint64_t)frames * 10000000u / dt % 10));

    /* accelerated fill of the other buffer, then flip back */
    struct gpu2d_surface s = { fi.buffer[target ^ 1], fi.width, fi.height, fi.stride, GPU2D_FMT_RGB565 };
    struct gpu2d_rect all = { 0, 0, fi.width, fi.height };
    uint64_t t2 = time_us();
    int r = gpu2d_fill(&s, &all, 0x203060);
    uint32_t fill_us = (uint32_t)(time_us() - t2);
    if (r) {
        cprintf("gpu2d fill: %d (no accelerator?)\n", r);
        return;
    }
    struct gpu2d_rect box = { (int16_t)(fi.width / 4), (int16_t)(fi.height / 4), (uint16_t)(fi.width / 2),
                              (uint16_t)(fi.height / 2) };
    gpu2d_fill(&s, &box, 0xFFA000);
    cprintf("gpu2d: full-screen fill in %lu us (%lu MB/s)\n", (unsigned long)fill_us,
            (unsigned long)(fi.buffer_size / (fill_us ? fill_us : 1)));
    task_sleep_ms(1500);
    fb_show(idx, target ^ 1);
    fb_wait_vsync(idx, 100);
    cprintf("buffer %u shown (blue with an orange box)\n", target ^ 1);
}

/* Self test of the 2D accelerator on buffers in RAM (no display needed) */
void kcmd_gpu2dtest(int, char **)
{
    const uint16_t w = 64, h = 32;
    uint16_t *a = (uint16_t *)kmalloc_aligned(w * h * 2, 64, KM_NOCACHE);
    uint16_t *b = (uint16_t *)kmalloc_aligned(w * h * 2, 64, KM_NOCACHE);
    uint32_t *argb = (uint32_t *)kmalloc_aligned(w * h * 4, 64, KM_NOCACHE);
    if (!a || !b || !argb) {
        cprintf("gpu2dtest: no memory\n");
        kfree(a);
        kfree(b);
        kfree(argb);
        return;
    }
    struct gpu2d_surface sa = { (uintptr_t)a, w, h, (uint32_t)w * 2, GPU2D_FMT_RGB565 };
    struct gpu2d_surface sb = { (uintptr_t)b, w, h, (uint32_t)w * 2, GPU2D_FMT_RGB565 };
    struct gpu2d_surface sc = { (uintptr_t)argb, w, h, (uint32_t)w * 4, GPU2D_FMT_ARGB8888 };
    int fails = 0;

    struct gpu2d_rect all = { 0, 0, w, h };
    memset(a, 0, w * h * 2);
    int r = gpu2d_fill(&sa, &all, 0xFF0000);
    for (int i = 0; i < w * h && !r; i++)
        if (a[i] != 0xF800) {
            cprintf("  fill: pixel %d = %04x, expected f800\n", i, a[i]);
            fails++;
            break;
        }
    cprintf("fill red: %s (%d)\n", r || fails ? "FAIL" : "ok", r);

    struct gpu2d_rect part = { 8, 4, 16, 8 };
    gpu2d_fill(&sa, &part, 0x0000FF);
    memset(b, 0, w * h * 2);
    struct gpu2d_rect src = { 0, 0, w, h };
    r = gpu2d_blit(&sb, 0, 0, &sa, &src, 0);
    int bad = memcmp(a, b, w * h * 2);
    cprintf("copy: %s (%d)\n", r || bad ? "FAIL" : "ok", r);
    fails += r || bad;

    /* 50% white over the red/blue image */
    for (int i = 0; i < w * h; i++)
        argb[i] = 0x80FFFFFFu;
    r = gpu2d_blit(&sb, 0, 0, &sc, &src, GPU2D_BLEND);
    uint16_t px = b[0];
    /* red 0xF800 blended 50% with white 0xFFFF: red stays 31, green ~31/63, blue ~15/31 */
    int g = (px >> 5) & 0x3F, bl = px & 0x1F;
    bool ok = !r && (px >> 11) >= 30 && g >= 28 && g <= 36 && bl >= 13 && bl <= 18;
    cprintf("blend 50%% white over red: %04x %s (%d)\n", px, ok ? "ok" : "FAIL", r);
    fails += !ok;
    cprintf("gpu2dtest: %s\n", fails ? "FAILED" : "ok");
    kfree(a);
    kfree(b);
    kfree(argb);
}
