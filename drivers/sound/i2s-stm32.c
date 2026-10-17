/*
 * i2s-stm32.ko - sound output of the STM32H7S78-DK: SPI6 in I2S mode sends to the board's
 * WM8904 codec, /dev/audio for programs (crtos/audio.h, the same device as on the EVKB).
 *
 * Two drivers in one module:
 *   - "wlf,wm8904": the Cirrus Logic (Wolfson) WM8904 codec, an I2C device (I2C1, 0x1a).
 *     16-bit registers (address byte, then the value, high byte first); playback only: the
 *     DACs to the headphone outputs, the codec the I2S slave with its system clock from the
 *     I2S master clock (256 fs). Its probe only checks the chip ID and resets it: the power-up
 *     (DC servo) needs that clock, so the I2S driver does it once the clock runs.
 *   - "st,stm32h7-i2s": SPI6 as I2S master transmitter (Philips, 16-bit stereo, MCK out), its
 *     kernel clock PLL3 Q (clk-stm32 STM32_CK_AUDIO): 256 fs x 2..12 (24.576 MHz for the
 *     48 kHz rates, 22.5792 MHz for the 44.1 kHz ones). Its node points at the codec:
 *     audio-codec = <&codec>.
 *
 * Samples go through a ring of 4096 frames into NPER periods of PERIOD frames in non-cacheable
 * memory, which a GPDMA1 channel sends to the transmitter round and round (a circular linked
 * list, one item per period). The DMA never stops while the module is loaded, so the codec
 * always has its clock: with nothing to play the periods hold silence and the channel's
 * interrupt is off. While something plays, the end of each period interrupts and refills that
 * period from the ring (silence when it ran dry); a moment after the device was closed and
 * all was played the interrupt goes off again.
 */
#include <stdbool.h>
#include <string.h>
#include <crtos/arch.h>
#include <crtos/audio.h>
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/i2c.h>
#include <crtos/irq.h>
#include <crtos/mm.h>
#include <crtos/module.h>
#include <crtos/of.h>
#include <crtos/poll.h>
#include <crtos/printk.h>
#include <crtos/sched.h>
#include <crtos/sync.h>
#include <crtos/vfs.h>
#include "stm32h7rsxx.h"

#define RING 4096u   /* frames (left | right << 16), a power of 2 */
#define PERIOD 256u  /* frames per DMA block: one interrupt each (5.3 ms at 48 kHz) */
#define NPER 4u      /* periods in the DMA buffer */
#define PERIOD_BYTES (PERIOD * 4u)
#define LLI_WORDS 3u /* a linked-list item: CBR1, CSAR, CLLR */
#define DMA_CFCR_ALL (DMA_CFCR_TCF | DMA_CFCR_HTF | DMA_CFCR_DTEF | DMA_CFCR_ULEF | DMA_CFCR_USEF | \
                      DMA_CFCR_SUSPF | DMA_CFCR_TOF)
#define SPI_IFCR_ALL (SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_UDRC | SPI_IFCR_OVRC | SPI_IFCR_CRCEC | \
                      SPI_IFCR_TIFREC | SPI_IFCR_MODFC | SPI_IFCR_SUSPC)
/* channel n of the GPDMA at @base */
#define GPDMA_CHANNEL(base, n)                                                          \
    ((DMA_Channel_TypeDef *)((uintptr_t)(base) + (GPDMA1_Channel0_BASE - GPDMA1_BASE) + \
                             (GPDMA1_Channel1_BASE - GPDMA1_Channel0_BASE) * (n)))
#define EV_SPACE 0x1u
#define EV_EMPTY 0x2u

/* ---- the WM8904 codec --------------------------------------------------------------------- */

enum
{
    WM_ID = 0x00, /* read: 0x8904; write: software reset */
    WM_BIAS0 = 0x04,
    WM_VMID0 = 0x05,
    WM_PWR0 = 0x0c,
    WM_PWR2 = 0x0e,
    WM_PWR6 = 0x12,
    WM_CLOCK1 = 0x15,
    WM_CLOCK2 = 0x16,
    WM_AIF1 = 0x19,
    WM_DAC1 = 0x21,
    WM_HPOUT_L = 0x39,
    WM_HPOUT_R = 0x3a,
    WM_DCS0 = 0x43,
    WM_DCS1 = 0x44,
    WM_DCS_RB0 = 0x4d,
    WM_HP0 = 0x5a,
    WM_CP0 = 0x62,
    WM_CLASSW0 = 0x68,
};

