#!/usr/bin/env python3
"""List the C sources of a NetSurf library the way its buildsystem does: every directory
below src/ (and other roots given) that holds a Makefile contributes its DIR_SOURCES.

    python tools/nsmake.py netsurf-libs/libcss [src bindings/hubbub ...]

Prints one path per line, relative to the library. Conditional blocks (ifeq ...) are read
as if every condition held; the caller filters what it does not want.
"""
import os
import re
import sys


def dir_sources(makefile):
    text = open(makefile, encoding='utf-8', errors='replace').read()
    text = re.sub(r'\\\r?\n', ' ', text)
    out = []
    for line in text.splitlines():
        line = line.split('#', 1)[0]
        m = re.match(r'\s*DIR_SOURCES\s*(:=|\+=|=)\s*(.*)', line)
        if not m:
            continue
        words = [w for w in m.group(2).split() if not w.startswith('$(')]
        if m.group(1) == '+=' or '$(DIR_SOURCES)' in m.group(2):
            out += words
        else:
            out = words
    return out


def walk(lib, root):
    found = []
    base = os.path.join(lib, root)
    for d, dirs, files in os.walk(base):
        dirs.sort()
        if 'Makefile' not in files or os.path.basename(d) in ('test', 'tests'):
            if 'Makefile' not in files and d != base:
                dirs[:] = []  # the buildsystem stops where a Makefile is missing
            continue
        rel = os.path.relpath(d, lib).replace('\\', '/')
        for s in dir_sources(os.path.join(d, 'Makefile')):
            found.append(rel + '/' + s)
    return found


if __name__ == '__main__':
    lib = sys.argv[1]
    roots = sys.argv[2:] or ['src']
    for r in roots:
        for s in walk(lib, r):
            print(s)
