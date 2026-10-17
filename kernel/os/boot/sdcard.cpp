/*
 * kernel/os/boot/sdcard.cpp - built-in SD card driver on USDHC1 (EVKB micro SD slot).
 *
 * Transfers use ADMA2 and complete by interrupt: the calling thread sleeps on an event
 * while the controller moves data, so the CPU is free for other tasks. Buffers the DMA
 * cannot use directly (misaligned, or sharing cache lines) go through a bounce buffer.
 * The pins, card power and the clock root are set up here because this driver runs
 * before any device tree or pinctrl driver is available.
 *
 * Bus modes: default speed (25 MHz) and high speed (50 MHz) at 3.3 V signalling; with a UHS-I
 * card the lines are switched to 1.8 V (CMD11, the board's VSELECT regulator) and SDR50
 * (99 MHz) or SDR104 (198 MHz) is used, with the sampling point found by tuning (CMD19). A
 * card that fails in a fast mode is initialised again one mode lower.
 *
 * Errors: a failed transfer resets the controller's command/data logic. Block I/O then asks
 * the card for its state (CMD13), stops a transfer the card is still in (CMD12) and tries
 * again; when the card does not answer or fails again, the controller and the card are
 * initialised from scratch (slot power cycle). The last transfers are kept in a trace that
 * is printed with the first error of a series.
 */
#include "sdcard.h"
#include "kernel.h"
#include <crtos/irq.h>
#include <string.h>
#include "fsl_usdhc.h"
#include "fsl_gpio.h"
#include "fsl_iomuxc.h"
#include "fsl_clock.h"
#include "fsl_cache.h"

#define SD_BASE         USDHC1
#define SD_IRQ          USDHC1_IRQn
#define SD_IRQ_PRIO     6
#define SD_SRC_CLK      198000000u      /* PLL2 PFD0 396 MHz / 2 */
#define SD_CD_GPIO      GPIO2           /* card detect, low = inserted */
#define SD_CD_PIN       28u
#define SD_PWR_GPIO     GPIO1           /* slot power enable */
#define SD_PWR_PIN      5u

#define ADMA_WORDS      64u             /* 32 descriptors */
#define MAX_BLOCKS      2048u           /* per command (1 MB) */
#define BOUNCE_BLOCKS   8u
#define CMD_TIMEOUT_MS  500u
/* data phase timeout: base + per block (the SD spec allows 100 ms read access time and
 * 250/500 ms write busy per block; cards pause longer for internal housekeeping) */
#define READ_TIMEOUT_MS(n)  (500u + (n))
#define WRITE_TIMEOUT_MS(n) (1000u + 2u * (n))
#define DATA_TIMEOUT_HW 0xFu            /* hardware data timeout: the longest (> 2 s at 198 MHz); cards
                                         * stay busy for over 600 ms after some writes */
/* Some cards lock up - they stay in the data state and never send the block - when a command
 * follows the end of a read too closely at bus clocks above 25 MHz (seen with MID 0x12
 * "SDU1": 10 us of extra gap is not enough, 25 us is). Keep a margin. */
#define CMD_GAP_US      50u
#define MAX_LOCKUPS     3               /* card lock-ups before falling back to 25 MHz */
#define SLOW_US         250000u         /* data transfers slower than this are logged */
#define RETRIES         3
#define MODE_FAILURES   3               /* failed transfers before a UHS mode is given up */

#define EV_CMD_OK       1u
#define EV_CMD_FAIL     2u
#define EV_DATA_OK      4u
#define EV_DATA_FAIL    8u
#define EV_ALL          (EV_CMD_OK | EV_CMD_FAIL | EV_DATA_OK | EV_DATA_FAIL)

#define XF_QUIET        1u              /* a failure is expected (probing): no report */
#define XF_TUNING       2u              /* a tuning block (CMD19) */

/* bus modes, slowest first */
#define MODE_DS         0               /* default speed, 25 MHz */
#define MODE_HS         1               /* high speed (SDR25), 50 MHz */
#define MODE_SDR50      2               /* UHS-I, 1.8 V, 100 MHz, tuned */
#define MODE_SDR104     3               /* UHS-I, 1.8 V, 208 MHz (198 here), tuned */

/* ACMD41 argument / OCR bits */
#define OCR_BUSY        0x80000000u     /* initialisation finished */
#define OCR_HCS         0x40000000u     /* host supports high capacity / card is SDHC/SDXC */
#define OCR_XPC         0x10000000u     /* SDXC: maximum performance (not power saving) */
#define OCR_S18         0x01000000u     /* request / accept 1.8 V signalling */
#define OCR_VDD         0x00FF8000u     /* 2.7 - 3.6 V */

/* R1 card status */
#define R1_ERRORS       0xFFF98008u     /* out of range ... erase reset, AKE sequence */
#define R1_READY        (1u << 8)
#define R1_STATE(r)     (((r) >> 9) & 0xFu)
#define STATE_TRAN      4u
#define STATE_DATA      5u
#define STATE_RCV       6u
#define STATE_PRG       7u

/* DMA descriptors and the bounce buffer live in DTCM (never cached, reachable by DMA) */
static uint32_t s_adma[ADMA_WORDS] __attribute__((aligned(32)));
static uint8_t s_bounce[BOUNCE_BLOCKS * SDCARD_BLOCK_SIZE] __attribute__((aligned(32)));

static usdhc_handle_t s_handle;
static struct event s_ev;
static struct mutex s_lock;
static bool s_host_ready;
static uint32_t s_max_hz = 50000000u;         /* limit of the 3.3 V modes */
/* SDR104 (198 MHz) with a sampling window of ~27 delay cells on this board; a card that keeps
 * failing in it goes down to SDR50 (MODE_FAILURES) */
static int s_max_mode = MODE_SDR104;
static uint32_t s_mode_failures;            /* failed transfers in a UHS mode */

