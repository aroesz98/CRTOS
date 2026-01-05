# UART Driver Module - CMake Build Configuration
# Author: Arkadiusz Szlanta
# Date: 05 Jan 2026
#
# Description:
# Builds the UART driver as a relocatable PIC kernel module (.ko file).
# Module is loaded at runtime by CRTOS DriverLoader/ModuleLoader.
#
# Usage (standalone):
#   cmake -B build -G Ninja
#   cmake --build build
#   cmake --install build

cmake_minimum_required(VERSION 3.20)

# Only define project if building standalone
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Include toolchain
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../arm-cortex-m7.cmake" CACHE PATH "Toolchain file")
    
    project(uart_driver
        VERSION 1.0.0
        DESCRIPTION "CRTOS UART Driver Module"
        LANGUAGES C CXX
    )
    
    set(CRTOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
else()
    # Building as subdirectory
    set(CRTOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif()

# Module configuration
set(MODULE_NAME "uart")
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
set(PIC_FLAGS
    -fPIC
    -msingle-pic-base
    -mpic-register=r9
    -mno-pic-data-is-text-relative
    -mlong-calls
)

# Additional module flags
set(MODULE_FLAGS
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

# Combine all compiler flags
set(COMPILE_FLAGS ${CPU_FLAGS} ${PIC_FLAGS} ${MODULE_FLAGS})

# C++ specific settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Apply compiler flags globally for this target
add_compile_options(${COMPILE_FLAGS})

# Source files
set(SOURCES
    UartDriver.cpp
)

# Create object library first
add_library(${MODULE_NAME}_obj OBJECT ${SOURCES})

target_include_directories(${MODULE_NAME}_obj PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CRTOS_ROOT}
    ${CRTOS_INC}
    ${DRIVERS_INC}
)

# Create relocatable module from objects (ld -r)
set(MODULE_OUTPUT "${CMAKE_BINARY_DIR}/${MODULE_NAME}${MODULE_EXT}")

add_custom_command(
    OUTPUT ${MODULE_OUTPUT}
    COMMAND ${CMAKE_LINKER} -r -o ${MODULE_OUTPUT} $<TARGET_OBJECTS:${MODULE_NAME}_obj>
    COMMAND arm-none-eabi-strip --strip-debug ${MODULE_OUTPUT}
    COMMAND ${CMAKE_SIZE} ${MODULE_OUTPUT}
    DEPENDS ${MODULE_NAME}_obj
    COMMENT "Creating relocatable PIC module ${MODULE_NAME}${MODULE_EXT}"
)

add_custom_target(${MODULE_NAME}_module ALL
    DEPENDS ${MODULE_OUTPUT}
)

# =============================================================================
# Analysis targets (optional)
# =============================================================================

# Show relocations
add_custom_target(${MODULE_NAME}_relocs
    COMMAND arm-none-eabi-readelf -r ${MODULE_OUTPUT}
    DEPENDS ${MODULE_OUTPUT}
    COMMENT "Showing relocations for ${MODULE_NAME}"
)

# Disassembly
add_custom_target(${MODULE_NAME}_disasm
    COMMAND arm-none-eabi-objdump -d ${MODULE_OUTPUT} > ${CMAKE_BINARY_DIR}/${MODULE_NAME}.dis
    DEPENDS ${MODULE_OUTPUT}
    COMMENT "Generating disassembly for ${MODULE_NAME}"
)

# =============================================================================
# Install target
# =============================================================================
install(
    FILES ${MODULE_OUTPUT}
    DESTINATION ${SDCARD_DIR}
    OPTIONAL
)

# =============================================================================
# Print configuration
# =============================================================================
message(STATUS "===========================================")
message(STATUS "CRTOS UART Driver Module (PIC)")
message(STATUS "===========================================")
message(STATUS "Module name: ${MODULE_NAME}${MODULE_EXT}")
message(STATUS "CRTOS root: ${CRTOS_ROOT}")
message(STATUS "Install path: ${SDCARD_DIR}")
message(STATUS "PIC: -fPIC -msingle-pic-base -mpic-register=r9")
message(STATUS "===========================================")
