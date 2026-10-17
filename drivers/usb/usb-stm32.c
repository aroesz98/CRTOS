/*
 * usb-stm32.ko - the USB controllers of the STM32H7S7 (Synopsys DWC2: OTG_HS with its on-chip
 * high-speed PHY, OTG_FS with a full-speed one) with the TinyUSB stack (third_party/tinyusb),
 * each as a device or as a host - the device tree decides:
 *
 *   dr_mode = "peripheral"   a USB serial port for the computer: /dev/ttyACM0 (usb-acm.c);
 *                            the getty service runs a shell on it
 *   dr_mode = "host"         keyboards, mice (HID boot protocol) and hubs (usb-hid.c)
 *
 * Both controllers may work at once, one of each kind: TinyUSB has one device stack and one
 * host stack. TinyUSB numbers them as ST does: port 0 OTG_FS, port 1 OTG_HS. On the
 * STM32H7S78-DK, OTG_HS is the USB-C connector USB1 and OTG_FS is USB2.
 *
 * A host port gives its devices 5 V. On the STM32H7S78-DK only USB1 can: USB2 is a sink-only
 * port (a TCPP01 overvoltage protection; jumper JP1 picks the board's 5 V source, USB2 one of
 * them), so it is the device side. USB1 is a USB-C port behind a TCPP03 protection chip
 * ("st,tcpp03", an I2C device: its gate drivers switch the board's 5 V onto VBUS): the host node
 * names it (vbus-supply) and the UCPD that watches the CC lines (typec-controller). The UCPD
 * presents Rp (a source); only when a sink's Rd appears on CC1 or CC2 - a device, a hub, an
 * adapter to USB-A - does VBUS go on, and it goes off when the sink leaves. A computer on that
 * port (Rp as well) never gets 5 V from the board.
 *
 * Clocks: the HS PHY takes the 24 MHz HSE as reference (RCC USBPHYCSEL, USBREFCKSEL); OTG_FS
 * runs from HSI48, which the clock recovery system (CRS) keeps on 48 MHz by the 32.768 kHz
 * LSE crystal (a USB host must be within 0.25 %, HSI48 alone is not). Power: the USB supply
 * detector and, for HS, the PHY's regulator.
 *
 * Each controller has a kernel thread running TinyUSB (tud_task or tuh_task); its interrupt
 * only queues events for it, and all TinyUSB callbacks of a side run in that side's thread.
 * Until TinyUSB has started, the controller's interrupts are masked (a controller left running
 * by the system before a debugger reset would otherwise interrupt for ever).
 */
#include <crtos/arch.h>
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/gpio.h>
#include <crtos/i2c.h>
#include <crtos/irq.h>
#include <crtos/module.h>
#include <crtos/of.h>
#include <crtos/printk.h>
#include <crtos/sched.h>
#include <crtos/sync.h>
#include <string.h>
#include "stm32h7rsxx.h"
#include "tusb.h"
#include "usb.h"

#define PORTS 2          /* TinyUSB's rhport: 0 OTG_FS, 1 OTG_HS */
#define IDLE_WAIT_MS 100 /* a thread looks at "stop" at least this often */
#define USBREF_24MHZ 0xau
#define TYPEC_POLL_MS 100 /* the CC lines looked at (two equal looks: tCCDebounce) */

/* TCPP03 register 0 */
#define TCPP_GDP_CLOSED 0x04u /* the gate driver provider: the board's 5 V onto VBUS */
#define TCPP_GDC_OPEN 0x08u   /* the gate driver consumer: VBUS into the board, off */
#define TCPP_NORMAL 0x10u
#define TCPP_DISCHARGE 0x40u

struct tcpp
{
    struct device *dev;
    struct i2c_client *client;
    struct gpio_desc *enable;
};

static struct tcpp *s_tcpp; /* the one on the board */

