/*
 * enet-imxrt.ko - the i.MX RT 10/100 Ethernet MAC (ENET) with an RMII PHY (the KSZ8081 of
 * the EVKB) as a network interface (crtos/net.h).
 *
 * The SDK driver (fsl_enet) sets the controller up; the rings are run here. The buffer
 * descriptors live in non-cacheable memory. Sending copies the frame into a non-cacheable
 * buffer of the transmit ring; when the ring is full the sender waits for the transmit
 * interrupt, which is on only then. Receiving: the receive interrupt wakes a thread that hands
 * each frame to the stack straight from its buffer - in cached memory, invalidated first
 * (reading non-cacheable memory was the slowest part of a frame) - and gives the descriptor
 * back to the controller.
 *
 * The controller checks and fills in the checksums (IPv4 header, TCP, UDP, ICMP): frames with
 * a wrong one are dropped on reception (NETDEV_F_RX_CSUM), and those sent get theirs inserted
 * (NETDEV_F_TX_CSUM, store and forward; the fields are cleared first, see clear_csums()); the
 * stack computes none for this interface.
 *
 * The chip gives the PHY its 50 MHz reference clock (ENET PLL, GPR1). The PHY (SDK
 * fsl_phyksz8081 over MDIO) is reset through its GPIO and negotiates by itself; the thread
 * checks its link once a second and sets the MAC's speed and duplex to match.
 *
 * Device tree (see dts/): reg, interrupts ("enet" first), clocks "ipg" and "ref" (the
 * reference clock), phy-reset-gpios, phy-reset-duration (ms), phy-handle (its "reg" is the
 * MDIO address), optional local-mac-address. Without one the MAC address is made from the
 * chip's unique ID (a locally administered address).
 */
#include <stdbool.h>
#include <string.h>
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/gpio.h>
#include <crtos/irq.h>
#include <crtos/mm.h>
#include <crtos/module.h>
#include <crtos/net.h>
#include <crtos/printk.h>
#include <crtos/sched.h>
#include <crtos/sync.h>
#include "fsl_cache.h"
#include "fsl_enet.h"
#include "fsl_phy.h"
#include "fsl_phyksz8081.h"

#define RX_RING 32u
#define TX_RING 16u
#define BUF_SIZE SDK_SIZEALIGN(ENET_FRAME_MAX_FRAMELEN, ENET_BUFF_ALIGNMENT)
#define TX_WAITS 20             /* waits of TX_WAIT_MS for a free transmit descriptor at most */
#define TX_WAIT_MS 5
#define LINK_CHECK_MS 1000
#define ANAR_PAUSE 0x0400u   /* auto-negotiation: pause frames (symmetric) */
#define ANAR_ASM_DIR 0x0800u /* ... asymmetric */
#define DTCM_START 0x20000000u
#define DTCM_END 0x20080000u

struct enet
{
    struct device *dev;
    ENET_Type *base;
    int irq;
    enet_handle_t handle;
    struct netdev nd;
    struct semaphore rx_sem; /* given by the receive interrupt */
    struct semaphore tx_sem; /* given by the transmit interrupt (on while a sender waits) */
    struct mutex tx_lock;
    task_t *thread;
    phy_handle_t phy;
    phy_ksz8081_resource_t phy_res;
    uint32_t phy_addr;
    struct gpio_desc *phy_reset;
    uint32_t reset_ms;
    bool phy_ready;
    void *rx_desc, *tx_desc; /* the descriptors: DTCM or non-cacheable (desc_alloc()) */
    uint8_t *tx_bufs;        /* the transmit buffers: non-cacheable */
    uint8_t *rx_bufs;        /* the receive buffers: cached */
    uint32_t rx_err[5];     /* damaged frames by cause: too long, not whole bytes, CRC, overrun, truncated */
    uint32_t rx_err_logged;
};

/* The SDK PHY driver's MDIO functions have no context: one controller */
static struct enet *s_mdio;

static status_t mdio_write(uint8_t phy, uint8_t reg, uint16_t v)
{
    return ENET_MDIOWrite(s_mdio->base, phy, reg, v);
}

static status_t mdio_read(uint8_t phy, uint8_t reg, uint16_t *v)
{
    return ENET_MDIORead(s_mdio->base, phy, reg, v);
}