/* filled by the interrupt handler during a transfer */
static volatile uint32_t s_irq_seen;        /* every status flag seen */
static volatile uint32_t s_dma_err;         /* ADMA error status at a DMA error, bit 31 = valid */
static volatile uint32_t s_dma_err_addr;    /* ADMA system address at a DMA error */
static volatile uint32_t s_t_cmd, s_t_data; /* cycle counter at the command / data interrupt */
static uint32_t s_last_data;                /* cycle counter at the end of the last data transfer */
static bool s_gap;                          /* the next command must keep CMD_GAP_US from it */

/* averages for sdcard_timing() */
static uint64_t s_sum_cmd, s_sum_data, s_sum_total;
static uint32_t s_sum_n;

/* recent transfers */
struct trace {
    uint32_t ms;        /* tick at the end */
    uint32_t arg;
    uint32_t resp;
    uint32_t irq;       /* status flags seen */
    uint32_t us;        /* duration */
    uint16_t blocks;
    uint8_t cmd;
    int8_t result;
};
#define TRACE_N 16u
static struct trace s_trace[TRACE_N];
static uint32_t s_trace_n;
static uint32_t s_fail_run;     /* failed transfers since the last good data transfer */

static struct {
    bool ready;
    bool sdhc;
    bool v18;                   /* 1.8 V signalling */
    bool cmd23;                 /* SET_BLOCK_COUNT before multi-block writes */
    int mode;                   /* MODE_* */
    uint32_t rca;
    uint32_t blocks;
    uint32_t clock;
    int width;
    uint32_t cid[4];
} s_card;

static struct {
    uint32_t reads, writes, errors, retries, reinits, lockups, slow, max_us, tunings;
} s_stats;
static bool s_card_cmd23;                   /* the card has CMD23 (s_card.cmd23: it is used) */

/* ---- host -------------------------------------------------------------------------------- */

static void xfer_callback(USDHC_Type *, usdhc_handle_t *, status_t st, void *)
{
    uint32_t bits = 0;
    switch (st) {
    case kStatus_USDHC_SendCommandSuccess:
        s_t_cmd = cpu_cycles();
        bits = EV_CMD_OK;
        break;
    case kStatus_USDHC_SendCommandFailed:
        bits = EV_CMD_FAIL;
        break;
    case kStatus_USDHC_TransferDataComplete:
        s_t_data = cpu_cycles();
        bits = EV_DATA_OK;
        break;
    case kStatus_USDHC_TransferDataFailed:
        bits = EV_DATA_FAIL;
        break;
    default:
        break;
    }
    if (bits)
        event_set(&s_ev, bits);
}

static void sd_irq(int, void *)
{
    uint32_t st = SD_BASE->INT_STATUS;
    s_irq_seen |= st;
    if (st & SD_BASE->INT_SIGNAL_EN & (uint32_t)kUSDHC_DmaErrorFlag) {
        /* the SDK handler clears a DMA error without reporting it unless another data flag
         * comes with it, and the transfer would never finish: fail the data phase here */
        s_dma_err = SD_BASE->ADMA_ERR_STATUS | 0x80000000u;
        s_dma_err_addr = SD_BASE->ADMA_SYS_ADDR;
        event_set(&s_ev, EV_DATA_FAIL);
    }
    USDHC_TransferHandleIRQ(SD_BASE, &s_handle);
}

static void trace_add(const usdhc_command_t *c, const usdhc_data_t *d, int result, uint32_t us)
{
    struct trace *t = &s_trace[s_trace_n++ % TRACE_N];
    t->ms = tick_get();
    t->arg = c->argument;
    t->resp = c->response[0];
    t->irq = s_irq_seen;
    t->us = us;
    t->blocks = d ? (uint16_t)d->blockCount : 0u;
    t->cmd = (uint8_t)c->index;
    t->result = (int8_t)result;
}

/* Block commands by size, with the time they took: [read, write][1, 2-7, 8-63, 64-255, 256+ blocks] */
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
        pr("  %8lu ms  CMD%-2u arg %08lx blocks %-4u resp %08lx irq %08lx %7lu us  %d\n", (unsigned long)t->ms,
           (unsigned)t->cmd, (unsigned long)t->arg, (unsigned)t->blocks, (unsigned long)t->resp,
           (unsigned long)t->irq, (unsigned long)t->us, (int)t->result);
    }
}

static void report(const char *what, const usdhc_command_t *c, const usdhc_data_t *d, uint32_t us)
{
    USDHC_Type *b = SD_BASE;
    const void *buf = d ? (d->rxData ? (const void *)d->rxData : (const void *)d->txData) : nullptr;
    printk("E: sd: %s: CMD%lu arg %08lx blocks %lu resp %08lx after %lu us\n", what, (unsigned long)c->index,
           (unsigned long)c->argument, (unsigned long)(d ? d->blockCount : 0u), (unsigned long)c->response[0],
           (unsigned long)us);
    printk("E: sd:   irq %08lx int %08lx sig %08lx pres %08lx prot %08lx sys %08lx mix %08lx blk %08lx\n",
           (unsigned long)s_irq_seen, (unsigned long)b->INT_STATUS, (unsigned long)b->INT_SIGNAL_EN,
           (unsigned long)b->PRES_STATE, (unsigned long)b->PROT_CTRL, (unsigned long)b->SYS_CTRL,
           (unsigned long)b->MIX_CTRL, (unsigned long)b->BLK_ATT);
    printk("E: sd:   adma %08lx @%08lx dma-error %08lx @%08lx desc %08lx %08lx buf %08lx wml %08lx\n",
           (unsigned long)b->ADMA_ERR_STATUS, (unsigned long)b->ADMA_SYS_ADDR, (unsigned long)s_dma_err,
           (unsigned long)s_dma_err_addr, (unsigned long)s_adma[0], (unsigned long)s_adma[1],
           (unsigned long)(uintptr_t)buf, (unsigned long)b->WTMK_LVL);
    printk("E: sd:   vend %08lx %08lx clk-tune %08lx dll %08lx\n", (unsigned long)b->VEND_SPEC,
           (unsigned long)b->VEND_SPEC2, (unsigned long)b->CLK_TUNE_CTRL_STATUS, (unsigned long)b->DLL_CTRL);
    /* do the DAT lines move at all while the controller waits? */
    uint32_t lo = 0xFFu, t0 = cpu_cycles();
    for (uint32_t n = 0; cpu_cycles() - t0 < SystemCoreClock / 100u && n < SystemCoreClock / 100u; n++)
        lo &= b->PRES_STATE >> 24;
    printk("E: sd:   DAT lines seen low within 10 ms: %02lx\n", (unsigned long)(~lo & 0xFFu));
    if (s_fail_run == 0) {
        printk("E: sd: recent transfers:\n");
        sdcard_dump_trace(printk);
    }
}

