/*
 * compat.h - included first in every file of nsgenbind (NetSurf's JavaScript binding
 * generator) by tools/netsurf_prepare.py: on Windows its utils.c brings strndup (MinGW's C
 * library lacks it), but the lexers use it without seeing that declaration.
 *
 * The other files here are nsgenbind's parsers and lexers as bison 3.8.2 and flex 2.6.4 made
 * them from third_party/netsurf-libs/nsgenbind/src (*.y, *.l; in WSL), so that the Windows
 * build needs neither tool.
 */
#ifndef NSGENBIND_COMPAT_H
#define NSGENBIND_COMPAT_H

#include <stddef.h>

#ifdef _WIN32
char *strndup(const char *s, size_t n);
#endif

#endif