static void enet_callback(ENET_Type *base, enet_handle_t *handle, enet_event_t event, enet_frame_info_t *info,
                          void *user)
{
    (void)handle;
    (void)info;
    struct enet *e = user;
    if (event == kENET_RxEvent)
    {
        sem_give(&e->rx_sem);
    }
    else if (event == kENET_TxEvent && (base->EIMR & ENET_EIMR_TXF_MASK))
    { /* a frame has gone: a sender waits for its descriptor */
        ENET_DisableInterrupts(base, kENET_TxFrameInterrupt);
        sem_give(&e->tx_sem);
    }
}

static void enet_irq(int irq, void *ctx)
{
    (void)irq;
    struct enet *e = ctx;
    ENET_CommonFrame0IRQHandler(e->base);
}

/* The controller fills in the checksums of IPv4 frames, adding the field into its sum: a field
 * that is not 0 (a program's own ICMP checksum, over a raw socket) would come out wrong. Clear
 * the fields it fills in: the IP header's always, the protocol's unless the frame is a
 * fragment (fragments it leaves as they are). */
static void clear_csums(uint8_t *f, uint32_t len)
{
    if (len < 14u + 20u || f[12] != 0x08u || f[13] != 0x00u)
        return;
    uint8_t *ip = f + 14;
    uint32_t ihl = (ip[0] & 0x0Fu) * 4u, at;
    if ((ip[0] >> 4) != 4u || ihl < 20u || 14u + ihl > len)
        return;
    ip[10] = ip[11] = 0;
    if ((((uint32_t)ip[6] << 8) | ip[7]) & 0x3FFFu) /* more fragments, or an offset */
        return;
    switch (ip[9])
    {
    case 1: /* ICMP */
        at = 2;
        break;
    case 6: /* TCP */
        at = 16;
        break;
    case 17: /* UDP */
        at = 6;
        break;
    default:
        return;
    }
    if (14u + ihl + at + 2u <= len)
        ip[ihl + at] = ip[ihl + at + 1u] = 0;
}

/* One frame into the transmit ring (copied: its buffers are non-cacheable); false: the ring is
 * full */
static bool tx_frame(struct enet *e, const void *frame, uint32_t len)
{
    enet_tx_bd_ring_t *ring = &e->handle.txBdRing[0];
    volatile enet_tx_bd_struct_t *bd = ring->txBdBase + ring->txGenIdx;
    if (bd->control & ENET_BUFFDESCRIPTOR_TX_READY_MASK)
        return false;
    uint8_t *buf = (uint8_t *)(uintptr_t)bd->buffer;
    memcpy(buf, frame, len);
    clear_csums(buf, len);
    bd->length = (uint16_t)len;
    bd->control |= ENET_BUFFDESCRIPTOR_TX_READY_MASK | ENET_BUFFDESCRIPTOR_TX_LAST_MASK;
    ring->txGenIdx = ring->txGenIdx + 1u == ring->txRingLen ? 0u : (uint16_t)(ring->txGenIdx + 1u);
    __DSB();
    e->base->TDAR = ENET_TDAR_TDAR_MASK;
    return true;
}

static int enet_xmit(struct netdev *nd, const void *frame, size_t len)
{
    struct enet *e = nd->priv;
    if (!nd->link_up || len > BUF_SIZE)
    {
        nd->tx_dropped++;
        return len > BUF_SIZE ? -EMSGSIZE : -ENETDOWN;
    }
    bool sent;
    int waits = 0;
    mutex_lock(&e->tx_lock, WAIT_FOREVER);
    for (;;)
    {
        sent = tx_frame(e, frame, (uint32_t)len);
        if (sent || waits++ == TX_WAITS)
            break;
        /* The ring is full (it empties at 8 frames a millisecond): wait for the next frame to
         * go. A frame that went meanwhile left its event set, so the interrupt comes at once. */
        ENET_EnableInterrupts(e->base, kENET_TxFrameInterrupt);
        sem_take(&e->tx_sem, TX_WAIT_MS);
    }
    if (waits)
        ENET_DisableInterrupts(e->base, kENET_TxFrameInterrupt);
    mutex_unlock(&e->tx_lock);
    if (!sent)
    {
        nd->tx_dropped++;
        return -EAGAIN;
    }
    nd->tx_packets++;
    nd->tx_bytes += (uint32_t)len;
    return 0;
}

