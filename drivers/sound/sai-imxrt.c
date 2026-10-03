/*
 * sai-imxrt.ko - sound output: an SAI of the i.MX RT sends I2S to the board's audio codec,
 * /dev/audio for programs (crtos/audio.h).
 *
 * Two drivers in one module:
 *   - "wlf,wm8960": the Cirrus Logic (Wolfson) WM8960 codec, an I2C device (LPI2C1, 0x1a on
 *     the EVKB). Write-only registers of 9 bits; set up for playback only: the DAC through
 *     the output mixers to the headphone jack and the speaker outputs. The codec is the I2S
 *     slave and takes its system clock from the SAI's master clock pin.
 *   - "fsl,imxrt1050-sai": the SAI transmitter, I2S master (bit clock and frame sync from its
 *     12.288 / 11.2896 MHz master clock, the clock root SAI1_ROOT of clk-imxrt), 16-bit
 *     stereo. Its node points at the codec: audio-codec = <&codec>.
 *
 * Samples go through a ring of 4096 frames into the 32-word FIFO of the transmitter, which
 * its "FIFO request" interrupt refills: no DMA, 16 words per interrupt (6000 per second at
 * 48 kHz, each a few hundred cycles). When the ring runs dry the interrupt sends silence; a
 * moment after the device was closed and all was played the transmitter stops (no
 * interrupts while nothing plays).
 */
#include <string.h>
#include <crtos/audio.h>
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/i2c.h>
#include <crtos/irq.h>
#include <crtos/module.h>
#include <crtos/of.h>
#include <crtos/poll.h>
#include <crtos/printk.h>
#include <crtos/sync.h>
#include <crtos/vfs.h>
#include "fsl_sai.h"

#define RING 4096u /* frames (left | right << 16), a power of 2 */
#define FIFO_WORDS 32u
#define WATERMARK 16u /* words left when the interrupt comes */
#define EV_SPACE 0x1u
#define EV_EMPTY 0x2u
#define MCLK_48K 12288000u
#define MCLK_44K1 11289600u

/* ---- the WM8960 codec --------------------------------------------------------------------- */

enum
{
    WM_LOUT1 = 0x02,
    WM_ROUT1 = 0x03,
    WM_CLOCK1 = 0x04,
    WM_DACCTL1 = 0x05,
    WM_IFACE1 = 0x07,
    WM_IFACE2 = 0x09,
    WM_LDAC = 0x0a,
    WM_RDAC = 0x0b,
    WM_RESET = 0x0f,
    WM_ADDCTL1 = 0x17,
    WM_POWER1 = 0x19,
    WM_POWER2 = 0x1a,
    WM_LOUTMIX = 0x22,
    WM_ROUTMIX = 0x25,
    WM_LOUT2 = 0x28,
    WM_ROUT2 = 0x29,
    WM_BYPASS1 = 0x2d,
    WM_BYPASS2 = 0x2e,
    WM_POWER3 = 0x2f,
    WM_ADDCTL4 = 0x30,
    WM_CLASSD1 = 0x31,
};

struct wm8960
{
    struct device *dev;
    struct i2c_client *client;
    uint32_t volume; /* 0..100 */
};

static struct wm8960 *s_codec; /* the one on the board */

static int wm_write(struct wm8960 *c, uint8_t reg, uint16_t val)
{
    uint8_t b[2] = {(uint8_t)((reg << 1) | ((val >> 8) & 1u)), (uint8_t)val};
    return i2c_write(c->client, b, 2) < 0 ? -EIO : 0;
}

/* headphones and speakers: 0x30 and below is off, 0x79 0 dB, 0x7f +6 dB; 1 dB steps */
static int wm_set_volume(struct wm8960 *c, uint32_t volume)
{
    if (volume > 100)
        volume = 100;
    uint16_t v = volume ? (uint16_t)(0x30 + (0x7f - 0x30) * volume / 100) : 0;
    int r = 0;
    /* bit 8 (volume update) latches both sides; bit 7: change at a zero crossing */
    r |= wm_write(c, WM_LOUT1, 0x080 | v);
    r |= wm_write(c, WM_ROUT1, 0x180 | v);
    r |= wm_write(c, WM_LOUT2, 0x080 | v);
    r |= wm_write(c, WM_ROUT2, 0x180 | v);
    if (!r)
        c->volume = volume;
    return r ? -EIO : 0;
}

