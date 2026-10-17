# gfxd: the graphics server - windows, composition with the PXP, input routing, the clipboard
# (heap: two texts of up to 64 KB while one replaces the other)
crtos_app(gfxd SOURCES gfxd.c STACK 8192 HEAP 262144 LIBS gfx)