static int enet_open(struct netdev *nd)
{
    (void)nd;
    return 0;
}

static void enet_stop(struct netdev *nd)
{
    (void)nd;
}

static const struct netdev_ops enet_ops = {enet_open, enet_stop, enet_xmit};

static void phy_setup(struct enet *e)
{
    if (e->phy_reset)
    { /* a clean reset: 10 ms low, then time to come up */
        gpiod_set_value(e->phy_reset, 1);
        task_sleep_ms(e->reset_ms);
        gpiod_set_value(e->phy_reset, 0);
        task_sleep_ms(20);
    }
    e->phy_res.read = mdio_read;
    e->phy_res.write = mdio_write;
    phy_config_t pc;
    memset(&pc, 0, sizeof(pc));
    pc.phyAddr = e->phy_addr;
    pc.ops = &phyksz8081_ops;
    pc.resource = &e->phy_res;
    pc.autoNeg = true;
    e->phy.ops = &phyksz8081_ops;
    status_t st = PHY_Init(&e->phy, &pc);
    if (st != kStatus_Success)
    {
        dev_err(e->dev, "PHY at %lu does not answer (%ld)\n", (unsigned long)e->phy_addr, (long)st);
        return;
    }
    /* also offer flow control (802.3x pause frames), then negotiate again */
    uint16_t anar = 0;
    if (mdio_read((uint8_t)e->phy_addr, PHY_AUTONEG_ADVERTISE_REG, &anar) == kStatus_Success)
    {
        mdio_write((uint8_t)e->phy_addr, PHY_AUTONEG_ADVERTISE_REG, anar | ANAR_PAUSE | ANAR_ASM_DIR);
        mdio_write((uint8_t)e->phy_addr, PHY_BASICCONTROL_REG, PHY_BCTL_AUTONEG_MASK | PHY_BCTL_RESTART_AUTONEG_MASK);
    }
    e->phy_ready = true;
    dev_info(e->dev, "PHY KSZ8081 at MDIO %lu, negotiating\n", (unsigned long)e->phy_addr);
}

/* Pause frames when the receive FIFO fills (the link partner waits a moment instead of the
 * FIFO overflowing), if the partner negotiated them too */
static void set_flow_control(struct enet *e, bool full_duplex)
{
    uint16_t lpa = 0;
    bool on = full_duplex && mdio_read((uint8_t)e->phy_addr, PHY_AUTONEG_LINKPARTNER_REG, &lpa) == kStatus_Success &&
              (lpa & (ANAR_PAUSE | ANAR_ASM_DIR));
    if (on)
    {
        e->base->OPD = ENET_OPD_PAUSE_DUR(0xFFF0u);
        e->base->RSEM = ENET_RSEM_RX_SECTION_EMPTY(0x84u); /* ~1 KB in the FIFO: send a pause */
        e->base->RCR |= ENET_RCR_FCE_MASK;
    }
    else
    {
        e->base->RSEM = 0;
        e->base->RCR &= ~ENET_RCR_FCE_MASK;
    }
    dev_info(e->dev, "flow control %s\n", on ? "on" : "off (the other side does not offer it)");
}

static void check_link(struct enet *e)
{
    bool up = false;
    if (!e->phy_ready || PHY_GetLinkStatus(&e->phy, &up) != kStatus_Success)
        return;
    if (up && !e->nd.link_up)
    {
        phy_speed_t speed = kPHY_Speed100M;
        phy_duplex_t duplex = kPHY_FullDuplex;
        PHY_GetLinkSpeedDuplex(&e->phy, &speed, &duplex);
        ENET_SetMII(e->base, speed == kPHY_Speed10M ? kENET_MiiSpeed10M : kENET_MiiSpeed100M,
                    duplex == kPHY_FullDuplex ? kENET_MiiFullDuplex : kENET_MiiHalfDuplex);
        set_flow_control(e, duplex == kPHY_FullDuplex);
        netdev_set_link(&e->nd, true, speed == kPHY_Speed10M ? 10u : 100u, duplex == kPHY_FullDuplex);
    }
    else if (!up && e->nd.link_up)
    {
        netdev_set_link(&e->nd, false, 0, false);
    }
}

/* A damaged frame: counted by cause, the first few logged (to tell a cabling fault from
 * overload) */
