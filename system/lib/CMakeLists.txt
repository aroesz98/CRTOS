# User-space libraries. They go to the toolchain directory (CRTOS_SYSROOT, see
# cmake/crtos-app.cmake), from which every program is linked - the same files arm-crtos-gcc
# and the SDK use.
#
# The start-up objects are always linked (crtos.specs: startfile): crt0 holds the entry
# point; memops the kernel's word-wise memcpy, memmove and memset instead of newlib-nano's
# byte loops; cxxrt the C++ runtime (operator new/delete on malloc), which so wins over
# libstdc++'s. libcrtos: the C library's system interface (newlib hooks), threads and locks
# (and POSIX threads on them), IPC, shared memory, sockets, the default program header.
# libcrtosheap (optional, -lcrtosheap): a malloc that grows beyond the arena into shared memory
# windows and fragments less (the compiler on the board links it).
# libgfx: client side of the graphics server, software drawing, pictures and icons, bitmap
# fonts, widgets and key <-> character tables. TFTLIB: the drawing library of the old firmware (C++,
# Adafruit/TFT_eSPI style), drawing on XRGB8888 surfaces - windows created with GFX_WIN_XRGB.
#
# Everything twice: for ordinary programs (lib/ of the toolchain directory) and, with -mxip
# (crtos.specs: position independent, data through r9), for programs that run in place
# (lib/xip/; targets *_xip).

set(CRTOS_LIB_SOURCES
  libcrtos/src/syscalls.c
  libcrtos/src/crtos.c
  libcrtos/src/thread.c
  libcrtos/src/pthread.c
  libcrtos/src/dirent.c
  libcrtos/src/socket.c
  libcrtos/src/posix.c
  libcrtos/src/process.c
  libcrtos/src/unix.c
  libcrtos/src/fnmatch.c
  libcrtos/src/glob.c
  libcrtos/src/libgen.c
  libcrtos/src/appinfo.c
)
set(GFX_SOURCES libgfx/src/gfx.c libgfx/src/draw.c libgfx/src/image.c libgfx/src/fonts.c libgfx/src/ui.c libgfx/src/keys.c
  libgfx/src/settings.c libgfx/src/wallpaper.c)
set(TFTLIB_SOURCES
  tftlib/TFTLIB_8BIT.cpp
  tftlib/Print.cpp
  tftlib/WString.cpp
  tftlib/dtostrf.c
  tftlib/memfill32.s
  tftlib/tftlib_glue.c
)

foreach(v "" "_xip")
  if(v)
    set(vflags -mxip)
  else()
    set(vflags "")
  endif()
  add_library(crtos_crt0${v} OBJECT libcrtos/src/crt0.c)
  add_library(crtos_memops${v} OBJECT ${CRTOS_TOP}/kernel/lib/memops.c)
  add_library(crtos_cxxrt${v} OBJECT libcrtos/src/cxxrt.cpp)
  add_library(crtos${v} STATIC ${CRTOS_LIB_SOURCES})
  add_library(crtosheap${v} STATIC libcrtos/src/heap.c)
  add_library(gfx${v} STATIC ${GFX_SOURCES})
  add_library(tftlib${v} STATIC ${TFTLIB_SOURCES})
  foreach(t crtos_crt0${v} crtos_memops${v} crtos_cxxrt${v} crtos${v} crtosheap${v} gfx${v} tftlib${v})
    target_compile_options(${t} PRIVATE ${CRTOS_APP_CFLAGS} ${vflags})
    target_include_directories(${t} PRIVATE ${CRTOS_APP_INCLUDES})
    target_compile_definitions(${t} PRIVATE ${CRTOS_APP_DEFINES})
  endforeach()
  target_include_directories(gfx${v} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libgfx/include)
  set_target_properties(gfx${v} PROPERTIES CRTOS_PUBLIC_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/libgfx/include
                                           OUTPUT_NAME gfx)
  target_compile_options(tftlib${v} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-variable;-Wno-unused-but-set-variable>")
  target_include_directories(tftlib${v} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tftlib)
  set_target_properties(tftlib${v} PROPERTIES CRTOS_PUBLIC_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/tftlib
                                              OUTPUT_NAME tftlib)
  set_target_properties(crtos${v} PROPERTIES OUTPUT_NAME crtos)
  set_target_properties(crtosheap${v} PROPERTIES OUTPUT_NAME crtosheap)
endforeach()

# the toolchain directory (cmake/crtos.cmake): start-up objects, libraries, specs, headers
crtos_toolchain()