#define WM_DAC1_PLAY 0x0640u /* unmute ramp, mute rate, 128x oversampling */
#define WM_DAC1_MUTE 0x0648u

struct wm8904
{
    struct device *dev;
    struct i2c_client *client;
    uint32_t volume; /* 0..100 */
    bool powered;
};

static struct wm8904 *s_codec; /* the one on the board */

static int wm_write(struct wm8904 *c, uint8_t reg, uint16_t val)
{
    uint8_t b[3] = {reg, (uint8_t)(val >> 8), (uint8_t)val};
    return i2c_write(c->client, b, 3) < 0 ? -EIO : 0;
}

static int wm_read(struct wm8904 *c, uint8_t reg, uint16_t *val)
{
    uint8_t b[2];
    if (i2c_write_read(c->client, &reg, 1, b, 2) < 0)
        return -EIO;
    *val = (uint16_t)(b[0] << 8 | b[1]);
    return 0;
}

/* headphones: 0x00 -57 dB ... 0x39 0 dB ... 0x3f +6 dB in 1 dB steps; 1..100 -> 0x10..0x3f,
 * 0 mutes; the update bit of the right side latches both */
static int wm_set_volume(struct wm8904 *c, uint32_t volume)
{
    if (volume > 100)
        volume = 100;
    uint16_t v = volume ? (uint16_t)(0x10 + (0x3f - 0x10) * volume / 100) : 0x100;
    int r = wm_write(c, WM_HPOUT_L, v);
    r |= wm_write(c, WM_HPOUT_R, 0x080 | v);
    if (!r)
        c->volume = volume;
    return r ? -EIO : 0;
}

/* SYSCLK = MCLK = 256 fs; the DSP's rate class */
static int wm_set_rate(struct wm8904 *c, uint32_t rate)
{
    uint16_t code = rate >= 44100 ? 5 : rate >= 32000 ? 4 : rate >= 22050 ? 3 : rate >= 16000 ? 2 : rate >= 11025 ? 1 : 0;
    return wm_write(c, WM_CLOCK1, (uint16_t)(0x0c00 | code));
}

/* Playback through the headphone outputs (ST's BSP sequence without the inputs). The master
 * clock must run: the DC servo calibrates with it. */
static int wm_power_up(struct wm8904 *c, uint32_t rate)
{
    static const struct
    {
        uint8_t reg;
        uint16_t val;
        uint16_t delay_ms;
    } SEQ[] = {
        {WM_CLOCK2, 0x0004, 0},   /* system clock on */
        {WM_BIAS0, 0x0018, 0},    /* bias current select */
        {WM_VMID0, 0x0047, 100},  /* VMID buffer, fast start (2 x 5 kOhm) */
        {WM_VMID0, 0x0043, 0},    /* then normal (2 x 50 kOhm) */
        {WM_BIAS0, 0x0019, 0},    /* master bias on */
        {WM_PWR0, 0x0000, 0},     /* inputs off */
        {WM_PWR2, 0x0003, 0},     /* headphone PGAs */
        {WM_CLOCK2, 0x0006, 0},   /* + the DSP clock */
        {WM_PWR6, 0x000c, 0},     /* both DACs */
        {WM_DAC1, WM_DAC1_MUTE, 0},
        {WM_AIF1, 0x0002, 0},     /* I2S, 16 bits, slave */
    };
    for (unsigned i = 0; i < sizeof(SEQ) / sizeof(SEQ[0]); i++)
    {
        if (wm_write(c, SEQ[i].reg, SEQ[i].val))
            return -EIO;
        if (SEQ[i].delay_ms)
            task_sleep_ms(SEQ[i].delay_ms);
    }
    if (wm_set_rate(c, rate) || wm_set_volume(c, c->volume))
        return -EIO;
    /* charge pump, the headphone stages one after the other, DC offsets calibrated */
    int r = wm_write(c, WM_CP0, 0x0001);
    r |= wm_write(c, WM_HP0, 0x0011);
    r |= wm_write(c, WM_HP0, 0x0033);
    r |= wm_write(c, WM_DCS0, 0x0003);
    r |= wm_write(c, WM_DCS1, 0x0030);
    if (r)
        return -EIO;
    uint16_t rb = 0;
    for (int i = 0; i < 30 && (rb & 3u) != 3u; i++)
    {
        task_sleep_ms(10);
        if (wm_read(c, WM_DCS_RB0, &rb))
            return -EIO;
    }
    if ((rb & 3u) != 3u)
        dev_warn(c->dev, "DC servo did not finish (readback %04x)\n", rb);
    r = wm_write(c, WM_HP0, 0x0077);
    r |= wm_write(c, WM_HP0, 0x00ff); /* outputs on, shorts removed */
    r |= wm_write(c, WM_CLASSW0, 0x0001);
    r |= wm_write(c, WM_DAC1, WM_DAC1_PLAY);
    if (r)
        return -EIO;
    c->powered = true;
    return 0;
}