struct port
{
    struct device *dev; /* NULL: not in use */
    UCPD_TypeDef *ucpd; /* a USB-C source port: its CC lines, */
    struct tcpp *tcpp;  /* its VBUS switch */
    bool vbus_on;
    int sink_seen;      /* looks with the other result in a row */
    USB_OTG_GlobalTypeDef *regs;
    int rhport, irq;
    bool device; /* the device side (else the host side) */
    task_t *thread;
    volatile bool ready, stop;
    struct semaphore done; /* the thread has ended */
};

static struct port s_ports[PORTS];
static uint32_t s_asserts; /* TinyUSB assertions that failed */

/* TinyUSB's failed assertions (CFG_TUSB_DEBUG_BREAKPOINT): counted, never a breakpoint */
void tusb_breakpoint(void)
{
    s_asserts++;
}

static int wait_bit(volatile uint32_t *reg, uint32_t bit)
{
    uint32_t t0 = cpu_cycles();
    while (!(*reg & bit))
        if (cpu_cycles() - t0 > SystemCoreClock / 100u)
            return -ETIMEDOUT;
    return 0;
}

/* the USB supply (VDD33USB) watched, and for HS the PHY's regulator on */
static int power_up(bool hs)
{
    uint32_t key = irq_lock();
    PWR->CSR2 |= PWR_CSR2_USB33DEN | (hs ? PWR_CSR2_USBHSREGEN : 0u);
    irq_unlock(key);
    return wait_bit(&PWR->CSR2, PWR_CSR2_USB33RDY);
}

/* OTG_HS: the PHY's reference, the 24 MHz HSE */
static void hs_clock(void)
{
    uint32_t key = irq_lock();
    RCC->CCIPR1 = (RCC->CCIPR1 & ~(RCC_CCIPR1_USBPHYCSEL | RCC_CCIPR1_USBREFCKSEL)) |
                  (USBREF_24MHZ << RCC_CCIPR1_USBREFCKSEL_Pos); /* USBPHYCSEL 0: HSE */
    irq_unlock(key);
}

/* OTG_FS: HSI48 (the "kernel" clock of the node), trimmed by the CRS against the LSE */
static void fs_clock(void)
{
    uint32_t key = irq_lock();
    RCC->CCIPR1 &= ~RCC_CCIPR1_OTGFSSEL; /* 0: HSI48 */
    RCC->APB1ENR2 |= RCC_APB1ENR2_CRSEN; /* (the CRS belongs to no node: its gate is set here) */
    (void)RCC->APB1ENR2;
    irq_unlock(key);
    CRS->CR &= ~CRS_CR_CEN;
    /* RELOAD = 48 MHz / 32768 Hz - 1; FELIM: the error a trimming step (0.14 %) is worth */
    CRS->CFGR = (1464u << CRS_CFGR_RELOAD_Pos) | (2u << CRS_CFGR_FELIM_Pos) | (1u << CRS_CFGR_SYNCSRC_Pos);
    CRS->CR |= CRS_CR_AUTOTRIMEN | CRS_CR_CEN;
}

/* ---- USB-C source power ------------------------------------------------------------------ */

static int tcpp_write(struct tcpp *t, uint8_t v)
{
    uint8_t b[2] = {0, v}; /* register 0 */
    return i2c_write(t->client, b, 2) < 0 ? -EIO : 0;
}

static int tcpp_probe(struct device *dev)
{
    struct tcpp *t = devm_kzalloc(dev, sizeof(*t), 0);
    if (!t)
        return -ENOMEM;
    t->dev = dev;
    t->client = i2c_client_get(dev);
    if (!t->client)
    {
        dev_err(dev, "not an I2C device\n");
        return -EINVAL;
    }
    int r = gpiod_get(dev, "enable", GPIOD_OUT_HIGH, &t->enable);
    if (r)
    {
        dev_err(dev, "no enable-gpios (%d)\n", r);
        return r == -EPROBE_DEFER ? r : -EINVAL;
    }
    task_sleep_ms(5);
    /* normal mode, VBUS switched off; the consumer side as after reset (closed: a board fed
     * through this port keeps its power) */
    if (tcpp_write(t, TCPP_NORMAL))
    {
        dev_err(dev, "no answer at 0x%02x\n", t->client->addr);
        return -ENODEV;
    }
    dev_set_drvdata(dev, t);
    s_tcpp = t;
    dev_info(dev, "TCPP03 USB-C protection at 0x%02x\n", t->client->addr);
    return 0;
}

