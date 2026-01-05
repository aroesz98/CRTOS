# Display Driver Module - CMake Build Configuration
# Author: Arkadiusz Szlanta
# Date: 04 Jan 2026
#
# Description:
# Builds the Display driver as a dynamically loadable kernel module (.ko file).
# Uses Ninja as the build generator for fast incremental builds.
#
# Usage (standalone):
#   cmake -B build -G Ninja
#   cmake --build build
#   cmake --install build
#
# Usage (from parent):
#   Included via add_subdirectory() from drivers_modules/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)

# Only define project if building standalone
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Cross-compilation toolchain
    set(CMAKE_SYSTEM_NAME Generic)
    set(CMAKE_SYSTEM_PROCESSOR arm)

    set(CMAKE_C_COMPILER arm-none-eabi-gcc)
    set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
    set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
    set(CMAKE_LINKER arm-none-eabi-ld)
    set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
    set(CMAKE_SIZE arm-none-eabi-size)

    # Disable compiler checks (cross-compiling)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

    project(display_driver
        VERSION 1.0.0
        DESCRIPTION "CRTOS Display Driver Module"
        LANGUAGES CXX
    )
    
    set(CRTOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
else()
    # Building as subdirectory
    set(CRTOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    
    # Cross-compilation toolchain (if not already set)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Generic")
        set(CMAKE_SYSTEM_NAME Generic)
        set(CMAKE_SYSTEM_PROCESSOR arm)
        set(CMAKE_C_COMPILER arm-none-eabi-gcc)
        set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
        set(CMAKE_LINKER arm-none-eabi-ld)
        set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    endif()
endif()

# Module configuration
set(MODULE_NAME "display")
set(MODULE_EXT ".ko")
set(CRTOS_INC "${CRTOS_ROOT}/CRTOS")
set(DRIVERS_INC "${CRTOS_ROOT}/CRTOS/Drivers")
set(SDCARD_DIR "${CRTOS_ROOT}/sdcard/drivers")

# CPU/Architecture flags (Cortex-M7 Thumb)
set(CPU_FLAGS
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard
)

# =============================================================================
# Position Independent Code (PIC) Configuration
# =============================================================================
# -fPIC                    : Generate position independent code
# -msingle-pic-base        : Use dedicated register for GOT base (no runtime resolver)
# -mpic-register=r9        : Use r9 as PIC base register (ARM EABI convention)
# -mno-pic-data-is-text-relative : Access data through GOT, not PC-relative
# -mlong-calls             : Generate indirect calls for all functions
#
# ModuleLoader will:
#   1. Load relocatable ELF (.ko) to RAM
#   2. Process all R_ARM_* relocations
#   3. Set r9 = GOT base before calling module entry point
# =============================================================================
set(COMMON_FLAGS
    ${CPU_FLAGS}
    -fPIC
    -msingle-pic-base
    -mpic-register=r9
    -mno-pic-data-is-text-relative
    -mlong-calls
    -ffunction-sections
    -fdata-sections
    -fno-common
    -fno-exceptions
    -fno-rtti
    -fvisibility=hidden
    -Wall
    -Wextra
    -Os
    -g
)

# Convert list to string for linker
string(REPLACE ";" " " COMMON_FLAGS_STR "${COMMON_FLAGS}")

# C++ specific flags
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Apply compiler flags
add_compile_options(${COMMON_FLAGS})

# Source files
set(DRIVER_SOURCES
    DisplayDriver.cpp
)

# Create relocatable object
add_library(${MODULE_NAME}_obj OBJECT ${DRIVER_SOURCES})

# Include directories
target_include_directories(${MODULE_NAME}_obj PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CRTOS_ROOT}
    ${CRTOS_INC}
    ${DRIVERS_INC}
)

# Create relocatable module from objects
set(MODULE_OUTPUT "${CMAKE_BINARY_DIR}/${MODULE_NAME}${MODULE_EXT}")

# Custom command to create .ko from object files
add_custom_command(
    OUTPUT ${MODULE_OUTPUT}
    COMMAND ${CMAKE_LINKER} -r -o ${MODULE_OUTPUT} $<TARGET_OBJECTS:${MODULE_NAME}_obj>
    COMMAND arm-none-eabi-strip --strip-debug ${MODULE_OUTPUT}
    COMMAND ${CMAKE_SIZE} ${MODULE_OUTPUT}
    DEPENDS ${MODULE_NAME}_obj
    COMMENT "Creating relocatable module ${MODULE_NAME}${MODULE_EXT}"
)

# Custom target for the .ko file
add_custom_target(${MODULE_NAME}_module ALL
    DEPENDS ${MODULE_OUTPUT}
)

# Install rule - copy to sdcard/drivers
install(
    FILES ${MODULE_OUTPUT}
    DESTINATION ${SDCARD_DIR}
)

# Print build info
message(STATUS "Display driver module: ${MODULE_NAME}${MODULE_EXT}")
message(STATUS "CRTOS includes: ${CRTOS_INC}")
message(STATUS "Install destination: ${SDCARD_DIR}")