static void wm_power_down(struct wm8904 *c)
{
    if (!c->powered)
        return;
    wm_write(c, WM_DAC1, WM_DAC1_MUTE);
    wm_write(c, WM_HP0, 0x0077);
    wm_write(c, WM_HP0, 0x0000);
    wm_write(c, WM_DCS0, 0x0000);
    wm_write(c, WM_CP0, 0x0000);
    wm_write(c, WM_PWR6, 0x0000);
    wm_write(c, WM_PWR2, 0x0000);
    wm_write(c, WM_VMID0, 0x0000);
    wm_write(c, WM_BIAS0, 0x0018);
    wm_write(c, WM_CLOCK2, 0x0000);
    c->powered = false;
}

static int wm_probe(struct device *dev)
{
    struct wm8904 *c = devm_kzalloc(dev, sizeof(*c), 0);
    if (!c)
        return -ENOMEM;
    c->dev = dev;
    c->client = i2c_client_get(dev);
    c->volume = 80;
    if (!c->client)
        return -ENODEV;
    uint16_t id = 0;
    if (wm_read(c, WM_ID, &id))
    {
        dev_err(dev, "no answer at 0x%02x\n", c->client->addr);
        return -ENODEV;
    }
    if (id != 0x8904)
    {
        dev_err(dev, "not a WM8904 (ID %04x)\n", id);
        return -ENODEV;
    }
    if (wm_write(c, WM_ID, 0))
        return -EIO;
    dev_set_drvdata(dev, c);
    s_codec = c;
    dev_info(dev, "WM8904 at 0x%02x\n", c->client->addr);
    return 0;
}

static void wm_remove(struct device *dev)
{
    struct wm8904 *c = dev_get_drvdata(dev);
    wm_power_down(c);
    if (s_codec == c)
        s_codec = NULL;
}

static const struct of_device_id wm_ids[] = {
    {"wlf,wm8904", NULL},
    {NULL, NULL},
};

static struct driver wm_driver = {
    .name = "wm8904",
    .of_match_table = wm_ids,
    .probe = wm_probe,
    .remove = wm_remove,
};

/* ---- the I2S transmitter and its DMA -------------------------------------------------------- */

struct i2s
{
    struct device *dev;
    SPI_TypeDef *spi;
    DMA_Channel_TypeDef *dma;
    uint32_t dma_request;
    int irq_spi, irq_dma;
    struct clk *mclk;
    struct wm8904 *codec;
    uint32_t mclk_hz, rate, channels;
    uint32_t *buf;            /* NPER periods, non-cacheable */
    uint32_t *lli;            /* NPER linked-list items, same 64 KB page (CLBAR) */
    uint32_t real[NPER];      /* frames of sound at the start of each period, silence after */
    unsigned cur;             /* the period the DMA sends */
    uint32_t ring[RING];
    volatile uint32_t head, tail; /* writer / interrupt */
    volatile bool active;     /* playing: the periods are refilled by the interrupt */
    volatile bool open;
    volatile bool want_space, want_empty;
    bool failed;              /* DMA error: the channel stopped */
    bool dry;                 /* the ring ran out while playing (one underrun) */
    uint32_t idle;            /* frames of silence sent since the device was closed */
    struct event ev;
    struct poll_head ph;
    struct mutex lock;        /* configuration */
    uint32_t played, underruns, fifo_errors, dma_errors;
};

static uint32_t ring_count(const struct i2s *s)
{
    return s->head - s->tail;
}

/* the period the DMA reads now */
static unsigned playing(const struct i2s *s)
{
    uint32_t off = s->dma->CSAR - (uint32_t)(uintptr_t)s->buf;
    return (unsigned)(off / PERIOD_BYTES) % NPER;
}

