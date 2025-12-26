cmake_minimum_required(VERSION 3.21)
project(lpc55s69_module C CXX ASM)

# Options for module metadata
set(MODULE_VERSION "0.0.0" CACHE STRING "Module semantic version M.m.p")
set(API_VERSION 1 CACHE STRING "API/ABI version expected by host")

# Split MODULE_VERSION into components
string(REPLACE "." ";" _ver_list ${MODULE_VERSION})
list(GET _ver_list 0 VER_MAJOR)
list(LENGTH _ver_list _ver_len)
if(_ver_len GREATER 1)
  list(GET _ver_list 1 VER_MINOR)
else()
  set(VER_MINOR 0)
endif()
if(_ver_len GREATER 2)
  list(GET _ver_list 2 VER_PATCH)
else()
  set(VER_PATCH 0)
endif()

# Build timestamp (seconds since epoch) - allow override via -DBUILD_UNIX_TIME
if(NOT DEFINED BUILD_UNIX_TIME)
  # Try to compute a portable timestamp via Python if available; fallback to 0
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import time; print(int(time.time()))" OUTPUT_VARIABLE BUILD_UNIX_TIME OUTPUT_STRIP_TRAILING_WHITESPACE)
  else()
    set(BUILD_UNIX_TIME 0)
  endif()
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)

# CPU/ABI flags for Cortex-M7 (Thumb, hard-float disabled by default; LPC55S69 has FP but often unused for modules)
set(CPU_FLAGS -mcpu=cortex-m7 -mthumb)

# Baremetal flags
set(BARE_FLAGS -ffreestanding -fno-builtin -fdata-sections -ffunction-sections -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables)

# Debug/Optimization flags
# Use -g3 for full debug info, -Os for size optimization
# For debugging: use -g3 -Og (or -g3 -O0 for no optimization)
# For release: use -g0 -Os
set(OPT_FLAGS -g3 -Og)

# Linker script
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker/lpc55s69_module.ld)

# Find objcopy
find_program(OBJCOPY arm-none-eabi-objcopy)
if(NOT OBJCOPY)
  if(DEFINED OBJCOPY)
    # From toolchain file
  else()
    message(FATAL_ERROR "arm-none-eabi-objcopy not found; set ARM_GCC_PATH or add to PATH")
  endif()
endif()

# Function to create a module target
function(add_module MODULE_SRC MODULE_TARGET)
  add_library(${MODULE_TARGET}_obj OBJECT
    ${MODULE_SRC}
    src/kernel.c
    src/syscalls.c
  )

  target_include_directories(${MODULE_TARGET}_obj PUBLIC include)

  # Define metadata
  target_compile_definitions(${MODULE_TARGET}_obj PUBLIC
    API_VERSION=${API_VERSION}
    MODULE_VER_MAJOR=${VER_MAJOR}
    MODULE_VER_MINOR=${VER_MINOR}
    MODULE_VER_PATCH=${VER_PATCH}
    BUILD_UNIX_TIME=${BUILD_UNIX_TIME}
  )

  # Compile options
  target_compile_options(${MODULE_TARGET}_obj PUBLIC ${CPU_FLAGS} ${BARE_FLAGS} ${OPT_FLAGS} -fPIE -Wall -Wextra -Werror=implicit-function-declaration)

  add_executable(${MODULE_TARGET}.elf $<TARGET_OBJECTS:${MODULE_TARGET}_obj>)

  target_link_options(${MODULE_TARGET}.elf PRIVATE
    ${CPU_FLAGS}
    -fPIE
    -T${LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,-Map,${CMAKE_BINARY_DIR}/${MODULE_TARGET}.map
    -nostartfiles -nostdlib -static
    -specs=nosys.specs -specs=nano.specs
  )

  # Link standard libraries needed for printf (newlib nano) and helpers
  target_link_libraries(${MODULE_TARGET}.elf c m nosys gcc)

  # Produce raw binary
  add_custom_command(TARGET ${MODULE_TARGET}.elf POST_BUILD
    COMMAND ${OBJCOPY} -O binary $<TARGET_FILE:${MODULE_TARGET}.elf> ${CMAKE_BINARY_DIR}/${MODULE_TARGET}.bin
    BYPRODUCTS ${CMAKE_BINARY_DIR}/${MODULE_TARGET}.bin
    COMMENT "Generating ${MODULE_TARGET}.bin"
  )

  # Install targets (optional)
  install(FILES ${CMAKE_BINARY_DIR}/${MODULE_TARGET}.bin DESTINATION .)
endfunction()

# Build all modules
add_module(src/htop_module.c htop_module)
add_module(src/copy_module.c copy_module)
add_module(src/shm_module.c shm_module)
add_module(src/dpc_test_module.c dpc_test_module)
