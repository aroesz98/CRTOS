/*
 * kernel/rtos/console_lpuart.cpp - built-in console on LPUART1 (1 Mbaud, 8N1).
 *
 * TX drains the printk ring from the TX FIFO interrupt; RX fills a ring read by whoever has
 * the input focus: the kernel monitor (CON_KMON) or the user terminal /dev/console
 * (CON_TTY). Ctrl-] typed while the terminal has the focus gives it to kmon; this is done
 * in the interrupt, so it works even when no program reads the terminal. The pins and the
 * UART clock root come from the board bootstrap code. Without the operating system part
 * (CONFIG_OS 0) there is no terminal: the input is the monitor's.
 */
#include "kernel.h"
#include <crtos/irq.h>
#include "fsl_device_registers.h"
#include "fsl_lpuart.h"
#include "board.h"
#include <crtos/tty.h>

bool tty_rx_filter(uint8_t c); /* os/tty.cpp: consumes Ctrl-C for the foreground process */

/* The terminal of the operating system part: Ctrl-C ends its foreground process, its readers
 * are woken (nothing without it) */
static bool term_filter(uint8_t c)
{
#if CONFIG_OS
    return tty_rx_filter(c);
#else
    (void)c;
    return false;
#endif
}

static void term_notify(void)
{
#if CONFIG_OS
    tty_rx_notify();
#endif
}

#define CON         LPUART1
#define CON_IRQ     LPUART1_IRQn
#define CON_BAUD    1000000u
#define CON_PRIO    8
#define TX_FIFO     4u
#define RX_MASK     (CONFIG_CONSOLE_RX_BUF - 1u)
static_assert((CONFIG_CONSOLE_RX_BUF & RX_MASK) == 0, "rx buffer must be a power of two");

/* STAT bits that are write-one-to-clear flags (everything else is configuration) */
#define STAT_W1C (LPUART_STAT_LBKDIF_MASK | LPUART_STAT_RXEDGIF_MASK | LPUART_STAT_IDLE_MASK | LPUART_STAT_OR_MASK | \
                  LPUART_STAT_NF_MASK | LPUART_STAT_FE_MASK | LPUART_STAT_PF_MASK | LPUART_STAT_MA1F_MASK |        \
                  LPUART_STAT_MA2F_MASK)

static volatile bool s_hw_ready;   /* UART configured (polled output works) */
static volatile bool s_irq_ready;  /* interrupt driven TX/RX active */
static uint8_t s_rx[CONFIG_CONSOLE_RX_BUF];
static volatile uint32_t s_rx_head, s_rx_tail;
static uint32_t s_rx_overrun, s_rx_dropped;
static struct wait_queue s_rx_wq;    /* readers waiting for data or the focus */
static volatile int s_focus = CON_KMON;
static char s_txq[2 * TX_FIFO];    /* expanded text waiting for FIFO space */
static uint32_t s_txq_pos, s_txq_len;

static void con_hw_init(void)
{
    if (s_hw_ready)
        return;
    lpuart_config_t cfg;
    LPUART_GetDefaultConfig(&cfg);
    cfg.baudRate_Bps = CON_BAUD;
    cfg.txFifoWatermark = 1;
    cfg.rxFifoWatermark = 2;
    cfg.enableTx = true;
    cfg.enableRx = true;
    LPUART_Init(CON, &cfg, BOARD_DebugConsoleSrcFreq());
    /* Raise RDRF also when the line has been idle for one character with data in the FIFO,
     * so received bytes below the watermark are not left behind */
    CON->CTRL &= ~(LPUART_CTRL_TE_MASK | LPUART_CTRL_RE_MASK);
    CON->FIFO = (CON->FIFO & ~(LPUART_FIFO_RXIDEN_MASK | LPUART_FIFO_TXOF_MASK | LPUART_FIFO_RXUF_MASK)) |
                LPUART_FIFO_RXIDEN(1);
    CON->CTRL |= LPUART_CTRL_TE_MASK | LPUART_CTRL_RE_MASK;
    s_hw_ready = true;
}

static inline uint32_t tx_fifo_count(void)
{
    return (CON->WATER & LPUART_WATER_TXCOUNT_MASK) >> LPUART_WATER_TXCOUNT_SHIFT;
}

static inline uint32_t rx_fifo_count(void)
{
    return (CON->WATER & LPUART_WATER_RXCOUNT_MASK) >> LPUART_WATER_RXCOUNT_SHIFT;
}

