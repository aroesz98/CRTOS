# CRTOS SDK: programs for CRTOS built outside the CRTOS source tree. A program's
# CMakeLists.txt ("crtos new NAME" writes one):
#
#   cmake_minimum_required(VERSION 3.20)
#   set(CMAKE_TOOLCHAIN_FILE ${CRTOS_SDK}/cmake/toolchain-arm-none-eabi.cmake)
#   project(hello C CXX ASM)
#   include(${CRTOS_SDK}/cmake/crtos-sdk.cmake)
#   crtos_app(hello SOURCES hello.c LIBS gfx)
#
# crtos_app() is described in crtos-app.cmake. The programs go to build/sdcard/crtos/<DEST>/
# ("crtos deploy" sends them to the board). The SDK directory is also the toolchain
# directory: lib/crtos.specs holds the rules of compiling and linking a program, the same
# ones bin/arm-crtos-gcc uses.

get_filename_component(CRTOS_SDK ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
set(CRTOS_SDCARD ${CMAKE_BINARY_DIR}/sdcard/crtos)
set(CRTOS_TOOLS ${CRTOS_SDK}/tools)
set(CRTOS_SYSROOT ${CRTOS_SDK})
set(CRTOS_SPECS ${CRTOS_SDK}/lib/crtos.specs)
set(CRTOS_APP_INCLUDES ${CRTOS_SDK}/include)
include(${CMAKE_CURRENT_LIST_DIR}/crtos-app.cmake)

# the libraries, as the CRTOS build made them (the start-up objects are found by crtos.specs)
add_library(crtos STATIC IMPORTED)
set_target_properties(crtos PROPERTIES IMPORTED_LOCATION ${CRTOS_SDK}/lib/libcrtos.a)
add_library(gfx STATIC IMPORTED)
set_target_properties(gfx PROPERTIES IMPORTED_LOCATION ${CRTOS_SDK}/lib/libgfx.a
  CRTOS_PUBLIC_INCLUDES ${CRTOS_SDK}/include)
add_library(tftlib STATIC IMPORTED)
set_target_properties(tftlib PROPERTIES IMPORTED_LOCATION ${CRTOS_SDK}/lib/libtftlib.a
  CRTOS_PUBLIC_INCLUDES ${CRTOS_SDK}/include/tftlib)
# ... and for programs that run in place (crtos_app XIP)
if(EXISTS ${CRTOS_SDK}/lib/xip/libcrtos.a)
  add_library(crtos_xip STATIC IMPORTED)
  set_target_properties(crtos_xip PROPERTIES IMPORTED_LOCATION ${CRTOS_SDK}/lib/xip/libcrtos.a)
  add_library(gfx_xip STATIC IMPORTED)
  set_target_properties(gfx_xip PROPERTIES IMPORTED_LOCATION ${CRTOS_SDK}/lib/xip/libgfx.a
    CRTOS_PUBLIC_INCLUDES ${CRTOS_SDK}/include)
  add_library(tftlib_xip STATIC IMPORTED)
  set_target_properties(tftlib_xip PROPERTIES IMPORTED_LOCATION ${CRTOS_SDK}/lib/xip/libtftlib.a
    CRTOS_PUBLIC_INCLUDES ${CRTOS_SDK}/include/tftlib)
endif()
