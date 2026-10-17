/*
 * lcdif-imxrt.ko - i.MX RT eLCDIF parallel RGB display controller with its panel.
 *
 * The panel (phandle "display") provides the enable and backlight GPIOs and its modes: the
 * entries of display-timings, one per panel that may be fitted (the EVKB's 480x272 one, an
 * 800x480 one...). The driver starts in the default mode (display-timings/native-mode, else
 * the first entry) and changes to another when the fb framework asks (set_size: a touch driver
 * found the panel's size, fb_suggest_size()), which it does only before anybody uses the
 * display. Two RGB565 frame buffers in non-cacheable memory are published as /dev/fb0; a
 * buffer switch requested with fb_show() takes effect at the next frame, which the
 * frame-done interrupt reports.
 */
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/fb.h>
#include <crtos/gpio.h>
#include <crtos/irq.h>
#include <crtos/mm.h>
#include <crtos/module.h>
#include <crtos/sched.h>
#include <crtos/sync.h>
#include <string.h>
#include "fsl_elcdif.h"

#define NBUF 2

struct lcdif
{
    struct device *dev;
    LCDIF_Type *base;
    int irq;
    int fb;
    struct event vsync; /* bit 0: frame done */
    struct gpio_desc *enable, *backlight;
    struct clk *pix, *pix_gate;
    struct device_node *timings; /* the panel's display-timings */
    struct device_node *mode;    /* the entry in use */
    uint8_t *buf[NBUF];
    uint32_t buf_size;
    volatile uint32_t frames;
    volatile int shown, pending;
    struct fb_info info;
};

static void lcdif_irq(int irq, void *ctx)
{
    (void)irq;
    struct lcdif *l = ctx;
    uint32_t st = ELCDIF_GetInterruptStatus(l->base);
    ELCDIF_ClearInterruptStatus(l->base, st);
    if (st & kELCDIF_CurFrameDone)
    {
        l->frames++;
        /* the controller has taken the next buffer for the coming frame: ask it which one,
         * a switch requested right at the frame end may have missed this one */
        uint32_t cur = l->base->CUR_BUF;
        l->shown = cur == (uint32_t)(uintptr_t)l->buf[1] ? 1 : 0;
        if (l->pending == l->shown)
            l->pending = -1;
        event_set(&l->vsync, 1u);
        if (l->fb >= 0)
            fb_vsync(l->fb, (unsigned)l->shown);
    }
}

static int lcdif_show(void *ctx, unsigned index)
{
    struct lcdif *l = ctx;
    if (index >= NBUF)
        return -EINVAL;
    ELCDIF_SetNextBufferAddr(l->base, (uint32_t)(uintptr_t)l->buf[index]);
    l->pending = (int)index;
    return 0;
}

static int lcdif_wait_vsync(void *ctx, uint32_t timeout)
{
    struct lcdif *l = ctx;
    event_clear(&l->vsync, 1u);
    return event_wait(&l->vsync, 1u, EVENT_ANY, timeout) < 0 ? -ETIMEDOUT : 0;
}

static int lcdif_blank(void *ctx, int blank)
{
    struct lcdif *l = ctx;
    if (l->backlight)
        gpiod_set_value(l->backlight, !blank);
    return 0;
}

static uint32_t prop(const struct device_node *np, const char *name, uint32_t def)
{
    uint32_t v = def;
    of_property_read_u32(np, name, &v);
    return v;
}

/* The panel mode of @w x @h; 0 x 0: the default one (native-mode, else the first) */
static struct device_node *find_mode(struct lcdif *l, uint32_t w, uint32_t h)
{
    if (!w && !h)
    {
        struct device_node *native = of_parse_phandle(l->timings, "native-mode", 0);
        return native ? native : l->timings->child;
    }
    for (struct device_node *t = l->timings->child; t; t = t->sibling)
        if (prop(t, "hactive", 0) == w && prop(t, "vactive", 0) == h)
            return t;
    return NULL;
}

static void free_buffers(void *arg)
{
    struct lcdif *l = arg;
    for (int i = 0; i < NBUF; i++)
    {
        kfree(l->buf[i]);
        l->buf[i] = NULL;
    }
}

