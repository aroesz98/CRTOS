/*
 * clk-imxrt.ko - i.MX RT1050 clock controller (CCM) provider.
 *
 * Clock ids (dt-bindings/clock/imxrt1050-clock.h): below 0x1000 they are CCGR gates in the
 * SDK clock_ip_name_t encoding; 0x1000+ are clock roots (rates, some can be set).
 * Gates of devices the kernel itself relies on are never switched off.
 */
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/module.h>
#include <dt-bindings/clock/imxrt1050-clock.h>
#include "fsl_clock.h"

static const uint32_t s_critical[] = {
    IMXRT1050_CLK_LPUART1,
    IMXRT1050_CLK_USDHC1,
    IMXRT1050_CLK_IOMUXC,
    IMXRT1050_CLK_IOMUXC_GPR,
    IMXRT1050_CLK_GPIO1,
    IMXRT1050_CLK_GPIO2,
    IMXRT1050_CLK_GPIO3,
    IMXRT1050_CLK_GPIO4,
    IMXRT1050_CLK_GPIO5,
    IMXRT1050_CLK_SEMC,
    IMXRT1050_CLK_OCRAM,
    IMXRT1050_CLK_FLEXSPI,
    IMXRT1050_CLK_DMA,
};

static int ccm_enable(void *ctx, uint32_t id)
{
    (void)ctx;
    if (id < IMXRT1050_CLK_ROOT_BASE)
    {
        CLOCK_EnableClock((clock_ip_name_t)id);
    }
    else if (id == IMXRT1050_CLK_ENET_REF)
    {
        /* ENET PLL (PLL6) at 50 MHz: the RMII reference clock, also given to the PHY */
        const clock_enet_pll_config_t cfg = {.enableClkOutput = true, .enableClkOutput25M = false, .loopDivider = 1};
        CLOCK_InitEnetPll(&cfg);
    }
    return 0;
}

static void ccm_disable(void *ctx, uint32_t id)
{
    (void)ctx;
    if (id >= IMXRT1050_CLK_ROOT_BASE)
        return;
    for (unsigned i = 0; i < sizeof(s_critical) / sizeof(s_critical[0]); i++)
        if (s_critical[i] == id)
            return;
    CLOCK_DisableClock((clock_ip_name_t)id);
}

static uint32_t lcdif_pre_source(void)
{
    switch (CLOCK_GetMux(kCLOCK_LcdifPreMux))
    {
    case 0:
        return CLOCK_GetFreq(kCLOCK_SysPllClk);
    case 1:
        return CLOCK_GetFreq(kCLOCK_Usb1PllPfd3Clk);
    case 2:
        return CLOCK_GetFreq(kCLOCK_VideoPllClk);
    case 3:
        return CLOCK_GetFreq(kCLOCK_SysPllPfd0Clk);
    case 4:
        return CLOCK_GetFreq(kCLOCK_SysPllPfd1Clk);
    default:
        return CLOCK_GetFreq(kCLOCK_Usb1PllPfd1Clk);
    }
}

static uint32_t ccm_get_rate(void *ctx, uint32_t id)
{
    (void)ctx;
    uint32_t src;
    switch (id)
    {
        case IMXRT1050_CLK_CPU:
            return CLOCK_GetFreq(kCLOCK_CpuClk);
        case IMXRT1050_CLK_AHB:
            return CLOCK_GetFreq(kCLOCK_AhbClk);
        case IMXRT1050_CLK_IPG:
            return CLOCK_GetFreq(kCLOCK_IpgClk);
        case IMXRT1050_CLK_PERCLK:
            return CLOCK_GetFreq(kCLOCK_PerClk);
        case IMXRT1050_CLK_SEMC_ROOT:
            return CLOCK_GetFreq(kCLOCK_SemcClk);
        case IMXRT1050_CLK_UART_ROOT:
            src = CLOCK_GetMux(kCLOCK_UartMux) ? CLOCK_GetFreq(kCLOCK_OscClk) : CLOCK_GetFreq(kCLOCK_Usb1PllClk) / 6u;
            return src / (CLOCK_GetDiv(kCLOCK_UartDiv) + 1u);
        case IMXRT1050_CLK_LPI2C_ROOT:
            src = CLOCK_GetMux(kCLOCK_Lpi2cMux) ? CLOCK_GetFreq(kCLOCK_OscClk) : CLOCK_GetFreq(kCLOCK_Usb1PllClk) / 8u;
            return src / (CLOCK_GetDiv(kCLOCK_Lpi2cDiv) + 1u);
        case IMXRT1050_CLK_LPSPI_ROOT:
        {
            static const clock_name_t sel[4] = {kCLOCK_Usb1PllPfd1Clk, kCLOCK_Usb1PllPfd0Clk, kCLOCK_SysPllClk,
                                                kCLOCK_SysPllPfd2Clk};
            return CLOCK_GetFreq(sel[CLOCK_GetMux(kCLOCK_LpspiMux) & 3u]) / (CLOCK_GetDiv(kCLOCK_LpspiDiv) + 1u);
        }
        case IMXRT1050_CLK_USDHC1_ROOT:
            src = CLOCK_GetMux(kCLOCK_Usdhc1Mux) ? CLOCK_GetFreq(kCLOCK_SysPllPfd0Clk) : CLOCK_GetFreq(kCLOCK_SysPllPfd2Clk);
            return src / (CLOCK_GetDiv(kCLOCK_Usdhc1Div) + 1u);
        case IMXRT1050_CLK_LCDIF_PIX:
            return lcdif_pre_source() / (CLOCK_GetDiv(kCLOCK_LcdifPreDiv) + 1u) / (CLOCK_GetDiv(kCLOCK_LcdifDiv) + 1u);
        case IMXRT1050_CLK_ENET_REF:
            return CLOCK_GetFreq(kCLOCK_EnetPll0Clk);
        case IMXRT1050_CLK_SAI1_ROOT:
        {
            static const clock_name_t sel[3] = {kCLOCK_Usb1PllPfd2Clk, kCLOCK_VideoPllClk, kCLOCK_AudioPllClk};
            uint32_t mux = CLOCK_GetMux(kCLOCK_Sai1Mux);
            return CLOCK_GetFreq(sel[mux < 3 ? mux : 2]) / (CLOCK_GetDiv(kCLOCK_Sai1PreDiv) + 1u) /
                (CLOCK_GetDiv(kCLOCK_Sai1Div) + 1u);
        }
        default:
            return CLOCK_GetFreq(kCLOCK_IpgClk); /* gates run on the peripheral bus clock */
    }
}

