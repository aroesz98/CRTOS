# The kernel: firmware for the board's HyperFlash (runs in place, XIP).
#
# Results in build/kernel/: crtos.axf (ELF with debug information, for the debugger),
# crtos.bin (the flash image, written at 0x60000000 by 'crtos flash') and crtos.map.
#
#   rtos/       the core: tasks and scheduler, locks, message queues, timers, interrupts and
#               faults, MPU, kernel heap, console, monitor - a complete RTOS
#   os/         the operating system on it: processes and system calls, IPC, files (VFS,
#               FAT, ramfs), the program loader, driver modules, the device tree, emulated
#               memory; os/boot: SD card, device tree, modules and init at start-up
#   subsys/     the frameworks the driver modules use (clocks, GPIO, I2C, SPI, display, input,
#               network)
#   lib/, include/, platform/evkbimxrt1050/ (the NXP board support), main.cpp
#
# CRTOS_PROFILE (top CMakeLists.txt): "os" builds everything (as the MCUXpresso IDE project's
# Release configuration does, whose flags these are); "rtos" the core with the RTOS
# application (crtos_rtos_app) and without the FAT library of the platform (CONFIG_OS=0).

set(PLATFORM ${CMAKE_CURRENT_SOURCE_DIR}/platform/evkbimxrt1050)

set(KERNEL_DIRS rtos lib platform)
if(NOT CRTOS_PROFILE STREQUAL "rtos")
  list(APPEND KERNEL_DIRS os subsys)
endif()
set(KERNEL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
foreach(d ${KERNEL_DIRS})
  file(GLOB_RECURSE srcs CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/${d}/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/${d}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/${d}/*.s
    ${CMAKE_CURRENT_SOURCE_DIR}/${d}/*.S)
  list(APPEND KERNEL_SOURCES ${srcs})
endforeach()
if(CRTOS_PROFILE STREQUAL "rtos")
  list(FILTER KERNEL_SOURCES EXCLUDE REGEX "/platform/evkbimxrt1050/(fatfs/|drivers/fsl_usdhc)")
endif()

add_executable(kernel ${KERNEL_SOURCES})
if(CRTOS_PROFILE STREQUAL "rtos")
  target_compile_definitions(kernel PRIVATE CONFIG_OS=0)
endif()
set_target_properties(kernel PROPERTIES
  OUTPUT_NAME crtos
  SUFFIX .axf
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kernel)

target_compile_definitions(kernel PRIVATE
  CPU_MIMXRT1052DVL6B CPU_MIMXRT1052DVL6B_cm7 MCUXPRESSO_SDK
  XIP_EXTERNAL_FLASH=1 XIP_BOOT_HEADER_ENABLE=1 XIP_BOOT_HEADER_DCD_ENABLE=1
  SDK_DEBUGCONSOLE=1 SDK_DEBUGCONSOLE_UART PRINTF_ADVANCED_ENABLE PRINTF_FLOAT_ENABLE
  __MCUXPRESSO __USE_CMSIS __NEWLIB__ NDEBUG SKIP_SYSCLK_INIT SD_ENABLED=1 USE_RTOS=0)

target_include_directories(kernel PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/rtos
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${PLATFORM}/board
  ${PLATFORM}/drivers
  ${PLATFORM}/device
  ${PLATFORM}/device/periph
  ${PLATFORM}/CMSIS
  ${PLATFORM}/CMSIS/m-profile
  ${PLATFORM}/xip
  ${PLATFORM}/component/uart
  ${PLATFORM}/utilities
  ${PLATFORM}/utilities/str
  ${PLATFORM}/utilities/debug_console_lite
  ${PLATFORM}/fatfs/source)

set(KERNEL_CFLAGS -O3 -fno-common -funroll-loops -g -gdwarf-4 -Wall
  -ffunction-sections -fdata-sections -fno-builtin -fstack-usage)
target_compile_options(kernel PRIVATE
  ${CRTOS_CPU_FLAGS}
  -specs=nano.specs
  "$<$<COMPILE_LANGUAGE:C>:-std=gnu99;${KERNEL_CFLAGS}>"
  "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17;-fno-rtti;-fno-exceptions;${KERNEL_CFLAGS}>"
  "$<$<COMPILE_LANGUAGE:ASM>:-x;assembler-with-cpp;-D__NEWLIB__>")

set(LDDIR ${PLATFORM}/linker)
target_link_options(kernel PRIVATE
  ${CRTOS_CPU_FLAGS}
  -nostdlib
  -L${LDDIR}
  -T ${LDDIR}/crtos.ld
  -u _printf_float
  -Wl,-Map=${CMAKE_BINARY_DIR}/kernel/crtos.map
  -Wl,--gc-sections
  -Wl,--sort-section=alignment
  -Wl,--cref
  -Wl,-z,max-page-size=0x20)
set_target_properties(kernel PROPERTIES LINK_DEPENDS
  "${LDDIR}/crtos.ld;${LDDIR}/memory.ld;${LDDIR}/libs.ld")
# the C++ driver links, as in the IDE project
set_target_properties(kernel PROPERTIES LINKER_LANGUAGE CXX)

add_custom_command(TARGET kernel POST_BUILD
  COMMAND ${CRTOS_OBJCOPY} -O binary $<TARGET_FILE:kernel> ${CMAKE_BINARY_DIR}/kernel/crtos.bin
  COMMAND ${CRTOS_SIZE} $<TARGET_FILE:kernel>
  # a stack frame larger than the MPU stack guard can jump over it
  COMMAND ${Python3_EXECUTABLE} ${CRTOS_TOP}/tools/stackcheck.py ${CMAKE_CURRENT_BINARY_DIR} 256
  COMMENT "Kernel image build/kernel/crtos.bin"
  VERBATIM)
