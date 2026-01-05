# CRTOS Driver Modules - CMake Build Configuration
# Author: Arkadiusz Szlanta
# Date: 03 Jan 2026
#
# Description:
# Top-level CMake file for building all driver modules.
# Each driver is built as a dynamically loadable kernel module (.ko file).
#
# Usage:
#   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-cortex-m7.cmake
#   cmake --build build
#   cmake --install build

cmake_minimum_required(VERSION 3.20)

# Include toolchain file if not already set
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/arm-cortex-m7.cmake" CACHE PATH "Toolchain file")
endif()

project(crtos_drivers
    VERSION 1.0.0
    DESCRIPTION "CRTOS Driver Modules"
    LANGUAGES C CXX
)

# Options
option(BUILD_UART_DRIVER "Build UART driver module" ON)
option(BUILD_DISPLAY_DRIVER "Build Display driver module" ON)
option(BUILD_SPI_DRIVER "Build SPI driver module" ON)
option(BUILD_PINMUX_DRIVER "Build PinMux driver module" ON)
option(BUILD_I2C_DRIVER "Build I2C driver module" OFF)
option(BUILD_GPIO_DRIVER "Build GPIO driver module" OFF)

# Add driver subdirectories
if(BUILD_UART_DRIVER)
    add_subdirectory(uart)
endif()

if(BUILD_DISPLAY_DRIVER)
    add_subdirectory(display)
endif()

if(BUILD_SPI_DRIVER)
    add_subdirectory(spi)
endif()

if(BUILD_PINMUX_DRIVER)
    add_subdirectory(pinmux)
endif()

# Future drivers
# if(BUILD_I2C_DRIVER)
#     add_subdirectory(i2c)
# endif()
# 
# if(BUILD_GPIO_DRIVER)
#     add_subdirectory(gpio)
# endif()

# Print summary
message(STATUS "===========================================")
message(STATUS "CRTOS Driver Modules Build Configuration")
message(STATUS "===========================================")
message(STATUS "UART Driver: ${BUILD_UART_DRIVER}")
message(STATUS "Display Driver: ${BUILD_DISPLAY_DRIVER}")
message(STATUS "SPI Driver: ${BUILD_SPI_DRIVER}")
message(STATUS "PinMux Driver: ${BUILD_PINMUX_DRIVER}")
message(STATUS "I2C Driver: ${BUILD_I2C_DRIVER}")
message(STATUS "GPIO Driver: ${BUILD_GPIO_DRIVER}")
message(STATUS "===========================================")
