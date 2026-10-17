/*
 * ltdc-stm32.ko - STM32 LTDC parallel RGB display controller with its panel (the RK050HR18
 * 5" 800x480 of the STM32H7S78-DK).
 *
 * The panel (phandle "display") provides the enable (DISP) and backlight GPIOs and its modes:
 * the entries of display-timings, as for lcdif-imxrt (Linux polarity properties). The
 * driver starts in the default mode (display-timings/native-mode, else the first entry) and
 * changes to another when the fb framework asks (set_size), only before anybody uses the
 * display. Two RGB565 frame buffers in non-cacheable memory are published as /dev/fb0 and
 * shown by layer 1 over a black background.
 *
 * Buffer switch: fb_show() writes the layer's address and asks for a reload in the next
 * vertical blanking (SRCR.VBR, done by the controller after the last active line). The line
 * interrupt at line 0 (the start of the next VSYNC, after that reload) counts the frame and
 * reports the buffer on the screen: the new one once the reload request has cleared.
 * FIFO underruns (the frame buffer memory too slow) and bus errors are counted.
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
#include "stm32h7rsxx.h"

#define NBUF 2
#define PF_RGB565 2u
#define BLEND_CA 0x405u /* layer x constant alpha over the background */

struct ltdc
{
    struct device *dev;
    LTDC_TypeDef *r;
    LTDC_Layer_TypeDef *l1;
    int irq, irq_err;
    int fb;
    struct event vsync; /* bit 0: a frame started */
    struct gpio_desc *enable, *backlight;
    struct clk *pix;
    struct device_node *timings; /* the panel's display-timings */
    struct device_node *mode;    /* the entry in use */
    uint8_t *buf[NBUF];
    uint32_t buf_size;
    volatile uint32_t frames;
    volatile int shown, pending;
    uint32_t underruns, bus_errors;
    struct fb_info info;
};

static void ltdc_irq(int irq, void *ctx)
{
    (void)irq;
    struct ltdc *l = ctx;
    LTDC_TypeDef *r = l->r;
    uint32_t st = r->ISR & r->IER;
    /* (the error interrupts go off until the next frame: at most one report each) */
    if (st & LTDC_ISR_FUIF)
    {
        r->ICR = LTDC_ICR_CFUIF;
        r->IER &= ~LTDC_IER_FUIE;
        l->underruns++;
    }
    if (st & LTDC_ISR_TERRIF)
    {
        r->ICR = LTDC_ICR_CTERRIF;
        r->IER &= ~LTDC_IER_TERRIE;
        l->bus_errors++;
    }
    if (st & LTDC_ISR_LIF)
    {
        r->ICR = LTDC_ICR_CLIF;
        r->IER |= LTDC_IER_FUIE | LTDC_IER_TERRIE;
        l->frames++;
        if (l->pending >= 0 && !(r->SRCR & LTDC_SRCR_VBR))
        {
            l->shown = l->pending;
            l->pending = -1;
        }
        event_set(&l->vsync, 1u);
        if (l->fb >= 0)
            fb_vsync(l->fb, (unsigned)l->shown);
    }
}

static int ltdc_show(void *ctx, unsigned index)
{
    struct ltdc *l = ctx;
    if (index >= NBUF)
        return -EINVAL;
    uint32_t key = irq_lock();
    l->l1->CFBAR = (uint32_t)(uintptr_t)l->buf[index];
    l->r->SRCR = LTDC_SRCR_VBR;
    l->pending = (int)index;
    irq_unlock(key);
    return 0;
}

static int ltdc_wait_vsync(void *ctx, uint32_t timeout)
{
    struct ltdc *l = ctx;
    event_clear(&l->vsync, 1u);
    return event_wait(&l->vsync, 1u, EVENT_ANY, timeout) < 0 ? -ETIMEDOUT : 0;
}

static int ltdc_blank(void *ctx, int blank)
{
    struct ltdc *l = ctx;
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
static struct device_node *find_mode(struct ltdc *l, uint32_t w, uint32_t h)
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
    struct ltdc *l = arg;
    for (int i = 0; i < NBUF; i++)
    {
        kfree(l->buf[i]);
        l->buf[i] = NULL;
    }
}

/* NBUF black buffers of @size bytes (all or none) */
static int alloc_buffers(struct ltdc *l, uint32_t size)
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

/* Program the pixel clock, the timings and layer 1 for mode @t (buffers allocated for it)
 * and start the controller; the frame buffer description follows */