static uint32_t total_real(const struct i2s *s)
{
    uint32_t n = 0;
    for (unsigned i = 0; i < NPER; i++)
        n += s->real[i];
    return n;
}

/* Period @p from the ring, silence after what it had (interrupt or irq_lock) */
static void refill(struct i2s *s, unsigned p)
{
    uint32_t *d = s->buf + p * PERIOD;
    uint32_t n = 0;
    while (n < PERIOD && s->head != s->tail)
    {
        d[n++] = s->ring[s->tail & (RING - 1u)];
        s->tail++;
    }
    if (n < PERIOD)
    {
        memset(d + n, 0, (PERIOD - n) * 4u);
        if (s->open && !s->dry)
            s->underruns++;
        s->dry = s->open;
        if (!s->open)
            s->idle += PERIOD - n;
    }
    else
    {
        s->dry = false;
    }
    s->real[p] = n;
    s->played += n;
    if (n && s->want_space && ring_count(s) <= RING * 3u / 4u)
    {
        s->want_space = false;
        event_set(&s->ev, EV_SPACE);
        poll_notify(&s->ph);
    }
    if (s->want_empty && s->head == s->tail && !total_real(s))
    {
        s->want_empty = false;
        event_set(&s->ev, EV_EMPTY);
    }
}

/* playing no more: the periods are silence, the interrupt off (interrupt or irq_lock) */
static void go_idle(struct i2s *s)
{
    s->active = false;
    irq_disable(s->irq_dma);
    event_set(&s->ev, EV_EMPTY | EV_SPACE);
    poll_notify(&s->ph);
}

static void dma_irq(int irq, void *ctx)
{
    struct i2s *s = ctx;
    DMA_Channel_TypeDef *ch = s->dma;
    (void)irq;
    uint32_t sr = ch->CSR;
    if (sr & (DMA_CSR_DTEF | DMA_CSR_ULEF | DMA_CSR_USEF))
    {
        /* the channel stopped itself: no more sound until the rate is set again */
        ch->CFCR = DMA_CFCR_DTEF | DMA_CFCR_ULEF | DMA_CFCR_USEF;
        s->dma_errors++;
        s->failed = true;
        go_idle(s);
        return;
    }
    if (!(sr & DMA_CSR_TCF) || !s->active)
        return;
    ch->CFCR = DMA_CFCR_TCF | DMA_CFCR_HTF;
    /* the period before the one the DMA is in now is done: normally s->cur; a late
     * interrupt that missed one leaves the period between to play again */
    unsigned now = playing(s), done = (now + NPER - 1u) % NPER;
    if (done != s->cur)
        s->fifo_errors++;
    s->cur = now;
    refill(s, done);
    s->spi->IER |= SPI_IER_UDRIE; /* (off after an underrun: at most one report per period) */
    /* closed and played out: silence everywhere, then the interrupt rests */
    uint32_t quiet = s->rate / 10u > NPER * PERIOD ? s->rate / 10u : NPER * PERIOD;
    if (!s->open && s->head == s->tail && s->idle >= quiet)
        go_idle(s);
}

/* the transmitter found its FIFO empty: the DMA was late */
static void spi_irq(int irq, void *ctx)
{
    struct i2s *s = ctx;
    (void)irq;
    if (s->spi->SR & SPI_SR_UDR)
    {
        s->spi->IFCR = SPI_IFCR_UDRC;
        s->spi->IER &= ~SPI_IER_UDRIE;
        s->fifo_errors++;
    }
}

/* Playback begins: the periods after the one the DMA is in get the ring's frames. With less
 * than they hold, the silence comes first and the sound ends at the last of them, so what
 * is written next follows without a gap. */
static void start(struct i2s *s)
{
    uint32_t key = irq_lock();
    if (!s->active && !s->failed)
    {
        unsigned p;
        do
        {
            p = playing(s);
            s->dma->CFCR = DMA_CFCR_TCF | DMA_CFCR_HTF;
        } while (playing(s) != p);
        s->cur = p;
        s->idle = 0;
        s->dry = false;
        uint32_t room = (NPER - 1u) * PERIOD, take = ring_count(s) < room ? ring_count(s) : room;
        uint32_t lead = room - take;
        for (unsigned i = 1; i < NPER; i++)
        {
            unsigned q = (p + i) % NPER;
            uint32_t *d = s->buf + q * PERIOD;
            uint32_t n = 0, zeros = lead < PERIOD ? lead : PERIOD;
            lead -= zeros;
            memset(d, 0, zeros * 4u);
            for (uint32_t k = zeros; k < PERIOD; k++, n++)
            {
                d[k] = s->ring[s->tail & (RING - 1u)];
                s->tail++;
            }
            s->real[q] = n;
            s->played += n;
        }
        s->active = true;
        irq_enable(s->irq_dma);
    }
    irq_unlock(key);
}

