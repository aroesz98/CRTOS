/*
 * sdcard_sdmmc.cpp - built-in SD card driver on SDMMC1 of the STM32H7S78-DK (micro SD slot
 * CN13: PC8-PC11 data, PC12 clock, PD2 command, PM14 card detect). The same interface as the
 * i.MX RT board's USDHC driver (kernel/os/boot/sdcard.h), the same recovery and statistics.
 *
 * Transfers use the controller's internal DMA (IDMA, one buffer) and complete by interrupt: the
 * calling thread sleeps on an event while the controller moves data. Buffers the DMA cannot
 * use directly (TCM, flash, misaligned, or sharing cache lines on a read) go through a bounce
 * buffer in AXI SRAM. The pins are set up here because this driver runs before any device
 * tree or pinctrl driver is available; the kernel clock (PLL2 S, 200 MHz) is the boot loader's.
 *
 * Bus modes: default speed (25 MHz) and high speed (50 MHz) at 3.3 V signalling. The slot has
 * no 1.8 V transceiver, so the UHS-I modes are not used.
 *
 * Errors: a failed transfer stops the data path and clears the controller. Block I/O then asks
 * the card for its state (CMD13), stops a transfer the card is still in (CMD12) and tries
 * again; when the card does not answer or fails again, the controller is reset and the card
 * initialised from scratch. The last transfers are kept in a trace that is printed with the
 * first error of a series.
 */
#include "os/boot/sdcard.h"
#include "kernel.h"
#include "platform.h"
#include <crtos/irq.h>
#include <string.h>

#define SD              SDMMC1
#define SD_IRQ          SDMMC1_IRQn
#define SD_IRQ_PRIO     6
#define SD_KER_CLK      200000000u      /* PLL2 S, chosen by the boot loader (CCIPR1 SDMMC12SEL) */

#define MAX_BLOCKS      2048u           /* per command (1 MB) */
#define BOUNCE_BLOCKS   8u
#define CMD_TIMEOUT_MS  500u
/* data phase timeout: base + per block (the SD spec allows 100 ms read access time and
 * 250/500 ms write busy per block; cards pause longer for internal housekeeping) */
#define READ_TIMEOUT_MS(n)  (500u + (n))
#define WRITE_TIMEOUT_MS(n) (1000u + 2u * (n))
#define SLOW_US         250000u         /* data transfers slower than this are logged */
#define RETRIES         3
#define MAX_LOCKUPS     3               /* card lock-ups before falling back to 25 MHz */
#define CMD_GAP_US      50u             /* a command right after the end of a read: see the USDHC driver */

#define EV_CMD_OK       1u
#define EV_CMD_FAIL     2u
#define EV_DATA_OK      4u
#define EV_DATA_FAIL    8u
#define EV_ALL          (EV_CMD_OK | EV_CMD_FAIL | EV_DATA_OK | EV_DATA_FAIL)

#define XF_QUIET        1u              /* a failure is expected (probing): no report */
#define XF_R1_CHECK     2u              /* the R1 response's error bits fail the command */

/* bus modes, slowest first (the names of the USDHC driver; UHS-I is not reached here) */
#define MODE_DS         0
#define MODE_HS         1
#define MODE_SDR50      2
#define MODE_SDR104     3

#define OCR_BUSY        0x80000000u
#define OCR_HCS         0x40000000u
#define OCR_XPC         0x10000000u
#define OCR_VDD         0x00FF8000u

#define R1_ERRORS       0xFFF98008u
#define R1_READY        (1u << 8)
#define R1_STATE(r)     (((r) >> 9) & 0xFu)
#define STATE_TRAN      4u
#define STATE_DATA      5u
#define STATE_RCV       6u
#define STATE_PRG       7u

/* responses */
enum { RSP_NONE, RSP_R1, RSP_R1B, RSP_R2, RSP_R3, RSP_R6, RSP_R7 };

#define STA_CMD_DONE    (SDMMC_STA_CMDREND | SDMMC_STA_CMDSENT)
#define STA_CMD_FAIL    (SDMMC_STA_CCRCFAIL | SDMMC_STA_CTIMEOUT)
#define STA_DATA_FAIL   (SDMMC_STA_DCRCFAIL | SDMMC_STA_DTIMEOUT | SDMMC_STA_TXUNDERR | SDMMC_STA_RXOVERR | \
                         SDMMC_STA_IDMATE)
#define ICR_ALL         0x1FE00FFFu     /* every static flag */

/* the bounce buffer must be where the IDMA reaches it (not DTCM): AXI SRAM */
static uint8_t s_bounce[BOUNCE_BLOCKS * SDCARD_BLOCK_SIZE] __attribute__((aligned(32), section(".bss.$SRAM_OC")));

static struct event s_ev;
static struct mutex s_lock;
static bool s_host_ready;
static uint32_t s_max_hz = 50000000u;
static int s_max_mode = MODE_HS;

