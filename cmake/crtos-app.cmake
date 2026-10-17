# Programs for CRTOS (.app files), for the source tree (cmake/crtos.cmake) and the SDK
# (sdk/crtos-sdk.cmake) alike.
#
#   crtos_app(<name> SOURCES <src...> [DEST apps|bin|sbin] [STACK bytes] [HEAP bytes]
#             [INCLUDES <dir...>] [DEFINES <def...>] [LIBS <library...>]
#             [FAST <list file> [FAST_KB kb]] [XIP] [ICON <picture>])
#       builds <CRTOS_SDCARD>/<DEST>/<name>.app: a program linked (ld -r) with libcrtos and
#       newlib-nano, which the kernel loads into its own MPU-protected memory (arena).
#       DEST defaults to CRTOS_DEFAULT_DEST (in the tree: system/services/ -> sbin, system/commands/ -> bin,
#       apps/ -> apps), else apps; DEST flash0:<dir> puts it into <CRTOS_FLASH0>/<dir> - the
#       flash file system /flash0 on the board ("crtos deploy" sends both). STACK (main thread) defaults to 16 KB, HEAP to 64 KB.
#       LIBS: gfx (windows and drawing), tftlib (TFTLIB drawing) or your own static libraries.
#       FAST: the hottest functions (one symbol a line, up to FAST_KB, default 48) for on-chip
#       memory: tools/appfast.py renames their sections ".fast.*", the kernel puts them into
#       ITCM when there is room (code from SDRAM waits for every instruction cache miss).
#       XIP: a program that runs in place (crtos.specs -mxip, toolchain/crtos-xip.ld): from
#       /flash0 its code stays in the flash and only its data takes memory; elsewhere it is
#       loaded into memory like any program. Its libraries are the *_xip variants (lib/xip).
#       Not with FAST.
#       ICON: the program's icon (a square PNG, 64 x 64 or larger): crtos_icon(<picture> <name>).
#   crtos_icon(<picture> <name>)   the icon of program <name> (PNG, or PAM/PPM) as
#       <CRTOS_SDCARD>/share/icons/<name>.pam (tools/icon.py): the window manager shows it in
#       the programs menu, on the task bar and in title bars; programs without one get
#       "application"
#   crtos_file(<src> <dest>)       copies a file to <CRTOS_SDCARD>/<dest>
#   crtos_rootfs(<dir>)            copies every file under <dir> to <CRTOS_SDCARD>/
#   crtos_add_subdirectories(<dir>)   add_subdirectory() for each one with a CMakeLists.txt
#
# Needs: CRTOS_SDCARD, CRTOS_APP_INCLUDES, CRTOS_TOOLS (modcheck.py, icon.py), CRTOS_SYSROOT (the
# toolchain directory: start-up objects, libraries), CRTOS_SPECS (crtos.specs: the rules of
# compiling and linking a program, shared with arm-crtos-gcc) and the target crtos (libcrtos).