/* drops what is queued: the ring and the periods not being sent yet */
static void flush(struct i2s *s)
{
    uint32_t key = irq_lock();
    s->head = s->tail;
    if (s->active)
    {
        unsigned now = playing(s);
        for (unsigned i = 0; i < NPER; i++)
            if (i != now)
            {
                memset(s->buf + i * PERIOD, 0, PERIOD_BYTES);
                s->real[i] = 0;
            }
    }
    irq_unlock(key);
}

/* frames written but not sent yet */
static uint32_t queued(struct i2s *s)
{
    uint32_t key = irq_lock();
    uint32_t q = ring_count(s);
    if (s->active)
    {
        unsigned now = playing(s);
        uint32_t sent = PERIOD - (s->dma->CBR1 & DMA_CBR1_BNDT) / 4u; /* of the current period */
        for (unsigned i = 0; i < NPER; i++)
            if (i != now)
                q += s->real[i];
        if (s->real[now] > sent)
            q += s->real[now] - sent;
    }
    irq_unlock(key);
    return q;
}

static void stop_stream(struct i2s *s)
{
    DMA_Channel_TypeDef *ch = s->dma;
    irq_disable(s->irq_dma);
    s->active = false;
    if (ch->CCR & DMA_CCR_EN)
    {
        ch->CCR |= DMA_CCR_SUSP;
        uint32_t t0 = cpu_cycles();
        while (!(ch->CSR & (DMA_CSR_SUSPF | DMA_CSR_IDLEF)) && cpu_cycles() - t0 < SystemCoreClock / 1000u)
        {
        }
        ch->CCR = DMA_CCR_RESET;
        t0 = cpu_cycles();
        while ((ch->CCR & DMA_CCR_EN) && cpu_cycles() - t0 < SystemCoreClock / 1000u)
        {
        }
    }
    ch->CFCR = DMA_CFCR_ALL;
    s->spi->CR1 &= ~SPI_CR1_SPE; /* (the master clock stops with it) */
}

/* Silence in all periods, the DMA round and round, the transmitter (and MCK) on */
static void start_stream(struct i2s *s)
{
    DMA_Channel_TypeDef *ch = s->dma;
    memset(s->buf, 0, NPER * PERIOD_BYTES);
    memset(s->real, 0, sizeof(s->real));
    const uint32_t link = DMA_CLLR_UB1 | DMA_CLLR_USA | DMA_CLLR_ULL;
    for (unsigned i = 0; i < NPER; i++)
    {
        uint32_t *item = s->lli + i * LLI_WORDS;
        uint32_t next = (uint32_t)(uintptr_t)(s->lli + ((i + 1u) % NPER) * LLI_WORDS);
        item[0] = PERIOD_BYTES;
        item[1] = (uint32_t)(uintptr_t)(s->buf + i * PERIOD);
        item[2] = (next & DMA_CLLR_LA) | link;
    }
    __asm volatile("dsb 0xF" ::: "memory");
    ch->CCR = 0;
    ch->CFCR = DMA_CFCR_ALL;
    ch->CLBAR = (uint32_t)(uintptr_t)s->lli & DMA_CLBAR_LBA;
    /* half-words from memory (port 0, incrementing) to TXDR (port 1), one per request */
    ch->CTR1 = (1u << DMA_CTR1_SDW_LOG2_Pos) | DMA_CTR1_SINC | (1u << DMA_CTR1_DDW_LOG2_Pos) | DMA_CTR1_DAP;
    ch->CTR2 = (s->dma_request << DMA_CTR2_REQSEL_Pos) | DMA_CTR2_DREQ; /* TC at each block's end */
    ch->CBR1 = PERIOD_BYTES;
    ch->CSAR = (uint32_t)(uintptr_t)s->buf;
    ch->CDAR = (uint32_t)(uintptr_t)&s->spi->TXDR;
    ch->CLLR = ((uint32_t)(uintptr_t)(s->lli + LLI_WORDS) & DMA_CLLR_LA) | link;
    ch->CCR = DMA_CCR_PRIO | DMA_CCR_TCIE | DMA_CCR_DTEIE | DMA_CCR_ULEIE | DMA_CCR_USEIE;
    ch->CCR |= DMA_CCR_EN;
    s->cur = 0;
    s->failed = false;
    s->spi->IFCR = SPI_IFCR_ALL;
    s->spi->CR1 |= SPI_CR1_SPE;
    s->spi->CR1 |= SPI_CR1_CSTART;
}

