/*
 * pinctrl-imxrt.ko - i.MX RT IOMUXC pin controller.
 *
 * Group nodes carry "fsl,pins" entries of 6 cells: mux register, mux mode, input (daisy)
 * register, input value, pad config register (absolute addresses, as in the SDK
 * IOMUXC_* macros) and the pad configuration. Bit 30 of the configuration sets SION
 * (input path forced on), bit 31 means "leave the pad configuration alone".
 */
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/module.h>
#include <crtos/pinctrl.h>

#define PAD_SION        (1u << 30)
#define PAD_NO_CONFIG   (1u << 31)
#define MUX_SION        (1u << 4)

static inline void wr(uint32_t addr, uint32_t v)
{
    *(volatile uint32_t *)(uintptr_t)addr = v;
}

static int iomuxc_apply(void *ctx, const struct device_node *group)
{
    (void)ctx;
    uint32_t len;
    const uint8_t *v = of_get_property(group, "fsl,pins", &len);
    if (!v || len % 24)
        return -EINVAL;
    for (uint32_t i = 0; i < len; i += 24)
    {
        uint32_t mux_reg = of_be32(v + i), mux_mode = of_be32(v + i + 4);
        uint32_t in_reg = of_be32(v + i + 8), in_val = of_be32(v + i + 12);
        uint32_t conf_reg = of_be32(v + i + 16), conf = of_be32(v + i + 20);
        wr(mux_reg, mux_mode | ((conf & PAD_SION) ? MUX_SION : 0));
        if (in_reg)
            wr(in_reg, in_val);
        if (conf_reg && !(conf & PAD_NO_CONFIG))
            wr(conf_reg, conf & 0x3FFFFFFFu);
    }
    return 0;
}

static const struct pinctrl_ops iomuxc_ops = {iomuxc_apply};

static int iomuxc_probe(struct device *dev)
{
    struct clk *clk;
    int r = devm_clk_get_enabled(dev, NULL, &clk);
    if (r == -EPROBE_DEFER)
        return r;
    r = pinctrl_register(dev->of_node, &iomuxc_ops, NULL);
    if (r)
        return r;
    dev_info(dev, "i.MX RT pin controller\n");
    return 0;
}

static void iomuxc_remove(struct device *dev)
{
    pinctrl_unregister(dev->of_node);
}

static const struct of_device_id iomuxc_ids[] = {
    {"fsl,imxrt1050-iomuxc", NULL},
    {NULL, NULL},
};

static struct driver iomuxc_driver = {
    .name = "pinctrl-imxrt",
    .of_match_table = iomuxc_ids,
    .probe = iomuxc_probe,
    .remove = iomuxc_remove,
};

static int init(void)
{
    return driver_register(&iomuxc_driver);
}

static void fini(void)
{
    driver_unregister(&iomuxc_driver);
}

MODULE("pinctrl-imxrt", "i.MX RT IOMUXC pin controller", init, fini);