/* NBUF black buffers of @size bytes (all or none) */
static int alloc_buffers(struct lcdif *l, uint32_t size)
{
    for (int i = 0; i < NBUF; i++)
    {
        l->buf[i] = kmalloc_aligned(size, 64, KM_NOCACHE);
        if (!l->buf[i])
        {
            free_buffers(l);
            return -ENOMEM;
        }
        memset(l->buf[i], 0, size);
    }
    l->buf_size = size;
    return 0;
}

/* Software reset of the stopped controller, not waiting forever: SFTRST, then the block sets
 * CLKGATE when it is done (ELCDIF_Reset() of the SDK waits for that without end - with the
 * pixel clock changed under the running block it never came, and the kernel monitor hung) */
static void soft_reset(struct lcdif *l)
{
    LCDIF_Type *base = l->base;
    base->CTRL_CLR = LCDIF_CTRL_CLKGATE_MASK;
    base->CTRL_SET = LCDIF_CTRL_SFTRST_MASK;
    for (int ms = 0; ms < 5 && !(base->CTRL & LCDIF_CTRL_CLKGATE_MASK); ms++)
        task_sleep_ms(1);
    if (!(base->CTRL & LCDIF_CTRL_CLKGATE_MASK))
        dev_warn(l->dev, "software reset not acknowledged\n");
    base->CTRL_CLR = LCDIF_CTRL_CLKGATE_MASK;
    base->CTRL_CLR = LCDIF_CTRL_SFTRST_MASK;
}

/* The RGB (DOTCLK) mode registers for @c: ELCDIF_RgbModeInit() of the SDK without its software
 * reset (soft_reset() comes first), for a mode change; at probe the block is still in its
 * power-on reset, which the SDK only releases. */
static void rgb_mode_regs(LCDIF_Type *base, const elcdif_rgb_mode_config_t *c)
{
    /* RGB565: 16-bit words, all four bytes valid */
    base->CTRL = LCDIF_CTRL_WORD_LENGTH(0U) | (uint32_t)c->dataBus | LCDIF_CTRL_DOTCLK_MODE_MASK |
                 LCDIF_CTRL_BYPASS_COUNT_MASK | LCDIF_CTRL_MASTER_MASK;
    base->CTRL1 = LCDIF_CTRL1_BYTE_PACKING_FORMAT(0x0FU);
    base->CTRL2 = (base->CTRL2 & ~LCDIF_CTRL2_OUTSTANDING_REQS_MASK) | LCDIF_CTRL2_OUTSTANDING_REQS(4);
    base->TRANSFER_COUNT = ((uint32_t)c->panelHeight << LCDIF_TRANSFER_COUNT_V_COUNT_SHIFT) |
                           ((uint32_t)c->panelWidth << LCDIF_TRANSFER_COUNT_H_COUNT_SHIFT);
    base->VDCTRL0 = LCDIF_VDCTRL0_ENABLE_PRESENT_MASK | LCDIF_VDCTRL0_VSYNC_PERIOD_UNIT_MASK |
                    LCDIF_VDCTRL0_VSYNC_PULSE_WIDTH_UNIT_MASK | c->polarityFlags | c->vsw;
    base->VDCTRL1 = (uint32_t)c->vsw + c->panelHeight + c->vfp + c->vbp;
    base->VDCTRL2 = ((uint32_t)c->hsw << LCDIF_VDCTRL2_HSYNC_PULSE_WIDTH_SHIFT) |
                    (((uint32_t)c->hfp + c->hbp + c->panelWidth + c->hsw) << LCDIF_VDCTRL2_HSYNC_PERIOD_SHIFT);
    base->VDCTRL3 = (((uint32_t)c->hbp + c->hsw) << LCDIF_VDCTRL3_HORIZONTAL_WAIT_CNT_SHIFT) |
                    (((uint32_t)c->vbp + c->vsw) << LCDIF_VDCTRL3_VERTICAL_WAIT_CNT_SHIFT);
    base->VDCTRL4 = LCDIF_VDCTRL4_SYNC_SIGNALS_ON_MASK | ((uint32_t)c->panelWidth << LCDIF_VDCTRL4_DOTCLK_H_VALID_DATA_CNT_SHIFT);
    base->CUR_BUF = c->bufferAddr;
    base->NEXT_BUF = c->bufferAddr;
}

/* Program the pixel clock and the controller for mode @t (buffers allocated for it) and start
 * it - @first: the controller comes out of its power-on reset; the frame buffer description
 * follows */
