# nes: NES emulator with a retro file explorer for the SD card.
#
# The emulator core in core/ is taken from https://github.com/ObaraEmmanuel/NES (MIT licence,
# core/LICENSE; commit aa880b9 of 19.09.2026): the CPU, the memory map, the APU, the mappers,
# NSF and Game Genie. Its changes for CRTOS are marked "CRTOS" (core/apu.c, the MMC5 mapper
# core/mappers/mmc5.c and its entry in core/mappers/mapper.c, the PPU hooks in mapper.h). Its
# SDL front end and its PPU are replaced by this program: port/ (the platform), nes_ppu.c
# (a PPU drawn by scanline, 20 times faster), nes_sound.c (the sound).
set(NES_SRC ${CMAKE_CURRENT_SOURCE_DIR}/core)

# The CPU, the memory map, the APU, the PPU and the clock are compiled together in
# nes_fast.c (inlined across files); the rest of the core here.
set(NES_CORE
  biquad.c genie.c nsf.c
  mappers/mapper.c mappers/axrom.c mappers/cnrom.c mappers/colordreams.c mappers/cprom.c
  mappers/gnrom.c mappers/mapper185.c mappers/mmc1.c mappers/mmc3.c mappers/mmc5.c mappers/uxrom.c
  mappers/vrc1.c)
list(TRANSFORM NES_CORE PREPEND ${NES_SRC}/)

# Both the core and libgfx have a "gfx.h": libgfx's comes first for this program's files, the
# core's files find their own next to them.
crtos_app(nes
  SOURCES nes_main.c nes_fast.c nes_font.c nes_draw.c nes_browser.c port/nes_port.c ${NES_CORE}
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/port ${CRTOS_TOP}/system/lib/libgfx/include ${NES_SRC} ${NES_SRC}/mappers
  STACK 65536 HEAP 4194304 LIBS gfx ICON icon.png)
# the emulation is all CPU time: the fastest code the compiler makes
target_compile_options(app_nes_objs PRIVATE -O3)
# third-party code: its own style of warnings
set_source_files_properties(${NES_CORE} nes_fast.c PROPERTIES COMPILE_OPTIONS "-w")