/* Reset the controller's command (and data) logic after a failed transfer */
static void set_sampling(bool tuned, uint32_t cell);
static int s_tune_cell = -1;                /* the sampling delay found by tuning, -1: none */

static void reset_lines(bool data)
{
    uint32_t dtw = SD_BASE->PROT_CTRL & USDHC_PROT_CTRL_DTW_MASK;
    uint32_t mix = SD_BASE->MIX_CTRL & (USDHC_MIX_CTRL_SMP_CLK_SEL_MASK | USDHC_MIX_CTRL_FBCLK_SEL_MASK);
    USDHC_DisableInterruptSignal(SD_BASE, kUSDHC_AllInterruptFlags);
    if (!USDHC_Reset(SD_BASE, (uint32_t)kUSDHC_ResetCommand | (data ? (uint32_t)kUSDHC_ResetData : 0u), 100000u))
        printk("E: sd: controller reset timeout\n");
    /* some eSDHC versions lose the bus width with the data line reset */
    if ((SD_BASE->PROT_CTRL & USDHC_PROT_CTRL_DTW_MASK) != dtw) {
        printk("W: sd: bus width lost in the data reset\n");
        SD_BASE->PROT_CTRL = (SD_BASE->PROT_CTRL & ~USDHC_PROT_CTRL_DTW_MASK) | dtw;
    }
    /* nor may the tuned sampling point go */
    if (mix && s_tune_cell >= 0 && (SD_BASE->MIX_CTRL & mix) != mix)
        set_sampling(true, (uint32_t)s_tune_cell);
    USDHC_ClearInterruptStatusFlags(SD_BASE, kUSDHC_AllInterruptFlags);
}

static int tune(void);

static int transfer(usdhc_command_t *cmd, usdhc_data_t *data, uint32_t flags = 0)
{
    /* in a tuned mode the controller may ask for the sampling point to be found again */
    if (s_card.mode >= MODE_SDR50 && !(flags & XF_TUNING) &&
        (USDHC_GetInterruptStatusFlags(SD_BASE) & (uint32_t)kUSDHC_ReTuningEventFlag)) {
        USDHC_ClearInterruptStatusFlags(SD_BASE, kUSDHC_ReTuningEventFlag);
        tune();
    }
    usdhc_adma_config_t dma;
    memset(&dma, 0, sizeof(dma));
    dma.dmaMode = kUSDHC_DmaModeAdma2;
    dma.burstLen = kUSDHC_EnBurstLenForINCR;
    dma.admaTable = s_adma;
    dma.admaTableWords = ADMA_WORDS;
    usdhc_transfer_t xfer;
    xfer.command = cmd;
    xfer.data = data;

    if (s_gap) {
        /* bounded by an iteration count as well: the cycle counter stops when a debugger
         * clears DEMCR, and this must not wait forever then */
        uint32_t gap = CMD_GAP_US * (SystemCoreClock / 1000000u);
        for (uint32_t n = 0; cpu_cycles() - s_last_data < gap && n < gap; n++)
            ;
        s_gap = false;
    }
    s_irq_seen = 0;
    s_dma_err = 0;
    s_dma_err_addr = 0;
    event_clear(&s_ev, EV_ALL);
    uint32_t t0 = cpu_cycles();
    const char *what = nullptr;
    int r = 0;
    if (USDHC_TransferNonBlocking(SD_BASE, &s_handle, &dma, &xfer) != kStatus_Success) {
        what = "start failed";
        r = -EIO;
    } else {
        /* a tuning block at a bad sampling point may never arrive: give up soon */
        uint32_t cmd_tmo = flags & XF_TUNING ? 10u : CMD_TIMEOUT_MS;
        int32_t ev = event_wait(&s_ev, EV_CMD_OK | EV_CMD_FAIL, EVENT_ANY, cmd_tmo);
        if (ev < 0 || (ev & EV_CMD_FAIL)) {
            what = ev < 0 ? "command timeout" : "command failed";
            r = ev < 0 ? -ETIMEDOUT : -EIO;
        } else if (data) {
            uint32_t tmo = data->txData ? WRITE_TIMEOUT_MS(data->blockCount) : READ_TIMEOUT_MS(data->blockCount);
            if (flags & XF_TUNING)
                tmo = 10u;
            ev = event_wait(&s_ev, EV_DATA_OK | EV_DATA_FAIL, EVENT_ANY, tmo);
            if (ev < 0) {
                what = "data timeout";
                r = -ETIMEDOUT;
            } else if (ev & EV_DATA_FAIL) {
                bool tmo_hw = (s_irq_seen & (uint32_t)kUSDHC_DataTimeoutFlag) != 0;
                what = s_dma_err ? "DMA error" : (tmo_hw ? "data timeout (controller)" : "data error");
                r = tmo_hw ? -ETIMEDOUT : -EIO;
            }
        }
    }
    uint32_t cycles = cpu_cycles() - t0;
    uint32_t us = cycles / (SystemCoreClock / 1000000u);
    trace_add(cmd, data, r, us);
    if (r) {
        if (!(flags & XF_QUIET)) {
            report(what, cmd, data, us);
            s_fail_run++;
        }
        reset_lines(data != nullptr || cmd->responseType == kCARD_ResponseTypeR1b);
        return r;
    }
    if (data) {
        s_fail_run = 0;
        s_last_data = s_t_data;
        s_gap = true;
        s_sum_cmd += s_t_cmd - t0;
        s_sum_data += s_t_data - t0;
        s_sum_total += cycles;
        s_sum_n++;
        if (us > s_stats.max_us)
            s_stats.max_us = us;
        if (us > SLOW_US) {
            s_stats.slow++;
            printk("W: sd: CMD%lu arg %08lx (%lu blocks) took %lu ms\n", (unsigned long)cmd->index,
                   (unsigned long)cmd->argument, (unsigned long)data->blockCount, (unsigned long)(us / 1000u));
        }
    }
    return 0;
}

