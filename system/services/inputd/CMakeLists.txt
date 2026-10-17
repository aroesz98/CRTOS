# inputd: touch screen, keys and mice -> pointer and key events for gfxd (and the mouse cursor)
crtos_app(inputd SOURCES inputd.c STACK 8192 HEAP 32768 LIBS gfx)
