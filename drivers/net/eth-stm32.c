/*
 * eth-stm32.ko - the STM32H7S7's Ethernet MAC (Synopsys Ethernet QoS) with an RMII PHY (the
 * LAN8742A of the STM32H7S78-DK) as a network interface (crtos/net.h).
 *
 * Register level, no vendor library. The DMA runs two rings of normal descriptors (16 bytes,
 * non-cacheable memory); each descriptor has a whole-frame buffer of BUF_SIZE in cached
 * memory (the DMA reaches AXI SRAM and PSRAM, not the TCMs): a frame to send is copied into
 * its buffer and the lines cleaned, a received one invalidated before the stack reads it.
 * The tail pointers tell the DMA how far it may go: transmit, up to the descriptor after the
 * last frame queued; receive, up to the last descriptor given back (as ST's HAL keeps it).
 *
 * The MAC fills in and checks the IPv4 header and TCP/UDP/ICMP checksums (transmit and
 * receive store and forward; damaged frames are dropped): NETDEV_F_TX_CSUM | RX_CSUM. The
 * fields it fills in are cleared first, as for enet-imxrt (a raw socket's own ICMP checksum).
 *
 * Receiving: the interrupt wakes a thread that hands each frame to the stack from its buffer
 * and gives the descriptor back. Sending waits for the transmit interrupt only while the ring
 * is full. The thread checks the PHY (standard MII registers over MDIO) once a second and sets
 * the MAC's speed and duplex to what it negotiated.
 *
 * The board: the PHY at MDIO address 0 makes the 50 MHz reference clock (REF_CLK) itself;
 * without it the MAC's reset never ends. On the STM32H7S78-DK, MDC is PC1 only with JP6 in
 * that position.
 *
 * Device tree: reg, interrupts, clocks (MAC, TX, RX), phy-handle (its "reg": the MDIO
 * address), optional local-mac-address; without one the address comes from the chip's unique
 * ID (locally administered).
 */
#include <stdbool.h>
#include <string.h>
#include <crtos/arch.h>
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/irq.h>
#include <crtos/mm.h>
#include <crtos/module.h>
#include <crtos/net.h>
#include <crtos/printk.h>
#include <crtos/sched.h>
#include <crtos/sync.h>
#include "stm32h7rsxx.h"

#define RX_RING 16u
#define TX_RING 8u
#define BUF_SIZE 1536u /* a whole frame, a multiple of the 32-byte cache line */
#define TX_WAITS 20    /* waits of TX_WAIT_MS for a free transmit descriptor at most */
#define TX_WAIT_MS 5
#define LINK_CHECK_MS 1000

/* descriptor bits (RM0477: TDES3 / RDES3) */
#define D3_OWN 0x80000000u
#define TD2_IOC 0x80000000u
#define TD3_FD 0x20000000u
#define TD3_LD 0x10000000u
#define TD3_CIC_FULL 0x00030000u /* IP header + payload checksums, pseudo-header included */
#define RD3_IOC 0x40000000u
#define RD3_BUF1V 0x01000000u
#define RD3_FD 0x20000000u
#define RD3_LD 0x10000000u
#define RD3_ES 0x00008000u
#define RD3_PL 0x00007fffu

/* MII registers */
#define MII_BMCR 0
#define MII_BMSR 1
#define MII_ID1 2
#define MII_ANAR 4
#define MII_ANLPAR 5
#define BMCR_RESET 0x8000u
#define BMCR_ANENABLE 0x1000u
#define BMCR_ANRESTART 0x0200u
#define BMSR_LINK 0x0004u
#define ANAR_CAPS 0x01e1u /* 100 FD, 100 HD, 10 FD, 10 HD, IEEE 802.3 */
#define AN_100FD 0x0100u  /* (the same bits in ANAR and ANLPAR) */
#define AN_100HD 0x0080u
#define AN_10FD 0x0040u

struct desc
{
    volatile uint32_t d[4];
};