/* Average timing of data transfers since the last call (microseconds) */
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

static int cmd(uint32_t index, uint32_t arg, usdhc_card_response_type_t rsp, uint32_t *resp, uint32_t flags = 0)
{
    usdhc_command_t c;
    memset(&c, 0, sizeof(c));
    c.index = index;
    c.argument = arg;
    c.type = kCARD_CommandTypeNormal;
    c.responseType = rsp;
    int r = transfer(&c, nullptr, flags);
    if (!r && resp)
        memcpy(resp, c.response, sizeof(c.response));
    return r;
}

static int app_cmd(uint32_t index, uint32_t arg, usdhc_card_response_type_t rsp, uint32_t *resp, uint32_t flags = 0)
{
    int r = cmd(55, s_card.rca << 16, kCARD_ResponseTypeR1, nullptr, flags);
    return r ? r : cmd(index, arg, rsp, resp, flags);
}

static int card_status(uint32_t *status, uint32_t flags = 0)
{
    uint32_t resp[4];
    int r = cmd(13, s_card.rca << 16, kCARD_ResponseTypeR1, resp, flags);
    if (!r)
        *status = resp[0];
    return r;
}

/* Wait until the card left the busy state (DAT0 high) */
static int wait_dat0(uint32_t timeout_ms)
{
    uint32_t t0 = tick_get();
    while (!(USDHC_GetPresentStatusFlags(SD_BASE) & kUSDHC_Data0LineLevelFlag)) {
        if (tick_get() - t0 > timeout_ms)
            return -ETIMEDOUT;
        task_sleep_ms(1);
    }
    return 0;
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
static void stop_transmission(void)
{
    usdhc_command_t c;
    memset(&c, 0, sizeof(c));
    c.index = 12;
    c.type = kCARD_CommandTypeAbort;
    c.responseType = kCARD_ResponseTypeR1b;
    transfer(&c, nullptr, XF_QUIET);
    wait_dat0(500);
    reset_lines(true); /* the controller expects a data reset after an abort command */
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

/* Pad settings of the bus pins: edge rate (SPEED 0-3), drive strength (DSE 1-7), slew */
static struct {
    uint8_t speed, dse, fast_slew;
} s_pads = { 0, 7, 1 };

static void set_pads(void)
{
    /* the edge rate follows the bus clock in the UHS modes (SDR50 100 MHz, SDR104 200 MHz) */
    uint32_t speed = s_card.mode == MODE_SDR104 ? 3u : (s_card.mode == MODE_SDR50 ? 2u : s_pads.speed);
    uint32_t common = IOMUXC_SW_PAD_CTL_PAD_SPEED(speed) | IOMUXC_SW_PAD_CTL_PAD_DSE(s_pads.dse) |
                      IOMUXC_SW_PAD_CTL_PAD_HYS_MASK | (s_pads.fast_slew ? IOMUXC_SW_PAD_CTL_PAD_SRE_MASK : 0u);
    uint32_t data_cfg = common | IOMUXC_SW_PAD_CTL_PAD_PKE_MASK | IOMUXC_SW_PAD_CTL_PAD_PUE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_PUS(1U); /* 47k pull-up */
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_00_USDHC1_CMD, data_cfg);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_01_USDHC1_CLK, common);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_02_USDHC1_DATA0, data_cfg);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_03_USDHC1_DATA1, data_cfg);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_04_USDHC1_DATA2, data_cfg);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_05_USDHC1_DATA3, data_cfg);
}

static void init_pins(void)
{
    CLOCK_EnableClock(kCLOCK_Iomuxc);
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_00_USDHC1_CMD, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_01_USDHC1_CLK, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_02_USDHC1_DATA0, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_03_USDHC1_DATA1, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_04_USDHC1_DATA2, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_05_USDHC1_DATA3, 0U);
    set_pads();

    /* card detect: GPIO2_IO28 with pull-up */
    IOMUXC_SetPinMux(IOMUXC_GPIO_B1_12_GPIO2_IO28, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_12_GPIO2_IO28,
                        IOMUXC_SW_PAD_CTL_PAD_SPEED(1U) | IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
                            IOMUXC_SW_PAD_CTL_PAD_PUE_MASK | IOMUXC_SW_PAD_CTL_PAD_HYS_MASK |
                            IOMUXC_SW_PAD_CTL_PAD_PUS(1U));
    gpio_pin_config_t in = { kGPIO_DigitalInput, 0U, kGPIO_NoIntmode };
    GPIO_PinInit(SD_CD_GPIO, SD_CD_PIN, &in);

    /* slot power: GPIO1_IO05. Keep it on: the card detect line is pulled up to the switched
     * slot supply, so it reads "inserted" while the slot is unpowered. */
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_05_GPIO1_IO05, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_05_GPIO1_IO05, 0x10B0U);
    gpio_pin_config_t out = { kGPIO_DigitalOutput, 1U, kGPIO_NoIntmode };
    GPIO_PinInit(SD_PWR_GPIO, SD_PWR_PIN, &out);

    IOMUXC_SetPinMux(IOMUXC_GPIO_B1_14_USDHC1_VSELECT, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_14_USDHC1_VSELECT, 0x0170A1U);
}

