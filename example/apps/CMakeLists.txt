# CRTOS Applications - Root CMake Configuration
# Author: Arkadiusz Szlanta
# Date: 05 Jan 2026
#
# Description:
# Builds all CRTOS user applications.
#
# Usage:
#   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-cortex-m7.cmake
#   cmake --build build
#   cmake --install build

cmake_minimum_required(VERSION 3.20)

project(crtos_apps
    VERSION 1.0.0
    DESCRIPTION "CRTOS User Applications"
    LANGUAGES CXX
)

# Output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Create apps directory on sdcard
set(SDCARD_APPS_DIR "${CMAKE_SOURCE_DIR}/../sdcard/apps")
file(MAKE_DIRECTORY ${SDCARD_APPS_DIR})

# Build all applications
option(BUILD_HELLO_APP "Build hello application" ON)

if(BUILD_HELLO_APP)
    add_subdirectory(hello)
endif()

# Summary
message(STATUS "===========================================")
message(STATUS "CRTOS Applications Build Configuration")
message(STATUS "===========================================")
message(STATUS "Hello App: ${BUILD_HELLO_APP}")
message(STATUS "Output: ${SDCARD_APPS_DIR}")
message(STATUS "===========================================")