static void tcpp_remove(struct device *dev)
{
    struct tcpp *t = dev_get_drvdata(dev);
    tcpp_write(t, TCPP_NORMAL);
    if (s_tcpp == t)
        s_tcpp = NULL;
}

static const struct of_device_id tcpp_ids[] = {
    {"st,tcpp03", NULL},
    {NULL, NULL},
};

static struct driver tcpp_driver = {
    .name = "tcpp03",
    .of_match_table = tcpp_ids,
    .probe = tcpp_probe,
    .remove = tcpp_remove,
};

/* VBUS of a USB-C host port on or off; on an I2C error it stays as it was (and the next look at
 * the CC lines tries again) */
static void vbus_set(struct port *p, bool on)
{
    int r;
    if (on)
    {
        r = tcpp_write(p->tcpp, TCPP_NORMAL | TCPP_GDC_OPEN);
        task_sleep_ms(2);
        if (!r)
            r = tcpp_write(p->tcpp, TCPP_NORMAL | TCPP_GDC_OPEN | TCPP_GDP_CLOSED);
    }
    else
    {
        r = tcpp_write(p->tcpp, TCPP_NORMAL | TCPP_GDC_OPEN | TCPP_DISCHARGE);
        task_sleep_ms(50);
        if (!r)
            r = tcpp_write(p->tcpp, TCPP_NORMAL);
    }
    if (r)
    {
        dev_err(p->dev, "switching VBUS %s failed (TCPP03): %d\n", on ? "on" : "off", r);
        return;
    }
    p->vbus_on = on;
    dev_info(p->dev, "%s on the USB-C port, VBUS %s\n", on ? "a device" : "no device", on ? "on" : "off");
}

/* a sink's Rd on either CC line (as a source the UCPD reports 01: vRd) */
static void typec_poll(struct port *p)
{
    uint32_t sr = p->ucpd->SR;
    uint32_t cc1 = (sr & UCPD_SR_TYPEC_VSTATE_CC1) >> UCPD_SR_TYPEC_VSTATE_CC1_Pos;
    uint32_t cc2 = (sr & UCPD_SR_TYPEC_VSTATE_CC2) >> UCPD_SR_TYPEC_VSTATE_CC2_Pos;
    bool sink = cc1 == 1u || cc2 == 1u;
    if (sink == p->vbus_on)
    {
        p->sink_seen = 0;
        return;
    }
    if (++p->sink_seen >= 2)
    {
        p->sink_seen = 0;
        vbus_set(p, sink);
    }
}

/* the UCPD as a USB-C source: Rp (default USB current) on both CC lines, the dead battery
 * pull-downs off */
static void typec_init(UCPD_TypeDef *u)
{
    uint32_t key = irq_lock();
    PWR->UCPDR |= PWR_UCPDR_UCPD_DBDIS;
    RCC->CR |= RCC_CR_HSION; /* (its kernel clock) */
    irq_unlock(key);
    u->CFG1 = 0;
    u->CFG1 = (1u << UCPD_CFG1_PSC_UCPDCLK_Pos) | (7u << UCPD_CFG1_TRANSWIN_Pos) | (0x10u << UCPD_CFG1_IFRGAP_Pos) |
              (0x0du << UCPD_CFG1_HBITCLKDIV_Pos);
    u->CFG1 |= UCPD_CFG1_UCPDEN;
    u->CR = (1u << UCPD_CR_ANASUBMODE_Pos) | (3u << UCPD_CR_CCENABLE_Pos); /* ANAMODE 0: source */
}