static volatile uint32_t s_irq_seen;
static volatile uint32_t s_t_cmd, s_t_data;
static uint32_t s_last_data;
static bool s_gap;

static uint64_t s_sum_cmd, s_sum_data, s_sum_total;
static uint32_t s_sum_n;

struct trace {
    uint32_t ms;
    uint32_t arg;
    uint32_t resp;
    uint32_t irq;
    uint32_t us;
    uint16_t blocks;
    uint8_t cmd;
    int8_t result;
};
#define TRACE_N 16u
static struct trace s_trace[TRACE_N];
static uint32_t s_trace_n;
static uint32_t s_fail_run;

static struct {
    bool ready;
    bool sdhc;
    bool cmd23;
    int mode;
    uint32_t rca;
    uint32_t blocks;
    uint32_t clock;
    int width;
    uint32_t cid[4];
} s_card;

static struct {
    uint32_t reads, writes, errors, retries, reinits, lockups, slow, max_us;
} s_stats;
static bool s_card_cmd23;
static uint32_t s_pin_speed = 3;        /* OSPEEDR of the bus pins (sdcard_set_pads) */

struct sd_cmd {
    uint32_t index;
    uint32_t arg;
    int rsp;
    bool stop;              /* CMD12: ends the data transfer */
    uint32_t resp[4];       /* as the USDHC driver keeps them: resp[0] = [31:0] ... resp[3] = [127:96] */
};

struct sd_data {
    uint8_t *buf;
    uint32_t block_size;    /* a power of two */
    uint32_t blocks;
    bool write;
};

/* ---- host -------------------------------------------------------------------------------- */

static void sd_irq(int, void *)
{
    uint32_t st = SD->STA;
    s_irq_seen |= st;
    uint32_t bits = 0;
    if (st & STA_CMD_FAIL)
        bits |= EV_CMD_FAIL;
    else if (st & STA_CMD_DONE) {
        s_t_cmd = cpu_cycles();
        bits |= EV_CMD_OK;
    }
    if (st & STA_DATA_FAIL)
        bits |= EV_DATA_FAIL;
    else if (st & SDMMC_STA_DATAEND) {
        s_t_data = cpu_cycles();
        bits |= EV_DATA_OK;
    }
    SD->ICR = st & ICR_ALL;
    if (bits)
        event_set(&s_ev, bits);
}

static void trace_add(const struct sd_cmd *c, const struct sd_data *d, int result, uint32_t us)
{
    struct trace *t = &s_trace[s_trace_n++ % TRACE_N];
    t->ms = tick_get();
    t->arg = c->arg;
    t->resp = c->resp[0];
    t->irq = s_irq_seen;
    t->us = us;
    t->blocks = d ? (uint16_t)d->blocks : 0u;
    t->cmd = (uint8_t)c->index;
    t->result = (int8_t)result;
}

static uint32_t s_hist_n[2][5];
static uint64_t s_hist_us[2][5];

static void hist_add(bool write, uint32_t blocks, uint32_t us)
{
    int b = blocks == 1 ? 0 : blocks < 8 ? 1 : blocks < 64 ? 2 : blocks < 256 ? 3 : 4;
    s_hist_n[write][b]++;
    s_hist_us[write][b] += us;
}

void sdcard_dump_hist(int (*pr)(const char *fmt, ...))
{
    static const char *const names[] = { "1", "2-7", "8-63", "64-255", "256+" };
    for (int w = 0; w < 2; w++)
        for (int b = 0; b < 5; b++)
            if (s_hist_n[w][b])
                pr("  %-5s %6s blocks: %6lu commands, %8lu us each\n", w ? "write" : "read", names[b],
                   (unsigned long)s_hist_n[w][b], (unsigned long)(s_hist_us[w][b] / s_hist_n[w][b]));
    memset(s_hist_n, 0, sizeof(s_hist_n));
    memset(s_hist_us, 0, sizeof(s_hist_us));
}

void sdcard_dump_trace(int (*pr)(const char *fmt, ...))
{
    uint32_t end = s_trace_n, n = end < TRACE_N ? end : TRACE_N;
    for (uint32_t i = end - n; i != end; i++) {
        const struct trace *t = &s_trace[i % TRACE_N];
        pr("  %8lu ms  CMD%-2u arg %08lx blocks %-4u resp %08lx sta %08lx %7lu us  %d\n", (unsigned long)t->ms,
           (unsigned)t->cmd, (unsigned long)t->arg, (unsigned)t->blocks, (unsigned long)t->resp,
           (unsigned long)t->irq, (unsigned long)t->us, (int)t->result);
    }
}

