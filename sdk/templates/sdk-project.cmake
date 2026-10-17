# A program for CRTOS built with the CRTOS SDK: "crtos build" in this directory
# (or: cmake -S . -B build -G Ninja -DCRTOS_SDK=<the SDK directory> && cmake --build build).
# The result is build/sdcard/crtos/@DEST@/@NAME@.app; "crtos deploy" sends it to the board.
cmake_minimum_required(VERSION 3.20)
if(NOT CRTOS_SDK)
  set(CRTOS_SDK $ENV{CRTOS_SDK})
endif()
if(NOT EXISTS "${CRTOS_SDK}/cmake/crtos-sdk.cmake")
  message(FATAL_ERROR "CRTOS_SDK: pass the SDK directory (-DCRTOS_SDK=... or the environment variable)")
endif()
set(CMAKE_TOOLCHAIN_FILE ${CRTOS_SDK}/cmake/toolchain-arm-none-eabi.cmake)
project(@NAME@ C CXX ASM)
include(${CRTOS_SDK}/cmake/crtos-sdk.cmake)
set(CRTOS_DEFAULT_DEST @DEST@)
