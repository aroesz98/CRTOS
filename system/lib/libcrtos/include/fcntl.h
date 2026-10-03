/*
 * fcntl.h - newlib's, with O_BINARY and O_TEXT as 0. newlib gives them Cygwin's bits, and
 * portable code (gnulib's binary-io.h, binutils, GCC) then takes CRTOS for a system with a
 * text mode and calls _setmode()/_fileno(). CRTOS files have no text mode: every open is
 * binary, as on any POSIX system.
 */
#ifndef CRTOS_FCNTL_H
#define CRTOS_FCNTL_H

#include_next <fcntl.h>

#undef O_BINARY
#undef O_TEXT
#define O_BINARY 0
#define O_TEXT   0

#endif
