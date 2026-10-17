# Cross compiler for everything CRTOS builds: the kernel, kernel modules and programs all use
# the same Arm GNU Toolchain (arm-none-eabi-gcc), so they share one ABI.
#
# Where it is looked for, in this order:
#   1. -DCRTOS_GCC_BIN=<dir> or the environment variable CRTOS_GCC_BIN (the directory of
#      arm-none-eabi-gcc)
#   2. arm-none-eabi-gcc on the PATH
#   3. the usual installation directories of the Arm GNU Toolchain (newest version first) and
#      of the toolchain that comes with MCUXpresso IDE

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(CMAKE_HOST_WIN32)
  set(_crtos_exe ".exe")
else()
  set(_crtos_exe "")
endif()

if(NOT CRTOS_GCC_BIN AND DEFINED ENV{CRTOS_GCC_BIN})
  set(CRTOS_GCC_BIN "$ENV{CRTOS_GCC_BIN}")
endif()

if(NOT CRTOS_GCC_BIN)
  find_program(_crtos_gcc arm-none-eabi-gcc NO_CACHE)
  if(_crtos_gcc)
    get_filename_component(CRTOS_GCC_BIN "${_crtos_gcc}" DIRECTORY)
  endif()
endif()

if(NOT CRTOS_GCC_BIN)
  set(_crtos_candidates "")
  foreach(_pattern
      "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/*/bin"
      "C:/Program Files/Arm GNU Toolchain arm-none-eabi/*/bin"
      "C:/Program Files (x86)/GNU Arm Embedded Toolchain/*/bin"
      "C:/nxp/MCUXpressoIDE_*/ide/tools/bin"
      "$ENV{HOME}/.local/arm-gnu-toolchain*/bin"
      "/opt/arm-gnu-toolchain*/bin"
      "/opt/gcc-arm-none-eabi*/bin"
      "/Applications/ArmGNUToolchain/*/arm-none-eabi/bin"
      "/usr/local/bin"
      "/usr/bin")
    file(GLOB _dirs LIST_DIRECTORIES true "${_pattern}")
    list(SORT _dirs COMPARE NATURAL ORDER DESCENDING)
    foreach(_d ${_dirs})
      if(EXISTS "${_d}/arm-none-eabi-gcc${_crtos_exe}")
        list(APPEND _crtos_candidates "${_d}")
      endif()
    endforeach()
  endforeach()
  if(_crtos_candidates)
    list(GET _crtos_candidates 0 CRTOS_GCC_BIN)
  endif()
endif()

if(NOT CRTOS_GCC_BIN OR NOT EXISTS "${CRTOS_GCC_BIN}/arm-none-eabi-gcc${_crtos_exe}")
  message(FATAL_ERROR
    "CRTOS: arm-none-eabi-gcc not found. Install the Arm GNU Toolchain "
    "(https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) and put its bin "
    "directory on the PATH, or pass -DCRTOS_GCC_BIN=<directory>. 'crtos doctor' checks this.")
endif()
set(CRTOS_GCC_BIN "${CRTOS_GCC_BIN}" CACHE PATH "Directory of arm-none-eabi-gcc" FORCE)

set(CMAKE_C_COMPILER   "${CRTOS_GCC_BIN}/arm-none-eabi-gcc${_crtos_exe}")
set(CMAKE_CXX_COMPILER "${CRTOS_GCC_BIN}/arm-none-eabi-g++${_crtos_exe}")
set(CMAKE_ASM_COMPILER "${CRTOS_GCC_BIN}/arm-none-eabi-gcc${_crtos_exe}")
set(CRTOS_OBJCOPY      "${CRTOS_GCC_BIN}/arm-none-eabi-objcopy${_crtos_exe}")
set(CRTOS_STRIP        "${CRTOS_GCC_BIN}/arm-none-eabi-strip${_crtos_exe}")
set(CRTOS_NM           "${CRTOS_GCC_BIN}/arm-none-eabi-nm${_crtos_exe}")
set(CRTOS_SIZE         "${CRTOS_GCC_BIN}/arm-none-eabi-size${_crtos_exe}")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