set(CRTOS_CPU_FLAGS -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(NOT CRTOS_FLASH0)
  set(CRTOS_FLASH0 ${CMAKE_BINARY_DIR}/flash0)
endif()

if(NOT CRTOS_SYSROOT OR NOT CRTOS_SPECS)
  message(FATAL_ERROR "crtos-app.cmake: set CRTOS_SYSROOT and CRTOS_SPECS first")
endif()

# crtos.specs adds -DCRTOS_USER=1, the sections per function and variable, -fno-common and,
# for C++, no exceptions, RTTI or thread-safe statics
set(CRTOS_APP_CFLAGS
  ${CRTOS_CPU_FLAGS}
  -specs=${CRTOS_SPECS}
  -O2 -g -Wall
)
set(CRTOS_APP_DEFINES CRTOS_USER=1)
# the link of a program: crtos.specs makes it a partial link (-r) with the start-up objects,
# libcrtos, newlib-nano, libm and libgcc from the toolchain directory
set(CRTOS_APP_LDFLAGS ${CRTOS_CPU_FLAGS} -specs=${CRTOS_SPECS} -B${CRTOS_SYSROOT}/lib/ -L${CRTOS_SYSROOT}/lib)

# what a custom command linking with these targets depends on: the targets themselves, or
# the files of imported ones (the SDK's libraries)
function(_crtos_link_deps out)
  set(deps "")
  foreach(t ${ARGN})
    get_target_property(imported ${t} IMPORTED)
    get_target_property(type ${t} TYPE)
    if(NOT imported)
      list(APPEND deps ${t})
    elseif(type STREQUAL "OBJECT_LIBRARY")
      get_target_property(files ${t} IMPORTED_OBJECTS)
      list(APPEND deps ${files})
    else()
      get_target_property(file ${t} IMPORTED_LOCATION)
      list(APPEND deps ${file})
    endif()
  endforeach()
  set(${out} ${deps} PARENT_SCOPE)
endfunction()

function(crtos_app name)
  cmake_parse_arguments(A "XIP" "DEST;STACK;HEAP;FAST;FAST_KB;ICON" "SOURCES;INCLUDES;DEFINES;LIBS" ${ARGN})
  if(A_XIP AND A_FAST)
    message(FATAL_ERROR "crtos_app(${name}): XIP and FAST do not go together")
  endif()
  get_property(xipok GLOBAL PROPERTY CRTOS_XIP_OK)
  if(A_XIP AND xipok STREQUAL "OFF")
    return()            # (no position-independent C library with this compiler: xiplibs.cmake)
  endif()
  set(xip "")
  set(ldflags ${CRTOS_APP_LDFLAGS})
  if(A_XIP)
    set(xip _xip)
    set(ldflags ${CRTOS_CPU_FLAGS} -specs=${CRTOS_SPECS} -mxip -B${CRTOS_SYSROOT}/lib/ -L${CRTOS_SYSROOT}/lib/xip
                -L${CRTOS_SYSROOT}/lib)
  endif()
  if(NOT A_DEST)
    if(CRTOS_DEFAULT_DEST)
      set(A_DEST ${CRTOS_DEFAULT_DEST})
    else()
      set(A_DEST apps)
    endif()
  endif()
  if(NOT A_STACK)
    set(A_STACK 16384)
  endif()
  if(NOT A_HEAP)
    set(A_HEAP 65536)
  endif()
  # the program description the loader reads (stack and heap sizes)
  set(info ${CMAKE_CURRENT_BINARY_DIR}/${name}_appinfo.c)
  file(WRITE ${info}.tmp
    "#include <crtos/syscall.h>\n"
    "__attribute__((used, section(\".crtos_app\"))) const struct crtos_app_info __crtos_app_info = {\n"
    "    CRTOS_APP_MAGIC, CRTOS_APP_ABI, ${A_STACK}u, ${A_HEAP}u, 0u\n};\n")
  configure_file(${info}.tmp ${info} COPYONLY)

  add_library(app_${name}_objs OBJECT ${A_SOURCES} ${info})
  target_compile_options(app_${name}_objs PRIVATE ${CRTOS_APP_CFLAGS})
  if(A_XIP)
    target_compile_options(app_${name}_objs PRIVATE -mxip)
  endif()
  target_include_directories(app_${name}_objs PRIVATE ${CRTOS_APP_INCLUDES} ${A_INCLUDES})
  target_compile_definitions(app_${name}_objs PRIVATE ${CRTOS_APP_DEFINES} ${A_DEFINES})
  set(libfiles "")
  set(libs "")
  foreach(l ${A_LIBS})
    if(A_XIP AND TARGET ${l}_xip)
      set(l ${l}_xip) # (a library without such a variant must itself be position independent)
    endif()
    list(APPEND libs ${l})
    list(APPEND libfiles $<TARGET_FILE:${l}>)
    get_target_property(inc ${l} CRTOS_PUBLIC_INCLUDES)
    if(inc)
      target_include_directories(app_${name}_objs PRIVATE ${inc})
    endif()
  endforeach()
  _crtos_link_deps(deps crtos${xip} ${libs})
  get_property(tcfiles GLOBAL PROPERTY CRTOS_TOOLCHAIN_FILES)
  if(A_XIP)
    # the position-independent C and C++ libraries (cmake/xiplibs.cmake, in the tree)
    get_property(xipfiles GLOBAL PROPERTY CRTOS_XIP_FILES)
    list(APPEND tcfiles ${xipfiles})
  endif()

  if(A_DEST MATCHES "^flash0:(.*)$")
    set(outdir ${CRTOS_FLASH0}/${CMAKE_MATCH_1})
  else()
    set(outdir ${CRTOS_SDCARD}/${A_DEST})
  endif()
  set(out ${outdir}/${name}.app)
  set(dbg ${CMAKE_CURRENT_BINARY_DIR}/${name}.debug.app)
  set(fast_cmd "")
  set(fast_dep "")
  if(A_FAST)
    if(NOT IS_ABSOLUTE ${A_FAST})
      set(A_FAST ${CMAKE_CURRENT_SOURCE_DIR}/${A_FAST})
    endif()
    if(NOT A_FAST_KB)
      set(A_FAST_KB 48)
    endif()
    set(fast_cmd COMMAND ${CMAKE_COMMAND} -E env CRTOS_GCC_BIN=${CRTOS_GCC_BIN}
                 ${Python3_EXECUTABLE} ${CRTOS_TOOLS}/appfast.py ${dbg} ${A_FAST} --limit ${A_FAST_KB})
    set(fast_dep ${A_FAST})
  endif()
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${outdir}
    # partial link keeps the relocations; everything must be resolved inside the program
    # (the program's own libraries may need each other: one group)
    COMMAND ${CMAKE_C_COMPILER} ${ldflags} -o ${dbg} $<TARGET_OBJECTS:app_${name}_objs>
            -Wl,--start-group ${libfiles} -Wl,--end-group
    # the hottest functions (FAST: a list of symbols) to on-chip memory
    ${fast_cmd}
    # nothing may stay undefined (a program cannot reach the kernel's symbols)
    COMMAND ${CMAKE_COMMAND} -E env CRTOS_GCC_BIN=${CRTOS_GCC_BIN}
            ${Python3_EXECUTABLE} ${CRTOS_TOOLS}/modcheck.py --apps --quiet ${dbg}
    COMMAND ${CRTOS_STRIP} --strip-debug -o ${out} ${dbg}
    DEPENDS app_${name}_objs $<TARGET_OBJECTS:app_${name}_objs> ${deps} ${tcfiles} ${fast_dep}
    COMMAND_EXPAND_LISTS
    COMMENT "Program ${A_DEST}/${name}.app"
    VERBATIM)
  add_custom_target(app_${name} ALL DEPENDS ${out})
  if(TARGET crtos_toolchain)
    add_dependencies(app_${name} crtos_toolchain)
  endif()
  if(A_ICON)
    crtos_icon(${A_ICON} ${name})
  endif()