static void report(const char *what, const struct sd_cmd *c, const struct sd_data *d, uint32_t us)
{
    printk("E: sd: %s: CMD%lu arg %08lx blocks %lu resp %08lx after %lu us\n", what, (unsigned long)c->index,
           (unsigned long)c->arg, (unsigned long)(d ? d->blocks : 0u), (unsigned long)c->resp[0], (unsigned long)us);
    printk("E: sd:   sta seen %08lx now %08lx power %08lx clkcr %08lx dctrl %08lx dcount %08lx idma %08lx @%08lx\n",
           (unsigned long)s_irq_seen, (unsigned long)SD->STA, (unsigned long)SD->POWER, (unsigned long)SD->CLKCR,
           (unsigned long)SD->DCTRL, (unsigned long)SD->DCOUNT, (unsigned long)SD->IDMACTRL,
           (unsigned long)SD->IDMABASER);
    if (s_fail_run == 0) {
        printk("E: sd: recent transfers:\n");
        sdcard_dump_trace(printk);
    }
}

/* Stop the data path after a failed transfer: the state machines and the FIFO start clean */
static void reset_lines(bool data)
{
    SD->MASK = 0;
    SD->IDMACTRL = 0;
    if (data || (SD->STA & (SDMMC_STA_DPSMACT | SDMMC_STA_CPSMACT))) {
        /* a pending data transfer ends with CMD12 (stop_transmission); here the controller only
         * drops what it has */
        SD->CMD &= ~SDMMC_CMD_CMDTRANS;
        SD->DCTRL = SDMMC_DCTRL_FIFORST;
        SD->DCTRL = 0;
    }
    SD->ICR = ICR_ALL;
}

/* Wait until the card left the busy state (DAT0 high) */
static int wait_dat0(uint32_t timeout_ms)
{
    uint32_t t0 = tick_get();
    while (SD->STA & SDMMC_STA_BUSYD0) {
        if (tick_get() - t0 > timeout_ms)
            return -ETIMEDOUT;
        task_sleep_ms(1);
    }
    return 0;
}

static int transfer(struct sd_cmd *c, struct sd_data *d, uint32_t flags = 0)
{
    if (s_gap) {
        uint32_t gap = CMD_GAP_US * (SystemCoreClock / 1000000u);
        for (uint32_t n = 0; cpu_cycles() - s_last_data < gap && n < gap; n++)
            ;
        s_gap = false;
    }
    s_irq_seen = 0;
    event_clear(&s_ev, EV_ALL);
    SD->ICR = ICR_ALL;

    uint32_t cmdr = (c->index & SDMMC_CMD_CMDINDEX) | SDMMC_CMD_CPSMEN;
    uint32_t mask = STA_CMD_FAIL;
    switch (c->rsp) {
    case RSP_NONE:
        mask |= SDMMC_STA_CMDSENT;
        break;
    case RSP_R2:
        cmdr |= SDMMC_CMD_WAITRESP;                     /* long */
        mask |= SDMMC_STA_CMDREND;
        break;
    case RSP_R3:
        cmdr |= SDMMC_CMD_WAITRESP_1;                   /* short, no CRC */
        mask |= SDMMC_STA_CMDREND;
        break;
    default:
        cmdr |= SDMMC_CMD_WAITRESP_0;                   /* short */
        mask |= SDMMC_STA_CMDREND;
        break;
    }
    if (c->stop)
        cmdr |= SDMMC_CMD_CMDSTOP;
    uint32_t tmo_ms = 0;
    if (d) {
        tmo_ms = d->write ? WRITE_TIMEOUT_MS(d->blocks) : READ_TIMEOUT_MS(d->blocks);
        uint64_t cycles = (uint64_t)tmo_ms * (s_card.clock ? s_card.clock : 400000u) / 1000u;
        SD->DTIMER = cycles > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)cycles;
        SD->DLEN = d->blocks * d->block_size;
        SD->DCTRL = ((uint32_t)__builtin_ctz(d->block_size) << SDMMC_DCTRL_DBLOCKSIZE_Pos) |
                    (d->write ? 0u : SDMMC_DCTRL_DTDIR);
        SD->IDMABASER = (uint32_t)(uintptr_t)d->buf;
        SD->IDMACTRL = SDMMC_IDMA_IDMAEN;
        cmdr |= SDMMC_CMD_CMDTRANS;
        mask |= STA_DATA_FAIL | SDMMC_STA_DATAEND;
    }
    uint32_t t0 = cpu_cycles();
    SD->MASK = mask;
    SD->ARG = c->arg;
    SD->CMD = cmdr;

    const char *what = nullptr;
    int r = 0;
    int32_t ev = event_wait(&s_ev, EV_CMD_OK | EV_CMD_FAIL, EVENT_ANY, CMD_TIMEOUT_MS);
    if (ev < 0 || (ev & EV_CMD_FAIL)) {
        bool crc = (s_irq_seen & SDMMC_STA_CCRCFAIL) != 0;
        what = ev < 0 ? "command timeout" : (crc ? "command CRC error" : "no response");
        r = ev < 0 || !crc ? -ETIMEDOUT : -EIO;
    } else {
        c->resp[0] = SD->RESP1;
        if (c->rsp == RSP_R2) {
            c->resp[3] = SD->RESP1;
            c->resp[2] = SD->RESP2;
            c->resp[1] = SD->RESP3;
            c->resp[0] = SD->RESP4;
        }
        if ((flags & XF_R1_CHECK) && (c->resp[0] & R1_ERRORS)) {
            what = "card status error";
            r = -EIO;
        } else if (d) {
            ev = event_wait(&s_ev, EV_DATA_OK | EV_DATA_FAIL, EVENT_ANY, tmo_ms);
            if (ev < 0) {
                what = "data timeout";
                r = -ETIMEDOUT;
            } else if (ev & EV_DATA_FAIL) {
                bool tmo_hw = (s_irq_seen & SDMMC_STA_DTIMEOUT) != 0;
                what = (s_irq_seen & SDMMC_STA_IDMATE) ? "DMA error" :
                       tmo_hw ? "data timeout (controller)" : "data error";
                r = tmo_hw ? -ETIMEDOUT : -EIO;
            }
        }
    }
    uint32_t cycles = cpu_cycles() - t0;
    uint32_t us = cycles / (SystemCoreClock / 1000000u);
    SD->MASK = 0;
    if (d) {
        SD->IDMACTRL = 0;
        SD->CMD &= ~SDMMC_CMD_CMDTRANS;
    }
    trace_add(c, d, r, us);
    if (r) {
        if (!(flags & XF_QUIET)) {
            report(what, c, d, us);
            s_fail_run++;
        }
        reset_lines(d != nullptr);
        return r;
    }
    if (c->rsp == RSP_R1B)
        wait_dat0(1000);
    if (d) {
        s_fail_run = 0;
        s_last_data = s_t_data;
        s_gap = !d->write;
        s_sum_cmd += s_t_cmd - t0;
        s_sum_data += s_t_data - t0;
        s_sum_total += cycles;
        s_sum_n++;
        if (us > s_stats.max_us)
            s_stats.max_us = us;
        if (us > SLOW_US) {
            s_stats.slow++;
            printk("W: sd: CMD%lu arg %08lx (%lu blocks) took %lu ms\n", (unsigned long)c->index,
                   (unsigned long)c->arg, (unsigned long)d->blocks, (unsigned long)(us / 1000u));
        }
    }
    return 0;
}

