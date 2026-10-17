/*
 * console_uart.cpp - built-in console on UART4 of the STM32H7S78-DK (PD0 RX, PD1 TX: the
 * ST-LINK's virtual COM port), 1 Mbaud 8N1. The same console as kernel/rtos/console_lpuart.cpp
 * on the i.MX RT board, on the STM32's USART with its FIFOs.
 *
 * TX drains the printk ring from the "TX FIFO not full" interrupt; RX fills a ring read by
 * whoever has the input focus: the kernel monitor (CON_KMON) or the user terminal /dev/console
 * (CON_TTY). Ctrl-] typed while the terminal has the focus gives it to kmon; this is done in
 * the interrupt, so it works even when no program reads the terminal. Without the operating
 * system part (CONFIG_OS 0) there is no terminal: the input is the monitor's.
 */
#include "kernel.h"
#include <crtos/irq.h>
#include <crtos/tty.h>
#include "platform.h"

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

#define CON         UART4
#define CON_IRQ     UART4_IRQn
#define CON_BAUD    1000000u
#define CON_PRIO    8
#define RX_MASK     (CONFIG_CONSOLE_RX_BUF - 1u)
static_assert((CONFIG_CONSOLE_RX_BUF & RX_MASK) == 0, "rx buffer must be a power of two");

#define ISR_ERRORS  (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)
#define ICR_ERRORS  (USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF)

static volatile bool s_hw_ready;   /* UART configured (polled output works) */
static volatile bool s_irq_ready;  /* interrupt driven TX/RX active */
static uint8_t s_rx[CONFIG_CONSOLE_RX_BUF];
static volatile uint32_t s_rx_head, s_rx_tail;
static uint32_t s_rx_overrun, s_rx_dropped;
static struct wait_queue s_rx_wq;    /* readers waiting for data or the focus */
static volatile int s_focus = CON_KMON;
static char s_txq[16];             /* expanded text waiting for FIFO space */
static uint32_t s_txq_pos, s_txq_len;

/* PD0/PD1 as UART4 (AF8); the UART's kernel clock is PCLK1 (150 MHz, the boot loader's) */
static void con_hw_init(void)
{
    if (s_hw_ready)
        return;
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIODEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_UART4EN;
    (void)RCC->APB1ENR1;
    GPIOD->AFR[0] = (GPIOD->AFR[0] & ~0xFFu) | 0x88u;
    GPIOD->PUPDR = (GPIOD->PUPDR & ~0xFu) | 0x5u;                  /* pull-ups */
    GPIOD->OSPEEDR = (GPIOD->OSPEEDR & ~0xFu) | 0x5u;              /* medium speed */
    GPIOD->MODER = (GPIOD->MODER & ~0xFu) | 0xAu;                  /* alternate function */
    CON->CR1 = 0;
    CON->PRESC = 0;
    uint32_t pclk1 = SystemCoreClock / 4u;                         /* AHB /2, APB1 /2 */
    CON->BRR = (pclk1 + CON_BAUD / 2u) / CON_BAUD;
    CON->CR2 = 0;
    CON->CR3 = 0;                                                  /* overruns are counted */
    CON->ICR = 0xFFFFFFFFu;
    CON->CR1 = USART_CR1_FIFOEN | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    s_hw_ready = true;
}

static void con_irq(int, void *)
{
    uint32_t isr = CON->ISR;
    if (isr & ISR_ERRORS) {
        if (isr & USART_ISR_ORE)
            s_rx_overrun++;
        CON->ICR = ICR_ERRORS;
    }

    /* RX: empty the FIFO into the ring */
    uint32_t got = 0;
    bool to_kmon = false;
    while (CON->ISR & USART_ISR_RXNE_RXFNE) {
        uint8_t c = (uint8_t)CON->RDR;
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

    /* TX: refill the FIFO from the log ring (LF is sent as CR LF), stop the interrupt when
     * everything has been sent */
    if (CON->CR1 & USART_CR1_TXEIE_TXFNFIE) {
        while (CON->ISR & USART_ISR_TXE_TXFNF) {
            if (s_txq_pos == s_txq_len) {
                char buf[sizeof(s_txq) / 2];
                uint32_t key = irq_lock();
                size_t k = log_console_read_locked(buf, sizeof(buf));
                if (!k)
                    CON->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
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
            CON->TDR = (uint8_t)s_txq[s_txq_pos++];
        }
    }
}

void console_kick(void)
{
    if (!s_irq_ready)
        return;
    uint32_t key = irq_lock();
    CON->CR1 |= USART_CR1_TXEIE_TXFNFIE;
    irq_unlock(key);
}

void console_init(void)
{
    wq_init(&s_rx_wq);
    con_hw_init();
    if (irq_request(CON_IRQ, con_irq, nullptr, CON_PRIO, "console") != 0)
        panic("console IRQ busy");
    CON->CR3 |= USART_CR3_EIE;
    CON->CR1 |= USART_CR1_RXNEIE_RXFNEIE;
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
    CON->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\n') {
            while (!(CON->ISR & USART_ISR_TXE_TXFNF)) {
            }
            CON->TDR = '\r';
        }
        while (!(CON->ISR & USART_ISR_TXE_TXFNF)) {
        }
        CON->TDR = (uint8_t)s[i];
    }
    while (!(CON->ISR & USART_ISR_TC)) {
    }
}

int console_panic_getc(void)
{
    con_hw_init();
    if (CON->ISR & ISR_ERRORS)
        CON->ICR = ICR_ERRORS;
    if (CON->ISR & USART_ISR_RXNE_RXFNE)
        return (uint8_t)CON->RDR;
    return -1;
}

void console_stats(uint32_t *overrun, uint32_t *dropped)
{
    *overrun = s_rx_overrun;
    *dropped = s_rx_dropped;
}
