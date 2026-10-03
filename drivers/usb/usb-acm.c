/*
 * usb-acm.c - the device side of drivers/usb: the board as a USB serial port (CDC ACM) of a
 * computer, /dev/ttyACM0. The getty service runs a shell on it.
 *
 * The computer sees "CRTOS console" (VID 0x1209 of pid.codes with its test PID 0x0001, the
 * serial number is the chip's unique ID) and uses its own CDC ACM driver (Windows 10 and
 * later, Linux, macOS); the baud rate does not matter. A terminal program opening the port
 * raises DTR: the "carrier" getty waits for (TTY_IOC_WAIT_CARRIER). Without it read()
 * returns 0 (end of file), poll() reports POLLHUP and written data is dropped.
 *
 * Only the USB thread calls TinyUSB: the file functions move data through two rings and ask
 * the thread (usbd_defer_func) to pass it on, so a reader or a writer may be interrupted or
 * killed at any moment without leaving the stack half way through something.
 */
#include <crtos/errno.h>
#include <crtos/irq.h>
#include <crtos/poll.h>
#include <crtos/printk.h>
#include <crtos/sync.h>
#include <crtos/tty.h>
#include <crtos/vfs.h>
#include <string.h>
#include "fsl_device_registers.h"
#include "fsl_clock.h"
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "usb.h"

#define DEV_NAME    "ttyACM0"
#define USB_VID     0x1209      /* pid.codes */
#define USB_PID     0x0001      /* pid.codes test PID: private use only */
#define RING_SIZE   2048u       /* each direction, a power of 2 */
#define CHUNK       512u        /* bytes copied per irq_lock() section */

#define EV_RX       0x1u        /* data to read, or the carrier changed */
#define EV_TX       0x2u        /* room to write, or the carrier changed */
#define EV_CARRIER  0x4u        /* a terminal attached */

enum { ITF_CDC, ITF_CDC_DATA, ITF_COUNT };
enum { STR_LANG, STR_VENDOR, STR_PRODUCT, STR_SERIAL, STR_CDC, STR_COUNT };

#define EP_NOTIFY   0x81
#define EP_OUT      0x02
#define EP_IN       0x82
#define CONFIG_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

struct ring {
    uint8_t buf[RING_SIZE];
    uint32_t head, tail;        /* free running: bytes written, bytes read */
};

static struct {
    struct ring rx, tx;         /* computer -> board, board -> computer; under irq_lock() */
    struct event ev;
    struct poll_head ph;
    volatile bool registered, ready, carrier;
    volatile bool work_queued;  /* acm_work() is on the USB thread's queue */
    volatile bool rx_held;      /* TinyUSB holds data the rx ring had no room for */
    uint8_t tmp[CHUNK];         /* the USB thread's copy buffer */
    char serial[20];
    uint16_t str[32];
    uint8_t other_speed[CONFIG_LEN];
} s_acm;

/* ---- descriptors --------------------------------------------------------------------------- */

static const tusb_desc_device_t s_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,            /* the CDC function has an IAD */
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = STR_VENDOR,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

static const tusb_desc_device_qualifier_t s_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 1,
    .bReserved = 0,
};

/* the bulk endpoints: 512 bytes at high speed, 64 at full speed */
static const uint8_t s_config_hs[CONFIG_LEN] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(ITF_CDC, STR_CDC, EP_NOTIFY, 16, EP_OUT, EP_IN, 512),
};

static const uint8_t s_config_fs[CONFIG_LEN] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(ITF_CDC, STR_CDC, EP_NOTIFY, 16, EP_OUT, EP_IN, 64),
};

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&s_device;
}

const uint8_t *tud_descriptor_device_qualifier_cb(void)
{
    return (const uint8_t *)&s_qualifier;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return tud_speed_get() == TUSB_SPEED_HIGH ? s_config_hs : s_config_fs;
}

/* The configuration at the speed the device does not run at now */
const uint8_t *tud_descriptor_other_speed_configuration_cb(uint8_t index)
{
    (void)index;
    memcpy(s_acm.other_speed, tud_speed_get() == TUSB_SPEED_HIGH ? s_config_fs : s_config_hs, CONFIG_LEN);
    s_acm.other_speed[1] = TUSB_DESC_OTHER_SPEED_CONFIG;
    return s_acm.other_speed;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    static const char *const strings[STR_COUNT] = {
        [STR_VENDOR] = "CRTOS", [STR_PRODUCT] = "CRTOS console", [STR_CDC] = "CRTOS shell",
    };
    (void)langid;
    size_t n;
    if (index == STR_LANG) {
        s_acm.str[1] = 0x0409; /* English (US) */
        n = 1;
    } else {
        const char *s = index == STR_SERIAL ? s_acm.serial : index < STR_COUNT ? strings[index] : NULL;
        if (!s)
            return NULL;
        n = strlen(s);
        if (n > 31)
            n = 31;
        for (size_t i = 0; i < n; i++)
            s_acm.str[1 + i] = (uint8_t)s[i];
    }
    s_acm.str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return s_acm.str;
}

