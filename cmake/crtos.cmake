# CRTOS build helpers of the source tree: kernel modules, the device tree, the checks. The
# programs' part (crtos_app, crtos_file, crtos_rootfs) is in crtos-app.cmake, shared with
# the SDK.
#
#   crtos_module(<name> SOURCES <src...> [ALIASES <compatible...>] [SDK_DRIVERS fsl_x.c...]
#                [INCLUDES <dir...>] [DEFINES <def...>])
#       builds build/sdcard/crtos/drivers/<name>.ko; ALIASES are the device tree "compatible"
#       strings the module drives (they go to modules.alias, so it is loaded automatically)
#   crtos_dtb(<name> <dts>)            -> build/sdcard/crtos/boot/<name>.dtb
#   crtos_toolchain()                  fills build/toolchain/arm-crtos (see below)
#   crtos_finish()                     writes modules.alias, checks the modules' symbols
#   crtos_rtos_app(SOURCES <src...> [SDK_DRIVERS fsl_x.c...] [INCLUDES <dir...>] [DEFINES <def...>])
#       the application of the RTOS build (CRTOS_PROFILE=rtos): its sources become part of the
#       kernel (firmware), compiled like the kernel; it defines app_main() (crtos/rtos.h).
#       SDK_DRIVERS: NXP SDK drivers (CRTOS_SDK_DRIVER_DIRS below) the application uses
#       (the board support has GPIO, LPUART, clocks and caches already)

set(CRTOS_TOOLS ${CRTOS_TOP}/tools)
set(CRTOS_APP_INCLUDES
  ${CRTOS_TOP}/system/lib/libcrtos/include
  ${CRTOS_TOP}/kernel/include
)
# the toolchain directory the programs are linked from, and the rules of the link
set(CRTOS_SYSROOT ${CMAKE_BINARY_DIR}/toolchain/arm-crtos)
set(CRTOS_SPECS ${CRTOS_TOP}/toolchain/crtos.specs)
include(${CMAKE_CURRENT_LIST_DIR}/crtos-app.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/xiplibs.cmake)

# ---- the toolchain directory -----------------------------------------------------------------
#
# build/toolchain/arm-crtos is what arm-none-eabi-gcc needs besides its own files to build
# CRTOS programs, laid out as crtos.specs expects it:
#   lib/      crtos.specs, crtos-app.ld, the start-up objects, libcrtos.a, libcrtosheap.a,
#             libgfx.a, libtftlib.a
#   include/  libcrtos' and libgfx's headers, crtos/ (the kernel interface programs use),
#             tftlib/
#   bin/      arm-crtos-gcc and friends ("crtos toolchain" builds them with the PC's compiler)
# Programs of this tree link from it (-B, -L); arm-crtos-gcc and the SDK are the same files.

# the kernel headers that belong to the program interface (system calls, device ioctls)
set(CRTOS_ABI_HEADERS syscall ioctl tty keys input audio poll list mtd gpu2d spi socket fb rtc flashfs)

function(_crtos_tc_copy src dest)
  set(out ${CRTOS_SYSROOT}/${dest})
  add_custom_command(OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -E copy ${src} ${out}
    DEPENDS ${src}
    VERBATIM)
  set_property(GLOBAL APPEND PROPERTY CRTOS_TOOLCHAIN_FILES ${out})
endfunction()