void sdcard_timing(uint32_t *cmd_us, uint32_t *data_us, uint32_t *total_us, uint32_t *count)
{
    uint32_t n = s_sum_n ? s_sum_n : 1;
    uint32_t mhz = SystemCoreClock / 1000000u;
    *cmd_us = (uint32_t)(s_sum_cmd / n / mhz);
    *data_us = (uint32_t)(s_sum_data / n / mhz);
    *total_us = (uint32_t)(s_sum_total / n / mhz);
    *count = s_sum_n;
    s_sum_cmd = s_sum_data = s_sum_total = 0;
    s_sum_n = 0;
}

static int cmd(uint32_t index, uint32_t arg, int rsp, uint32_t *resp, uint32_t flags = 0)
{
    struct sd_cmd c;
    memset(&c, 0, sizeof(c));
    c.index = index;
    c.arg = arg;
    c.rsp = rsp;
    int r = transfer(&c, nullptr, flags);
    if (!r && resp)
        memcpy(resp, c.resp, sizeof(c.resp));
    return r;
}

static int app_cmd(uint32_t index, uint32_t arg, int rsp, uint32_t *resp, uint32_t flags = 0)
{
    int r = cmd(55, s_card.rca << 16, RSP_R1, nullptr, flags);
    return r ? r : cmd(index, arg, rsp, resp, flags);
}

static int card_status(uint32_t *status, uint32_t flags = 0)
{
    uint32_t resp[4];
    int r = cmd(13, s_card.rca << 16, RSP_R1, resp, flags);
    if (!r)
        *status = resp[0];
    return r;
}

/* After a write: poll CMD13 until the card is back in the transfer state */
static int wait_ready(uint32_t timeout_ms)
{
    uint32_t t0 = tick_get();
    for (;;) {
        uint32_t st;
        int r = card_status(&st);
        if (r)
            return r;
        if ((st & R1_READY) && R1_STATE(st) == STATE_TRAN)
            return 0;
        if (tick_get() - t0 > timeout_ms)
            return -ETIMEDOUT;
        task_sleep_ms(1);
    }
}

/* CMD12: end the data transfer the card is in */
static int stop_transmission(uint32_t flags = XF_QUIET)
{
    struct sd_cmd c;
    memset(&c, 0, sizeof(c));
    c.index = 12;
    c.rsp = RSP_R1B;
    c.stop = true;
    return transfer(&c, nullptr, flags);
}

