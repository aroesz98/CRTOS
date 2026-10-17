# Kernel modules of the NXP MIMXRT1050-EVKB (drivers/CMakeLists.txt includes the file of the
# board, CRTOS_BOARD): the i.MX RT1052's peripherals. Each ALIAS is a device tree "compatible".

# i.MX RT platform: pins, clocks, GPIO (+ GPIO interrupts)
crtos_module(pinctrl-imxrt SOURCES imxrt/pinctrl-imxrt.c ALIASES fsl,imxrt1050-iomuxc)
crtos_module(clk-imxrt SOURCES imxrt/clk-imxrt.c ALIASES fsl,imxrt1050-ccm)
crtos_module(gpio-imxrt SOURCES imxrt/gpio-imxrt.c ALIASES fsl,imxrt1050-gpio)

# buses
crtos_module(lpi2c-imxrt SOURCES imxrt/lpi2c-imxrt.c SDK_DRIVERS fsl_lpi2c.c ALIASES fsl,imxrt1050-lpi2c)
crtos_module(lpspi-imxrt SOURCES spi/lpspi-imxrt.c SDK_DRIVERS fsl_lpspi.c ALIASES fsl,imxrt1050-lpspi)

# serial ports other than the console: /dev/ttyS*
crtos_module(lpuart-imxrt SOURCES tty/lpuart-imxrt.c SDK_DRIVERS fsl_lpuart.c ALIASES fsl,imxrt1050-lpuart)

# the boot flash (HyperFlash on the FlexSPI): /dev/mtd*, kernel updates. Its flash code runs
# from this module's RAM, so the SDK driver must be part of it.
crtos_module(flexspi-mtd SOURCES mtd/flexspi-mtd.c SDK_DRIVERS fsl_flexspi.c ALIASES fsl,imxrt1050-flexspi)

# display and 2D acceleration
crtos_module(lcdif-imxrt SOURCES display/lcdif-imxrt.c SDK_DRIVERS fsl_elcdif.c ALIASES fsl,imxrt1050-lcdif)
crtos_module(pxp-imxrt SOURCES display/pxp-imxrt.c SDK_DRIVERS fsl_pxp.c ALIASES fsl,imxrt1050-pxp)

# network
set(PHY_DIR ${CRTOS_TOP}/third_party/nxp-sdk/components/phy)
crtos_module(enet-imxrt SOURCES net/enet-imxrt.c ${PHY_DIR}/device/phyksz8081/fsl_phyksz8081.c SDK_DRIVERS fsl_enet.c
             INCLUDES ${PHY_DIR} ${PHY_DIR}/device/phyksz8081 DEFINES FSL_FEATURE_PHYKSZ8081_USE_RMII50M_MODE=1
             ALIASES fsl,imxrt1050-enet)

# sound: the SAI (I2S) and the board's WM8960 codec, /dev/audio
crtos_module(sai-imxrt SOURCES sound/sai-imxrt.c SDK_DRIVERS fsl_sai.c ALIASES fsl,imxrt1050-sai wlf,wm8960)

# real-time clock
crtos_module(rtc-snvs SOURCES rtc/rtc-snvs.c ALIASES fsl,imxrt1050-snvs-rtc)

# entropy: /dev/random
crtos_module(trng-imxrt SOURCES misc/trng-imxrt.c SDK_DRIVERS fsl_trng.c ALIASES fsl,imxrt1050-trng)

# USB: TinyUSB (third_party/tinyusb) on the ChipIdea/EHCI controller of the i.MX RT, as the
# device tree's dr_mode says: a device (a CDC ACM serial port, /dev/ttyACM0) or a host
# (keyboards and mice with the HID boot protocol, hubs)
set(TUSB_DIR ${CRTOS_TOP}/third_party/tinyusb/src)
crtos_module(usb-imxrt SOURCES usb/usb-imxrt.c usb/usb-acm.c usb/usb-hid.c
             ${TUSB_DIR}/tusb.c ${TUSB_DIR}/common/tusb_fifo.c
             ${TUSB_DIR}/device/usbd.c ${TUSB_DIR}/class/cdc/cdc_device.c
             ${TUSB_DIR}/portable/chipidea/ci_hs/dcd_ci_hs.c
             ${TUSB_DIR}/host/usbh.c ${TUSB_DIR}/host/hub.c ${TUSB_DIR}/class/hid/hid_host.c
             ${TUSB_DIR}/portable/ehci/ehci.c ${TUSB_DIR}/portable/chipidea/ci_hs/hcd_ci_hs.c
             SDK_DRIVERS fsl_clock.c
             INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/usb ${TUSB_DIR}
             ALIASES fsl,imxrt1050-usb)