/* The DAC's sample rate: SYSCLK (= MCLK) / 256 / DACDIV */
static int wm_set_rate(struct wm8960 *c, uint32_t mclk, uint32_t rate)
{
    static const struct
    {
        uint16_t tenths;
        uint16_t code;
    } DIV[] = {
        {10, 0},
        {15, 1},
        {20, 2},
        {30, 3},
        {40, 4},
        {55, 5},
        {60, 6},
    };
    uint32_t tenths = (uint32_t)((uint64_t)mclk * 10u / 256u / rate);
    for (unsigned i = 0; i < sizeof(DIV) / sizeof(DIV[0]); i++)
        if (DIV[i].tenths == tenths)
            return wm_write(c, WM_CLOCK1, (uint16_t)((DIV[i].code << 6) | (DIV[i].code << 3)));
    return -EINVAL;
}

static int wm_setup(struct wm8960 *c)
{
    static const struct
    {
        uint8_t reg;
        uint16_t val;
    } INIT[] = {
        {WM_RESET, 0x000},
        {WM_POWER1, 0x0c0},  /* VMID 2 x 50 kOhm, VREF */
        {WM_POWER2, 0x1f8},  /* DAC left/right, headphone out 1, speakers */
        {WM_POWER3, 0x00c},  /* output mixers */
        {WM_IFACE2, 0x040},  /* one LRCLK for the DAC and the ADC */
        {WM_IFACE1, 0x002},  /* I2S, 16 bits, slave */
        {WM_LOUTMIX, 0x100}, /* left DAC -> left output mixer */
        {WM_ROUTMIX, 0x100},
        {WM_BYPASS1, 0x000},
        {WM_BYPASS2, 0x000},
        {WM_ADDCTL1, 0x0c0},
        {WM_ADDCTL4, 0x040},
        {WM_LDAC, 0x0ff}, /* digital volume 0 dB (update with the right one) */
        {WM_RDAC, 0x1ff},
        {WM_CLASSD1, 0x0f7}, /* both class D speaker outputs */
        {WM_DACCTL1, 0x000}, /* DAC not muted */
    };
    for (unsigned i = 0; i < sizeof(INIT) / sizeof(INIT[0]); i++)
        if (wm_write(c, INIT[i].reg, INIT[i].val))
            return -EIO;
    return wm_set_volume(c, c->volume);
}

static int wm_probe(struct device *dev)
{
    struct wm8960 *c = devm_kzalloc(dev, sizeof(*c), 0);
    if (!c)
        return -ENOMEM;
    c->dev = dev;
    c->client = i2c_client_get(dev);
    c->volume = 80;
    if (!c->client)
        return -ENODEV;
    int r = wm_setup(c);
    if (r)
    {
        dev_err(dev, "no answer at 0x%02x\n", c->client->addr);
        return r;
    }
    dev_set_drvdata(dev, c);
    s_codec = c;
    dev_info(dev, "WM8960 at 0x%02x: playback to the headphones and the speakers\n", c->client->addr);
    return 0;
}

static void wm_remove(struct device *dev)
{
    struct wm8960 *c = dev_get_drvdata(dev);
    wm_write(c, WM_DACCTL1, 0x008); /* mute */
    wm_write(c, WM_POWER2, 0x000);
    wm_write(c, WM_POWER1, 0x000);
    if (s_codec == c)
        s_codec = NULL;
}

static const struct of_device_id wm_ids[] = {
    {"wlf,wm8960", NULL},
    {NULL, NULL},
};

static struct driver wm_driver = {
    .name = "wm8960",
    .of_match_table = wm_ids,
    .probe = wm_probe,
    .remove = wm_remove,
};

/* ---- the SAI transmitter ------------------------------------------------------------------- */

struct sai
{
    struct device *dev;
    I2S_Type *base;
    int irq;
    struct clk *mclk;
    struct wm8960 *codec;
    uint32_t mclk_hz, rate, channels;
    uint32_t ring[RING];
    volatile uint32_t head, tail; /* writer / interrupt */
    volatile bool running;        /* transmitter on */
    volatile bool open;
    volatile bool want_space, want_empty;
    bool dry;      /* the ring ran out while playing (one underrun) */
    uint32_t idle; /* frames of silence sent since the device was closed */
    struct event ev;
    struct poll_head ph;
    struct mutex lock; /* configuration */
    uint32_t played, underruns, fifo_errors;
};

static uint32_t ring_count(const struct sai *s)
{
    return s->head - s->tail;
}

static uint32_t fifo_count(I2S_Type *b)
{
    uint32_t tfr = b->TFR[0];
    uint32_t wfp = (tfr & I2S_TFR_WFP_MASK) >> I2S_TFR_WFP_SHIFT;
    uint32_t rfp = (tfr & I2S_TFR_RFP_MASK) >> I2S_TFR_RFP_SHIFT;
    return (wfp - rfp) & (2u * FIFO_WORDS - 1u); /* pointers with a wrap bit */
}