/* After a failed data transfer: get the card back to the transfer state */
static int recover_card(void)
{
    bool stopped = false;
    uint32_t t0 = tick_get();
    for (;;) {
        uint32_t st;
        int r = card_status(&st);
        if (r)
            return r;
        uint32_t state = R1_STATE(st);
        if (state == STATE_TRAN)
            return 0;
        if ((state == STATE_DATA || state == STATE_RCV) && !stopped) {
            printk("sd: card still in %s state, stopping it\n", state == STATE_DATA ? "data" : "receive");
            stop_transmission();
            reset_lines(true);
            stopped = true;
            continue;
        }
        if ((state != STATE_PRG && state != STATE_DATA && state != STATE_RCV) || tick_get() - t0 > 1000u) {
            printk("E: sd: card stuck in state %lu (status %08lx)\n", (unsigned long)state, (unsigned long)st);
            return -EIO;
        }
        task_sleep_ms(1);
    }
}

/* PC8-PC11 data (PC10: AF12, the others AF11), PC12 clock, PD2 command (AF11), PM14 card detect */
static void set_pin(GPIO_TypeDef *g, uint32_t pin, uint32_t af, bool pull_up)
{
    g->AFR[pin >> 3] = (g->AFR[pin >> 3] & ~(0xFu << ((pin & 7u) * 4u))) | (af << ((pin & 7u) * 4u));
    g->OSPEEDR = (g->OSPEEDR & ~(3u << (pin * 2u))) | (s_pin_speed << (pin * 2u));
    g->PUPDR = (g->PUPDR & ~(3u << (pin * 2u))) | ((pull_up ? 1u : 0u) << (pin * 2u));
    g->OTYPER &= ~(1u << pin);
    g->MODER = (g->MODER & ~(3u << (pin * 2u))) | (2u << (pin * 2u));
}

static void init_pins(void)
{
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOCEN | RCC_AHB4ENR_GPIODEN | RCC_AHB4ENR_GPIOMEN;
    (void)RCC->AHB4ENR;
    set_pin(GPIOC, 8, 11, true);
    set_pin(GPIOC, 9, 11, true);
    set_pin(GPIOC, 10, 12, true);
    set_pin(GPIOC, 11, 11, true);
    set_pin(GPIOC, 12, 11, false);
    set_pin(GPIOD, 2, 11, true);
    /* card detect: input with pull-up, low = inserted */
    GPIOM->PUPDR = (GPIOM->PUPDR & ~(3u << 28)) | (1u << 28);
    GPIOM->MODER &= ~(3u << 28);
}

/* Bus clock = 200 MHz / (2 * CLKDIV) */
static uint32_t set_clock(uint32_t hz)
{
    uint32_t div = (SD_KER_CLK + 2u * hz - 1u) / (2u * hz);
    if (div < 1u)
        div = 1u;
    if (div > 1023u)
        div = 1023u;
    SD->CLKCR = (SD->CLKCR & ~SDMMC_CLKCR_CLKDIV) | div;
    return SD_KER_CLK / (2u * div);
}

static void set_width(int width)
{
    SD->CLKCR = (SD->CLKCR & ~SDMMC_CLKCR_WIDBUS) | (width == 4 ? SDMMC_CLKCR_WIDBUS_0 : 0u);
}

/* Reset the whole controller and program its static configuration (hardware flow control:
 * the clock pauses rather than the FIFO overruns when the bus is busy) */
static void host_config(void)
{
    RCC->AHB5ENR |= RCC_AHB5ENR_SDMMC1EN;
    RCC->AHB5RSTR |= RCC_AHB5RSTR_SDMMC1RST;
    (void)RCC->AHB5RSTR;
    RCC->AHB5RSTR &= ~RCC_AHB5RSTR_SDMMC1RST;
    (void)RCC->AHB5RSTR;
    SD->POWER = 0;
    SD->CLKCR = SDMMC_CLKCR_HWFC_EN;
    set_clock(400000u);
    SD->POWER = SDMMC_POWER_PWRCTRL;            /* power on: the clock runs */
    SD->MASK = 0;
    SD->ICR = ICR_ALL;
}

static void init_host(void)
{
    host_config();
    if (irq_request(SD_IRQ, sd_irq, nullptr, SD_IRQ_PRIO, "sdmmc1"))
        panic("SDMMC1 IRQ busy");
}

int sdcard_present(void)
{
    return (GPIOM->IDR & (1u << 14)) == 0;
}

static bool detect(uint32_t debounce_ms, uint32_t timeout_ms)
{
    uint32_t stable = 0;
    task_sleep_ms(5);
    for (uint32_t ms = 0; ms < timeout_ms; ms++) {
        stable = sdcard_present() ? stable + 1 : 0;
        if (stable >= debounce_ms)
            return true;
        task_sleep_ms(1);
    }
    return false;
}

