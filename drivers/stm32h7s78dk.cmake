# Kernel modules of the ST STM32H7S78-DK (drivers/CMakeLists.txt includes the file of the board,
# CRTOS_BOARD): the STM32H7S7's peripherals. Each ALIAS is a device tree "compatible" string.

# the boot flash (octal NOR on XSPI2, memory-mapped by the boot loader): /dev/mtd*, kernel
# updates; its flash code runs from this module's RAM
crtos_module(xspi-mtd SOURCES mtd/xspi-mtd.c ALIASES st,stm32-xspi)

# STM32 platform: pins, clocks, GPIO (+ EXTI interrupts)
crtos_module(pinctrl-stm32 SOURCES stm32/pinctrl-stm32.c ALIASES st,stm32h7rs-pinctrl)
crtos_module(clk-stm32 SOURCES stm32/clk-stm32.c ALIASES st,stm32h7rs-rcc)
crtos_module(gpio-stm32 SOURCES stm32/gpio-stm32.c ALIASES st,stm32h7rs-exti st,stm32h7rs-gpio)

# buses
crtos_module(i2c-stm32 SOURCES stm32/i2c-stm32.c ALIASES st,stm32-i2c)

# serial ports other than the console (UART7 on the Arduino connector: the ESP32 pad bridge)
crtos_module(uart-stm32 SOURCES stm32/uart-stm32.c ALIASES st,stm32-uart)

# display and 2D acceleration: the LTDC and its panel (/dev/fb0), the NeoChrom GPU2D
crtos_module(ltdc-stm32 SOURCES display/ltdc-stm32.c ALIASES st,stm32-ltdc)
crtos_module(gpu2d-stm32 SOURCES display/gpu2d-stm32.c ALIASES st,stm32-gpu2d)

# sound: SPI6 as I2S (GPDMA1) and the board's WM8904 codec, /dev/audio
crtos_module(i2s-stm32 SOURCES sound/i2s-stm32.c ALIASES st,stm32h7-i2s wlf,wm8904)

# real-time clock
crtos_module(rtc-stm32 SOURCES rtc/rtc-stm32.c ALIASES st,stm32-rtc)

# entropy: /dev/random
crtos_module(rng-stm32 SOURCES misc/rng-stm32.c ALIASES st,stm32-rng)

# network: the Ethernet MAC with the board's RMII PHY
crtos_module(eth-stm32 SOURCES net/eth-stm32.c ALIASES st,stm32-ethernet)

# USB: TinyUSB (third_party/tinyusb) on the Synopsys DWC2 controllers (OTG_HS, OTG_FS), as the
# device tree's dr_mode says: a device (a CDC ACM serial port, /dev/ttyACM0) or a host
# (keyboards and mice with the HID boot protocol, hubs)
set(TUSB_DIR ${CRTOS_TOP}/third_party/tinyusb/src)
crtos_module(usb-stm32 SOURCES usb/usb-stm32.c usb/usb-acm.c usb/usb-hid.c
             ${TUSB_DIR}/tusb.c ${TUSB_DIR}/common/tusb_fifo.c
             ${TUSB_DIR}/device/usbd.c ${TUSB_DIR}/class/cdc/cdc_device.c
             ${TUSB_DIR}/host/usbh.c ${TUSB_DIR}/host/hub.c ${TUSB_DIR}/class/hid/hid_host.c
             ${TUSB_DIR}/portable/synopsys/dwc2/dcd_dwc2.c ${TUSB_DIR}/portable/synopsys/dwc2/hcd_dwc2.c
             ${TUSB_DIR}/portable/synopsys/dwc2/dwc2_common.c
             INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/usb ${TUSB_DIR}
             ALIASES st,stm32-otg-hs st,stm32-otg-fs st,tcpp03)
