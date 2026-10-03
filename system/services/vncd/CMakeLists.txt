# vncd: remote desktop - the screen, pointer, keys and clipboard for a VNC viewer (port 5900)
# (heap: the zlib buffers and the clipboard; the copies of the screen are shared memory)
crtos_app(vncd SOURCES vncd.c des.c deflate.c STACK 8192 HEAP 327680 LIBS gfx)