/* Tops the FIFO up with whole frames from the ring, or silence (interrupt or irq_lock) */
static void fill_fifo(struct sai *s)
{
    I2S_Type *b = s->base;
    uint32_t frames = (FIFO_WORDS - fifo_count(b)) / 2u;
    uint32_t taken = 0;
    while (frames--)
    {
        uint32_t v = 0;
        if (s->head != s->tail)
        {
            v = s->ring[s->tail & (RING - 1u)];
            s->tail++;
            taken++;
            s->dry = false;
        }
        else
        {
            if (!s->dry && s->open)
                s->underruns++;
            s->dry = true;
            if (!s->open)
                s->idle++;
        }
        b->TDR[0] = v & 0xffffu;
        b->TDR[0] = v >> 16;
    }
    s->played += taken;
    if (taken)
    {
        if (s->want_space && ring_count(s) <= RING * 3u / 4u)
        {
            s->want_space = false;
            event_set(&s->ev, EV_SPACE);
            poll_notify(&s->ph);
        }
        if (s->want_empty && s->head == s->tail)
        {
            s->want_empty = false;
            event_set(&s->ev, EV_EMPTY);
        }
    }
}

static void stop_locked(struct sai *s)
{
    I2S_Type *b = s->base;
    b->TCSR &= ~(I2S_TCSR_FRIE_MASK | I2S_TCSR_FEIE_MASK);
    SAI_TxEnable(b, false);
    b->TCSR |= I2S_TCSR_FR_MASK; /* empty FIFO: the next start begins with a left sample */
    s->running = false;
}

static void sai_irq(int irq, void *ctx)
{
    struct sai *s = ctx;
    I2S_Type *b = s->base;
    (void)irq;
    uint32_t csr = b->TCSR;
    if (csr & I2S_TCSR_FEF_MASK)
    {
        /* the FIFO ran empty (this interrupt came too late): clear it, start again at the
         * next frame with a left sample */
        s->fifo_errors++;
        b->TCSR = (csr & 0xffe3ffffu) | I2S_TCSR_FEF_MASK | I2S_TCSR_FR_MASK;
    }
    fill_fifo(s);
    /* closed and played out: 100 ms of silence, then the transmitter rests */
    if (!s->open && s->head == s->tail && s->idle >= s->rate / 10u)
    {
        stop_locked(s);
        event_set(&s->ev, EV_EMPTY | EV_SPACE);
    }
}

/* starts the transmitter (a full FIFO first) */
static void start(struct sai *s)
{
    uint32_t key = irq_lock();
    if (!s->running)
    {
        I2S_Type *b = s->base;
        s->idle = 0;
        s->dry = false;
        fill_fifo(s);
        SAI_TxEnable(b, true);
        b->TCSR |= I2S_TCSR_FRIE_MASK | I2S_TCSR_FEIE_MASK;
        s->running = true;
    }
    irq_unlock(key);
}

static void flush(struct sai *s)
{
    uint32_t key = irq_lock();
    s->head = s->tail;
    irq_unlock(key);
}

static int set_rate(struct sai *s, uint32_t rate)
{
    static const uint32_t RATES[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000};
    bool ok = false;
    for (unsigned i = 0; i < sizeof(RATES) / sizeof(RATES[0]); i++)
        ok |= RATES[i] == rate;
    if (!ok)
        return -EINVAL;
    uint32_t mclk = rate % 11025u == 0 ? MCLK_44K1 : MCLK_48K;
    uint32_t key = irq_lock();
    if (s->running)
        stop_locked(s);
    s->head = s->tail;
    irq_unlock(key);
    if (mclk != s->mclk_hz)
    {
        int r = clk_set_rate(s->mclk, mclk);
        if (r)
            return r;
        s->mclk_hz = clk_get_rate(s->mclk);
    }
    SAI_TxSetBitClockRate(s->base, s->mclk_hz, rate, 16, 2);
    int r = wm_set_rate(s->codec, s->mclk_hz, rate);
    if (r)
        return r;
    s->rate = rate;
    return 0;
}

/* ---- /dev/audio ---------------------------------------------------------------------------- */

/* opened read-only: status and volume, next to the program that plays */
static bool observer(const struct file *f)
{
    return (f->flags & VFS_O_ACCMODE) == VFS_O_RDONLY;
}

