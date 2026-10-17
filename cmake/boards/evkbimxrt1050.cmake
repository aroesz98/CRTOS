# NXP MIMXRT1050-EVKB (i.MX RT1052, Cortex-M7 600 MHz): the board CRTOS started on.
#
# What a board file sets (cmake/crtos.cmake includes the one of CRTOS_BOARD):
#   CRTOS_PLATFORM           the kernel's board support (kernel/platform/<board>)
#   CRTOS_BOARD_DTS          the device tree of the board
#   CRTOS_BOARD_GROUP        the group of third_party/sources.txt the board needs besides "base"
#   CRTOS_BOARD_DEFINE       defined for the kernel and the modules (crtos/config.h)
#   CRTOS_KERNEL_DEFINES     preprocessor definitions of the kernel
#   CRTOS_KERNEL_INCLUDES    include directories of the kernel (after kernel/, kernel/rtos and
#                            kernel/include)
#   CRTOS_KERNEL_EXCLUDE     regular expression: sources of kernel/rtos and kernel/os the board
#                            replaces with its own (none here)
#   CRTOS_KERNEL_EXTRA       more kernel sources outside the board's directory
#   CRTOS_KERNEL_IMAGE_HEADER  ON: the board's boot loader checks the kernel image, so
#                            tools/imagehdr.py writes its size and CRC-32 into crtos.bin's header
#                            after the link (OFF here: the i.MX RT boot ROM checks no CRC)
#   CRTOS_SDK_DRIVER_DIRS    where SDK_DRIVERS of crtos_module() / crtos_rtos_app() are found
#   CRTOS_SDK_INCLUDES       the SDK's headers (for the RTOS application)
#   CRTOS_MODULE_INCLUDES    include directories of the kernel modules (and the RTOS application)
#   CRTOS_MODULE_DEFINES     their preprocessor definitions
# (tools/crtos.py reads CRTOS_BOARD from the build directory and has its own table of how the
# board is flashed and reached: BOARDS there.)

set(CRTOS_PLATFORM ${CRTOS_TOP}/kernel/platform/evkbimxrt1050)
set(CRTOS_BOARD_DTS ${CRTOS_TOP}/dts/evkbimxrt1050.dts)
set(CRTOS_BOARD_GROUP "")
set(CRTOS_BOARD_DEFINE CRTOS_BOARD_EVKBIMXRT1050=1)

set(CRTOS_KERNEL_DEFINES
  CPU_MIMXRT1052DVL6B CPU_MIMXRT1052DVL6B_cm7 MCUXPRESSO_SDK
  XIP_EXTERNAL_FLASH=1 XIP_BOOT_HEADER_ENABLE=1 XIP_BOOT_HEADER_DCD_ENABLE=1
  SDK_DEBUGCONSOLE=1 SDK_DEBUGCONSOLE_UART PRINTF_ADVANCED_ENABLE PRINTF_FLOAT_ENABLE
  __MCUXPRESSO __USE_CMSIS __NEWLIB__ NDEBUG SKIP_SYSCLK_INIT SD_ENABLED=1 USE_RTOS=0)
set(CRTOS_KERNEL_INCLUDES
  ${CRTOS_PLATFORM}/board
  ${CRTOS_PLATFORM}/drivers
  ${CRTOS_PLATFORM}/device
  ${CRTOS_PLATFORM}/device/periph
  ${CRTOS_PLATFORM}/CMSIS
  ${CRTOS_PLATFORM}/CMSIS/m-profile
  ${CRTOS_PLATFORM}/xip
  ${CRTOS_PLATFORM}/component/uart
  ${CRTOS_PLATFORM}/utilities
  ${CRTOS_PLATFORM}/utilities/str
  ${CRTOS_PLATFORM}/utilities/debug_console_lite
  ${CRTOS_PLATFORM}/fatfs/source)
set(CRTOS_KERNEL_EXCLUDE "")
set(CRTOS_KERNEL_EXTRA "")
set(CRTOS_KERNEL_IMAGE_HEADER OFF)

# The NXP SDK's peripheral drivers (fsl_*.c, .h) of the i.MX RT1052, as the MCUXpresso SDK
# 25.06 repositories hold them (third_party/sources.txt): one directory per IP block, and
# several versions of some blocks (dcdc_1 or dcdc_2...) - these are the ones of this chip.
# SDK_DRIVERS names a file of one of them; all of them are on the modules' include path.
set(CRTOS_SDK ${CRTOS_TOP}/third_party/nxp-sdk)
set(CRTOS_SDK_DRIVER_DIRS ${CRTOS_SDK}/devices-rt/RT1050/MIMXRT1052/drivers)
foreach(d flexram nic301 romapi)
  list(APPEND CRTOS_SDK_DRIVER_DIRS ${CRTOS_SDK}/devices-rt/RT1050/MIMXRT1052/drivers/${d})
endforeach()
foreach(d adc_12b1msps_sar adc_etc aipstz aoi bee cache/armv7-m7 cmp common csi dcdc_1 dcp dmamux
          edma elcdif enc enet ewm flexcan flexio flexio/i2c flexio/i2s flexio/spi flexio/uart
          flexram flexspi gpc_1 gpt igpio kpp lpi2c lpspi lpuart ocotp pit pmu pwm pxp qtmr_1
          rtwdog sai semc snvs_hp snvs_lp spdif src tempmon trng tsc usdhc wdog01 xbara xbarb)
  list(APPEND CRTOS_SDK_DRIVER_DIRS ${CRTOS_SDK}/core/drivers/${d})
endforeach()
set(CRTOS_SDK_INCLUDES ${CRTOS_SDK_DRIVER_DIRS})

set(CRTOS_MODULE_INCLUDES
  ${CRTOS_TOP}/kernel/include
  ${CRTOS_PLATFORM}/device
  ${CRTOS_PLATFORM}/device/periph
  ${CRTOS_PLATFORM}/CMSIS
  ${CRTOS_PLATFORM}/CMSIS/m-profile
  ${CRTOS_PLATFORM}/drivers
  ${CRTOS_SDK_DRIVER_DIRS}
  ${CRTOS_TOP}/dts/include)
set(CRTOS_MODULE_DEFINES CPU_MIMXRT1052DVL6B CPU_MIMXRT1052DVL6B_cm7 NDEBUG CRTOS_MODULE=1
  ${CRTOS_BOARD_DEFINE})