/* No interrupts from the controller, none pending (a controller left running) */
static void quiet(USB_OTG_GlobalTypeDef *u)
{
    u->GINTMSK = 0;
    u->GINTSTS = 0xffffffffu; /* (write 1 to clear) */
}

static void usb_irq(int irq, void *ctx)
{
    (void)irq;
    struct port *p = (struct port *)ctx;
    if (p->ready)
        tusb_int_handler((uint8_t)p->rhport, true);
    else
        quiet(p->regs); /* (TinyUSB turns them on as it starts) */
}

static void usb_thread(void *arg)
{
    struct port *p = (struct port *)arg;
    const tusb_rhport_init_t init = {
        .role = p->device ? TUSB_ROLE_DEVICE : TUSB_ROLE_HOST,
        .speed = p->rhport == 1 ? TUSB_SPEED_HIGH : TUSB_SPEED_FULL,
    };
    if (p->device)
    {
        /* no VBUS sense line: always "plugged in" (the computer sees the board when it is) */
        const tud_configure_dwc2_t cfg = {.bm_double_buffered = 0, .vbus_sensing = false};
        tud_configure((uint8_t)p->rhport, TUD_CFGID_DWC2, &cfg);
    }
    if (!tusb_rhport_init((uint8_t)p->rhport, &init))
    {
        dev_err(p->dev, "TinyUSB did not start\n");
        sem_give(&p->done);
        return;
    }
    p->ready = true;
    if (p->device)
    {
        usb_acm_ready();
        while (!p->stop)
        {
            tud_task_ext(IDLE_WAIT_MS, false);
            usb_acm_poll();
        }
        tud_deinit((uint8_t)p->rhport);
    }
    else
    {
        while (!p->stop)
        {
            uint32_t wait = usb_hid_repeat();
            if (p->ucpd && wait > TYPEC_POLL_MS)
                wait = TYPEC_POLL_MS;
            tuh_task_ext(wait, false);
            if (p->ucpd)
                typec_poll(p);
        }
        if (p->vbus_on)
            vbus_set(p, false);
        tuh_deinit((uint8_t)p->rhport);
    }
    sem_give(&p->done);
}

/* the controller in use for @device's side, or NULL */
static struct port *port_of_side(bool device)
{
    for (int i = 0; i < PORTS; i++)
        if (s_ports[i].dev && s_ports[i].device == device)
            return &s_ports[i];
    return NULL;
}