struct eth
{
    struct device *dev;
    ETH_TypeDef *r;
    int irq;
    struct netdev nd;
    struct semaphore rx_sem; /* given by the receive interrupt */
    struct semaphore tx_sem; /* given by the transmit interrupt (on while a sender waits) */
    struct mutex tx_lock;
    task_t *thread;
    uint32_t phy;
    bool phy_ok;
    struct desc *rx, *tx;    /* the rings: non-cacheable */
    uint8_t *rx_buf, *tx_buf; /* cached */
    uint32_t rx_next;        /* the next receive descriptor to look at */
    uint32_t tx_next;        /* the next transmit descriptor to fill */
    volatile uint32_t bus_errors; /* fatal bus errors of the DMA (it stops) */
    uint32_t bus_errors_logged, rx_err_logged;
};

/* ---- MDIO ---------------------------------------------------------------------------------- */

static int mdio_wait(ETH_TypeDef *r)
{
    uint32_t t0 = cpu_cycles();
    while (r->MACMDIOAR & ETH_MACMDIOAR_MB)
        if (cpu_cycles() - t0 > SystemCoreClock / 100u)
            return -ETIMEDOUT;
    return 0;
}

/* (CR 0101: HCLK / 124, 2.4 MHz at 300 MHz) */
#define MDIO_CR (5u << ETH_MACMDIOAR_CR_Pos)

static int mdio_read(struct eth *e, uint32_t reg, uint16_t *v)
{
    ETH_TypeDef *r = e->r;
    if (mdio_wait(r))
        return -ETIMEDOUT;
    r->MACMDIOAR = (e->phy << ETH_MACMDIOAR_PA_Pos) | (reg << ETH_MACMDIOAR_RDA_Pos) | MDIO_CR | ETH_MACMDIOAR_MOC_RD |
                   ETH_MACMDIOAR_MB;
    if (mdio_wait(r))
        return -ETIMEDOUT;
    *v = (uint16_t)r->MACMDIODR;
    return 0;
}

static int mdio_write(struct eth *e, uint32_t reg, uint16_t v)
{
    ETH_TypeDef *r = e->r;
    if (mdio_wait(r))
        return -ETIMEDOUT;
    r->MACMDIODR = v;
    r->MACMDIOAR = (e->phy << ETH_MACMDIOAR_PA_Pos) | (reg << ETH_MACMDIOAR_RDA_Pos) | MDIO_CR | ETH_MACMDIOAR_MOC_WR |
                   ETH_MACMDIOAR_MB;
    return mdio_wait(r);
}

/* ---- sending ------------------------------------------------------------------------------- */

static void eth_irq(int irq, void *ctx)
{
    (void)irq;
    struct eth *e = ctx;
    ETH_TypeDef *r = e->r;
    uint32_t sr = r->DMACSR;
    r->DMACSR = sr; /* write one to clear */
    if (sr & (ETH_DMACSR_RI | ETH_DMACSR_RBU))
        sem_give(&e->rx_sem);
    if ((sr & ETH_DMACSR_TI) && (r->DMACIER & ETH_DMACIER_TIE))
    {
        r->DMACIER &= ~ETH_DMACIER_TIE; /* a frame went: the sender that waited goes on */
        sem_give(&e->tx_sem);
    }
    if (sr & ETH_DMACSR_FBE)
        e->bus_errors++;
}

/* The MAC fills in the checksums of IPv4 frames, adding the field into its sum: clear the
 * fields it fills in (the IP header's always, the protocol's unless the frame is a fragment) */
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

