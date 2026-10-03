# snes: Super Nintendo emulator with a retro file browser for the SD card.
#
# The emulator core in core/ is taken from Snes9x (https://github.com/snes9xgit/snes9x,
# snes9x/ next to this tree, commit 1bcc369 of 24.09.2026; the Snes9x licence: core/LICENSE,
# free for non-commercial use): only the files a console without cartridge coprocessors
# needs - the 65C816 CPU, the memory map, DMA, the PPU and its tile renderer, the APU (SPC700
# and DSP) - and two cartridge coprocessors, DSP-1 (Mario Kart, Pilotwings) and the Super FX
# (Yoshi's Island, Star Fox). Its changes for CRTOS are marked "CRTOS". What it refers to but
# this program leaves out is in snes_stubs.cpp (other coprocessors, cheats, movies) and
# snes_input.cpp (the controller ports, in place of controls.cpp); snes_core.cpp is the port
# (settings, S9x* calls) and the C interface of snes_main.c. snes_fxjit.cpp translates the
# Super FX's program to Thumb-2 (with exactly the interpreter's results), snes_idle.cpp
# fast-forwards the 65816's waiting.
set(SNES_SRC ${CMAKE_CURRENT_SOURCE_DIR}/core)
set(SNES_CORE
  cpu.cpp cpuexec.cpp cpuops.cpp dma.cpp gfx.cpp globals.cpp memmap.cpp ppu.cpp tile.cpp
  tileimpl-n1x1.cpp tileimpl-n2x1.cpp tileimpl-h2x1.cpp clip.cpp
  apu/apu.cpp apu/bapu/smp/smp.cpp apu/bapu/dsp/sdsp.cpp
  dsp.cpp dsp1.cpp fxemu.cpp fxinst.cpp)
list(TRANSFORM SNES_CORE PREPEND ${SNES_SRC}/)

# The core keeps the ROM in a std::vector and file names in std::string: the out-of-line
# parts of those come from the toolchain's libstdc++ (nano); libcrtos goes before it, so its
# operator new/delete (on malloc) are the ones used.
# The core and the port use no C++ library (no <string>, <vector>...): C strings and plain
# blocks, so nothing of libstdc++ is linked and every file compiles with little memory.

# heap: the ROM (up to 6 MB), save RAM, tile caches and screens (about 10 MB)
# (the core and libgfx both have a gfx.h: libgfx's comes first for this program's C files, the
# core's files find their own next to them, snes_core.cpp names it core/gfx.h)
crtos_app(snes
  SOURCES snes_main.c snes_browser.c snes_draw.c snes_font.c snes_core.cpp snes_idle.cpp snes_fxjit.cpp snes_input.cpp snes_stubs.cpp
          ${SNES_CORE}
  INCLUDES ${CRTOS_TOP}/system/lib/libgfx/include ${SNES_SRC} ${SNES_SRC}/apu ${SNES_SRC}/apu/bapu ${CMAKE_CURRENT_SOURCE_DIR}
  STACK 65536 HEAP 11534336 LIBS gfx crtos ICON icon.png
  FAST snes_fast.txt FAST_KB 62) # the hottest code in ITCM (snes_fast.txt)
# the emulation is all CPU time: the fastest code the compiler makes
target_compile_options(app_snes_objs PRIVATE -O3 "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>")
# third-party code: its own style of warnings
set_source_files_properties(${SNES_CORE} PROPERTIES COMPILE_OPTIONS "-w")