static void rx_error(struct enet *e, uint16_t ctl)
{
    static const uint16_t bits[5] = {ENET_BUFFDESCRIPTOR_RX_LENVLIOLATE_MASK, ENET_BUFFDESCRIPTOR_RX_NOOCTET_MASK,
                                     ENET_BUFFDESCRIPTOR_RX_CRC_MASK, ENET_BUFFDESCRIPTOR_RX_OVERRUN_MASK,
                                     ENET_BUFFDESCRIPTOR_RX_TRUNC_MASK};
    for (int i = 0; i < 5; i++)
        if (ctl & bits[i])
            e->rx_err[i]++;
    e->nd.rx_errors++;
    if (e->rx_err_logged < 8)
    {
        e->rx_err_logged++;
        dev_warn(e->dev, "receive errors: %lu too long, %lu alignment, %lu CRC, %lu overrun, %lu truncated\n",
                 (unsigned long)e->rx_err[0], (unsigned long)e->rx_err[1], (unsigned long)e->rx_err[2],
                 (unsigned long)e->rx_err[3], (unsigned long)e->rx_err[4]);
    }
}

/* The frames the controller has written: each goes to the stack straight from its buffer
 * (the stack copies it), then the descriptor back to the controller. A buffer holds a whole
 * frame (BUF_SIZE), so a frame of several descriptors is one too long. */
static void rx_frames(struct enet *e)
{
    enet_rx_bd_ring_t *ring = &e->handle.rxBdRing[0];
    for (;;)
    {
        volatile enet_rx_bd_struct_t *bd = ring->rxBdBase + ring->rxGenIdx;
        uint16_t ctl = bd->control;
        if (ctl & ENET_BUFFDESCRIPTOR_RX_EMPTY_MASK)
            break;
        uint32_t len = bd->length; /* (without the FCS) */
        uint8_t *buf = (uint8_t *)(uintptr_t)bd->buffer;
        if (!(ctl & ENET_BUFFDESCRIPTOR_RX_LAST_MASK))
        {
            /* the start of a frame too long for a buffer: dropped with the rest */
        }
        else if ((ctl & ENET_BUFFDESCRIPTOR_RX_ERR_MASK) || !len || len > BUF_SIZE)
        {
            rx_error(e, ctl);
        }
        else
        {
            /* lines of it may still be cached from the buffer's previous frame (we only read) */
            DCACHE_InvalidateByRange((uint32_t)buf, len);
            netdev_rx(&e->nd, buf, len);
        }
        bd->control = (uint16_t)((ctl & ENET_BUFFDESCRIPTOR_RX_WRAP_MASK) | ENET_BUFFDESCRIPTOR_RX_EMPTY_MASK);
        ring->rxGenIdx = ring->rxGenIdx + 1u == ring->rxRingLen ? 0u : (uint16_t)(ring->rxGenIdx + 1u);
        __DSB();
        e->base->RDAR = ENET_RDAR_RDAR_MASK; /* (it may have stopped on a ring with no empty descriptor) */
    }
}

/* Received frames go to the stack from here; the link is watched as well */
static void enet_thread(void *arg)
{
    struct enet *e = arg;
    phy_setup(e);
    uint32_t last = tick_get() - LINK_CHECK_MS;
    while (!task_should_stop())
    {
        sem_take(&e->rx_sem, 200);
        rx_frames(e);
        if (tick_get() - last >= LINK_CHECK_MS)
        {
            last = tick_get();
            check_link(e);
        }
    }
}

static void mac_address(struct enet *e)
{
    const void *prop = of_get_property(e->dev->of_node, "local-mac-address", NULL);
    if (prop)
    {
        memcpy(e->nd.mac, prop, 6);
        return;
    }
    /* from the unique ID in the fuses: locally administered, unicast */
    uint32_t u0 = OCOTP->CFG0, u1 = OCOTP->CFG1;
    e->nd.mac[0] = 0x02;
    e->nd.mac[1] = (uint8_t)(u1 >> 8);
    e->nd.mac[2] = (uint8_t)u1;
    e->nd.mac[3] = (uint8_t)(u0 >> 16);
    e->nd.mac[4] = (uint8_t)(u0 >> 8);
    e->nd.mac[5] = (uint8_t)u0;
}

static const char *where(const void *p)
{
    uintptr_t a = (uintptr_t)p;
    return a >= DTCM_START && a < DTCM_END ? "DTCM" : a >= 0x20200000u && a < 0x20280000u ? "OCRAM" : "SDRAM";
}

