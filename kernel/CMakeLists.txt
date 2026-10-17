# The kernel: firmware for the board's flash (runs in place, XIP).
#
# Results in build/kernel/: crtos.axf (ELF with debug information, for the debugger),
# crtos.bin (the flash image, written by 'crtos flash': at 0x60000000 into the HyperFlash of the
# i.MX RT board, at 0x70000000 into the NOR flash of the STM32 board) and crtos.map; on the
# STM32 board also crtos-boot.bin, the boot loader for its internal flash (platform/
# stm32h7s78dk/boot).
#
#   rtos/       the core: tasks and scheduler, locks, message queues, timers, interrupts and
#               faults, MPU, kernel heap, console, monitor - a complete RTOS
#   os/         the operating system on it: processes and system calls, IPC, files (VFS,
#               FAT, ramfs), the program loader, driver modules, the device tree, emulated
#               memory; os/boot: SD card, device tree, modules and init at start-up
#   subsys/     the frameworks the driver modules use (clocks, GPIO, I2C, SPI, display, input,
#               network)
#   lib/, include/, main.cpp
#   platform/<board>/  the board support of CRTOS_BOARD (cmake/boards/<board>.cmake):
#               evkbimxrt1050 (NXP's SDK files), stm32h7s78dk (start-up, console, SD card)
#
# CRTOS_PROFILE (top CMakeLists.txt): "os" builds everything (as the MCUXpresso IDE project's
# Release configuration does, whose flags these are); "rtos" the core with the RTOS
# application (crtos_rtos_app) and without the FAT library of the platform (CONFIG_OS=0).

set(PLATFORM ${CRTOS_PLATFORM})

set(KERNEL_DIRS rtos lib)
if(NOT CRTOS_PROFILE STREQUAL "rtos")
  list(APPEND KERNEL_DIRS os subsys)
endif()
set(KERNEL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
foreach(d ${KERNEL_DIRS} ${PLATFORM})
  if(NOT IS_ABSOLUTE ${d})
    set(d ${CMAKE_CURRENT_SOURCE_DIR}/${d})
  endif()
  file(GLOB_RECURSE srcs CONFIGURE_DEPENDS ${d}/*.c ${d}/*.cpp ${d}/*.s ${d}/*.S)
  list(APPEND KERNEL_SOURCES ${srcs})
endforeach()
if(CRTOS_KERNEL_EXCLUDE)
  list(FILTER KERNEL_SOURCES EXCLUDE REGEX "${CRTOS_KERNEL_EXCLUDE}")
endif()
list(APPEND KERNEL_SOURCES ${CRTOS_KERNEL_EXTRA})
if(CRTOS_PROFILE STREQUAL "rtos")
  # no SD card in the RTOS: FatFs, the card drivers and the board's SD card code stay out
  list(FILTER KERNEL_SOURCES EXCLUDE REGEX "/fatfs/|/drivers/fsl_usdhc|/sdcard[^/]*[.]cpp$")
endif()

add_executable(kernel ${KERNEL_SOURCES})
if(CRTOS_PROFILE STREQUAL "rtos")
  target_compile_definitions(kernel PRIVATE CONFIG_OS=0)
endif()
set_target_properties(kernel PROPERTIES
  OUTPUT_NAME crtos
  SUFFIX .axf
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kernel)

target_compile_definitions(kernel PRIVATE ${CRTOS_KERNEL_DEFINES} ${CRTOS_BOARD_DEFINE})

target_include_directories(kernel PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/rtos
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CRTOS_KERNEL_INCLUDES})

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

# the board's boot loader (a program of its own, in the board's boot/), when it has one
if(EXISTS ${PLATFORM}/boot/CMakeLists.txt)
  add_subdirectory(${PLATFORM}/boot boot)
endif()

# a board whose boot loader checks the image (CRTOS_KERNEL_IMAGE_HEADER; STM32: size and CRC-32
# in its header) gets them written into crtos.bin
if(CRTOS_KERNEL_IMAGE_HEADER)
  set(KERNEL_IMAGE_CMD COMMAND ${Python3_EXECUTABLE} ${CRTOS_TOP}/tools/imagehdr.py ${CMAKE_BINARY_DIR}/kernel/crtos.bin)
endif()

add_custom_command(TARGET kernel POST_BUILD
  COMMAND ${CRTOS_OBJCOPY} -O binary $<TARGET_FILE:kernel> ${CMAKE_BINARY_DIR}/kernel/crtos.bin
  ${KERNEL_IMAGE_CMD}
  COMMAND ${CRTOS_SIZE} $<TARGET_FILE:kernel>
  # a stack frame larger than the MPU stack guard can jump over it
  COMMAND ${Python3_EXECUTABLE} ${CRTOS_TOP}/tools/stackcheck.py ${CMAKE_CURRENT_BINARY_DIR} 256
  COMMENT "Kernel image build/kernel/crtos.bin"
  VERBATIM)