endfunction()

function(crtos_icon src name)
  if(NOT IS_ABSOLUTE ${src})
    set(src ${CMAKE_CURRENT_SOURCE_DIR}/${src})
  endif()
  set(out ${CRTOS_SDCARD}/share/icons/${name}.pam)
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${Python3_EXECUTABLE} ${CRTOS_TOOLS}/icon.py ${src} ${out}
    DEPENDS ${src} ${CRTOS_TOOLS}/icon.py
    COMMENT "Icon share/icons/${name}.pam"
    VERBATIM)
  add_custom_target(icon_${name} ALL DEPENDS ${out})
endfunction()

# Every subdirectory of <dir> that has a CMakeLists.txt (one program each)
function(crtos_add_subdirectories dir)
  file(GLOB children LIST_DIRECTORIES true CONFIGURE_DEPENDS ${dir}/*)
  list(SORT children)
  foreach(child ${children})
    if(IS_DIRECTORY ${child} AND EXISTS ${child}/CMakeLists.txt)
      add_subdirectory(${child})
    endif()
  endforeach()
endfunction()

# ---- files for the SD card -------------------------------------------------------------------

function(crtos_file src dest)
  set(out ${CRTOS_SDCARD}/${dest})
  get_filename_component(dir ${out} DIRECTORY)
  string(MAKE_C_IDENTIFIER "file_${dest}" tgt)
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${dir}
    COMMAND ${CMAKE_COMMAND} -E copy ${src} ${out}
    DEPENDS ${src}
    COMMENT "File ${dest}"
    VERBATIM)
  add_custom_target(${tgt} ALL DEPENDS ${out})
endfunction()

function(crtos_rootfs dir)
  file(GLOB_RECURSE files RELATIVE ${dir} CONFIGURE_DEPENDS ${dir}/*)
  foreach(f ${files})
    crtos_file(${dir}/${f} ${f})
  endforeach()
endfunction()