static void con_irq(int, void *)
{
    uint32_t stat = CON->STAT;
    if (stat & (LPUART_STAT_OR_MASK | LPUART_STAT_FE_MASK | LPUART_STAT_NF_MASK | LPUART_STAT_PF_MASK)) {
        if (stat & LPUART_STAT_OR_MASK)
            s_rx_overrun++;
        CON->STAT = (stat & ~STAT_W1C) |
                    (stat & (LPUART_STAT_OR_MASK | LPUART_STAT_FE_MASK | LPUART_STAT_NF_MASK | LPUART_STAT_PF_MASK));
    }

    /* RX: empty the FIFO into the ring */
    uint32_t n = rx_fifo_count();
    uint32_t got = 0;
    bool to_kmon = false;
    while (n--) {
        uint8_t c = (uint8_t)CON->DATA;
        if (s_focus == CON_TTY) {
            if (c == TTY_KEY_KMON) { /* what the terminal did not read yet is dropped */
                s_focus = CON_KMON;
                s_rx_tail = s_rx_head;
                to_kmon = true;
                continue;
            }
            if (term_filter(c))
                continue;
        }
        uint32_t h = s_rx_head;
        if (h - s_rx_tail < CONFIG_CONSOLE_RX_BUF) {
            s_rx[h & RX_MASK] = c;
            s_rx_head = h + 1;
            got++;
        } else {
            s_rx_dropped++;
        }
    }
    if (to_kmon) {
        static const char msg[] = "\n[kmon - 'exit' gives the console back]\nkmon> ";
        log_write(msg, sizeof(msg) - 1);
    }
    if (got || to_kmon) {
        wq_wake_all(&s_rx_wq, 0);
        term_notify();
    }

    /* TX: refill the FIFO from the log ring (LF is sent as CR LF), stop the interrupt
     * when everything has been sent */
    if ((CON->CTRL & LPUART_CTRL_TIE_MASK) && (stat & LPUART_STAT_TDRE_MASK)) {
        uint32_t space = TX_FIFO - tx_fifo_count();
        while (space) {
            if (s_txq_pos == s_txq_len) {
                char buf[TX_FIFO];
                uint32_t key = irq_lock();
                size_t k = log_console_read_locked(buf, sizeof(buf));
                if (!k)
                    CON->CTRL &= ~LPUART_CTRL_TIE_MASK;
                irq_unlock(key);
                if (!k)
                    break;
                s_txq_pos = s_txq_len = 0;
                for (size_t i = 0; i < k; i++) {
                    if (buf[i] == '\n')
                        s_txq[s_txq_len++] = '\r';
                    s_txq[s_txq_len++] = buf[i];
                }
            }
            CON->DATA = (uint8_t)s_txq[s_txq_pos++];
            space--;
        }
    }
}

void console_kick(void)
{
    if (!s_irq_ready)
        return;
    uint32_t key = irq_lock();
    CON->CTRL |= LPUART_CTRL_TIE_MASK;
    irq_unlock(key);
}

void console_init(void)
{
    wq_init(&s_rx_wq);
    con_hw_init();
    if (irq_request(CON_IRQ, con_irq, nullptr, CON_PRIO, "console") != 0)
        panic("console IRQ busy");
    CON->CTRL |= LPUART_CTRL_RIE_MASK | LPUART_CTRL_ORIE_MASK;
    s_irq_ready = true;
    console_kick(); /* send what printk collected so far */
}

void console_set_focus(int who)
{
    uint32_t key = irq_lock();
    bool changed = s_focus != who;
    s_focus = who;
    if (changed)
        s_rx_tail = s_rx_head; /* typed for the previous owner */
    wq_wake_all(&s_rx_wq, 0);
    irq_unlock(key);
    if (changed)
        term_notify();
}

int console_focus(void)
{
    return s_focus;
}

int console_rx_pending(void)
{
    return s_rx_head != s_rx_tail;
}

/*
 * Read whatever has arrived for @who (at least one byte), waiting up to @timeout ticks for
 * data and the focus. kmon drops Ctrl-] (it already has the console) unless CON_BINARY.
 */
int console_read_as(int who, void *buf, size_t max, uint32_t timeout, uint32_t flags)
{
    uint8_t *p = (uint8_t *)buf;
    uint32_t deadline = tick_get() + timeout;
    if (!max)
        return 0;
    for (;;) {
        uint32_t key = irq_lock();
        uint32_t n = 0;
        while (s_focus == who && n < max && s_rx_tail != s_rx_head) {
            uint8_t c = s_rx[s_rx_tail & RX_MASK];
            s_rx_tail++;
            if (c == TTY_KEY_KMON && who == CON_KMON && !(flags & CON_BINARY))
                continue;
            p[n++] = c;
        }
        if (n) {
            irq_unlock(key);
            return (int)n;
        }
        uint32_t left = WAIT_FOREVER;
        if (timeout != WAIT_FOREVER) {
            int32_t rem = (int32_t)(deadline - tick_get());
            if (timeout == NO_WAIT || rem <= 0) {
                irq_unlock(key);
                return -ETIMEDOUT;
            }
            left = (uint32_t)rem;
        }
        int r = sched_block(&s_rx_wq, left, key);
        if (r == -EINTR)
            return r;
    }
}

/* kmon: raw bytes (uploads) */
int console_read(void *buf, size_t max, uint32_t timeout)
{
    return console_read_as(CON_KMON, buf, max, timeout, CON_BINARY);
}

int console_getc(uint32_t timeout)
{
    uint8_t c;
    int r = console_read_as(CON_KMON, &c, 1, timeout, 0);
    return r < 0 ? r : c;
}

/* ---- polled access for panics and fault reports (interrupts may be off) ---- */

void console_panic_write(const char *s, size_t len)
{
    con_hw_init();
    CON->CTRL &= ~LPUART_CTRL_TIE_MASK;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\n') {
            while (tx_fifo_count() >= TX_FIFO) {
            }
            CON->DATA = '\r';
        }
        while (tx_fifo_count() >= TX_FIFO) {
        }
        CON->DATA = (uint8_t)s[i];
    }
    while (!(CON->STAT & LPUART_STAT_TC_MASK)) {
    }
}

int console_panic_getc(void)
{
    con_hw_init();
    uint32_t stat = CON->STAT;
    if (stat & LPUART_STAT_OR_MASK)
        CON->STAT = (stat & ~STAT_W1C) | LPUART_STAT_OR_MASK;
    if (rx_fifo_count())
        return (uint8_t)CON->DATA;
    return -1;
}

void console_stats(uint32_t *overrun, uint32_t *dropped)
{
    *overrun = s_rx_overrun;
    *dropped = s_rx_dropped;
}