static void start_mode(struct lcdif *l, struct device_node *t, int first)
{
    uint32_t w = prop(t, "hactive", 480), h = prop(t, "vactive", 272);
    /* the video PLL changes (bypassed, relocked) with the pixel clock gated off: a stopped
     * controller that got the glitches did not start again in four of five changes */
    if (!first)
        clk_disable(l->pix_gate);
    clk_set_rate(l->pix, prop(t, "clock-frequency", 9300000));
    if (!first)
        clk_enable(l->pix_gate);
    uint32_t real_clk = clk_get_rate(l->pix);

    uint32_t pol = 0;
    pol |= prop(t, "de-active", 1) ? kELCDIF_DataEnableActiveHigh : kELCDIF_DataEnableActiveLow;
    pol |= prop(t, "vsync-active", 0) ? kELCDIF_VsyncActiveHigh : kELCDIF_VsyncActiveLow;
    pol |= prop(t, "hsync-active", 0) ? kELCDIF_HsyncActiveHigh : kELCDIF_HsyncActiveLow;
    pol |= prop(t, "pixelclk-active", 1) ? kELCDIF_DriveDataOnRisingClkEdge : kELCDIF_DriveDataOnFallingClkEdge;
    elcdif_rgb_mode_config_t cfg = {
        .panelWidth = (uint16_t)w,
        .panelHeight = (uint16_t)h,
        .hsw = (uint8_t)prop(t, "hsync-len", 41),
        .hfp = (uint8_t)prop(t, "hfront-porch", 4),
        .hbp = (uint8_t)prop(t, "hback-porch", 8),
        .vsw = (uint8_t)prop(t, "vsync-len", 10),
        .vfp = (uint8_t)prop(t, "vfront-porch", 4),
        .vbp = (uint8_t)prop(t, "vback-porch", 2),
        .polarityFlags = pol,
        .bufferAddr = (uint32_t)(uintptr_t)l->buf[0],
        .pixelFormat = kELCDIF_PixelFormatRGB565,
        .dataBus = kELCDIF_DataBus16Bit,
    };
    l->shown = 0;
    l->pending = -1;
    if (first)
    {
        ELCDIF_RgbModeInit(l->base, &cfg);
    }
    else
    {
        soft_reset(l);
        rgb_mode_regs(l->base, &cfg);
    }
    ELCDIF_EnableInterrupts(l->base, kELCDIF_CurFrameDoneInterruptEnable);
    ELCDIF_RgbModeStart(l->base);
    l->mode = t;

    uint32_t htotal = w + cfg.hsw + cfg.hfp + cfg.hbp, vtotal = h + cfg.vsw + cfg.vfp + cfg.vbp;
    memset(&l->info, 0, sizeof(l->info));
    l->info.width = (uint16_t)w;
    l->info.height = (uint16_t)h;
    l->info.stride = w * 2u;
    l->info.format = FB_FMT_RGB565;
    l->info.bpp = 16;
    l->info.nbuffers = NBUF;
    l->info.buffer_size = l->buf_size;
    for (int i = 0; i < NBUF; i++)
        l->info.buffer[i] = (uintptr_t)l->buf[i];
    l->info.refresh_mhz = (uint32_t)((uint64_t)real_clk * 1000u / (htotal * vtotal));
    dev_info(l->dev, "%lux%lu RGB565, pixel clock %lu Hz, %lu.%03lu Hz refresh\n", (unsigned long)w,
             (unsigned long)h, (unsigned long)real_clk, (unsigned long)(l->info.refresh_mhz / 1000u),
             (unsigned long)(l->info.refresh_mhz % 1000u));
}

/* Stop scanning out: the controller finishes its frame first (at most one, 34 ms at 30 Hz),
 * then the buffers are free. As ELCDIF_RgbModeStop(), but not waiting forever. */
static void stop(struct lcdif *l)
{
    ELCDIF_DisableInterrupts(l->base, kELCDIF_CurFrameDoneInterruptEnable);
    l->base->CTRL_CLR = LCDIF_CTRL_DOTCLK_MODE_MASK;
    for (int ms = 0; ms < 50 && (l->base->CTRL & (LCDIF_CTRL_DOTCLK_MODE_MASK | LCDIF_CTRL_RUN_MASK)); ms++)
        task_sleep_ms(1);
    if (l->base->CTRL & LCDIF_CTRL_RUN_MASK)
    {
        dev_warn(l->dev, "the controller did not finish its frame: stopped\n");
        l->base->CTRL_CLR = LCDIF_CTRL_RUN_MASK;
    }
}

