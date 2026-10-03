#!/usr/bin/env python3
"""Mark a program's hottest functions for on-chip memory: renames their sections
".text.<symbol>" to ".fast.<symbol>" in the partially linked program (crtos_app FAST). The
kernel's loader puts ".fast*" sections into ITCM (else OCRAM) when there is room, so they do
not run from SDRAM through the small instruction cache.

    python tools/appfast.py PROGRAM.debug.app LIST [--limit KB]

LIST has one symbol per line (the name "nm" shows, C++ names mangled); "#" starts a comment.
The functions are taken in the list's order until --limit KB (default 48) of code; a symbol
the program does not have (inlined, removed) is skipped with a note.

The binutils are those of CRTOS_GCC_BIN (the build sets it) or the ones on the PATH.
"""
import os
import re
import shutil
import subprocess
import sys


def binutil(name):
    d = os.environ.get('CRTOS_GCC_BIN')
    if d:
        return os.path.join(d, name)
    return shutil.which(name) or name


OBJCOPY = binutil('arm-none-eabi-objcopy')
READELF = binutil('arm-none-eabi-readelf')


def sections(path):
    """name -> size of the program's sections"""
    out = subprocess.run([READELF, '-SW', path], capture_output=True, text=True).stdout
    found = {}
    for line in out.splitlines():
        m = re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)', line)
        if m:
            found[m.group(1)] = int(m.group(2), 16)
    return found


def main():
    args = sys.argv[1:]
    limit = 48 * 1024
    if '--limit' in args:
        i = args.index('--limit')
        limit = int(args[i + 1]) * 1024
        del args[i:i + 2]
    if len(args) != 2:
        sys.exit(__doc__)
    path, listfile = args
    have = sections(path)
    renames, total, missing = [], 0, []
    for line in open(listfile, encoding='utf-8'):
        sym = line.split('#', 1)[0].strip()
        if not sym:
            continue
        sec = '.text.' + sym
        if sec not in have:
            missing.append(sym)
            continue
        size = (have[sec] + 3) & ~3
        if total + size > limit:
            continue
        total += size
        renames += ['--rename-section', '%s=.fast.%s' % (sec, sym)]
    if missing:
        print('appfast: %s: not in the program: %s' % (os.path.basename(path), ', '.join(missing[:8]) +
                                                    (' ...' if len(missing) > 8 else '')))
    if renames:
        subprocess.run([OBJCOPY] + renames + [path], check=True)
    print('appfast: %s: %d functions, %d KB fast code' % (os.path.basename(path), len(renames) // 2,
                                                          (total + 1023) // 1024))


if __name__ == '__main__':
    main()
