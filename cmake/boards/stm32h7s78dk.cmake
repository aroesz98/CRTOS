# ST STM32H7S78-DK (STM32H7S7L8, Cortex-M7 600 MHz): 5" 800x480 LCD with GT911 touch, NeoChrom
# GPU2D, 32 MB PSRAM (XSPI1), 128 MB octal NOR flash (XSPI2), microSD, Ethernet, two USB ports,
# WM8904 audio codec, ST-LINK V3 (SWD and the console's virtual COM port).
#
# The chip has 64 KB of internal flash: the boot loader (kernel/platform/stm32h7s78dk/boot)
# lives there and starts the kernel, which runs in place from the NOR flash at 0x70000000.
# See cmake/boards/evkbimxrt1050.cmake for what a board file sets.

set(CRTOS_PLATFORM ${CRTOS_TOP}/kernel/platform/stm32h7s78dk)
set(CRTOS_BOARD_DTS ${CRTOS_TOP}/dts/stm32h7s78dk.dts)
set(CRTOS_BOARD_GROUP stm32)
set(CRTOS_BOARD_DEFINE CRTOS_BOARD_STM32H7S78DK=1)

set(CRTOS_STM32_HAL ${CRTOS_TOP}/third_party/stm32/hal-h7rs)
set(CRTOS_STM32_CMSIS ${CRTOS_TOP}/third_party/stm32/cmsis-device-h7rs/Include)
# the Cortex-M7 core headers (CMSIS 6) are the same for every board; they live with the first one
set(CRTOS_CMSIS_CORE ${CRTOS_TOP}/kernel/platform/evkbimxrt1050/CMSIS ${CRTOS_TOP}/kernel/platform/evkbimxrt1050/CMSIS/m-profile)
# FatFs (the SD card's file system) as well
set(CRTOS_FATFS ${CRTOS_TOP}/kernel/platform/evkbimxrt1050/fatfs/source)

set(CRTOS_KERNEL_DEFINES STM32H7S7xx NDEBUG)
set(CRTOS_KERNEL_INCLUDES
  ${CRTOS_PLATFORM}/board
  ${CRTOS_STM32_CMSIS}
  ${CRTOS_CMSIS_CORE}
  ${CRTOS_FATFS})
# the console and the SD card are the board's own (kernel/platform/stm32h7s78dk), and the
# boot loader is a program of its own (boot/CMakeLists.txt)
set(CRTOS_KERNEL_EXCLUDE "/kernel/rtos/console_lpuart\\.cpp$|/kernel/os/boot/sdcard\\.cpp$|/platform/stm32h7s78dk/(boot|hal)/")
set(CRTOS_KERNEL_EXTRA ${CRTOS_FATFS}/ff.c ${CRTOS_FATFS}/ffunicode.c)
# the boot loader checks the image's header (board/image.h: size, CRC-32)
set(CRTOS_KERNEL_IMAGE_HEADER ON)

# SDK_DRIVERS of a module: the STM32CubeH7RS HAL/LL sources (stm32h7rsxx_hal_x.c, _ll_x.c);
# a module using the HAL defines HAL_GetTick() and HAL_Delay() itself (the kernel has none;
# the modules of drivers/ use the registers through the CMSIS device header)
set(CRTOS_SDK_DRIVER_DIRS ${CRTOS_STM32_HAL}/Src)
set(CRTOS_SDK_INCLUDES ${CRTOS_STM32_HAL}/Inc ${CRTOS_PLATFORM}/hal)

set(CRTOS_MODULE_INCLUDES
  ${CRTOS_TOP}/kernel/include
  ${CRTOS_PLATFORM}/board
  ${CRTOS_PLATFORM}/hal
  ${CRTOS_STM32_CMSIS}
  ${CRTOS_CMSIS_CORE}
  ${CRTOS_STM32_HAL}/Inc
  ${CRTOS_TOP}/drivers/stm32
  ${CRTOS_TOP}/dts/include)
set(CRTOS_MODULE_DEFINES STM32H7S7xx NDEBUG CRTOS_MODULE=1 ${CRTOS_BOARD_DEFINE})
