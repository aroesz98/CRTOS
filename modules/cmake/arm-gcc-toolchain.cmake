# CMake toolchain file for Arm GNU toolchain (arm-none-eabi)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Allow overriding via environment
if(DEFINED ENV{ARM_GCC_PATH})
  set(ARM_GCC_BIN "$ENV{ARM_GCC_PATH}")
else()
  set(ARM_GCC_BIN "")
endif()

if(ARM_GCC_BIN STREQUAL "")
  find_program(CMAKE_C_COMPILER arm-none-eabi-gcc)
  find_program(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
  find_program(CMAKE_CXX_COMPILER arm-none-eabi-g++)
  find_program(OBJCOPY arm-none-eabi-objcopy)
else()
  set(CMAKE_C_COMPILER   "${ARM_GCC_BIN}/arm-none-eabi-gcc")
  set(CMAKE_ASM_COMPILER "${ARM_GCC_BIN}/arm-none-eabi-gcc")
  set(CMAKE_CXX_COMPILER "${ARM_GCC_BIN}/arm-none-eabi-g++")
  set(OBJCOPY            "${ARM_GCC_BIN}/arm-none-eabi-objcopy")
endif()

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
