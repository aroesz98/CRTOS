/*
 * libs.ld - libraries of the kernel: newlib-nano and the GCC runtime. The C library's
 * system hooks (_sbrk, _close, ...) are in kernel/lib/syscalls.c and the board's platform.cpp.
 */

GROUP (
  "libgcc.a"
  "libc_nano.a"
  "libstdc++_nano.a"
  "libm.a"
  "crti.o"
  "crtn.o"
  "crtbegin.o"
  "crtend.o"
)