/* a short read into the bounce buffer (SCR, switch status) */
static int read_reg(uint32_t index, uint32_t arg, uint32_t bytes)
{
    struct sd_cmd c;
    struct sd_data d;
    memset(&c, 0, sizeof(c));
    c.index = index;
    c.arg = arg;
    c.rsp = RSP_R1;
    d.buf = s_bounce;
    d.block_size = bytes;
    d.blocks = 1;
    d.write = false;
    SCB_CleanInvalidateDCache_by_Addr(s_bounce, 64);
    int r = transfer(&c, &d, XF_QUIET | XF_R1_CHECK);
    SCB_InvalidateDCache_by_Addr(s_bounce, 64);
    return r;
}

/* ACMD51: the SD configuration register (8 bytes, MSB first) - which commands the card has */
static void read_scr(void)
{
    if (cmd(55, s_card.rca << 16, RSP_R1, nullptr, XF_QUIET) || read_reg(51, 0, 8))
        return;
    s_card_cmd23 = (s_bounce[3] >> 1) & 1u; /* SCR bit 33: CMD23 supported */
    s_card.cmd23 = s_card_cmd23;
}

/* ---- bus modes ------------------------------------------------------------------------------ */

static const char *const s_mode_names[] = { "default speed", "high speed", "SDR50", "SDR104" };

/* CMD6 for group 1 (bus speed): check (@set false) or switch to function @fn; the 64-byte status
 * arrives in s_bounce, MSB first */
static int switch_func(bool set, uint32_t fn)
{
    return read_reg(6, (set ? 0x80000000u : 0u) | 0x00FFFFF0u | (fn & 0xFu), 64);
}

static void select_mode(void)
{
    uint32_t support = 0x3u;
    if (switch_func(false, 0xFu) == 0)
        support = ((uint32_t)s_bounce[12] << 8) | s_bounce[13];
    if (s_max_mode >= MODE_HS && s_max_hz > 25000000u && (support & (1u << MODE_HS)) &&
        switch_func(true, MODE_HS) == 0 && (s_bounce[16] & 0x0Fu) == MODE_HS) {
        task_sleep_ms(1);
        s_card.mode = MODE_HS;
        s_card.clock = set_clock(s_max_hz < 50000000u ? s_max_hz : 50000000u);
        return;
    }
    s_card.mode = MODE_DS;
    s_card.clock = set_clock(s_max_hz < 25000000u ? s_max_hz : 25000000u);
}

static int init_card(void)
{
    uint32_t resp[4];
    memset(&s_card, 0, sizeof(s_card));
    set_width(1);
    s_card.clock = set_clock(400000u);
    task_sleep_ms(2);                   /* at least 74 clocks before the first command */

    cmd(0, 0, RSP_NONE, nullptr, XF_QUIET);
    task_sleep_ms(1);
    bool v2 = cmd(8, 0x1AAu, RSP_R7, resp, XF_QUIET) == 0 && (resp[0] & 0xFFu) == 0xAAu;

    uint32_t ocr = 0;
    uint32_t arg = OCR_VDD | (v2 ? OCR_HCS | OCR_XPC : 0u);
    uint32_t t0 = tick_get();
    do {
        if (app_cmd(41, arg, RSP_R3, resp, XF_QUIET) == 0) {
            ocr = resp[0];
            if (ocr & OCR_BUSY)
                break;
        }
        task_sleep_ms(5);
    } while (tick_get() - t0 < 1500);
    if (!(ocr & OCR_BUSY))
        return -ETIMEDOUT;
    s_card.sdhc = v2 && (ocr & OCR_HCS);

    if (cmd(2, 0, RSP_R2, s_card.cid))
        return -EIO;
    if (cmd(3, 0, RSP_R6, resp))
        return -EIO;
    s_card.rca = resp[0] >> 16;

    if (cmd(9, s_card.rca << 16, RSP_R2, resp))
        return -EIO;
    /* resp[3] = CSD[127:96] ... resp[0] = CSD[31:0] */
    uint32_t csd_ver = (resp[3] >> 30) & 3u;
    if (csd_ver == 0) {
        uint32_t c_size = ((resp[2] & 0x3FFu) << 2) | ((resp[1] >> 30) & 3u);
        uint32_t mult = (resp[1] >> 15) & 7u;
        uint32_t bl_len = (resp[2] >> 16) & 0xFu;
        uint64_t bytes = (uint64_t)(c_size + 1) << (mult + 2 + bl_len);
        s_card.blocks = (uint32_t)(bytes / SDCARD_BLOCK_SIZE);
    } else {
        uint32_t c_size = ((resp[2] & 0x3Fu) << 16) | ((resp[1] >> 16) & 0xFFFFu);
        s_card.blocks = (c_size + 1) * 1024u;
    }

    if (cmd(7, s_card.rca << 16, RSP_R1B, nullptr))
        return -EIO;
    if (!s_card.sdhc)
        cmd(16, SDCARD_BLOCK_SIZE, RSP_R1, nullptr);

    s_card.width = 1;
    if (app_cmd(6, 2, RSP_R1, nullptr) == 0) { /* 4-bit bus */
        set_width(4);
        s_card.width = 4;
    }
    read_scr();
    select_mode();
    s_card.ready = true;
    return 0;
}