/* Pixel clock: video PLL (24 MHz * loop / post, VCO 648..1296 MHz) / pre / div */
static int set_lcdif_pix(uint32_t rate)
{
    uint32_t best_err = 0xFFFFFFFFu, bl = 0, bp = 0, bpre = 0, bdiv = 0;
    static const uint8_t posts[] = {1, 2, 4, 8, 16};
    for (unsigned pi = 0; pi < sizeof(posts); pi++)
    {
        for (uint32_t loop = 27; loop <= 54; loop++)
        {
            uint32_t pll = 24000000u * loop / posts[pi];
            for (uint32_t pre = 1; pre <= 8; pre++)
            {
                for (uint32_t div = 1; div <= 8; div++)
                {
                    uint32_t f = pll / pre / div;
                    uint32_t err = f > rate ? f - rate : rate - f;
                    if (err < best_err)
                    {
                        best_err = err;
                        bl = loop;
                        bp = posts[pi];
                        bpre = pre;
                        bdiv = div;
                    }
                }
            }
        }
    }
    if (!bl)
        return -EINVAL;
    /* the SDK ORs the new post divider into the old one without clearing it (from /1 to /2
     * gave select 3, i.e. /1: a second mode ran at twice its pixel clock) - cleared here, the
     * PLL bypassed meanwhile */
    CCM_ANALOG->PLL_VIDEO = (CCM_ANALOG->PLL_VIDEO | CCM_ANALOG_PLL_VIDEO_BYPASS_MASK) &
                            ~CCM_ANALOG_PLL_VIDEO_POST_DIV_SELECT_MASK;
    clock_video_pll_config_t cfg = {(uint8_t)bl, (uint8_t)bp, 0, 0, 0};
    CLOCK_InitVideoPll(&cfg);
    CLOCK_SetMux(kCLOCK_LcdifPreMux, 2); /* video PLL */
    CLOCK_SetDiv(kCLOCK_LcdifPreDiv, bpre - 1);
    CLOCK_SetDiv(kCLOCK_LcdifDiv, bdiv - 1);
    return 0;
}

/* Audio master clock: the audio PLL (24 MHz * (loop + num / denom), 648..1300 MHz) at 64
 * times the rate, divided by 4 and 16: 12.288 MHz for 48 kHz and its fractions, 11.2896 MHz
 * for 44.1 kHz. Only the SAIs use the audio PLL. */