static void start_mode(struct ltdc *l, struct device_node *t)
{
    LTDC_TypeDef *r = l->r;
    uint32_t w = prop(t, "hactive", 800), h = prop(t, "vactive", 480);
    uint32_t hsw = prop(t, "hsync-len", 4), hbp = prop(t, "hback-porch", 8), hfp = prop(t, "hfront-porch", 8);
    uint32_t vsw = prop(t, "vsync-len", 4), vbp = prop(t, "vback-porch", 8), vfp = prop(t, "vfront-porch", 8);
    clk_set_rate(l->pix, prop(t, "clock-frequency", 25000000));
    uint32_t real_clk = clk_get_rate(l->pix);

    /* (Linux semantics: hsync/vsync-active 1 = high, de-active 1 = high, pixelclk-active 1 =
     * data driven on the rising edge; the GCR bits invert the default) */
    uint32_t gcr = 0;
    if (prop(t, "hsync-active", 0))
        gcr |= LTDC_GCR_HSPOL;
    if (prop(t, "vsync-active", 0))
        gcr |= LTDC_GCR_VSPOL;
    if (!prop(t, "de-active", 1))
        gcr |= LTDC_GCR_DEPOL;
    if (!prop(t, "pixelclk-active", 1))
        gcr |= LTDC_GCR_PCPOL;

    uint32_t ahbp = hsw + hbp - 1u, avbp = vsw + vbp - 1u;
    r->GCR = 0;
    r->SSCR = ((hsw - 1u) << 16) | (vsw - 1u);
    r->BPCR = (ahbp << 16) | avbp;
    r->AWCR = ((ahbp + w) << 16) | (avbp + h);
    r->TWCR = ((ahbp + w + hfp) << 16) | (avbp + h + vfp);
    r->BCCR = 0;

    LTDC_Layer_TypeDef *l1 = l->l1;
    l1->WHPCR = ((ahbp + w) << 16) | (ahbp + 1u);
    l1->WVPCR = ((avbp + h) << 16) | (avbp + 1u);
    l1->PFCR = PF_RGB565;
    l1->CACR = 255;
    l1->DCCR = 0;
    l1->BFCR = BLEND_CA;
    l1->CKCR = 0;
    l1->CFBAR = (uint32_t)(uintptr_t)l->buf[0];
    l1->CFBLR = ((w * 2u) << 16) | (w * 2u + 7u);
    l1->CFBLNR = h;
    l1->CR = LTDC_LxCR_LEN;
    r->SRCR = LTDC_SRCR_IMR;

    l->shown = 0;
    l->pending = -1;
    r->LIPCR = 0;
    r->ICR = LTDC_ICR_CLIF | LTDC_ICR_CFUIF | LTDC_ICR_CTERRIF | LTDC_ICR_CRRIF;
    r->IER = LTDC_IER_LIE | LTDC_IER_FUIE | LTDC_IER_TERRIE;
    r->GCR = gcr | LTDC_GCR_LTDCEN;
    l->mode = t;

    uint32_t htotal = w + hsw + hfp + hbp, vtotal = h + vsw + vfp + vbp;
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

/* Stop scanning out; the buffers are free after the frame in progress (at most one) */
static void stop(struct ltdc *l)
{
    l->r->IER = 0;
    l->r->GCR &= ~LTDC_GCR_LTDCEN;
    task_sleep_ms(25);
}

static int ltdc_set_size(void *ctx, unsigned width, unsigned height, struct fb_info *info)
{
    struct ltdc *l = ctx;
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
                start_mode(l, old);
            else
                memset(&l->info, 0, sizeof(l->info)); /* stopped, no buffers to describe */
            return r;
        }
        start_mode(l, t);
    }
    *info = l->info;
    return 0;
}

static const struct fb_ops ltdc_fb_ops = {ltdc_show, ltdc_wait_vsync, ltdc_blank, ltdc_set_size};

static int ltdc_probe(struct device *dev)
{
    struct ltdc *l = devm_kzalloc(dev, sizeof(*l), 0);
    if (!l)
        return -ENOMEM;
    l->dev = dev;
    l->r = device_map(dev, 0);
    if (!l->r)
        return -EINVAL;
    l->l1 = (LTDC_Layer_TypeDef *)((uintptr_t)l->r + (LTDC_Layer1_BASE - LTDC_BASE));
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

    struct clk *bus;
    int r = devm_clk_get_enabled(dev, "bus", &bus);
    if (!r)
        r = devm_clk_get_enabled(dev, "pix", &l->pix);
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
    l->irq_err = device_get_irq(dev, 1);
    if (l->irq < 0)
        return l->irq;

    r = alloc_buffers(l, prop(t, "hactive", 800) * prop(t, "vactive", 480) * 2u);
    if (r)
        return r;
    devm_add_action(dev, free_buffers, l);

    r = irq_request(l->irq, ltdc_irq, l, 5, dev->name);
    if (!r && l->irq_err >= 0)
    {
        r = irq_request(l->irq_err, ltdc_irq, l, 5, dev->name);
        if (r)
            irq_free(l->irq);
    }
    if (r)
        return r;
    start_mode(l, t);
    /* the panel on (DISP), a frame of black later the backlight */
    if (l->enable)
        gpiod_set_value(l->enable, 1);
    task_sleep_ms(40);
    if (l->backlight)
        gpiod_set_value(l->backlight, 1);

    dev_set_drvdata(dev, l);
    l->fb = fb_register(&l->info, &ltdc_fb_ops, l);
    if (l->fb < 0)
    {
        stop(l);
        irq_free(l->irq);
        if (l->irq_err >= 0)
            irq_free(l->irq_err);
        return l->fb;
    }
    return 0;
}

static void ltdc_remove(struct device *dev)
{
    struct ltdc *l = dev_get_drvdata(dev);
    fb_unregister(l->fb);
    if (l->backlight)
        gpiod_set_value(l->backlight, 0);
    if (l->enable)
        gpiod_set_value(l->enable, 0);
    stop(l);
    irq_free(l->irq);
    if (l->irq_err >= 0)
        irq_free(l->irq_err);
    if (l->underruns || l->bus_errors)
        dev_info(dev, "%lu FIFO underruns, %lu bus errors\n", (unsigned long)l->underruns,
                 (unsigned long)l->bus_errors);
}

static const struct of_device_id ltdc_ids[] = {
    {"st,stm32-ltdc", NULL},
    {NULL, NULL},
};

static struct driver ltdc_driver = {
    .name = "ltdc-stm32",
    .of_match_table = ltdc_ids,
    .probe = ltdc_probe,
    .remove = ltdc_remove,
};

static int init(void)
{
    return driver_register(&ltdc_driver);
}

static void fini(void)
{
    driver_unregister(&ltdc_driver);
}

MODULE("ltdc-stm32", "STM32 LTDC display controller", init, fini);