/* One frame into the transmit ring; false: the ring is full */
static bool tx_frame(struct eth *e, const void *frame, uint32_t len)
{
    struct desc *d = &e->tx[e->tx_next];
    if (d->d[3] & D3_OWN)
        return false;
    uint8_t *buf = e->tx_buf + e->tx_next * BUF_SIZE;
    memcpy(buf, frame, len);
    clear_csums(buf, len);
    SCB_CleanDCache_by_Addr(buf, (int32_t)len);
    d->d[0] = (uint32_t)(uintptr_t)buf;
    d->d[1] = 0;
    d->d[2] = TD2_IOC | len;
    d->d[3] = D3_OWN | TD3_FD | TD3_LD | TD3_CIC_FULL | len;
    e->tx_next = (e->tx_next + 1u) % TX_RING;
    __asm volatile("dsb 0xF" ::: "memory");
    e->r->DMACTDTPR = (uint32_t)(uintptr_t)&e->tx[e->tx_next];
    return true;
}

static int eth_xmit(struct netdev *nd, const void *frame, size_t len)
{
    struct eth *e = nd->priv;
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
        /* the ring is full: wait for a frame to go (the interrupt is on only now) */
        while (sem_take(&e->tx_sem, 0) == 0)
        {
        }
        e->r->DMACIER |= ETH_DMACIER_TIE;
        if (!(e->tx[e->tx_next].d[3] & D3_OWN))
            continue;
        sem_take(&e->tx_sem, TX_WAIT_MS);
    }
    e->r->DMACIER &= ~ETH_DMACIER_TIE;
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

static int eth_open(struct netdev *nd)
{
    (void)nd;
    return 0;
}

static void eth_stop(struct netdev *nd)
{
    (void)nd;
}

static const struct netdev_ops eth_ops = {eth_open, eth_stop, eth_xmit};

/* ---- receiving ----------------------------------------------------------------------------- */

static void rx_arm(struct eth *e, uint32_t i)
{
    struct desc *d = &e->rx[i];
    d->d[0] = (uint32_t)(uintptr_t)(e->rx_buf + i * BUF_SIZE);
    d->d[1] = 0;
    d->d[2] = 0;
    __asm volatile("dsb 0xF" ::: "memory");
    d->d[3] = D3_OWN | RD3_IOC | RD3_BUF1V;
}

/* The frames the DMA has written: each to the stack from its buffer, then the descriptor
 * back. A buffer holds a whole frame, so a frame of several descriptors is one too long. */
static void rx_frames(struct eth *e)
{
    for (int n = 0; n < (int)RX_RING; n++)
    {
        uint32_t i = e->rx_next;
        struct desc *d = &e->rx[i];
        uint32_t d3 = d->d[3];
        if (d3 & D3_OWN)
            break;
        uint32_t len = d3 & RD3_PL;
        uint8_t *buf = e->rx_buf + i * BUF_SIZE;
        if ((d3 & (RD3_FD | RD3_LD)) != (RD3_FD | RD3_LD) || (d3 & RD3_ES) || !len || len > BUF_SIZE)
        {
            e->nd.rx_errors++;
            if (e->rx_err_logged < 8)
            {
                e->rx_err_logged++;
                dev_warn(e->dev, "damaged frame (RDES3 %08lx RDES1 %08lx)\n", (unsigned long)d3,
                         (unsigned long)d->d[1]);
            }
        }
        else
        {
            SCB_InvalidateDCache_by_Addr(buf, (int32_t)len);
            netdev_rx(&e->nd, buf, len);
        }
        rx_arm(e, i);
        e->rx_next = (i + 1u) % RX_RING;
        e->r->DMACRDTPR = (uint32_t)(uintptr_t)d; /* (it may have stopped: no descriptor left) */
    }
}

/* ---- the PHY and the link ------------------------------------------------------------------ */