function(crtos_toolchain)
  # the start-up objects under the names crtos.specs asks for (lib/ and, position
  # independent for programs that run in place, lib/xip/)
  foreach(pair "crtos_crt0:crtos-crt0.o" "crtos_memops:crtos-memops.o" "crtos_cxxrt:crtos-cxxrt.o"
               "crtos_crt0_xip:xip/crtos-crt0.o" "crtos_memops_xip:xip/crtos-memops.o"
               "crtos_cxxrt_xip:xip/crtos-cxxrt.o")
    string(REPLACE ":" ";" pair ${pair})
    list(GET pair 0 tgt)
    list(GET pair 1 file)
    set(out ${CRTOS_SYSROOT}/lib/${file})
    add_custom_command(OUTPUT ${out}
      COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:${tgt}> ${out}
      DEPENDS ${tgt} $<TARGET_OBJECTS:${tgt}>
      COMMAND_EXPAND_LISTS
      VERBATIM)
    set_property(GLOBAL APPEND PROPERTY CRTOS_TOOLCHAIN_FILES ${out})
  endforeach()
  _crtos_tc_copy(${CRTOS_TOP}/toolchain/crtos.specs lib/crtos.specs)
  _crtos_tc_copy(${CRTOS_TOP}/toolchain/crtos-app.ld lib/crtos-app.ld)
  _crtos_tc_copy(${CRTOS_TOP}/toolchain/crtos-xip.ld lib/crtos-xip.ld)
  # headers
  file(GLOB_RECURSE hs RELATIVE ${CRTOS_TOP}/system/lib/libcrtos/include CONFIGURE_DEPENDS ${CRTOS_TOP}/system/lib/libcrtos/include/*.h)
  foreach(h ${hs})
    _crtos_tc_copy(${CRTOS_TOP}/system/lib/libcrtos/include/${h} include/${h})
  endforeach()
  foreach(h ${CRTOS_ABI_HEADERS})
    _crtos_tc_copy(${CRTOS_TOP}/kernel/include/crtos/${h}.h include/crtos/${h}.h)
  endforeach()
  file(GLOB hs RELATIVE ${CRTOS_TOP}/system/lib/libgfx/include CONFIGURE_DEPENDS ${CRTOS_TOP}/system/lib/libgfx/include/*.h)
  foreach(h ${hs})
    _crtos_tc_copy(${CRTOS_TOP}/system/lib/libgfx/include/${h} include/${h})
  endforeach()
  file(GLOB_RECURSE hs RELATIVE ${CRTOS_TOP}/system/lib/tftlib CONFIGURE_DEPENDS ${CRTOS_TOP}/system/lib/tftlib/*.h
       ${CRTOS_TOP}/system/lib/tftlib/*.hpp)
  foreach(h ${hs})
    _crtos_tc_copy(${CRTOS_TOP}/system/lib/tftlib/${h} include/tftlib/${h})
  endforeach()
  # the libraries themselves are built straight into lib/ and lib/xip/, and with them the C and
  # C++ runtime libraries in position-independent form (cmake/xiplibs.cmake)
  set_target_properties(crtos crtosheap gfx tftlib PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CRTOS_SYSROOT}/lib)
  set_target_properties(crtos_xip crtosheap_xip gfx_xip tftlib_xip PROPERTIES
                        ARCHIVE_OUTPUT_DIRECTORY ${CRTOS_SYSROOT}/lib/xip)
  crtos_xiplibs(${CRTOS_SYSROOT}/lib/xip)
  get_property(files GLOBAL PROPERTY CRTOS_TOOLCHAIN_FILES)
  get_property(xipfiles GLOBAL PROPERTY CRTOS_XIP_FILES)
  add_custom_target(crtos_toolchain ALL DEPENDS ${files} ${xipfiles} crtos crtosheap gfx tftlib crtos_xip
                    crtosheap_xip gfx_xip tftlib_xip)
endfunction()

# ---- kernel modules --------------------------------------------------------------------------

set(CRTOS_PLATFORM ${CRTOS_TOP}/kernel/platform/evkbimxrt1050)

# The NXP SDK's peripheral drivers (fsl_*.c, .h) of the i.MX RT1052, as the MCUXpresso SDK
# 25.06 repositories hold them (third_party/sources.txt): one directory per IP block, and
# several versions of some blocks (dcdc_1 or dcdc_2...) - these are the ones of this chip.
# SDK_DRIVERS names a file of one of them; all of them are on the modules' include path.
set(CRTOS_SDK ${CRTOS_TOP}/third_party/nxp-sdk)
set(CRTOS_SDK_DRIVER_DIRS ${CRTOS_SDK}/devices-rt/RT1050/MIMXRT1052/drivers)
foreach(d flexram nic301 romapi)
  list(APPEND CRTOS_SDK_DRIVER_DIRS ${CRTOS_SDK}/devices-rt/RT1050/MIMXRT1052/drivers/${d})
endforeach()
foreach(d adc_12b1msps_sar adc_etc aipstz aoi bee cache/armv7-m7 cmp common csi dcdc_1 dcp dmamux
          edma elcdif enc enet ewm flexcan flexio flexio/i2c flexio/i2s flexio/spi flexio/uart
          flexram flexspi gpc_1 gpt igpio kpp lpi2c lpspi lpuart ocotp pit pmu pwm pxp qtmr_1
          rtwdog sai semc snvs_hp snvs_lp spdif src tempmon trng tsc usdhc wdog01 xbara xbarb)
  list(APPEND CRTOS_SDK_DRIVER_DIRS ${CRTOS_SDK}/core/drivers/${d})
endforeach()

# crtos_sdk_drivers(<var> fsl_x.c...): the paths of these SDK driver sources
function(crtos_sdk_drivers var)
  set(paths)
  foreach(f ${ARGN})
    set(found)
    foreach(d ${CRTOS_SDK_DRIVER_DIRS})
      if(EXISTS ${d}/${f})
        set(found ${d}/${f})
        break()
      endif()
    endforeach()
    if(NOT found)
      message(FATAL_ERROR "SDK_DRIVERS: ${f} is not in the NXP SDK drivers of this chip (CRTOS_SDK_DRIVER_DIRS)")
    endif()
    list(APPEND paths ${found})
  endforeach()
  set(${var} ${paths} PARENT_SCOPE)
endfunction()

set(CRTOS_MODULE_CFLAGS
  ${CRTOS_CPU_FLAGS}
  -O2 -g -Wall
  -mlong-calls                  # the kernel is out of BL range (flash vs OCRAM/SDRAM)
  -ffunction-sections -fdata-sections
  -fno-common
  -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables
  "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti;-fno-threadsafe-statics;-fno-use-cxa-atexit>"
)

set(CRTOS_MODULE_INCLUDES
  ${CRTOS_TOP}/kernel/include
  ${CRTOS_PLATFORM}/device
  ${CRTOS_PLATFORM}/device/periph
  ${CRTOS_PLATFORM}/CMSIS
  ${CRTOS_PLATFORM}/CMSIS/m-profile
  ${CRTOS_PLATFORM}/drivers
  ${CRTOS_SDK_DRIVER_DIRS}
  ${CRTOS_TOP}/dts/include
)

set(CRTOS_MODULE_DEFINES CPU_MIMXRT1052DVL6B CPU_MIMXRT1052DVL6B_cm7 NDEBUG CRTOS_MODULE=1)

set_property(GLOBAL PROPERTY CRTOS_ALIAS_LINES "")

function(crtos_module name)
  cmake_parse_arguments(M "" "" "SOURCES;ALIASES;SDK_DRIVERS;INCLUDES;DEFINES" ${ARGN})
  crtos_sdk_drivers(sdk ${M_SDK_DRIVERS})
  set(srcs ${M_SOURCES} ${sdk})

  add_library(${name}_objs OBJECT ${srcs})
  target_compile_options(${name}_objs PRIVATE ${CRTOS_MODULE_CFLAGS})
  target_include_directories(${name}_objs PRIVATE ${CRTOS_MODULE_INCLUDES} ${M_INCLUDES})
  target_compile_definitions(${name}_objs PRIVATE ${CRTOS_MODULE_DEFINES} ${M_DEFINES})

  set(ko ${CRTOS_SDCARD}/drivers/${name}.ko)
  set(ko_debug ${CMAKE_CURRENT_BINARY_DIR}/${name}.debug.ko)
  add_custom_command(
    OUTPUT ${ko}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CRTOS_SDCARD}/drivers
    # partial link (ld -r) keeps the relocations; libgcc supplies 64-bit division etc.
    COMMAND ${CMAKE_C_COMPILER} ${CRTOS_CPU_FLAGS} -nostdlib -r -o ${ko_debug} $<TARGET_OBJECTS:${name}_objs> -lgcc
    COMMAND ${CRTOS_STRIP} --strip-debug -o ${ko} ${ko_debug}
    DEPENDS ${name}_objs $<TARGET_OBJECTS:${name}_objs>
    COMMAND_EXPAND_LISTS
    COMMENT "Module ${name}.ko"
    VERBATIM)
  add_custom_target(${name} ALL DEPENDS ${ko})
  set_property(GLOBAL APPEND PROPERTY CRTOS_MODULE_TARGETS ${name})

  foreach(a ${M_ALIASES})
    set_property(GLOBAL APPEND PROPERTY CRTOS_ALIAS_LINES "${a} ${name}")
  endforeach()
endfunction()

# ---- the device tree -------------------------------------------------------------------------

function(crtos_dtb name dts)
  get_filename_component(dts_dir ${dts} DIRECTORY)
  file(GLOB_RECURSE dts_deps ${dts_dir}/*.dts ${dts_dir}/*.dtsi ${dts_dir}/*.h)
  set(out ${CRTOS_SDCARD}/boot/${name}.dtb)
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CRTOS_SDCARD}/boot
    COMMAND ${CMAKE_COMMAND} -E env CRTOS_GCC_BIN=${CRTOS_GCC_BIN}
            ${Python3_EXECUTABLE} ${CRTOS_TOP}/tools/dtc.py -I ${dts_dir}/include -o ${out} ${dts}
    DEPENDS ${dts_deps} ${CRTOS_TOP}/tools/dtc.py
    COMMENT "Device tree ${name}.dtb"
    VERBATIM)
  add_custom_target(dtb_${name} ALL DEPENDS ${out})
endfunction()

function(crtos_finish)
  get_property(lines GLOBAL PROPERTY CRTOS_ALIAS_LINES)
  set(text "# compatible module (generated by the build)\n")
  foreach(l ${lines})
    string(APPEND text "${l}\n")
  endforeach()
  file(MAKE_DIRECTORY ${CRTOS_SDCARD}/drivers)
  file(WRITE ${CMAKE_BINARY_DIR}/modules.alias.tmp "${text}")
  configure_file(${CMAKE_BINARY_DIR}/modules.alias.tmp ${CRTOS_SDCARD}/drivers/modules.alias COPYONLY)
  # every undefined symbol must be exported by the kernel or another module
  get_property(mods GLOBAL PROPERTY CRTOS_MODULE_TARGETS)
  add_custom_target(modcheck ALL
    COMMAND ${CMAKE_COMMAND} -E env CRTOS_GCC_BIN=${CRTOS_GCC_BIN}
            ${Python3_EXECUTABLE} ${CRTOS_TOP}/tools/modcheck.py ${CRTOS_SDCARD}/drivers
    DEPENDS ${mods}
    COMMENT "Checking module symbols"
    VERBATIM)
endfunction()

# ---- the RTOS build's application ------------------------------------------------------------

function(crtos_rtos_app)
  cmake_parse_arguments(A "" "" "SOURCES;SDK_DRIVERS;INCLUDES;DEFINES" ${ARGN})
  if(NOT CRTOS_PROFILE STREQUAL "rtos" OR NOT TARGET kernel)
    message(FATAL_ERROR "crtos_rtos_app(): only in the RTOS build (crtos build --rtos ${CMAKE_CURRENT_SOURCE_DIR})")
  endif()
  if(NOT A_SOURCES)
    message(FATAL_ERROR "crtos_rtos_app(): no SOURCES")
  endif()
  crtos_sdk_drivers(sdk ${A_SDK_DRIVERS})
  target_sources(kernel PRIVATE ${A_SOURCES} ${sdk})
  # (after the kernel's own: the board support's drivers win over the SDK's copies)
  target_include_directories(kernel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${A_INCLUDES}
                             ${CRTOS_SDK_DRIVER_DIRS})
  if(A_DEFINES)
    target_compile_definitions(kernel PRIVATE ${A_DEFINES})
  endif()
endfunction()
