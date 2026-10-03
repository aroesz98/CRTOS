# Kernel modules (layer 1: drivers). Each ALIAS is a device tree "compatible" string.
# (Example modules: examples/drivers; the display check touchpaint: tests/drivers.)

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
crtos_module(flashfs SOURCES fs/flashfs.c ALIASES crtos,flashfs)

# input
crtos_module(ft5406 SOURCES input/ft5406.c ALIASES focaltech,ft5406)
crtos_module(gt911 SOURCES input/gt911.c ALIASES goodix,gt911)
crtos_module(gpio-keys SOURCES input/gpio-keys.c ALIASES gpio-keys)
crtos_module(esp32-pad SOURCES input/esp32-pad.c ALIASES crtos,esp32-pad)

# display and 2D acceleration
crtos_module(lcdif-imxrt SOURCES display/lcdif-imxrt.c SDK_DRIVERS fsl_elcdif.c ALIASES fsl,imxrt1050-lcdif)
crtos_module(pxp-imxrt SOURCES display/pxp-imxrt.c SDK_DRIVERS fsl_pxp.c ALIASES fsl,imxrt1050-pxp)

# network
set(PHY_DIR ${CRTOS_TOP}/third_party/nxp-sdk/components/phy)
crtos_module(enet-imxrt SOURCES net/enet-imxrt.c ${PHY_DIR}/device/phyksz8081/fsl_phyksz8081.c SDK_DRIVERS fsl_enet.c
             INCLUDES ${PHY_DIR} ${PHY_DIR}/device/phyksz8081 DEFINES FSL_FEATURE_PHYKSZ8081_USE_RMII50M_MODE=1
             ALIASES fsl,imxrt1050-enet)

# the TCP/IP stack: lwIP (IPv4) with its sockets, behind the kernel's socket calls
set(LWIP_DIR ${CRTOS_TOP}/third_party/lwip/src)
set(LWIP_SOURCES
  core/init.c core/def.c core/dns.c core/inet_chksum.c core/ip.c core/mem.c core/memp.c core/netif.c core/pbuf.c
  core/raw.c core/stats.c core/sys.c core/tcp.c core/tcp_in.c core/tcp_out.c core/timeouts.c core/udp.c
  core/ipv4/dhcp.c core/ipv4/etharp.c core/ipv4/icmp.c core/ipv4/igmp.c core/ipv4/ip4.c core/ipv4/ip4_addr.c
  core/ipv4/ip4_frag.c
  api/api_lib.c api/api_msg.c api/err.c api/netbuf.c api/netdb.c api/sockets.c api/tcpip.c
  netif/ethernet.c)
list(TRANSFORM LWIP_SOURCES PREPEND ${LWIP_DIR}/)
crtos_module(net-lwip SOURCES net/lwip/crtos_lwip.c net/lwip/lwip_side.c net/lwip/sys_arch.c ${LWIP_SOURCES}
             INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/net/lwip ${LWIP_DIR}/include
             ALIASES crtos,lwip)

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

# checks: sh /sd/crtos/share/flashfs/flashtest.sh
crtos_file(${CMAKE_CURRENT_SOURCE_DIR}/fs/test/flashtest.sh share/flashfs/flashtest.sh)
