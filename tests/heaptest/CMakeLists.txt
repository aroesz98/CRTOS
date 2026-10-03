# heaptest: checks of libcrtosheap, the compiler's malloc (run in place, as the compiler; a
# small arena heap, so the last checks need shared memory windows), and of emulated memory
crtos_app(heaptest SOURCES heaptest.c vmemtest.c XIP DEST flash0:bin STACK 16384 HEAP 1048576 LIBS crtosheap)
