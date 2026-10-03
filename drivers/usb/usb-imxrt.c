/*
 * usb-imxrt.ko - the USB controllers of the i.MX RT (USB OTG1 and OTG2) with the TinyUSB stack
 * (third_party/tinyusb), each as a device or as a host - the device tree decides:
 *
 *   dr_mode = "peripheral"   a USB serial port for the computer: /dev/ttyACM0 (usb-acm.c);
 *                            the getty service runs a shell on it
 *   dr_mode = "host"         keyboards, mice (HID boot protocol) and hubs (usb-hid.c)
 *
 * Both controllers may work at once, one of each kind: TinyUSB has one device stack and one
 * host stack. Device tree: compatible "fsl,imxrt1050-usb", reg (0x402e0000 USB1, 0x402e0200
 * USB2), interrupts, dr_mode (default: peripheral). The 480 MHz USB PLL and the PHY are set up
 * here (the "fsl,usbphy" node only documents them). On the EVKB, USB1 is the connector J9 and
 * USB2 is J10 (the board supplies its 5 V).
 *
 * Each controller has a kernel thread running TinyUSB (tud_task or tuh_task); its interrupt
 * only queues events for it, and all TinyUSB callbacks of a side run in that side's thread.
 *
 * A reset through the debug probe does not reset the controllers: one may still run as the
 * system before left it (a host makes an interrupt every frame). Its interrupts are turned off
 * before ours is taken, and again if one comes before TinyUSB has started - an interrupt
 * nobody answers would come back at once, for ever, and the thread that starts TinyUSB would
 * never run.
 */
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/irq.h>
#include <crtos/module.h>
#include <crtos/of.h>
#include <crtos/printk.h>
#include <crtos/sched.h>
#include <crtos/sync.h>
#include <string.h>
#include "fsl_device_registers.h"
#include "fsl_clock.h"
#include "tusb.h"
#include "device/dcd.h"
#include "usb.h"

#define PORTS        2          /* USB1, USB2 */
#define IDLE_WAIT_MS 100        /* a thread looks at "stop" (and VBUS) at least this often */

struct port {
    struct device *dev;         /* NULL: not in use */
    USB_Type *regs;
    int rhport, irq;
    bool device;                /* the device side (else the host side) */
    task_t *thread;
    volatile bool ready, stop;
    struct semaphore done;      /* the thread has ended */
};

static struct port s_ports[PORTS];
static uint32_t s_asserts;      /* TinyUSB assertions that failed */

/* TinyUSB's failed assertions (CFG_TUSB_DEBUG_BREAKPOINT): counted, never a breakpoint */
void tusb_breakpoint(void)
{
    s_asserts++;
}

/* The 480 MHz USB PLL, the controller's clock and the PHY: powered, out of reset, low and
 * full speed devices allowed (keyboards, mice), TX calibration of the NXP boards; the
 * charger detector (it would load D+ and D-) off */
static void clock_phy_init(int port)
{
    USBPHY_Type *phy = port ? USBPHY2 : USBPHY1;
    if (port) {
        CLOCK_EnableUsbhs1PhyPllClock(kCLOCK_Usbphy480M, 480000000u);
        CLOCK_EnableUsbhs1Clock(kCLOCK_Usb480M, 480000000u);
    } else {
        CLOCK_EnableUsbhs0PhyPllClock(kCLOCK_Usbphy480M, 480000000u);
        CLOCK_EnableUsbhs0Clock(kCLOCK_Usb480M, 480000000u);
    }
    USB_ANALOG->INSTANCE[port].CHRG_DETECT_SET =
        USB_ANALOG_CHRG_DETECT_SET_CHK_CHRG_B(1) | USB_ANALOG_CHRG_DETECT_SET_EN_B(1);
    phy->CTRL |= USBPHY_CTRL_SET_ENUTMILEVEL2_MASK | USBPHY_CTRL_SET_ENUTMILEVEL3_MASK;
    phy->PWD = 0;
    uint32_t tx = phy->TX & ~(USBPHY_TX_D_CAL_MASK | USBPHY_TX_TXCAL45DM_MASK | USBPHY_TX_TXCAL45DP_MASK);
    phy->TX = tx | USBPHY_TX_D_CAL(0x0c) | USBPHY_TX_TXCAL45DP(0x06) | USBPHY_TX_TXCAL45DM(0x06);
}

/* 5 V on the connector: from the computer (device side: TinyUSB's ChipIdea port does not
 * notice unplugging), from the board (host side: only reported) */