/* ---- rings (under irq_lock) ---------------------------------------------------------------- */

static uint32_t ring_count(const struct ring *r)
{
    return r->head - r->tail;
}

static uint32_t ring_put(struct ring *r, const uint8_t *p, uint32_t n)
{
    uint32_t room = RING_SIZE - ring_count(r);
    if (n > room)
        n = room;
    for (uint32_t done = 0; done < n;) {
        uint32_t at = r->head & (RING_SIZE - 1);
        uint32_t k = RING_SIZE - at < n - done ? RING_SIZE - at : n - done;
        memcpy(r->buf + at, p + done, k);
        r->head += k;
        done += k;
    }
    return n;
}

static uint32_t ring_get(struct ring *r, uint8_t *p, uint32_t n)
{
    uint32_t count = ring_count(r);
    if (n > count)
        n = count;
    for (uint32_t done = 0; done < n;) {
        uint32_t at = r->tail & (RING_SIZE - 1);
        uint32_t k = RING_SIZE - at < n - done ? RING_SIZE - at : n - done;
        memcpy(p + done, r->buf + at, k);
        r->tail += k;
        done += k;
    }
    return n;
}

/* ---- the USB thread's side ----------------------------------------------------------------- */

static void wake(uint32_t bits)
{
    event_set(&s_acm.ev, bits);
    poll_notify(&s_acm.ph);
}

/* What the computer sent: from TinyUSB into the rx ring, as far as it has room */
static void move_rx(void)
{
    bool moved = false;
    for (;;) {
        uint32_t key = irq_lock();
        uint32_t room = RING_SIZE - ring_count(&s_acm.rx);
        irq_unlock(key);
        uint32_t n = tud_cdc_n_available(0);
        s_acm.rx_held = n > room;
        if (n > room)
            n = room;
        if (n > CHUNK)
            n = CHUNK;
        if (n)
            n = tud_cdc_n_read(0, s_acm.tmp, n);
        if (!n)
            break;
        key = irq_lock();
        ring_put(&s_acm.rx, s_acm.tmp, n); /* fits: readers only make more room */
        irq_unlock(key);
        moved = true;
    }
    if (moved)
        wake(EV_RX);
}

/* What the board writes: from the tx ring into TinyUSB, as far as it has room. @flush: start
 * the transfer here (not needed where TinyUSB goes on sending by itself) */
static void move_tx(bool flush)
{
    bool moved = false;
    for (;;) {
        uint32_t n = tud_cdc_n_write_available(0);
        if (n > CHUNK)
            n = CHUNK;
        uint32_t key = irq_lock();
        n = ring_get(&s_acm.tx, s_acm.tmp, n);
        irq_unlock(key);
        if (!n)
            break;
        tud_cdc_n_write(0, s_acm.tmp, n); /* fits: only this thread fills TinyUSB's FIFO */
        moved = true;
    }
    if (moved) {
        if (flush)
            tud_cdc_n_write_flush(0);
        wake(EV_TX);
    }
}

static void acm_work(void *param)
{
    (void)param;
    s_acm.work_queued = false;
    if (!s_acm.carrier)
        return;
    move_rx();
    move_tx(true);
}

/* From the file side: have the USB thread run acm_work() */
static void request_work(void)
{
    if (!s_acm.ready)
        return;
    uint32_t key = irq_lock();
    bool queue = !s_acm.work_queued;
    s_acm.work_queued = true;
    irq_unlock(key);
    if (queue)
        usbd_defer_func(acm_work, NULL, false);
}

static void set_carrier(bool on)
{
    if (on == s_acm.carrier)
        return;
    uint32_t key = irq_lock();
    s_acm.carrier = on;
    s_acm.rx.head = s_acm.rx.tail = 0; /* every session starts clean */
    s_acm.tx.head = s_acm.tx.tail = 0;
    irq_unlock(key);
    if (on)
        tud_cdc_n_read_flush(0);
    else
        tud_cdc_n_write_clear(0);
    wake(EV_RX | EV_TX | (on ? EV_CARRIER : 0));
    printk("usb: a terminal %s /dev/" DEV_NAME "\n", on ? "opened" : "closed");
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void)itf;
    (void)rts;
    set_carrier(dtr);
}

void tud_cdc_rx_cb(uint8_t itf)
{
    (void)itf;
    if (s_acm.carrier)
        move_rx();
    else
        tud_cdc_n_read_flush(0);
}

void tud_cdc_tx_complete_cb(uint8_t itf)
{
    (void)itf;
    if (s_acm.carrier)
        move_tx(false); /* TinyUSB sends on after this callback */
}

void tud_mount_cb(void)
{
    printk("usb: connected to a computer (%s speed)\n", tud_speed_get() == TUSB_SPEED_HIGH ? "high" : "full");
    set_carrier(false);
}