static void report_mode(void)
{
    printk("sd: %d-bit bus, %s, %lu MHz, 3.3 V signalling%s\n", s_card.width, s_mode_names[s_card.mode],
           (unsigned long)(s_card.clock / 1000000u), s_card.cmd23 ? ", CMD23" : "");
}

static int reinit(void)
{
    s_stats.reinits++;
    printk("W: sd: re-initialising the card\n");
    host_config();
    int r = init_card();
    if (r)
        printk("E: sd: card initialisation failed (%d)\n", r);
    else
        report_mode();
    return r;
}

int sdcard_init(void)
{
    if (!s_host_ready) {
        mutex_init(&s_lock);
        event_init(&s_ev);
        init_pins();
        init_host();
        s_host_ready = true;
    }
    if (!detect(30, 150))
        return -ENODEV;
    mutex_lock(&s_lock, WAIT_FOREVER);
    int r = init_card();
    if (!r)
        report_mode();
    mutex_unlock(&s_lock);
    return r;
}

int sdcard_reinit(void)
{
    if (!s_host_ready)
        return -ENODEV;
    mutex_lock(&s_lock, WAIT_FOREVER);
    int r = reinit();
    mutex_unlock(&s_lock);
    return r;
}

/* Pin settings of the bus (signal integrity experiments): @speed is the output speed 0-3;
 * drive strength and slew rate are not separate settings on this chip */
void sdcard_set_pads(uint32_t speed, uint32_t dse, uint32_t fast_slew)
{
    (void)dse;
    (void)fast_slew;
    mutex_lock(&s_lock, WAIT_FOREVER);
    s_pin_speed = speed & 3u;
    init_pins();
    mutex_unlock(&s_lock);
}

uint32_t sdcard_set_clock(uint32_t hz)
{
    mutex_lock(&s_lock, WAIT_FOREVER);
    s_max_hz = hz < 400000u ? 400000u : (hz > 50000000u ? 50000000u : hz);
    reinit();
    uint32_t r = s_card.clock;
    mutex_unlock(&s_lock);
    return r;
}

int sdcard_set_mode(int mode)
{
    mutex_lock(&s_lock, WAIT_FOREVER);
    s_max_mode = mode <= MODE_DS ? MODE_DS : MODE_HS;
    s_max_hz = 50000000u;
    reinit();
    int r = s_card.mode;
    mutex_unlock(&s_lock);
    return r;
}

int sdcard_set_cmd23(int on)
{
    mutex_lock(&s_lock, WAIT_FOREVER);
    int was = s_card.cmd23;
    s_card.cmd23 = on && s_card_cmd23;
    mutex_unlock(&s_lock);
    return was;
}

const char *sdcard_mode_name(int mode)
{
    return mode >= MODE_DS && mode <= MODE_SDR104 ? s_mode_names[mode] : "?";
}

/* ---- block I/O ----------------------------------------------------------------------------- */

/* the memories the IDMA reaches: AXI SRAM1-4 and the PSRAM, all cached but the PSRAM's last
 * 2 MB (platform_mpu_map) */
#define AXI_SRAM_BASE   0x24000000u
#define AXI_SRAM_END    0x24072000u
#define PSRAM_BASE      0x90000000u
#define PSRAM_NOCACHE   0x91E00000u
#define PSRAM_END       0x92000000u

static bool is_cached(uintptr_t a)
{
    return (a >= AXI_SRAM_BASE && a < AXI_SRAM_END) || (a >= PSRAM_BASE && a < PSRAM_NOCACHE);
}

/* Can the IDMA transfer directly to/from this buffer? Not from the TCMs (the core's own) or the
 * flash; writes only clean the cache (harmless for neighbours sharing a line); reads invalidate,
 * which would drop a neighbour's changes, so they need whole cache lines. */
static bool dma_direct(const void *buf, uint32_t bytes, bool write)
{
    uintptr_t a = (uintptr_t)buf;
    bool axi = a >= AXI_SRAM_BASE && a < AXI_SRAM_END;
    bool psram = a >= PSRAM_BASE && a < PSRAM_END;
    if (!axi && !psram)
        return false;
    if (a & 3u)
        return false;
    if (!write && is_cached(a) && ((a | bytes) & 31u))
        return false;
    return true;
}

