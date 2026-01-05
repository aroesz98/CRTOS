# ARM Cortex-M7 Toolchain for CRTOS Applications
# Author: Arkadiusz Szlanta
# Date: 05 Jan 2026
#
# Description:
# CMake toolchain file for building CRTOS applications.
# Configures ARM bare-metal toolchain for Cortex-M7 with PIC support.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Find ARM toolchain
find_program(ARM_GCC arm-none-eabi-gcc
    PATHS
    "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.3 rel1/bin"
    "C:/Program Files/Arm GNU Toolchain arm-none-eabi/14.3 rel1/bin"
    "/usr/bin"
    "/usr/local/bin"
)

if(ARM_GCC)
    get_filename_component(TOOLCHAIN_DIR ${ARM_GCC} DIRECTORY)
else()
    set(TOOLCHAIN_DIR "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.3 rel1/bin")
endif()

set(CMAKE_C_COMPILER "${TOOLCHAIN_DIR}/arm-none-eabi-gcc.exe")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_DIR}/arm-none-eabi-g++.exe")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_DIR}/arm-none-eabi-gcc.exe")
set(CMAKE_LINKER "${TOOLCHAIN_DIR}/arm-none-eabi-ld.exe")
set(CMAKE_OBJCOPY "${TOOLCHAIN_DIR}/arm-none-eabi-objcopy.exe")
set(CMAKE_SIZE "${TOOLCHAIN_DIR}/arm-none-eabi-size.exe")
set(CMAKE_AR "${TOOLCHAIN_DIR}/arm-none-eabi-ar.exe")

# Skip compiler tests (cross-compiling)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Compiler/linker flags
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")

set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostartfiles -nostdlib")

# PIC flags for applications
set(PIC_FLAGS "-fPIC -msingle-pic-base -mpic-register=r9 -mno-pic-data-is-text-relative -mlong-calls")

message(STATUS "Using ARM Cortex-M7 toolchain for applications")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  Linker: ${CMAKE_LINKER}")