/* Bus clock = SD_SRC_CLK / prescaler / divisor. USDHC_SetSdClock() can pick a prescaler that
 * is not a power of two (e.g. for /6), which the hardware does not support. */
static uint32_t set_clock(uint32_t hz)
{
    uint32_t best = 0, best_pre = 256, best_div = 16;
    for (uint32_t pre = 1; pre <= 256; pre <<= 1)
        for (uint32_t div = 1; div <= 16; div++) {
            uint32_t f = SD_SRC_CLK / (pre * div);
            if (f <= hz && f > best) {
                best = f;
                best_pre = pre;
                best_div = div;
            }
        }
    if (!best)
        best = SD_SRC_CLK / (best_pre * best_div);
    uint32_t sys = SD_BASE->SYS_CTRL & ~(USDHC_SYS_CTRL_DVS_MASK | USDHC_SYS_CTRL_SDCLKFS_MASK);
    SD_BASE->SYS_CTRL = sys | USDHC_SYS_CTRL_DVS(best_div - 1u) | USDHC_SYS_CTRL_SDCLKFS(best_pre >> 1);
    uint32_t t0 = cpu_cycles();
    for (uint32_t n = 0; !(SD_BASE->PRES_STATE & USDHC_PRES_STATE_SDSTB_MASK) && cpu_cycles() - t0 < SystemCoreClock / 100u &&
                         n < SystemCoreClock / 100u;
         n++)
        ;
    return best;
}

/* Reset the whole controller and program its static configuration */
static void host_config(void)
{
    usdhc_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.dataTimeout = DATA_TIMEOUT_HW;
    cfg.endianMode = kUSDHC_EndianModeLittle;
    /* half the 128-word buffer: with a full-buffer watermark the controller stops SDCLK at
     * the end of each block and occasionally never restarts it (card left in the data
     * state, transfer never completes) */
    cfg.readWatermarkLevel = 64U;
    cfg.writeWatermarkLevel = 64U;
    cfg.readBurstLen = 16U;
    cfg.writeBurstLen = 16U;
    USDHC_Reset(SD_BASE, kUSDHC_ResetAll, 100000u);
    USDHC_Init(SD_BASE, &cfg);
}

static void init_host(void)
{
    CLOCK_EnableClock(kCLOCK_Usdhc1);
    CLOCK_InitSysPfd(kCLOCK_Pfd0, 24U); /* 528 MHz * 18 / 24 = 396 MHz */
    CLOCK_SetDiv(kCLOCK_Usdhc1Div, 1U); /* / 2 */
    CLOCK_SetMux(kCLOCK_Usdhc1Mux, 1U); /* PLL2 PFD0 */
    host_config();

    static const usdhc_transfer_callback_t cb = { nullptr, nullptr, nullptr, nullptr, xfer_callback, nullptr };
    if (irq_request(SD_IRQ, sd_irq, nullptr, SD_IRQ_PRIO, "usdhc1"))
        panic("USDHC1 IRQ busy");
    USDHC_TransferCreateHandle(SD_BASE, &s_handle, &cb, nullptr);
}

int sdcard_present(void)
{
    return GPIO_PinRead(SD_CD_GPIO, SD_CD_PIN) == 0U;
}

/* Card detect must stay asserted for a while (contact bounce while inserting); the line
 * also needs a moment to settle after the pad and the slot power were configured */
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

/* ACMD51: the SD configuration register (8 bytes, MSB first) - which commands the card has */
static void read_scr(void)
{
    if (cmd(55, s_card.rca << 16, kCARD_ResponseTypeR1, nullptr, XF_QUIET))
        return;
    usdhc_command_t c;
    usdhc_data_t d;
    memset(&c, 0, sizeof(c));
    memset(&d, 0, sizeof(d));
    c.index = 51;
    c.type = kCARD_CommandTypeNormal;
    c.responseType = kCARD_ResponseTypeR1;
    c.responseErrorFlags = R1_ERRORS;
    d.blockSize = 8;
    d.blockCount = 1;
    d.rxData = (uint32_t *)s_bounce;
    if (transfer(&c, &d, XF_QUIET))
        return;
    s_card_cmd23 = (s_bounce[3] >> 1) & 1u; /* SCR bit 33: CMD23 supported */
    s_card.cmd23 = s_card_cmd23;
}

/* ---- bus modes ------------------------------------------------------------------------------ */

static const char *const s_mode_names[] = { "default speed", "high speed", "SDR50", "SDR104" };
static const uint32_t s_mode_hz[] = { 25000000u, 50000000u, 100000000u, 208000000u };

/* CMD6 for group 1 (bus speed): check (@set false) or switch to function @fn (= MODE_*);
 * the 64-byte status arrives in s_bounce, MSB first */
static int switch_func(bool set, uint32_t fn)
{
    usdhc_command_t c;
    usdhc_data_t d;
    memset(&c, 0, sizeof(c));
    memset(&d, 0, sizeof(d));
    c.index = 6;
    c.argument = (set ? 0x80000000u : 0u) | 0x00FFFFF0u | (fn & 0xFu); /* other groups unchanged */
    c.type = kCARD_CommandTypeNormal;
    c.responseType = kCARD_ResponseTypeR1;
    c.responseErrorFlags = R1_ERRORS;
    d.blockSize = 64;
    d.blockCount = 1;
    d.rxData = (uint32_t *)s_bounce;
    return transfer(&c, &d, XF_QUIET);
}

/* CMD11: card and host move to 1.8 V signalling (SD spec 4.2.4.2). The board's regulator
 * follows the controller's VSELECT output. */
