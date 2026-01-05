# Hello Application - CMake Build Configuration
# Author: Arkadiusz Szlanta
# Date: 05 Jan 2026
#
# Description:
# Builds hello.app as a relocatable PIC application for CRTOS.

cmake_minimum_required(VERSION 3.20)

# Only define project if building standalone
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../arm-cortex-m7.cmake" CACHE PATH "Toolchain file")
    
    project(hello_app
        VERSION 1.0.0
        DESCRIPTION "CRTOS Hello Application"
        LANGUAGES CXX
    )
    
    set(APPS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
else()
    set(APPS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
endif()

# Application configuration
set(APP_NAME "hello")
set(APP_EXT ".app")
set(COMMON_INC "${APPS_ROOT}/common")
set(LINKER_SCRIPT "${APPS_ROOT}/common/app.ld")
set(SDCARD_DIR "${APPS_ROOT}/../sdcard/apps")

# CPU flags
set(CPU_FLAGS
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard
)

# =============================================================================
# Position Independent Code (PIC) Configuration
# =============================================================================
set(PIC_FLAGS
    -fPIC
    -msingle-pic-base
    -mpic-register=r9
    -mno-pic-data-is-text-relative
    -mlong-calls
)

# Compiler flags
set(COMMON_FLAGS
    ${CPU_FLAGS}
    ${PIC_FLAGS}
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

# Convert to string
string(REPLACE ";" " " COMMON_FLAGS_STR "${COMMON_FLAGS}")

# C++ settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Linker flags for executable application
set(LINKER_FLAGS
    -T${LINKER_SCRIPT}
    -nostartfiles
    -nostdlib
    -nodefaultlibs
    -Wl,--gc-sections
    -Wl,--emit-relocs
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${APP_NAME}.map
)

# Source files
set(APP_SOURCES
    hello_app.cpp
)

# Create executable
add_executable(${APP_NAME}${APP_EXT} ${APP_SOURCES})

target_include_directories(${APP_NAME}${APP_EXT} PRIVATE
    ${COMMON_INC}
)

target_compile_options(${APP_NAME}${APP_EXT} PRIVATE
    ${COMMON_FLAGS}
)

target_link_options(${APP_NAME}${APP_EXT} PRIVATE
    ${LINKER_FLAGS}
)

# Show size after build
add_custom_command(TARGET ${APP_NAME}${APP_EXT} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${APP_NAME}${APP_EXT}>
    COMMENT "Application size:"
)

# Install to sdcard
install(TARGETS ${APP_NAME}${APP_EXT}
    RUNTIME DESTINATION ${SDCARD_DIR}
)

# Status message
message(STATUS "===========================================")
message(STATUS "CRTOS Application: ${APP_NAME}${APP_EXT}")
message(STATUS "Install path: ${SDCARD_DIR}")
message(STATUS "PIC: ${PIC_FLAGS}")
message(STATUS "===========================================")