static int audio_open(struct file *f)
{
    struct sai *s = f->dev;
    if (observer(f))
        return 0;
    mutex_lock(&s->lock, WAIT_FOREVER);
    int r = 0;
    if (s->open)
    {
        r = -EBUSY;
    }
    else
    {
        s->channels = 2;
        if (s->rate != 48000)
            r = set_rate(s, 48000);
        if (!r)
            s->open = true;
    }
    mutex_unlock(&s->lock);
    return r;
}

static int audio_close(struct file *f)
{
    struct sai *s = f->dev;
    if (!observer(f))
        s->open = false; /* the interrupt stops the transmitter once all was played */
    return 0;
}

static int audio_write(struct file *f, const void *buf, size_t len)
{
    struct sai *s = f->dev;
    const int16_t *p = buf;
    if (observer(f))
        return -EBADF;
    size_t frames = len / (2u * s->channels), done = 0;
    while (done < frames)
    {
        uint32_t room = RING - ring_count(s);
        uint32_t n = frames - done < room ? (uint32_t)(frames - done) : room;
        uint32_t h = s->head;
        if (s->channels == 1)
        {
            for (uint32_t i = 0; i < n; i++)
            {
                uint16_t v = (uint16_t)p[done + i];
                s->ring[(h + i) & (RING - 1u)] = v | (uint32_t)v << 16;
            }
        }
        else
        {
            for (uint32_t i = 0; i < n; i++)
            {
                const int16_t *q = p + 2u * (done + i);
                s->ring[(h + i) & (RING - 1u)] = (uint16_t)q[0] | (uint32_t)(uint16_t)q[1] << 16;
            }
        }
        s->head = h + n; /* published after the samples */
        done += n;
        if (n)
            start(s);
        if (done == frames)
            break;
        if (f->flags & VFS_O_NONBLOCK)
            break;
        uint32_t key = irq_lock();
        bool full = ring_count(s) == RING && s->running;
        if (full)
        {
            event_clear(&s->ev, EV_SPACE);
            s->want_space = true;
        }
        irq_unlock(key);
        if (full)
        {
            int32_t r = event_wait(&s->ev, EV_SPACE, EVENT_ANY, WAIT_FOREVER);
            if (r < 0)
                return done ? (int)(done * 2u * s->channels) : r;
        }
    }
    if (!done && frames)
        return -EAGAIN;
    return (int)(done * 2u * s->channels);
}

static int audio_ioctl(struct file *f, unsigned cmd, void *arg)
{
    struct sai *s = f->dev;
    uint32_t v = (uint32_t)(uintptr_t)arg;
    int r = 0;
    if (observer(f) && _IOC_DIR(cmd) != _IOC_READ && cmd != AUDIO_IOC_SET_VOLUME)
        return -EBADF;
    switch (cmd)
    {
    case AUDIO_IOC_SET_RATE:
        mutex_lock(&s->lock, WAIT_FOREVER);
        r = set_rate(s, v);
        mutex_unlock(&s->lock);
        return r;
    case AUDIO_IOC_GET_RATE:
        *(uint32_t *)arg = s->rate;
        return 0;
    case AUDIO_IOC_SET_CHANNELS:
        if (v != 1 && v != 2)
            return -EINVAL;
        s->channels = v;
        return 0;
    case AUDIO_IOC_GET_QUEUED:
    {
        uint32_t key = irq_lock();
        uint32_t q = ring_count(s) + (s->running ? fifo_count(s->base) / 2u : 0);
        irq_unlock(key);
        *(uint32_t *)arg = q;
        return 0;
    }
    case AUDIO_IOC_GET_SPACE:
        *(uint32_t *)arg = RING - ring_count(s);
        return 0;
    case AUDIO_IOC_SET_VOLUME:
        mutex_lock(&s->lock, WAIT_FOREVER);
        r = wm_set_volume(s->codec, v);
        mutex_unlock(&s->lock);
        return r;
    case AUDIO_IOC_GET_VOLUME:
        *(uint32_t *)arg = s->codec->volume;
        return 0;
    case AUDIO_IOC_DRAIN:
        for (;;)
        {
            uint32_t key = irq_lock();
            bool empty = s->head == s->tail || !s->running;
            if (!empty)
            {
                event_clear(&s->ev, EV_EMPTY);
                s->want_empty = true;
            }
            irq_unlock(key);
            if (empty)
                return 0;
            int32_t w = event_wait(&s->ev, EV_EMPTY, EVENT_ANY, WAIT_FOREVER);
            if (w < 0)
                return w;
        }
    case AUDIO_IOC_FLUSH:
        flush(s);
        return 0;
    case AUDIO_IOC_GET_STATS:
    {
        struct audio_stats *st = arg;
        st->rate = s->rate;
        st->buffer = RING;
        st->played = s->played;
        st->underruns = s->underruns;
        st->fifo_errors = s->fifo_errors;
        return 0;
    }
    default:
        return -ENOTTY;
    }
}