static int switch_voltage(void)
{
    uint32_t resp[4];
    if (cmd(11, 0, kCARD_ResponseTypeR1, resp, XF_QUIET))
        return -EIO;
    /* the card holds DAT[3:0] low until the switch is done */
    if ((USDHC_GetPresentStatusFlags(SD_BASE) >> USDHC_PRES_STATE_DLSL_SHIFT) & 0xFu)
        return -EIO;
    UDSHC_SelectVoltage(SD_BASE, true);
    task_sleep_ms(10); /* at least 5 ms; the regulator settles */
    USDHC_ForceClockOn(SD_BASE, true); /* the card finishes with the clock running */
    int r = -EIO;
    for (uint32_t t0 = tick_get(); tick_get() - t0 < 100u;) {
        task_sleep_ms(1);
        if (((USDHC_GetPresentStatusFlags(SD_BASE) >> USDHC_PRES_STATE_DLSL_SHIFT) & 0xFu) == 0xFu) {
            r = 0;
            break;
        }
    }
    USDHC_ForceClockOn(SD_BASE, false);
    return r;
}

/* The tuning block a card sends for CMD19 on a 4-bit bus (SD spec 4.2.4.5) */
static const uint8_t s_tuning_pattern[64] = {
    0xff, 0x0f, 0xff, 0x00, 0xff, 0xcc, 0xc3, 0xcc, 0xc3, 0x3c, 0xcc, 0xff, 0xfe, 0xff, 0xfe, 0xef,
    0xff, 0xdf, 0xff, 0xdd, 0xff, 0xfb, 0xff, 0xfb, 0xbf, 0xff, 0x7f, 0xff, 0x77, 0xf7, 0xbd, 0xef,
    0xff, 0xf0, 0xff, 0xf0, 0x0f, 0xfc, 0xcc, 0x3c, 0xcc, 0x33, 0xcc, 0xcf, 0xff, 0xef, 0xff, 0xee,
    0xff, 0xfd, 0xff, 0xfd, 0xdf, 0xff, 0xbf, 0xff, 0xbb, 0xff, 0xf7, 0xff, 0xf7, 0x7f, 0x7b, 0xde,
};

#define TUNE_CELLS      128u            /* sampling delay cells (CLK_TUNE_CTRL_STATUS DLY_CELL_SET_PRE) */
#define TUNE_MIN_WINDOW 4u              /* fewer passing cells than this: the mode is not used */

/* Sample with the tuned clock, delayed by @cell (0: back to the plain clock) */
static void set_sampling(bool tuned, uint32_t cell)
{
    s_tune_cell = tuned ? (int)cell : -1;
    uint32_t tune = USDHC_MIX_CTRL_SMP_CLK_SEL_MASK | USDHC_MIX_CTRL_FBCLK_SEL_MASK;
    SD_BASE->MIX_CTRL = tuned ? SD_BASE->MIX_CTRL | tune : SD_BASE->MIX_CTRL & ~(tune | USDHC_MIX_CTRL_EXE_TUNE_MASK);
    USDHC_SetTuningDelay(SD_BASE, tuned ? cell : 0u, 0u, 0u);
    SDK_DelayAtLeastUs(10u, SystemCoreClock); /* the delay line settles */
    /* the status half of the register shows the delay once it is in use */
    for (int n = 0; n < 10000; n++)
        if (((SD_BASE->CLK_TUNE_CTRL_STATUS >> 16) & 0x7FFFu) == (SD_BASE->CLK_TUNE_CTRL_STATUS & 0x7FFFu))
            break;
}

/* One tuning block, compared with the pattern */
static bool tuning_block_ok(void)
{
    usdhc_command_t c;
    usdhc_data_t d;
    memset(&c, 0, sizeof(c));
    memset(&d, 0, sizeof(d));
    c.index = 19;
    c.type = kCARD_CommandTypeNormal;
    c.responseType = kCARD_ResponseTypeR1;
    d.blockSize = sizeof(s_tuning_pattern);
    d.blockCount = 1;
    d.rxData = (uint32_t *)s_bounce;
    memset(s_bounce, 0, sizeof(s_tuning_pattern));
    return transfer(&c, &d, XF_QUIET | XF_TUNING) == 0 && !memcmp(s_bounce, s_tuning_pattern, sizeof(s_tuning_pattern));
}

/* Find the sampling point (SDR50, SDR104): every delay cell is tried with a tuning block and the
 * middle of the widest run of good cells is kept (the way Linux tunes this controller). The
 * controller's own "standard tuning" never completes a CMD19 on this board. */
static int tune(void)
{
    s_stats.tunings++;
    USDHC_EnableAutoTuning(SD_BASE, false);
    SD_BASE->TUNING_CTRL &= ~USDHC_TUNING_CTRL_STD_TUNING_EN_MASK;
    uint32_t best_start = 0, best_len = 0, start = 0, len = 0;
    for (uint32_t cell = 0; cell < TUNE_CELLS; cell++) {
        set_sampling(true, cell);
        SD_BASE->MIX_CTRL |= USDHC_MIX_CTRL_EXE_TUNE_MASK; /* manual tuning in progress */
        if (tuning_block_ok()) {
            if (!len)
                start = cell;
            len++;
        } else {
            len = 0;
        }
        if (len > best_len) {
            best_len = len;
            best_start = start;
        }
    }
    SD_BASE->MIX_CTRL &= ~USDHC_MIX_CTRL_EXE_TUNE_MASK;
    if (best_len < TUNE_MIN_WINDOW) {
        set_sampling(false, 0);
        printk("W: sd: tuning found no sampling window (widest %lu cells)\n", (unsigned long)best_len);
        return -EIO;
    }
    uint32_t cell = best_start + best_len / 2;
    set_sampling(true, cell);
    reset_lines(true);
    int bad = 0;
    for (int i = 0; i < 8; i++) /* the chosen point must be solid */
        bad += !tuning_block_ok();
    printk("sd: tuned: delay cells %lu-%lu pass, using %lu%s\n", (unsigned long)best_start,
           (unsigned long)(best_start + best_len - 1), (unsigned long)cell, bad ? " - but it fails" : "");
    return bad ? -EIO : 0;
}