static int lcdif_set_size(void *ctx, unsigned width, unsigned height, struct fb_info *info)
{
    struct lcdif *l = ctx;
    struct device_node *t = find_mode(l, width, height);
    if (!t)
        return -ENOENT;
    if (t != l->mode)
    {
        struct device_node *old = l->mode;
        uint32_t old_size = l->buf_size;
        stop(l);
        free_buffers(l);
        int r = alloc_buffers(l, width * height * 2u);
        if (r)
        {
            /* (the old buffers were just freed: they fit again) */
            if (alloc_buffers(l, old_size) == 0)
                start_mode(l, old, 0);
            return r;
        }
        start_mode(l, t, 0);
    }
    *info = l->info;
    return 0;
}

static const struct fb_ops lcdif_fb_ops = {lcdif_show, lcdif_wait_vsync, lcdif_blank, lcdif_set_size};

static int lcdif_probe(struct device *dev)
{
    struct lcdif *l = devm_kzalloc(dev, sizeof(*l), 0);
    if (!l)
        return -ENOMEM;
    l->dev = dev;
    l->base = device_map(dev, 0);
    l->pending = -1;
    l->fb = -1; /* no frame notifications before fb_register() */
    event_init(&l->vsync);

    struct device_node *panel = of_parse_phandle(dev->of_node, "display", 0);
    l->timings = panel ? of_get_child_by_name(panel, "display-timings") : NULL;
    struct device_node *t = l->timings ? find_mode(l, 0, 0) : NULL;
    if (!t)
    {
        dev_err(dev, "no panel timings\n");
        return -EINVAL;
    }

    struct clk *axi;
    int r = devm_clk_get_enabled(dev, "axi", &axi);
    if (!r)
        r = devm_clk_get_enabled(dev, "pix-gate", &l->pix_gate);
    if (!r)
        r = devm_clk_get(dev, "pix", &l->pix);
    if (r)
        return r;
    r = gpiod_get_index(dev, panel, "enable-gpios", 0, GPIOD_OUT_LOW, &l->enable);
    if (r == -EPROBE_DEFER)
        return r;
    if (r)
        l->enable = NULL;
    r = gpiod_get_index(dev, panel, "backlight-gpios", 0, GPIOD_OUT_LOW, &l->backlight);
    if (r == -EPROBE_DEFER)
        return r;
    if (r)
        l->backlight = NULL;
    l->irq = device_get_irq(dev, 0);
    if (l->irq < 0)
        return l->irq;

    r = alloc_buffers(l, prop(t, "hactive", 480) * prop(t, "vactive", 272) * 2u);
    if (r)
        return r;
    devm_add_action(dev, free_buffers, l);

    /* panel reset/enable (LCD_DISP) */
    if (l->enable)
    {
        gpiod_set_value(l->enable, 0);
        task_sleep_ms(5);
        gpiod_set_value(l->enable, 1);
    }

    r = irq_request(l->irq, lcdif_irq, l, 5, dev->name);
    if (r)
        return r;
    start_mode(l, t, 1);
    if (l->backlight)
        gpiod_set_value(l->backlight, 1);

    dev_set_drvdata(dev, l);
    l->fb = fb_register(&l->info, &lcdif_fb_ops, l);
    if (l->fb < 0)
    {
        stop(l);
        irq_free(l->irq);
        return l->fb;
    }
    return 0;
}

static void lcdif_remove(struct device *dev)
{
    struct lcdif *l = dev_get_drvdata(dev);
    fb_unregister(l->fb);
    if (l->backlight)
        gpiod_set_value(l->backlight, 0);
    stop(l);
    irq_free(l->irq);
}

static const struct of_device_id lcdif_ids[] = {
    {"fsl,imxrt1050-lcdif", NULL},
    {NULL, NULL},
};

static struct driver lcdif_driver = {
    .name = "lcdif-imxrt",
    .of_match_table = lcdif_ids,
    .probe = lcdif_probe,
    .remove = lcdif_remove,
};

static int init(void)
{
    return driver_register(&lcdif_driver);
}

static void fini(void)
{
    driver_unregister(&lcdif_driver);
}

MODULE("lcdif-imxrt", "i.MX RT eLCDIF display controller", init, fini);