static bool vbus_present(const struct port *p)
{
    return (USB_ANALOG->INSTANCE[p->rhport].VBUS_DETECT_STAT & USB_ANALOG_VBUS_DETECT_STAT_VBUS_VALID_MASK) != 0;
}

static void device_loop(struct port *p)
{
    bool vbus = false;
    while (!p->stop) {
        tud_task_ext(IDLE_WAIT_MS, false);
        usb_acm_poll();
        bool now = vbus_present(p);
        if (now != vbus) {
            printk("usb: cable %s\n", now ? "plugged in" : "unplugged");
            if (!now)
                dcd_event_bus_signal((uint8_t)p->rhport, DCD_EVENT_UNPLUGGED, false);
            vbus = now;
        }
    }
}

/* No interrupts from the controller, none pending (a controller left running) */
static void quiet(USB_Type *u)
{
    u->USBINTR = 0;
    u->USBSTS = u->USBSTS; /* (write 1 to clear) */
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
        .speed = p->device ? TUSB_SPEED_HIGH : TUSB_SPEED_AUTO,
    };
    if (!tusb_rhport_init((uint8_t)p->rhport, &init)) {
        dev_err(p->dev, "TinyUSB did not start\n");
        sem_give(&p->done);
        return;
    }
    p->ready = true;
    if (p->device) {
        usb_acm_ready();
        device_loop(p);
        tud_deinit((uint8_t)p->rhport);
    } else {
        while (!p->stop)
            tuh_task_ext(usb_hid_repeat(), false);
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
    static const char *const thread_names[PORTS] = { "usb1", "usb2" };
    const char *mode = "peripheral";
    of_property_read_string(dev->of_node, "dr_mode", &mode);
    bool device = strcmp(mode, "host") != 0;
    struct port *other = port_of_side(device);
    if (other) {
        dev_info(dev, "one USB %s at a time (%s is one)\n", device ? "device" : "host", other->dev->name);
        return -EBUSY;
    }
    void *base = device_map(dev, 0);
    int irq = device_get_irq(dev, 0);
    if (!base || irq < 0)
        return irq == -EPROBE_DEFER ? -EPROBE_DEFER : -ENODEV;
    int n = (uintptr_t)base == USB1_BASE ? 0 : (uintptr_t)base == USB2_BASE ? 1 : -1;
    if (n < 0 || s_ports[n].dev)
        return -ENODEV;
    struct port *p = &s_ports[n];
    memset(p, 0, sizeof(*p));
    p->dev = dev;
    p->regs = (USB_Type *)base;
    p->rhport = n;
    p->irq = irq;
    p->device = device;
    sem_init(&p->done, 0, 1);
    if (device) {
        int r = usb_acm_start();
        if (r) {
            p->dev = NULL;
            return r;
        }
    }
    clock_phy_init(n);
    quiet(p->regs);
    int r = irq_request(irq, usb_irq, p, 6, dev->name);
    if (!r) {
        p->thread = kthread_create(thread_names[n], usb_thread, p, PRIO_HIGH - 3, 3072);
        if (!p->thread) {
            irq_free(irq);
            r = -ENOMEM;
        }
    }
    if (r) {
        if (device)
            usb_acm_stop();
        p->dev = NULL;
        return r;
    }
    if (device)
        dev_info(dev, "USB device on USB%d (TinyUSB %s): serial port /dev/ttyACM0\n", n + 1, TUSB_VERSION_STRING);
    else
        dev_info(dev, "USB host on USB%d (TinyUSB %s): keyboards, mice, hubs%s\n", n + 1, TUSB_VERSION_STRING,
                 vbus_present(p) ? "" : " - no 5 V on the connector yet");
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
    irq_free(p->irq);
    if (p->device)
        usb_acm_stop();
    else
        usb_hid_stop();
    p->dev = NULL;
}

static const struct of_device_id usb_ids[] = {
    { "fsl,imxrt1050-usb", NULL },
    { NULL, NULL },
};

static struct driver usb_driver = {
    .name = "usb-imxrt",
    .of_match_table = usb_ids,
    .probe = usb_probe,
    .remove = usb_remove,
};

static int usb_init(void)
{
    return driver_register(&usb_driver);
}

static void usb_exit(void)
{
    driver_unregister(&usb_driver);
}

MODULE("usb-imxrt", "USB (TinyUSB): serial port device and host for keyboards, mice, hubs", usb_init, usb_exit);
