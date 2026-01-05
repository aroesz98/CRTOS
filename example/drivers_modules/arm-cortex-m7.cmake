# ARM Cortex-M7 Toolchain File for CRTOS Driver Modules
# Author: Arkadiusz Szlanta
# Date: 03 Jan 2026
#
# Usage:
#   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-cortex-m7.cmake

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Toolchain prefix
set(TOOLCHAIN_PREFIX "arm-none-eabi-")

# Find toolchain programs
find_program(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
find_program(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
find_program(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
find_program(CMAKE_LINKER ${TOOLCHAIN_PREFIX}ld)
find_program(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy)
find_program(CMAKE_OBJDUMP ${TOOLCHAIN_PREFIX}objdump)
find_program(CMAKE_SIZE ${TOOLCHAIN_PREFIX}size)
find_program(CMAKE_AR ${TOOLCHAIN_PREFIX}ar)
find_program(CMAKE_RANLIB ${TOOLCHAIN_PREFIX}ranlib)
find_program(CMAKE_STRIP ${TOOLCHAIN_PREFIX}strip)

# Disable compiler checks for cross-compiling
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# CPU specific flags
set(CPU_FLAGS "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")

# Common flags
set(COMMON_FLAGS "${CPU_FLAGS} -ffunction-sections -fdata-sections -fno-common")

# =============================================================================
# Position Independent Code (PIC) configuration for Cortex-M7
# =============================================================================
# -fPIC                    : Generate position independent code
# -msingle-pic-base        : Use a single register (r9) as PIC base, no dynamic linking overhead
# -mpic-register=r9        : Dedicate r9 for GOT base pointer (platform ABI standard)
# -mno-pic-data-is-text-relative : Force data access through GOT, not PC-relative
# -mlong-calls             : Generate indirect calls through function pointers for >4MB range
#
# The GOT (Global Offset Table) will contain:
#   - Addresses of global/static variables
#   - Function pointers for external calls
#
# At module load time, CRTOS will:
#   1. Allocate memory for code + data + bss + GOT
#   2. Parse ELF relocation sections (.rel.dyn, .rel.plt)
#   3. Apply relocations: add load address to each GOT entry
#   4. Set r9 = GOT base before calling module entry point
# =============================================================================
set(PIC_FLAGS "-fPIC -msingle-pic-base -mpic-register=r9 -mno-pic-data-is-text-relative")

# Module-specific flags
# -fvisibility=hidden      : Hide internal symbols, only export what's marked visible
# -mlong-calls             : Use long calls for functions (works with any load address)
set(MODULE_FLAGS "${PIC_FLAGS} -fvisibility=hidden -mlong-calls")

# Combine flags
set(CMAKE_C_FLAGS_INIT "${COMMON_FLAGS} ${MODULE_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${COMMON_FLAGS} ${MODULE_FLAGS} -fno-exceptions -fno-rtti")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS}")

# Optimization flags per build type
set(CMAKE_C_FLAGS_DEBUG_INIT "-Og -g3")
set(CMAKE_C_FLAGS_RELEASE_INIT "-Os")
set(CMAKE_C_FLAGS_RELWITHDEBINFO_INIT "-Os -g")
set(CMAKE_C_FLAGS_MINSIZEREL_INIT "-Os -DNDEBUG")

set(CMAKE_CXX_FLAGS_DEBUG_INIT "-Og -g3")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "-Os")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT "-Os -g")
set(CMAKE_CXX_FLAGS_MINSIZEREL_INIT "-Os -DNDEBUG")

# Don't search for programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Search for libraries and headers in the target environment
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Print toolchain info
message(STATUS "Using ARM Cortex-M7 toolchain")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  Linker: ${CMAKE_LINKER}")