/* I2S master transmitter, Philips, 16-bit data in 16-bit channels, MCK = 256 fs; fs =
 * kernel clock / 256 / @m (m = 2 x I2SDIV + ODD) */
static void i2s_config(struct i2s *s, uint32_t m)
{
    SPI_TypeDef *spi = s->spi;
    spi->CR1 &= ~SPI_CR1_IOLOCK;
    spi->I2SCFGR = SPI_I2SCFGR_I2SMOD | (2u << SPI_I2SCFGR_I2SCFG_Pos) | SPI_I2SCFGR_MCKOE |
                   ((m / 2u) << SPI_I2SCFGR_I2SDIV_Pos) | ((m & 1u) << SPI_I2SCFGR_ODD_Pos);
    spi->CFG1 = SPI_CFG1_TXDMAEN; /* FIFO threshold: one data */
    spi->CFG2 = SPI_CFG2_AFCNTR;  /* the pins stay driven while the I2S is off */
    spi->IER = SPI_IER_UDRIE;
}

static int set_rate(struct i2s *s, uint32_t rate)
{
    static const uint32_t RATES[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000};
    bool ok = false;
    for (unsigned i = 0; i < sizeof(RATES) / sizeof(RATES[0]); i++)
        ok |= RATES[i] == rate;
    if (!ok)
        return -EINVAL;
    /* the kernel clock near 24.576 / 22.5792 MHz: the same for the whole family, so a change
     * within it does not move PLL3 - except to 32 kHz: 2 x I2SDIV + ODD = 3 is not allowed, it
     * takes 4 (32.768 MHz) */
    uint32_t base = rate % 11025u == 0 ? 22579200u : 24576000u;
    uint32_t m = (base + 128u * rate) / (256u * rate);
    if (m < 2u)
        m = 2u;
    if (m == 3u)
        m = 4u;
    uint32_t key = irq_lock();
    s->head = s->tail;
    irq_unlock(key);
    stop_stream(s);
    int r = clk_set_rate(s->mclk, 256u * rate * m);
    if (!r)
    {
        s->mclk_hz = clk_get_rate(s->mclk);
        i2s_config(s, m);
        if (s->codec->powered)
            r = wm_set_rate(s->codec, rate);
    }
    start_stream(s);
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
    struct i2s *s = f->dev;
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
        if (s->rate != 48000 || s->failed)
            r = set_rate(s, 48000);
        if (!r)
            s->open = true;
    }
    mutex_unlock(&s->lock);
    return r;
}

static int audio_close(struct file *f)
{
    struct i2s *s = f->dev;
    if (!observer(f))
        s->open = false; /* the interrupt goes idle once all was played */
    return 0;
}

static int audio_write(struct file *f, const void *buf, size_t len)
{
    struct i2s *s = f->dev;
    const int16_t *p = buf;
    if (observer(f))
        return -EBADF;
    if (s->failed)
        return -EIO;
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
        bool full = ring_count(s) == RING && s->active;
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
        if (s->failed)
            break;
    }
    if (!done && frames)
        return s->failed ? -EIO : -EAGAIN;
    return (int)(done * 2u * s->channels);
}

static int audio_ioctl(struct file *f, unsigned cmd, void *arg)
{
    struct i2s *s = f->dev;
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
        *(uint32_t *)arg = queued(s);
        return 0;
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
            bool empty = !s->active || (s->head == s->tail && !total_real(s));
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
        st->fifo_errors = s->fifo_errors + s->dma_errors;
        return 0;
    }
    default:
        return -ENOTTY;
    }
}