/* The fastest bus mode that the card, the signalling voltage and the limits allow. -EAGAIN:
 * tuning failed - s_max_mode is lowered and the card must be initialised again (after many
 * tuning blocks at bad sampling points a card does not always answer properly) */
static int select_mode(void)
{
    uint32_t support = 0x3u; /* default and high speed, if the card does not say */
    if (switch_func(false, 0xFu) == 0)
        support = ((uint32_t)s_bounce[12] << 8) | s_bounce[13]; /* bits 415:400, function n = bit n */
    int top = s_card.v18 ? s_max_mode : (s_max_mode < MODE_HS ? s_max_mode : MODE_HS);
    if (!s_card.v18 && s_max_hz <= 25000000u)
        top = MODE_DS;
    for (int m = top; m > MODE_DS; m--) {
        if (!(support & (1u << m)))
            continue;
        if (switch_func(true, (uint32_t)m) || (s_bounce[16] & 0x0Fu) != (uint32_t)m) /* bits 379:376 */
            continue;
        task_sleep_ms(1);
        s_card.mode = m;
        set_pads();
        uint32_t hz = s_mode_hz[m];
        if (m <= MODE_HS && s_max_hz < hz)
            hz = s_max_hz;
        s_card.clock = set_clock(hz);
        if (m >= MODE_SDR50 && tune()) {
            printk("W: sd: tuning failed in %s mode\n", s_mode_names[m]);
            set_sampling(false, 0);
            s_max_mode = m - 1;
            return -EAGAIN;
        }
        return 0;
    }
    s_card.mode = MODE_DS;
    set_pads();
    s_card.clock = set_clock(s_max_hz < 25000000u ? s_max_hz : 25000000u);
    return 0;
}

/* Initialise the card; @uhs: offer 1.8 V signalling. -EAGAIN: the voltage switch or the tuning
 * failed (try again: s_max_mode says how) */
static int init_card(bool uhs)
{
    uint32_t resp[4];
    memset(&s_card, 0, sizeof(s_card));
    set_pads();
    /* a card always starts with 3.3 V signalling; no tuning, no DDR left from before */
    UDSHC_SelectVoltage(SD_BASE, false);
    USDHC_EnableAutoTuning(SD_BASE, false);
    SD_BASE->TUNING_CTRL &= ~USDHC_TUNING_CTRL_STD_TUNING_EN_MASK;
    set_sampling(false, 0);

    /* power cycle the slot */
    GPIO_PinWrite(SD_PWR_GPIO, SD_PWR_PIN, 0U);
    task_sleep_ms(20);
    GPIO_PinWrite(SD_PWR_GPIO, SD_PWR_PIN, 1U);
    task_sleep_ms(40);

    USDHC_SetDataBusWidth(SD_BASE, kUSDHC_DataBusWidth1Bit);
    set_clock(400000U);
    USDHC_SetCardActive(SD_BASE, 1000U); /* 80 initialisation clocks */

    cmd(0, 0, kCARD_ResponseTypeNone, nullptr, XF_QUIET);
    task_sleep_ms(1);
    bool v2 = cmd(8, 0x1AAu, kCARD_ResponseTypeR7, resp, XF_QUIET) == 0 && (resp[0] & 0xFFu) == 0xAAu;

    uint32_t ocr = 0;
    uint32_t arg = OCR_VDD | (v2 ? OCR_HCS | OCR_XPC : 0u) | (v2 && uhs ? OCR_S18 : 0u);
    uint32_t t0 = tick_get();
    do {
        if (app_cmd(41, arg, kCARD_ResponseTypeR3, resp, XF_QUIET) == 0) {
            ocr = resp[0];
            if (ocr & OCR_BUSY)
                break;
        }
        task_sleep_ms(5);
    } while (tick_get() - t0 < 1500);
    if (!(ocr & OCR_BUSY))
        return -ETIMEDOUT;
    s_card.sdhc = v2 && (ocr & OCR_HCS);
    if (uhs && s_card.sdhc && (ocr & OCR_S18)) { /* a UHS-I card: 1.8 V now, before CMD2 */
        if (switch_voltage())
            return -EAGAIN;
        s_card.v18 = true;
    }

    if (cmd(2, 0, kCARD_ResponseTypeR2, s_card.cid))
        return -EIO;
    if (cmd(3, 0, kCARD_ResponseTypeR6, resp))
        return -EIO;
    s_card.rca = resp[0] >> 16;

    if (cmd(9, s_card.rca << 16, kCARD_ResponseTypeR2, resp))
        return -EIO;
    /* R2 as stored by the USDHC driver: resp[3] = CSD[127:96] ... resp[0] = CSD[31:0] (CRC dropped) */
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

    if (cmd(7, s_card.rca << 16, kCARD_ResponseTypeR1b, nullptr))
        return -EIO;
    wait_dat0(500);
    if (!s_card.sdhc)
        cmd(16, SDCARD_BLOCK_SIZE, kCARD_ResponseTypeR1, nullptr);

    s_card.width = 1;
    if (app_cmd(6, 2, kCARD_ResponseTypeR1, nullptr) == 0) { /* 4-bit bus */
        USDHC_SetDataBusWidth(SD_BASE, kUSDHC_DataBusWidth4Bit);
        s_card.width = 4;
    }
    read_scr();
    if (select_mode())
        return -EAGAIN;
    s_card.ready = true;
    return 0;
}