static int usb_probe(struct device *dev)
{
    static const char *const thread_names[PORTS] = {"usb-fs", "usb-hs"};
    const char *mode = "peripheral";
    of_property_read_string(dev->of_node, "dr_mode", &mode);
    bool device = strcmp(mode, "host") != 0;
    struct port *other = port_of_side(device);
    if (other)
    {
        dev_info(dev, "one USB %s at a time (%s is one)\n", device ? "device" : "host", other->dev->name);
        return -EBUSY;
    }
    void *base = device_map(dev, 0);
    int irq = device_get_irq(dev, 0);
    if (!base || irq < 0)
        return irq == -EPROBE_DEFER ? -EPROBE_DEFER : -ENODEV;
    int n = (uintptr_t)base == USB_OTG_FS_PERIPH_BASE ? 0 : (uintptr_t)base == USB_OTG_HS_PERIPH_BASE ? 1 : -1;
    if (n < 0 || s_ports[n].dev)
        return -ENODEV;
    bool hs = n == 1;
    /* a USB-C source port: its VBUS switch must be there first */
    struct device_node *vbus = of_parse_phandle(dev->of_node, "vbus-supply", 0);
    struct device_node *typec = of_parse_phandle(dev->of_node, "typec-controller", 0);
    uint32_t typec_base = 0, typec_size;
    if (!device && vbus && typec)
    {
        if (!s_tcpp || s_tcpp->dev->of_node != vbus)
        {
            dev_info(dev, "waiting for its VBUS switch (%s)\n", vbus->name);
            return -EPROBE_DEFER;
        }
        if (of_get_reg(typec, 0, &typec_base, &typec_size))
            return -EINVAL;
    }
    if (hs)
        hs_clock();
    int r = power_up(hs);
    if (r)
    {
        dev_err(dev, "no USB supply (VDD33USB)\n");
        return r;
    }
    /* the controller's clocks (and HSI48 + LSE for OTG_FS, the PHY's for OTG_HS) */
    for (int i = 0; i < 5; i++)
    {
        struct clk *clk;
        static const char *const names[] = {"otg", "phy", "kernel", "sync", "typec"};
        r = devm_clk_get_enabled(dev, names[i], &clk);
        if (r == -ENOENT && i > 0)
            continue;
        if (r)
            return r;
    }
    if (!hs)
        fs_clock();

    struct port *p = &s_ports[n];
    memset(p, 0, sizeof(*p));
    p->dev = dev;
    p->regs = (USB_OTG_GlobalTypeDef *)base;
    p->rhport = n;
    p->irq = irq;
    p->device = device;
    if (typec_base)
    {
        p->ucpd = (UCPD_TypeDef *)(uintptr_t)typec_base;
        p->tcpp = s_tcpp;
        typec_init(p->ucpd);
    }
    sem_init(&p->done, 0, 1);
    if (device)
    {
        r = usb_acm_start();
        if (r)
        {
            p->dev = NULL;
            return r;
        }
    }
    quiet(p->regs);
    r = irq_request(irq, usb_irq, p, 6, dev->name);
    if (!r)
    {
        p->thread = kthread_create(thread_names[n], usb_thread, p, PRIO_HIGH - 3, 3072);
        if (!p->thread)
        {
            irq_free(irq);
            r = -ENOMEM;
        }
    }
    if (r)
    {
        if (device)
            usb_acm_stop();
        p->dev = NULL;
        return r;
    }
    if (device)
        dev_info(dev, "USB device on OTG_%s (TinyUSB %s): serial port /dev/ttyACM0\n", hs ? "HS" : "FS",
                 TUSB_VERSION_STRING);
    else
        dev_info(dev, "USB host on OTG_%s (TinyUSB %s): keyboards, mice, hubs%s\n", hs ? "HS" : "FS",
                 TUSB_VERSION_STRING, p->ucpd ? "; USB-C: VBUS when a device is there" : "");
    return 0;
}

static void usb_remove(struct device *dev)
{
    struct port *p = NULL;
    for (int i = 0; i < PORTS; i++)
        if (s_ports[i].dev == dev)
            p = &s_ports[i];
    if (!p)
        return;
    p->stop = true;
    sem_take(&p->done, 2000);
    p->ready = false;
    quiet(p->regs);
    irq_free(p->irq);
    if (p->device)
        usb_acm_stop();
    else
        usb_hid_stop();
    p->dev = NULL;
}

static const struct of_device_id usb_ids[] = {
    {"st,stm32-otg-hs", NULL},
    {"st,stm32-otg-fs", NULL},
    {NULL, NULL},
};

static struct driver usb_driver = {
    .name = "usb-stm32",
    .of_match_table = usb_ids,
    .probe = usb_probe,
    .remove = usb_remove,
};

static int usb_init(void)
{
    int r = driver_register(&tcpp_driver);
    if (r)
        return r;
    r = driver_register(&usb_driver);
    if (r)
        driver_unregister(&tcpp_driver);
    return r;
}

static void usb_exit(void)
{
    driver_unregister(&usb_driver);
    driver_unregister(&tcpp_driver);
}

MODULE("usb-stm32", "USB (TinyUSB): serial port device and host for keyboards, mice, hubs", usb_init, usb_exit);
