/*
 * libs.ld - libraries of the kernel: newlib-nano and the GCC runtime. The C library's
 * system hooks (_sbrk, _close, ...) are in kernel/lib/syscalls.c
 *
 * Derived from the linker script MCUXpresso IDE v25.6 generates for MIMXRT1052xxxxB
 * (Copyright 2008-2013 Code Red Technologies Ltd, Copyright 2013-2026 NXP,
 * SPDX-License-Identifier: BSD-3-Clause), kept here so that the kernel builds without the IDE.
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