/* With 1.8 V signalling if allowed, else (or if that fails) with 3.3 V */
static int init_card_any(void)
{
    int r = -EAGAIN;
    for (int tries = 0; r == -EAGAIN && tries < 4; tries++) {
        int mode = s_max_mode;
        r = init_card(s_max_mode >= MODE_SDR50);
        if (r == -EAGAIN && s_max_mode == mode) { /* not the tuning: the switch to 1.8 V failed */
            printk("W: sd: the switch to 1.8 V failed, staying at 3.3 V\n");
            s_max_mode = MODE_HS;
        }
    }
    return r;
}

static void report_mode(void)
{
    printk("sd: %d-bit bus, %s, %lu MHz, %s signalling%s\n", s_card.width, s_mode_names[s_card.mode],
           (unsigned long)(s_card.clock / 1000000u), s_card.v18 ? "1.8 V" : "3.3 V",
           s_card.cmd23 ? ", CMD23" : "");
}

/* Start over: controller reset, slot power cycle, card initialisation */
static int reinit(void)
{
    s_stats.reinits++;
    printk("W: sd: re-initialising the card\n");
    host_config();
    int r = init_card_any();
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
    int r = init_card_any();
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

/* Change the bus pad settings (signal integrity experiments) */
void sdcard_set_pads(uint32_t speed, uint32_t dse, uint32_t fast_slew)
{
    mutex_lock(&s_lock, WAIT_FOREVER);
    s_pads.speed = (uint8_t)(speed & 3u);
    s_pads.dse = (uint8_t)(dse < 1u ? 1u : (dse > 7u ? 7u : dse));
    s_pads.fast_slew = fast_slew ? 1u : 0u;
    set_pads();
    mutex_unlock(&s_lock);
}

/* Limit the bus clock and initialise the card again (above 25 MHz: high speed mode; the UHS
 * modes are left out) */
uint32_t sdcard_set_clock(uint32_t hz)
{
    mutex_lock(&s_lock, WAIT_FOREVER);
    s_max_hz = hz < 400000u ? 400000u : (hz > 50000000u ? 50000000u : hz);
    s_max_mode = MODE_HS;
    reinit();
    uint32_t r = s_card.clock;
    mutex_unlock(&s_lock);
    return r;
}

/* The fastest bus mode to use (0 default speed ... 3 SDR104); initialises the card again */
int sdcard_set_mode(int mode)
{
    mutex_lock(&s_lock, WAIT_FOREVER);
    s_max_mode = mode < MODE_DS ? MODE_DS : (mode > MODE_SDR104 ? MODE_SDR104 : mode);
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

static bool is_cached(uintptr_t a)
{
    return (a >= 0x20200000u && a < 0x20240000u) || (a >= 0x80000000u && a < 0x81E00000u);
}

/* Can the ADMA engine transfer directly to/from this buffer? Writes only clean the cache
 * (harmless for neighbours sharing a line); reads invalidate, which would drop a neighbour's
 * changes, so they need whole cache lines. */
static bool dma_direct(const void *buf, uint32_t bytes, bool write)
{
    uintptr_t a = (uintptr_t)buf;
    if (a >= 0x60000000u && a < 0x70000000u) /* flash: read only */
        return false;
    if (a & 3u) /* ADMA2 needs word alignment */
        return false;
    if (!write && is_cached(a) && ((a | bytes) & 31u))
        return false;
    return true;
}

static int xfer_blocks(bool write, uint32_t lba, uint8_t *buf, uint32_t n)
{
    usdhc_command_t c;
    usdhc_data_t d;
    memset(&c, 0, sizeof(c));
    memset(&d, 0, sizeof(d));
    c.index = write ? (n > 1 ? 25u : 24u) : (n > 1 ? 18u : 17u);
    c.argument = s_card.sdhc ? lba : lba * SDCARD_BLOCK_SIZE;
    c.type = kCARD_CommandTypeNormal;
    c.responseType = kCARD_ResponseTypeR1;
    c.responseErrorFlags = R1_ERRORS;
    /* a known length (CMD23 first) lets the card write whole units (about 30 % faster); reads
     * stay open-ended, ended by CMD12 - with CMD23 they were slower on the cards tried */
    d.enableAutoCommand23 = n > 1 && write && s_card.cmd23;
    d.enableAutoCommand12 = n > 1 && !d.enableAutoCommand23;
    d.blockSize = SDCARD_BLOCK_SIZE;
    d.blockCount = n;
    if (write)
        d.txData = (const uint32_t *)buf;
    else
        d.rxData = (uint32_t *)buf;

    uint32_t bytes = n * SDCARD_BLOCK_SIZE;
    bool cached = is_cached((uintptr_t)buf);
    if (cached) {
        if (write)
            DCACHE_CleanByRange((uint32_t)buf, bytes);
        else
            DCACHE_CleanInvalidateByRange((uint32_t)buf, bytes);
    }
    int r = transfer(&c, &d);
    if (cached && !write)
        DCACHE_InvalidateByRange((uint32_t)buf, bytes); /* drop lines fetched speculatively */
    if (!r && write)
        r = wait_ready(1000);
    return r;
}

/* One command's worth of blocks, directly or through the bounce buffer */
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
            /* A card that stopped in the middle of a transfer (timeout) does not recover with
             * CMD12, only with a power cycle. Otherwise the first retry just gets the card back
             * to the transfer state. */
            if (s_card.mode >= MODE_SDR50 && ++s_mode_failures >= MODE_FAILURES) {
                /* a fast mode that keeps failing is given up: the card starts again one lower */
                s_mode_failures = 0;
                s_max_mode = s_card.mode - 1;
                printk("W: sd: too many errors in %s mode, going down to %s\n", s_mode_names[s_card.mode],
                       s_mode_names[s_max_mode]);
                r = -ETIMEDOUT; /* re-initialise now */
            } else if (r == -ETIMEDOUT && ++s_stats.lockups >= MAX_LOCKUPS && s_max_hz > 25000000u) {
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
    info->v18 = s_card.v18;
    info->tunings = s_stats.tunings;
}