static void phy_setup(struct eth *e)
{
    uint16_t id1 = 0, v = 0;
    if (mdio_read(e, MII_ID1, &id1) || id1 == 0xffffu || id1 == 0)
    {
        dev_err(e->dev, "no PHY at MDIO %lu (is JP6 on PC1?)\n", (unsigned long)e->phy);
        return;
    }
    mdio_write(e, MII_BMCR, BMCR_RESET);
    for (int i = 0; i < 50; i++)
    {
        task_sleep_ms(10);
        if (!mdio_read(e, MII_BMCR, &v) && !(v & BMCR_RESET))
            break;
    }
    mdio_write(e, MII_ANAR, ANAR_CAPS);
    mdio_write(e, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
    e->phy_ok = true;
    dev_info(e->dev, "PHY %04x at MDIO %lu, negotiating\n", id1, (unsigned long)e->phy);
}

static void check_link(struct eth *e)
{
    uint16_t bmsr = 0, anar = 0, lpa = 0;
    if (!e->phy_ok || mdio_read(e, MII_BMSR, &bmsr) || mdio_read(e, MII_BMSR, &bmsr)) /* (latched low) */
        return;
    bool up = bmsr & BMSR_LINK;
    if (up && !e->nd.link_up)
    {
        if (mdio_read(e, MII_ANAR, &anar) || mdio_read(e, MII_ANLPAR, &lpa))
            return;
        uint16_t common = anar & lpa;
        bool fast = common & (AN_100FD | AN_100HD), full = fast ? (common & AN_100FD) : (common & AN_10FD);
        ETH_TypeDef *r = e->r;
        uint32_t cr = r->MACCR & ~(ETH_MACCR_TE | ETH_MACCR_RE | ETH_MACCR_FES | ETH_MACCR_DM);
        r->MACCR = cr;
        r->MACCR = cr | (fast ? ETH_MACCR_FES : 0u) | (full ? ETH_MACCR_DM : 0u) | ETH_MACCR_TE | ETH_MACCR_RE;
        netdev_set_link(&e->nd, true, fast ? 100u : 10u, full);
    }
    else if (!up && e->nd.link_up)
    {
        netdev_set_link(&e->nd, false, 0, false);
    }
}

/* Received frames go to the stack from here; the link is watched as well */
static void eth_thread(void *arg)
{
    struct eth *e = arg;
    phy_setup(e);
    uint32_t last = tick_get() - LINK_CHECK_MS;
    while (!task_should_stop())
    {
        sem_take(&e->rx_sem, 200);
        rx_frames(e);
        if (e->bus_errors != e->bus_errors_logged)
        {
            e->bus_errors_logged = e->bus_errors;
            dev_err(e->dev, "DMA bus error: the interface has stopped (reload the module)\n");
        }
        if (tick_get() - last >= LINK_CHECK_MS)
        {
            last = tick_get();
            check_link(e);
        }
    }
}

/* ---- the device ---------------------------------------------------------------------------- */

static void mac_address(struct eth *e)
{
    const void *prop = of_get_property(e->dev->of_node, "local-mac-address", NULL);
    if (prop)
    {
        memcpy(e->nd.mac, prop, 6);
        return;
    }
    /* from the unique ID: locally administered, unicast; its words folded together */
    uint8_t id[12] = {0};
    board_unique_id(id, sizeof(id));
    e->nd.mac[0] = 0x02;
    for (int i = 0; i < 5; i++)
        e->nd.mac[1 + i] = (uint8_t)(id[i] ^ id[i + 5] ^ (i < 2 ? id[10 + i] : 0));
}

static void free_dma(struct eth *e)
{
    kfree(e->rx);
    kfree(e->tx);
    kfree(e->rx_buf);
    kfree(e->tx_buf);
    e->rx = e->tx = NULL;
    e->rx_buf = e->tx_buf = NULL;
}

/* The MAC, MTL and DMA as ST's HAL sets them up by default, the rings, the address */
static int mac_init(struct eth *e)
{
    ETH_TypeDef *r = e->r;
    r->DMAMR |= ETH_DMAMR_SWR;
    uint32_t t0 = cpu_cycles();
    while (r->DMAMR & ETH_DMAMR_SWR)
        if (cpu_cycles() - t0 > SystemCoreClock / 10u)
            return -ETIMEDOUT; /* no reference clock from the PHY */
    r->MAC1USTCR = SystemCoreClock / 2u / 1000000u - 1u; /* (HCLK) */
    r->MACCR = ETH_MACCR_IPC | ETH_MACCR_CST | ETH_MACCR_ACS | ETH_MACCR_FES | ETH_MACCR_DM;
    r->MACPFR = ETH_MACPFR_PM; /* our address, broadcast, all multicast */
    r->MACA0HR = ((uint32_t)e->nd.mac[5] << 8) | e->nd.mac[4];
    r->MACA0LR = ((uint32_t)e->nd.mac[3] << 24) | ((uint32_t)e->nd.mac[2] << 16) | ((uint32_t)e->nd.mac[1] << 8) |
                 e->nd.mac[0];
    r->MMCRIMR = 0xffffffffu; /* no counter interrupts */
    r->MMCTIMR = 0xffffffffu;
    r->MTLTQOMR |= ETH_MTLTQOMR_TSF;                                     /* store and forward */
    r->MTLRQOMR = (r->MTLRQOMR & ~ETH_MTLRQOMR_DISTCPEF) | ETH_MTLRQOMR_RSF; /* (and checksum errors dropped) */
    r->DMASBMR = ETH_DMASBMR_AAL | ETH_DMASBMR_FB;
    r->DMACCR = 0;                                                       /* 16-byte descriptors */
    r->DMACTCR = 32u << ETH_DMACTCR_TPBL_Pos;
    r->DMACRCR = (32u << ETH_DMACRCR_RPBL_Pos) | (BUF_SIZE << ETH_DMACRCR_RBSZ_Pos);

    memset(e->tx, 0, TX_RING * sizeof(struct desc));
    e->tx_next = 0;
    r->DMACTDRLR = TX_RING - 1u;
    r->DMACTDLAR = (uint32_t)(uintptr_t)e->tx;
    r->DMACTDTPR = (uint32_t)(uintptr_t)e->tx;
    for (uint32_t i = 0; i < RX_RING; i++)
        rx_arm(e, i);
    e->rx_next = 0;
    r->DMACRDRLR = RX_RING - 1u;
    r->DMACRDLAR = (uint32_t)(uintptr_t)e->rx;
    r->DMACRDTPR = (uint32_t)(uintptr_t)&e->rx[RX_RING - 1u];

    r->DMACSR = 0xffffffffu;
    r->DMACIER = ETH_DMACIER_NIE | ETH_DMACIER_RIE | ETH_DMACIER_AIE | ETH_DMACIER_FBEE | ETH_DMACIER_RBUE;
    r->DMACTCR |= ETH_DMACTCR_ST;
    r->DMACRCR |= ETH_DMACRCR_SR;
    /* (the MAC's transmitter and receiver go on with the link: check_link()) */
    return 0;
}

static int eth_probe(struct device *dev)
{
    struct eth *e = devm_kzalloc(dev, sizeof(*e), 0);
    if (!e)
        return -ENOMEM;
    e->dev = dev;
    e->r = device_map(dev, 0);
    e->irq = device_get_irq(dev, 0);
    if (!e->r || e->irq < 0)
        return e->irq == -EPROBE_DEFER ? -EPROBE_DEFER : -ENODEV;
    e->phy = 0;
    struct device_node *phy = of_parse_phandle(dev->of_node, "phy-handle", 0);
    if (phy)
        of_property_read_u32(phy, "reg", &e->phy);

    /* RMII, chosen while the MAC is held in reset */
    RCC->AHB1RSTR |= RCC_AHB1RSTR_ETH1RST;
    SBS->PMCR = (SBS->PMCR & ~SBS_PMCR_ETH_PHYSEL) | SBS_PMCR_ETH_PHYSEL_2;
    (void)SBS->PMCR;
    RCC->AHB1RSTR &= ~RCC_AHB1RSTR_ETH1RST;
    for (int i = 0; i < 3; i++)
    {
        struct clk *clk;
        static const char *const names[] = {"mac", "tx", "rx"};
        int r = devm_clk_get_enabled(dev, names[i], &clk);
        if (r)
            return r;
    }

    e->rx = kmalloc_aligned(RX_RING * sizeof(struct desc), 64, KM_NOCACHE | KM_DMA);
    e->tx = kmalloc_aligned(TX_RING * sizeof(struct desc), 64, KM_NOCACHE | KM_DMA);
    e->rx_buf = kmalloc_aligned(RX_RING * BUF_SIZE, 32, KM_DMA);
    e->tx_buf = kmalloc_aligned(TX_RING * BUF_SIZE, 32, KM_DMA);
    if (!e->rx || !e->tx || !e->rx_buf || !e->tx_buf)
    {
        free_dma(e);
        return -ENOMEM;
    }
    /* no line of the buffers may be written back over what the DMA puts there */
    SCB_CleanInvalidateDCache_by_Addr(e->rx_buf, (int32_t)(RX_RING * BUF_SIZE));

    mac_address(e);
    sem_init(&e->rx_sem, 0, 1);
    sem_init(&e->tx_sem, 0, 1);
    mutex_init(&e->tx_lock);
    int r = mac_init(e);
    if (r)
    {
        dev_err(dev, "the MAC does not come out of reset: no 50 MHz reference clock from the PHY\n");
        free_dma(e);
        return r;
    }
    r = irq_request(e->irq, eth_irq, e, 6, dev->name);
    if (r)
    {
        e->r->DMACTCR &= ~ETH_DMACTCR_ST;
        e->r->DMACRCR &= ~ETH_DMACRCR_SR;
        free_dma(e);
        return r;
    }
    e->nd.ops = &eth_ops;
    e->nd.priv = e;
    e->nd.mtu = 1500;
    e->nd.features = NETDEV_F_TX_CSUM | NETDEV_F_RX_CSUM;
    netdev_register(&e->nd);
    dev_set_drvdata(dev, e);
    e->thread = kthread_create("eth", eth_thread, e, PRIO_HIGH - 2, 2048);
    if (!e->thread)
    {
        netdev_unregister(&e->nd);
        irq_free(e->irq);
        e->r->DMACTCR &= ~ETH_DMACTCR_ST;
        e->r->DMACRCR &= ~ETH_DMACRCR_SR;
        free_dma(e);
        return -ENOMEM;
    }
    dev_info(dev, "Ethernet 10/100, RMII, %02x:%02x:%02x:%02x:%02x:%02x, checksums in hardware\n", e->nd.mac[0],
             e->nd.mac[1], e->nd.mac[2], e->nd.mac[3], e->nd.mac[4], e->nd.mac[5]);
    return 0;
}

static void eth_remove(struct device *dev)
{
    struct eth *e = dev_get_drvdata(dev);
    kthread_stop(e->thread);
    netdev_unregister(&e->nd);
    ETH_TypeDef *r = e->r;
    r->MACCR &= ~(ETH_MACCR_TE | ETH_MACCR_RE);
    r->DMACTCR &= ~ETH_DMACTCR_ST;
    r->DMACRCR &= ~ETH_DMACRCR_SR;
    r->DMACIER = 0;
    irq_free(e->irq);
    task_sleep_ms(2); /* (a frame the DMA was writing) */
    free_dma(e);
}

static const struct of_device_id eth_ids[] = {
    {"st,stm32-ethernet", NULL},
    {NULL, NULL},
};

static struct driver eth_driver = {
    .name = "eth-stm32",
    .of_match_table = eth_ids,
    .probe = eth_probe,
    .remove = eth_remove,
};

static int init(void)
{
    return driver_register(&eth_driver);
}

static void fini(void)
{
    driver_unregister(&eth_driver);
}

MODULE("eth-stm32", "STM32 Ethernet MAC with an RMII PHY", init, fini);