void tud_umount_cb(void)
{
    printk("usb: disconnected\n");
    set_carrier(false);
}

void usb_acm_ready(void)
{
    s_acm.ready = true;
}

void usb_acm_poll(void)
{
    if (s_acm.carrier && (s_acm.work_queued || s_acm.rx_held))
        acm_work(NULL); /* (a request the full queue of TinyUSB did not take) */
}

/* ---- /dev/ttyACM0 -------------------------------------------------------------------------- */

static int acm_read(struct file *f, void *buf, size_t len)
{
    if (!len)
        return 0;
    if (len > CHUNK)
        len = CHUNK;
    for (;;) {
        uint32_t key = irq_lock();
        uint32_t n = ring_get(&s_acm.rx, buf, (uint32_t)len);
        bool carrier = s_acm.carrier;
        if (!n && carrier)
            event_clear(&s_acm.ev, EV_RX);
        irq_unlock(key);
        if (n) {
            if (s_acm.rx_held)
                request_work(); /* room again for what TinyUSB holds */
            return (int)n;
        }
        if (!carrier)
            return 0;
        if (f->flags & VFS_O_NONBLOCK)
            return -EAGAIN;
        int32_t r = event_wait(&s_acm.ev, EV_RX, EVENT_ANY, WAIT_FOREVER);
        if (r < 0)
            return r;
    }
}

static int acm_write(struct file *f, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    size_t done = 0;
    while (done < len) {
        uint32_t want = len - done > CHUNK ? CHUNK : (uint32_t)(len - done);
        uint32_t key = irq_lock();
        bool carrier = s_acm.carrier;
        uint32_t n = carrier ? ring_put(&s_acm.tx, p + done, want) : 0;
        if (carrier && !n)
            event_clear(&s_acm.ev, EV_TX);
        irq_unlock(key);
        if (!carrier)
            return (int)len; /* nobody listens: dropped */
        if (n) {
            done += n;
            request_work();
            continue;
        }
        if (f->flags & VFS_O_NONBLOCK)
            return done ? (int)done : -EAGAIN;
        int32_t r = event_wait(&s_acm.ev, EV_TX, EVENT_ANY, WAIT_FOREVER);
        if (r < 0)
            return done ? (int)done : r;
    }
    return (int)len;
}

static int acm_ioctl(struct file *f, unsigned cmd, void *arg)
{
    (void)f;
    switch (cmd) {
    case TTY_IOC_WAIT_CARRIER:
        for (;;) {
            uint32_t key = irq_lock();
            bool carrier = s_acm.carrier;
            if (!carrier)
                event_clear(&s_acm.ev, EV_CARRIER);
            irq_unlock(key);
            if (carrier)
                return 0;
            int32_t r = event_wait(&s_acm.ev, EV_CARRIER, EVENT_ANY, WAIT_FOREVER);
            if (r < 0)
                return r;
        }
    case TTY_IOC_GET_MODE: /* a raw line: whoever reads it edits */
        *(uint32_t *)arg = 0;
        return 0;
    case TTY_IOC_GET_SIZE: {
        struct tty_size *sz = arg;
        sz->cols = 80;
        sz->rows = 24;
        return 0;
    }
    default:
        return -ENOTTY;
    }
}

static int acm_poll(struct file *f, struct poll_entry *e)
{
    (void)f;
    uint32_t key = irq_lock();
    int mask = POLLHUP;
    if (s_acm.carrier)
        mask = (ring_count(&s_acm.rx) ? POLLIN : 0) | (ring_count(&s_acm.tx) < RING_SIZE ? POLLOUT : 0);
    poll_add(&s_acm.ph, e);
    irq_unlock(key);
    return mask;
}

static int acm_fstat(struct file *f, struct vfs_stat *st)
{
    (void)f;
    memset(st, 0, sizeof(*st));
    st->mode = VFS_S_IFCHR;
    return 0;
}

static const struct file_ops acm_ops = {
    .read = acm_read,
    .write = acm_write,
    .ioctl = acm_ioctl,
    .fstat = acm_fstat,
    .poll = acm_poll,
};

int usb_acm_start(void)
{
    memset(&s_acm, 0, sizeof(s_acm));
    event_init(&s_acm.ev);
    poll_head_init(&s_acm.ph);
    /* the serial number: the chip's unique ID (fuses) */
    CLOCK_EnableClock(kCLOCK_Ocotp);
    ksnprintf(s_acm.serial, sizeof(s_acm.serial), "%08lX%08lX", (unsigned long)OCOTP->CFG1,
              (unsigned long)OCOTP->CFG0);
    int r = devfs_register(DEV_NAME, &acm_ops, NULL);
    s_acm.registered = !r;
    return r;
}

void usb_acm_stop(void)
{
    s_acm.ready = false;
    if (s_acm.registered)
        devfs_unregister(DEV_NAME);
    s_acm.registered = false;
}