static void free_dma(struct enet *e)
{
    kfree(e->rx_desc);
    kfree(e->tx_desc);
    kfree(e->tx_bufs);
    kfree(e->rx_bufs);
    e->rx_desc = e->tx_desc = NULL;
    e->tx_bufs = e->rx_bufs = NULL;
}

/* Descriptors in DTCM when there is room: not cached, and the controller reaches it through
 * the core's slave port at once. In SDRAM a descriptor, read before each frame, waited behind
 * the display and the 2D accelerator, and the receive FIFO overflowed meanwhile. */
static void *desc_alloc(size_t size)
{
    uint8_t *p = kmalloc_aligned(size, ENET_BUFF_ALIGNMENT, KM_FAST);
    if (p && ((uintptr_t)p < DTCM_START || (uintptr_t)p >= DTCM_END))
    { /* (KM_FAST goes on to other pools) */
        kfree(p);
        p = NULL;
    }
    if (!p)
        p = kmalloc_aligned(size, ENET_BUFF_ALIGNMENT, KM_NOCACHE | KM_DMA);
    if (p)
        memset(p, 0, size);
    return p;
}

static int enet_probe(struct device *dev)
{
    if (s_mdio)
        return -EBUSY;
    struct enet *e = devm_kzalloc(dev, sizeof(*e), 0);
    if (!e)
        return -ENOMEM;
    e->dev = dev;
    e->base = device_map(dev, 0);
    e->irq = device_get_irq(dev, 0);
    if (!e->base || e->irq < 0)
        return e->irq == -EPROBE_DEFER ? -EPROBE_DEFER : -ENODEV;
    struct clk *clk;
    int r = devm_clk_get_enabled(dev, "ipg", &clk);
    if (!r)
        r = devm_clk_get_enabled(dev, "ref", &clk);
    if (r)
        return r;
    r = gpiod_get(dev, "phy-reset", GPIOD_OUT_HIGH, &e->phy_reset); /* held in reset for now */
    if (r == -EPROBE_DEFER)
        return r;
    if (r)
        e->phy_reset = NULL;
    e->reset_ms = 10;
    of_property_read_u32(dev->of_node, "phy-reset-duration", &e->reset_ms);
    e->phy_addr = 0;
    struct device_node *phy = of_parse_phandle(dev->of_node, "phy-handle", 0);
    if (phy)
        of_property_read_u32(phy, "reg", &e->phy_addr);

    /* the chip drives the 50 MHz reference clock out to the PHY */
    IOMUXC_GPR->GPR1 = (IOMUXC_GPR->GPR1 & ~IOMUXC_GPR_GPR1_ENET_TX_CLK_SEL_MASK) | IOMUXC_GPR_GPR1_ENET_REF_CLK_DIR_MASK;

    /* 64-byte aligned: the descriptors (desc_alloc()), the transmit buffers in non-cacheable
     * memory, the receive buffers in cached memory - on the chip when there is room (OCRAM),
     * whole lines (invalidating one touches nothing else) */
    size_t rxbd = SDK_SIZEALIGN(RX_RING * sizeof(enet_rx_bd_struct_t), ENET_BUFF_ALIGNMENT);
    size_t txbd = SDK_SIZEALIGN(TX_RING * sizeof(enet_tx_bd_struct_t), ENET_BUFF_ALIGNMENT);
    e->rx_desc = desc_alloc(rxbd);
    e->tx_desc = desc_alloc(txbd);
    e->tx_bufs = kmalloc_aligned(TX_RING * BUF_SIZE, ENET_BUFF_ALIGNMENT, KM_NOCACHE | KM_DMA);
    e->rx_bufs = kmalloc_aligned(RX_RING * BUF_SIZE, ENET_BUFF_ALIGNMENT, KM_DMA);
    if (!e->rx_desc || !e->tx_desc || !e->tx_bufs || !e->rx_bufs)
    {
        free_dma(e);
        return -ENOMEM;
    }
    memset(e->tx_bufs, 0, TX_RING * BUF_SIZE);
    /* no line of the buffers may be written back over what the controller puts there */
    DCACHE_InvalidateByRange((uint32_t)e->rx_bufs, RX_RING * BUF_SIZE);
    enet_buffer_config_t bc;
    memset(&bc, 0, sizeof(bc));
    bc.rxBdNumber = RX_RING;
    bc.txBdNumber = TX_RING;
    bc.rxBuffSizeAlign = BUF_SIZE;
    bc.txBuffSizeAlign = BUF_SIZE;
    bc.rxBdStartAddrAlign = (volatile enet_rx_bd_struct_t *)e->rx_desc;
    bc.txBdStartAddrAlign = (volatile enet_tx_bd_struct_t *)e->tx_desc;
    bc.rxBufferAlign = e->rx_bufs;
    bc.txBufferAlign = e->tx_bufs;
    bc.rxMaintainEnable = false; /* (rx_frames() invalidates what it reads) */
    bc.txMaintainEnable = false; /* non-cacheable */

    mac_address(e);
    enet_config_t cfg;
    ENET_GetDefaultConfig(&cfg);
    cfg.miiMode = kENET_RmiiMode;
    cfg.miiSpeed = kENET_MiiSpeed100M;
    cfg.miiDuplex = kENET_MiiFullDuplex;
    /* (the SDK installs its handler of an interrupt only if it is on here: the transmit one is
     * turned off right after, and on only while a sender waits) */
    cfg.interrupt = kENET_RxFrameInterrupt | kENET_TxFrameInterrupt;
    cfg.txAccelerConfig = kENET_TxAccelIpCheckEnabled | kENET_TxAccelProtoCheckEnabled;
    cfg.rxAccelerConfig = kENET_RxAccelIpCheckEnabled | kENET_RxAccelProtoCheckEnabled | kENET_RxAccelPadRemoveEnabled;
    cfg.callback = enet_callback;
    cfg.userData = e;

    sem_init(&e->rx_sem, 0, 1);
    sem_init(&e->tx_sem, 0, 1);
    mutex_init(&e->tx_lock);
    s_mdio = e;
    status_t st = ENET_Init(e->base, &e->handle, &cfg, &bc, e->nd.mac, CLOCK_GetFreq(kCLOCK_IpgClk));
    if (st != kStatus_Success)
    {
        s_mdio = NULL;
        free_dma(e);
        dev_err(dev, "ENET_Init failed (%ld)\n", (long)st);
        return -EIO;
    }
    ENET_DisableInterrupts(e->base, kENET_TxFrameInterrupt);
    r = irq_request(e->irq, enet_irq, e, 6, dev->name);
    if (r)
    {
        ENET_Deinit(e->base);
        s_mdio = NULL;
        free_dma(e);
        return r;
    }
    ENET_ActiveRead(e->base);

    e->nd.ops = &enet_ops;
    e->nd.priv = e;
    e->nd.mtu = 1500;
    e->nd.features = NETDEV_F_TX_CSUM | NETDEV_F_RX_CSUM;
    netdev_register(&e->nd);
    dev_set_drvdata(dev, e);
    e->thread = kthread_create("enet", enet_thread, e, PRIO_HIGH - 2, 2048);
    if (!e->thread)
    {
        netdev_unregister(&e->nd);
        irq_free(e->irq);
        ENET_Deinit(e->base);
        s_mdio = NULL;
        free_dma(e);
        return -ENOMEM;
    }
    dev_info(dev, "ENET 10/100, RMII, %d rx / %d tx descriptors in %s, receive buffers in %s, checksums in hardware\n",
             RX_RING, TX_RING, where(e->rx_desc), where(e->rx_bufs));
    return 0;
}

static void enet_remove(struct device *dev)
{
    struct enet *e = dev_get_drvdata(dev);
    kthread_stop(e->thread);
    netdev_unregister(&e->nd);
    irq_free(e->irq);
    ENET_Deinit(e->base);
    free_dma(e);
    s_mdio = NULL;
}

static const struct of_device_id enet_ids[] = {
    {"fsl,imxrt1050-enet", NULL},
    {NULL, NULL},
};

static struct driver enet_driver = {
    .name = "enet-imxrt",
    .of_match_table = enet_ids,
    .probe = enet_probe,
    .remove = enet_remove,
};

static int init(void)
{
    return driver_register(&enet_driver);
}

static void fini(void)
{
    driver_unregister(&enet_driver);
}

MODULE("enet-imxrt", "i.MX RT ENET 10/100 Ethernet with a KSZ8081 PHY", init, fini);