static int audio_poll(struct file *f, struct poll_entry *e)
{
    struct i2s *s = f->dev;
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

static void free_dma_buffer(void *arg)
{
    kfree(arg);
}

static int i2s_probe(struct device *dev)
{
    struct device_node *cnp = of_parse_phandle(dev->of_node, "audio-codec", 0);
    if (!cnp)
    {
        dev_err(dev, "no audio-codec\n");
        return -ENODEV;
    }
    if (!s_codec || s_codec->dev->of_node != cnp)
        return -EPROBE_DEFER; /* the codec's driver has not come yet */
    uint32_t channel = 0;
    struct i2s *s = devm_kzalloc(dev, sizeof(*s), 0);
    if (!s)
        return -ENOMEM;
    s->dev = dev;
    s->codec = s_codec;
    s->spi = device_map(dev, 0);
    DMA_TypeDef *gpdma = device_map(dev, 1);
    s->irq_spi = device_get_irq(dev, 0);
    s->irq_dma = device_get_irq(dev, 1);
    if (of_property_read_u32(dev->of_node, "st,dma-channel", &channel) || channel > 15u ||
        of_property_read_u32(dev->of_node, "st,dma-request", &s->dma_request) || !s->spi || !gpdma)
        return -EINVAL;
    if (s->irq_spi < 0 || s->irq_dma < 0)
        return s->irq_dma == -EPROBE_DEFER || s->irq_spi == -EPROBE_DEFER ? -EPROBE_DEFER : -ENODEV;
    s->dma = GPDMA_CHANNEL(gpdma, channel);
    struct clk *bus, *dmaclk;
    int r = devm_clk_get_enabled(dev, "bus", &bus);
    if (!r)
        r = devm_clk_get_enabled(dev, "dma", &dmaclk);
    if (!r)
        r = devm_clk_get_enabled(dev, "mclk", &s->mclk);
    if (r)
        return r;
    /* the periods and the list items in one non-cacheable block within a 64 KB page */
    uint32_t *mem = kmalloc_bounded(NPER * PERIOD_BYTES + NPER * LLI_WORDS * 4u, 32, 0x10000, KM_NOCACHE | KM_DMA);
    if (!mem)
        return -ENOMEM;
    devm_add_action(dev, free_dma_buffer, mem);
    s->buf = mem;
    s->lli = mem + NPER * PERIOD;
    event_init(&s->ev);
    poll_head_init(&s->ph);
    mutex_init(&s->lock);

    r = irq_request(s->irq_dma, dma_irq, s, 3, dev->name);
    if (r)
        return r;
    irq_disable(s->irq_dma); /* on while something plays */
    r = irq_request(s->irq_spi, spi_irq, s, 6, dev->name);
    if (r)
    {
        irq_free(s->irq_dma);
        return r;
    }
    dev_set_drvdata(dev, s);
    r = set_rate(s, 48000);
    if (!r)
        r = wm_power_up(s->codec, 48000); /* the master clock runs now */
    if (!r)
        r = devfs_register("audio", &audio_ops, s);
    if (r)
    {
        stop_stream(s);
        s->spi->CR1 = 0;
        irq_free(s->irq_spi);
        irq_free(s->irq_dma);
        return r;
    }
    dev_info(dev, "/dev/audio: I2S to %s, kernel clock %lu Hz, GPDMA1 channel %lu\n", s->codec->dev->name,
             (unsigned long)s->mclk_hz, (unsigned long)channel);
    return 0;
}

static void i2s_remove(struct device *dev)
{
    struct i2s *s = dev_get_drvdata(dev);
    devfs_unregister("audio");
    wm_write(s->codec, WM_DAC1, WM_DAC1_MUTE);
    stop_stream(s);
    s->spi->IER = 0;
    s->spi->CR1 = 0;
    irq_free(s->irq_spi);
    irq_free(s->irq_dma);
}

static const struct of_device_id i2s_ids[] = {
    {"st,stm32h7-i2s", NULL},
    {NULL, NULL},
};

static struct driver i2s_driver = {
    .name = "i2s-stm32",
    .of_match_table = i2s_ids,
    .probe = i2s_probe,
    .remove = i2s_remove,
};

static int init(void)
{
    int r = driver_register(&wm_driver);
    if (r)
        return r;
    r = driver_register(&i2s_driver);
    if (r)
        driver_unregister(&wm_driver);
    return r;
}

static void fini(void)
{
    driver_unregister(&i2s_driver);
    driver_unregister(&wm_driver);
}

MODULE("i2s-stm32", "sound: SPI6 (I2S) and the WM8904 codec, /dev/audio", init, fini);
