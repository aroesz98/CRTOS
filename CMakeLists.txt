# CRTOS - one build for the whole system.
#
#   crtos build                              (the simple way: tools/crtos.py does the rest)
#   cmake -S . -B build -G Ninja && cmake --build build
#   crtos build --rtos [DIR]                 the RTOS alone with the application in DIR
#                                            (CRTOS_PROFILE=rtos, see below)
#
# What comes out (everything under build/):
#   kernel/crtos.bin, crtos.axf   the kernel: firmware for the board's flash ('crtos flash')
#   sdcard/crtos/                 everything that goes to the SD card as /crtos
#                                 (drivers, services, programs, device tree, configuration)
#   sdk/                          ('crtos sdk') headers, libraries and CMake support for
#                                 programs built outside this tree (sdk/README.md)
#   compile_commands.json         for editors (VS Code, CLion, clangd)
#
# Where things are:
#   kernel/     the kernel and the board support (kernel/platform/evkbimxrt1050)
#   drivers/    kernel modules (.ko) for the devices of the device tree (dts/)
#   system/     the base system (user space): the programs' libraries (system/lib: libcrtos,
#               libgfx, TFTLIB), services, command-line programs, the window manager,
#               terminal, files, settings, system monitor - always built
#   apps/       optional programs with a window: games, the web browser, demos
#   tests/      tests and measurements run on the board
#   examples/   example programs, driver modules and the RTOS application
#   rootfs/     files copied to the SD card as they are (etc/*.cfg, certificates, fonts)
#   sdk/        program templates ('crtos new') and the SDK's own files
#   tools/      the crtos command (tools/crtos.py) and the build's helpers
#   third_party/  sources of other projects (lwIP, TinyUSB, pdpmake, NXP SDK drivers; NetSurf):
#               not in the repository - fetched at the commits of third_party/sources.txt
#   patches/    CRTOS' changes to them (and to the GCC built for the board)
#   docs/       documentation
cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/cmake/toolchain-arm-none-eabi.cmake)
project(crtos VERSION 0.8 LANGUAGES C CXX ASM)

option(CRTOS_BUILD_KERNEL "Build the kernel (firmware)" ON)
option(CRTOS_BUILD_APPS "Build the optional programs of apps/ (games, web browser, demos)" ON)
option(CRTOS_BUILD_TESTS "Build the tests of tests/" ON)
option(CRTOS_BUILD_EXAMPLES "Build the examples of examples/" ON)
option(CRTOS_BUILD_NETSURF "Build the NetSurf web browser (needs 'crtos setup --netsurf')" ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CRTOS_TOP ${CMAKE_CURRENT_LIST_DIR})
set(CRTOS_SDCARD ${CMAKE_BINARY_DIR}/sdcard/crtos)

include(cmake/crtos.cmake)

# The third-party sources: tools/thirdparty.py fetches the missing "base" ones (git, network;
# about 40 MB once), brings every checkout to the commit of third_party/sources.txt and applies
# patches/. Changing either of them, or a checkout (its HEAD: also one deleted), configures
# again.
execute_process(COMMAND ${Python3_EXECUTABLE} ${CRTOS_TOP}/tools/thirdparty.py --build
                WORKING_DIRECTORY ${CRTOS_TOP} RESULT_VARIABLE thirdparty_rc)
if(thirdparty_rc)
  message(FATAL_ERROR "third_party: the sources are not complete (above); "
                      "'python tools/thirdparty.py --status' shows each one")
endif()
file(GLOB_RECURSE thirdparty_deps ${CRTOS_TOP}/patches/*.patch)
file(STRINGS ${CRTOS_TOP}/third_party/sources.txt thirdparty_lines REGEX "^[^# \t]")
foreach(line ${thirdparty_lines})
  string(REGEX MATCH "^[^ \t]+" dir "${line}")
  if(EXISTS ${CRTOS_TOP}/third_party/${dir}/.git/HEAD)
    list(APPEND thirdparty_deps ${CRTOS_TOP}/third_party/${dir}/.git/HEAD)
  endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${CRTOS_TOP}/third_party/sources.txt ${thirdparty_deps})

# "os": the whole system (above); "rtos": the kernel's core alone with one application in the
# firmware - the directory CRTOS_RTOS_APP, whose CMakeLists.txt calls crtos_rtos_app() ("crtos
# build --rtos [DIR]" configures it in build/rtos)
set(CRTOS_PROFILE os CACHE STRING "os: the whole system; rtos: the kernel's core with CRTOS_RTOS_APP")
set_property(CACHE CRTOS_PROFILE PROPERTY STRINGS os rtos)
if(CRTOS_PROFILE STREQUAL "rtos")
  set(CRTOS_RTOS_APP ${CRTOS_TOP}/examples/rtos/blinky CACHE PATH "the RTOS application's directory")
  add_subdirectory(kernel)
  add_subdirectory(${CRTOS_RTOS_APP} rtos-app)
  return()
endif()

if(CRTOS_BUILD_KERNEL)
  add_subdirectory(kernel)
endif()

crtos_dtb(board ${CRTOS_TOP}/dts/evkbimxrt1050.dts)
add_subdirectory(drivers)
add_subdirectory(system)
if(CRTOS_BUILD_APPS)
  add_subdirectory(apps)
endif()
if(CRTOS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(CRTOS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
crtos_rootfs(${CRTOS_TOP}/rootfs)
add_subdirectory(sdk sdk-rules)         # build/sdk is where 'crtos sdk' puts the SDK

crtos_finish()