static int set_sai1_root(uint32_t rate)
{
    uint64_t pll = (uint64_t)rate * 64u;
    if (pll < 648000000u || pll > 1300000000u)
        return -EINVAL;
    uint32_t loop = (uint32_t)(pll / 24000000u);
    uint32_t num = (uint32_t)(pll % 24000000u) / 24u; /* of 1000000: 24 MHz * num / 10^6 */
    CCM_ANALOG->PLL_AUDIO |= CCM_ANALOG_PLL_AUDIO_BYPASS_MASK;
    CCM_ANALOG->PLL_AUDIO_NUM = CCM_ANALOG_PLL_AUDIO_NUM_A(num);
    CCM_ANALOG->PLL_AUDIO_DENOM = CCM_ANALOG_PLL_AUDIO_DENOM_B(1000000u);
    CCM_ANALOG->MISC2 &= ~(CCM_ANALOG_MISC2_AUDIO_DIV_LSB_MASK | CCM_ANALOG_MISC2_AUDIO_DIV_MSB_MASK);
    CCM_ANALOG->PLL_AUDIO = (CCM_ANALOG->PLL_AUDIO & ~(CCM_ANALOG_PLL_AUDIO_DIV_SELECT_MASK |
                                                       CCM_ANALOG_PLL_AUDIO_POWERDOWN_MASK |
                                                       CCM_ANALOG_PLL_AUDIO_POST_DIV_SELECT_MASK |
                                                       CCM_ANALOG_PLL_AUDIO_BYPASS_CLK_SRC_MASK)) |
                            CCM_ANALOG_PLL_AUDIO_ENABLE_MASK | CCM_ANALOG_PLL_AUDIO_DIV_SELECT(loop) |
                            CCM_ANALOG_PLL_AUDIO_POST_DIV_SELECT(2); /* post divider 1 */
    for (uint32_t spin = 0; !(CCM_ANALOG->PLL_AUDIO & CCM_ANALOG_PLL_AUDIO_LOCK_MASK); spin++)
        if (spin > 1000000u)
            return -ETIMEDOUT;
    CCM_ANALOG->PLL_AUDIO &= ~CCM_ANALOG_PLL_AUDIO_BYPASS_MASK;
    CLOCK_SetMux(kCLOCK_Sai1Mux, 2);
    CLOCK_SetDiv(kCLOCK_Sai1PreDiv, 3);
    CLOCK_SetDiv(kCLOCK_Sai1Div, 15);
    return 0;
}

static uint32_t divider_for(uint32_t src, uint32_t rate, uint32_t max)
{
    uint32_t d = (src + rate / 2) / rate;
    if (d < 1)
        d = 1;
    if (d > max)
        d = max;
    return d - 1;
}

static int ccm_set_rate(void *ctx, uint32_t id, uint32_t rate)
{
    (void)ctx;
    if (!rate)
        return -EINVAL;
    switch (id)
    {
    case IMXRT1050_CLK_LCDIF_PIX:
        return set_lcdif_pix(rate);
    case IMXRT1050_CLK_SAI1_ROOT:
        return set_sai1_root(rate);
    case IMXRT1050_CLK_LPI2C_ROOT: /* PLL3 / 8 = 60 MHz */
        CLOCK_SetMux(kCLOCK_Lpi2cMux, 0);
        CLOCK_SetDiv(kCLOCK_Lpi2cDiv, divider_for(CLOCK_GetFreq(kCLOCK_Usb1PllClk) / 8u, rate, 64));
        return 0;
    case IMXRT1050_CLK_UART_ROOT: /* PLL3 / 6 = 80 MHz; the console shares it: only if unchanged */
        return ccm_get_rate(NULL, id) == rate ? 0 : -EBUSY;
    case IMXRT1050_CLK_LPSPI_ROOT: /* PLL3 PFD0 / 1..8 (the gates of the LPSPIs must be off) */
        CLOCK_SetMux(kCLOCK_LpspiMux, 1);
        CLOCK_SetDiv(kCLOCK_LpspiDiv, divider_for(CLOCK_GetFreq(kCLOCK_Usb1PllPfd0Clk), rate, 8));
        return 0;
    default:
        return -ENOTSUP;
    }
}

static const struct clk_ops ccm_ops = {ccm_enable, ccm_disable, ccm_get_rate, ccm_set_rate};

static int ccm_probe(struct device *dev)
{
    int r = clk_provider_register(dev->of_node, &ccm_ops, NULL);
    if (r)
        return r;
    dev_info(dev, "clock controller: cpu %lu MHz, ahb %lu MHz, ipg %lu MHz\n",
             (unsigned long)(CLOCK_GetFreq(kCLOCK_CpuClk) / 1000000u),
             (unsigned long)(CLOCK_GetFreq(kCLOCK_AhbClk) / 1000000u),
             (unsigned long)(CLOCK_GetFreq(kCLOCK_IpgClk) / 1000000u));
    return 0;
}

static void ccm_remove(struct device *dev)
{
    clk_provider_unregister(dev->of_node);
}

static const struct of_device_id ccm_ids[] = {
    {"fsl,imxrt1050-ccm", NULL},
    {NULL, NULL},
};

static struct driver ccm_driver = {
    .name = "clk-imxrt",
    .of_match_table = ccm_ids,
    .probe = ccm_probe,
    .remove = ccm_remove,
};

static int init(void)
{
    return driver_register(&ccm_driver);
}

static void fini(void)
{
    driver_unregister(&ccm_driver);
}

MODULE("clk-imxrt", "i.MX RT1050 clock controller", init, fini);