static int xfer_blocks(bool write, uint32_t lba, uint8_t *buf, uint32_t n)
{
    struct sd_cmd c;
    struct sd_data d;
    memset(&c, 0, sizeof(c));
    uint32_t addr = s_card.sdhc ? lba : lba * SDCARD_BLOCK_SIZE;
    /* a known length (CMD23 first) lets the card write whole units; reads stay open-ended,
     * ended by CMD12 */
    bool pre = n > 1 && write && s_card.cmd23;
    if (pre && cmd(23, n, RSP_R1, nullptr, XF_R1_CHECK))
        return -EIO;
    c.index = write ? (n > 1 ? 25u : 24u) : (n > 1 ? 18u : 17u);
    c.arg = addr;
    c.rsp = RSP_R1;
    d.buf = buf;
    d.block_size = SDCARD_BLOCK_SIZE;
    d.blocks = n;
    d.write = write;

    uint32_t bytes = n * SDCARD_BLOCK_SIZE;
    bool cached = is_cached((uintptr_t)buf);
    if (cached) {
        if (write)
            SCB_CleanDCache_by_Addr(buf, (int32_t)bytes);
        else
            SCB_CleanInvalidateDCache_by_Addr(buf, (int32_t)bytes);
    }
    int r = transfer(&c, &d, XF_R1_CHECK);
    if (cached && !write)
        SCB_InvalidateDCache_by_Addr(buf, (int32_t)bytes); /* drop lines fetched speculatively */
    if (!r && n > 1 && !pre)
        r = stop_transmission(0);
    if (!r && write)
        r = wait_ready(1000);
    return r;
}

static int xfer_chunk(bool write, uint32_t lba, uint8_t *buf, uint32_t n, bool bounce)
{
    if (!bounce)
        return xfer_blocks(write, lba, buf, n);
    if (write)
        memcpy(s_bounce, buf, n * SDCARD_BLOCK_SIZE);
    int r = xfer_blocks(write, lba, s_bounce, n);
    if (!r && !write)
        memcpy(buf, s_bounce, n * SDCARD_BLOCK_SIZE);
    return r;
}

static int rw(bool write, uint32_t lba, uint8_t *buf, uint32_t count)
{
    if (!s_card.ready)
        return -ENODEV;
    if (count == 0)
        return 0;
    if (lba >= s_card.blocks || count > s_card.blocks - lba)
        return -EINVAL;
    mutex_lock(&s_lock, WAIT_FOREVER);
    int r = 0;
    while (count && !r) {
        bool bounce = !dma_direct(buf, SDCARD_BLOCK_SIZE, write);
        uint32_t max = bounce ? BOUNCE_BLOCKS : MAX_BLOCKS;
        uint32_t n = count > max ? max : count;
        for (int attempt = 1;; attempt++) {
            uint32_t t0 = cpu_cycles();
            r = xfer_chunk(write, lba, buf, n, bounce);
            if (!r) {
                hist_add(write, n, (cpu_cycles() - t0) / (SystemCoreClock / 1000000u));
                break;
            }
            s_stats.errors++;
            if (attempt == RETRIES || !s_card.ready)
                break;
            s_stats.retries++;
            if (r == -ETIMEDOUT && ++s_stats.lockups >= MAX_LOCKUPS && s_max_hz > 25000000u) {
                printk("W: sd: card keeps locking up above 25 MHz, slowing down\n");
                s_max_hz = 25000000u;
            }
            if ((r == -ETIMEDOUT || attempt > 1 || recover_card()) && reinit())
                break;
        }
        if (r)
            break;
        lba += n;
        buf += n * SDCARD_BLOCK_SIZE;
        count -= n;
        if (write)
            s_stats.writes += n;
        else
            s_stats.reads += n;
    }
    mutex_unlock(&s_lock);
    return r;
}

int sdcard_read(uint32_t lba, void *buf, uint32_t count)
{
    return rw(false, lba, (uint8_t *)buf, count);
}

int sdcard_write(uint32_t lba, const void *buf, uint32_t count)
{
    return rw(true, lba, (uint8_t *)buf, count);
}

void sdcard_get_info(struct sdcard_info *info)
{
    memset(info, 0, sizeof(*info));
    info->present = s_host_ready ? sdcard_present() : 0;
    info->ready = s_card.ready;
    info->high_capacity = s_card.sdhc;
    info->blocks = s_card.blocks;
    info->clock_hz = s_card.clock;
    info->bus_width = s_card.width;
    memcpy(info->cid, s_card.cid, sizeof(info->cid));
    info->reads = s_stats.reads;
    info->writes = s_stats.writes;
    info->errors = s_stats.errors;
    info->retries = s_stats.retries;
    info->reinits = s_stats.reinits;
    info->slow = s_stats.slow;
    info->max_us = s_stats.max_us;
    info->mode = s_card.mode;
    info->v18 = 0;
    info->tunings = 0;
}
