#!/usr/bin/env python3
"""Check that every undefined symbol of the built modules (.ko) is exported by the kernel
(KSYM entries in kernel/os/ksyms.cpp) or by another module (EXPORT_SYMBOL), and that
every relocation type is one the kernel loader supports.

    python tools/modcheck.py [build/sdcard/crtos/drivers]
    python tools/modcheck.py --apps [--quiet] <program.app...>   (programs: nothing may be undefined)

The binutils are those of CRTOS_GCC_BIN (the build sets it) or the ones on the PATH.
"""
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))


def binutil(name):
    d = os.environ.get('CRTOS_GCC_BIN')
    if d:
        return os.path.join(d, name)
    return shutil.which(name) or name


NM = binutil('arm-none-eabi-nm')
READELF = binutil('arm-none-eabi-readelf')
SUPPORTED = {'R_ARM_NONE', 'R_ARM_ABS32', 'R_ARM_REL32', 'R_ARM_THM_CALL', 'R_ARM_THM_JUMP24', 'R_ARM_TARGET1',
             'R_ARM_V4BX', 'R_ARM_PREL31', 'R_ARM_THM_MOVW_ABS_NC', 'R_ARM_THM_MOVT_ABS', 'R_ARM_THM_JUMP19',
             'R_ARM_THM_JUMP11'}


def kernel_exports():
    text = open(os.path.join(ROOT, 'kernel', 'os', 'ksyms.cpp'), encoding='utf-8').read()
    return set(re.findall(r'KSYM\((\w+)\)', text))


LOADER_SYMBOLS = {'__exidx_start', '__exidx_end'}


def check_xip(path):
    """A program that runs in place (ELF ET_DYN, toolchain/crtos-xip.ld): the problems found"""
    problems = []
    hdr = subprocess.run([READELF, '-hlSW', path], capture_output=True, text=True).stdout
    if re.search(r'^\s+(INTERP|TLS)\s', hdr, re.M):  # (program headers of those types)
        problems.append('needs a dynamic linker or thread-local storage')
    if not re.search(r'\] \.got\s', hdr):
        problems.append('no .got')
    rel = subprocess.run([READELF, '-rW', path], capture_output=True, text=True).stdout
    bad = sorted(set(re.findall(r'(R_ARM_\w+)', rel)) - {'R_ARM_RELATIVE', 'R_ARM_NONE'})
    if bad:
        problems.append('dynamic relocations %s' % bad)
    out = subprocess.run([NM, path], capture_output=True, text=True).stdout
    if not any(l.split()[-1] == '_start' and l.split()[-2] in 'Tt' for l in out.splitlines() if len(l.split()) == 3):
        problems.append('no _start')
    return problems


def check_apps(paths, quiet=False):
    """Programs (.app): nothing undefined except what the loader provides, entry point _start,
    only supported relocations; programs that run in place: only R_ARM_RELATIVE, a GOT, no
    dynamic linker. @quiet: print only the problems."""
    ok = True
    for path in paths:
        name = os.path.basename(path)
        with open(path, 'rb') as f:
            head = f.read(18)
        if head[:4] == b'\x7fELF' and head[16] == 3:  # ET_DYN
            problems = check_xip(path)
            if problems:
                ok = False
                print('%s: %s' % (name, '; '.join(problems)))
            elif not quiet:
                print('%s: %d bytes (runs in place)' % (name, os.path.getsize(path)))
            continue
        out = subprocess.run([NM, path], capture_output=True, text=True).stdout
        undef = [l.split()[-1] for l in out.splitlines() if l.strip().startswith('U ')]
        missing = sorted(set(undef) - LOADER_SYMBOLS)
        has_start = any(l.split()[-1] == '_start' and l.split()[-2] in 'Tt' for l in out.splitlines()
                        if len(l.split()) == 3)
        rel = subprocess.run([READELF, '-r', path], capture_output=True, text=True).stdout
        bad = sorted(set(re.findall(r'(R_ARM_\w+)', rel)) - SUPPORTED)
        size = os.path.getsize(path)
        if missing or bad or not has_start:
            ok = False
            print('%s: unresolved %s unsupported relocations %s%s' % (name, missing, bad,
                                                                   '' if has_start else ' no _start'))
        elif not quiet:
            print('%s: %d bytes' % (name, size))
    if not quiet or not ok:
        print('appcheck: %d programs, %s' % (len(paths), 'ok' if ok else 'PROBLEMS'))
    return 0 if ok else 1


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--apps':
        args = sys.argv[2:]
        quiet = '--quiet' in args
        return check_apps([a for a in args if a != '--quiet'], quiet)
    moddir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'sdcard', 'crtos', 'drivers')
    kos = sorted(f for f in os.listdir(moddir) if f.endswith('.ko'))
    exports = kernel_exports()
    undef, defined_exports = {}, set()
    for ko in kos:
        path = os.path.join(moddir, ko)
        out = subprocess.run([NM, path], capture_output=True, text=True).stdout
        undef[ko] = [l.split()[-1] for l in out.splitlines() if l.strip().startswith('U ')]
        defined_exports |= set(re.findall(r'__ksymtab_(\w+)', out))
    ok = True
    for ko in kos:
        missing = [s for s in undef[ko] if s not in exports and s not in defined_exports]
        rel = subprocess.run([READELF, '-r', os.path.join(moddir, ko)], capture_output=True, text=True).stdout
        types = set(re.findall(r'(R_ARM_\w+)', rel))
        bad = sorted(types - SUPPORTED)
        if missing or bad:
            ok = False
            print('%s: unresolved %s unsupported relocations %s' % (ko, missing, bad))
    print('modcheck: %d modules, %s' % (len(kos), 'ok' if ok else 'PROBLEMS'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