static int audio_poll(struct file *f, struct poll_entry *e)
{
    struct sai *s = f->dev;
    uint32_t key = irq_lock();
    int mask = ring_count(s) < RING ? POLLOUT : 0;
    if (!mask)
        s->want_space = true;
    poll_add(&s->ph, e);
    irq_unlock(key);
    return mask;
}

static int audio_fstat(struct file *f, struct vfs_stat *st)
{
    (void)f;
    memset(st, 0, sizeof(*st));
    st->mode = VFS_S_IFCHR;
    return 0;
}

static const struct file_ops audio_ops = {
    .open = audio_open,
    .write = audio_write,
    .ioctl = audio_ioctl,
    .fstat = audio_fstat,
    .close = audio_close,
    .poll = audio_poll,
};

static int sai_probe(struct device *dev)
{
    struct device_node *cnp = of_parse_phandle(dev->of_node, "audio-codec", 0);
    if (!cnp)
    {
        dev_err(dev, "no audio-codec\n");
        return -ENODEV;
    }
    if (!s_codec || s_codec->dev->of_node != cnp)
        return -EPROBE_DEFER; /* the codec's driver has not come yet */
    struct sai *s = devm_kzalloc(dev, sizeof(*s), 0);
    if (!s)
        return -ENOMEM;
    s->dev = dev;
    s->codec = s_codec;
    s->base = device_map(dev, 0);
    s->irq = device_get_irq(dev, 0);
    if (!s->base || s->irq < 0)
        return s->irq == -EPROBE_DEFER ? -EPROBE_DEFER : -ENODEV;
    struct clk *bus;
    int r = devm_clk_get_enabled(dev, "bus", &bus);
    if (!r)
        r = devm_clk_get(dev, "mclk", &s->mclk);
    if (!r)
        r = clk_set_rate(s->mclk, MCLK_48K);
    if (r)
        return r;
    s->mclk_hz = clk_get_rate(s->mclk);
    event_init(&s->ev);
    poll_head_init(&s->ph);
    mutex_init(&s->lock);

    /* the codec's system clock: the SAI1 master clock root on its MCLK pin */
    if (s->base == SAI1)
        IOMUXC_GPR->GPR1 = (IOMUXC_GPR->GPR1 & ~IOMUXC_GPR_GPR1_SAI1_MCLK1_SEL_MASK) |
                           IOMUXC_GPR_GPR1_SAI1_MCLK_DIR_MASK;
    SAI_Init(s->base);
    sai_transceiver_t cfg;
    SAI_GetClassicI2SConfig(&cfg, kSAI_WordWidth16bits, kSAI_Stereo, kSAI_Channel0Mask);
    cfg.syncMode = kSAI_ModeAsync;
    cfg.masterSlave = kSAI_Master;
    cfg.fifo.fifoWatermark = WATERMARK;
    SAI_TxSetConfig(s->base, &cfg);
    s->rate = 0;
    r = set_rate(s, 48000);
    if (r)
        return r;

    r = irq_request(s->irq, sai_irq, s, 3, dev->name);
    if (r)
        return r;
    dev_set_drvdata(dev, s);
    r = devfs_register("audio", &audio_ops, s);
    if (r)
    {
        irq_free(s->irq);
        return r;
    }
    dev_info(dev, "/dev/audio: I2S to %s, master clock %lu Hz\n", s->codec->dev->name, (unsigned long)s->mclk_hz);
    return 0;
}

static void sai_remove(struct device *dev)
{
    struct sai *s = dev_get_drvdata(dev);
    devfs_unregister("audio");
    uint32_t key = irq_lock();
    if (s->running)
        stop_locked(s);
    irq_unlock(key);
    irq_free(s->irq);
    SAI_Deinit(s->base);
}

static const struct of_device_id sai_ids[] = {
    {"fsl,imxrt1050-sai", NULL},
    {NULL, NULL},
};

static struct driver sai_driver = {
    .name = "sai-imxrt",
    .of_match_table = sai_ids,
    .probe = sai_probe,
    .remove = sai_remove,
};

static int init(void)
{
    int r = driver_register(&wm_driver);
    if (r)
        return r;
    r = driver_register(&sai_driver);
    if (r)
        driver_unregister(&wm_driver);
    return r;
}

static void fini(void)
{
    driver_unregister(&sai_driver);
    driver_unregister(&wm_driver);
}

MODULE("sai-imxrt", "sound: SAI (I2S) and the WM8960 codec, /dev/audio", init, fini);
