#!/usr/bin/env python3
"""Map addresses from a fault report to symbols and source lines of a program or module.

    python tools/appsym.py FILE BASE [--fast FBASE] ADDR...

FILE is the unstripped ELF (e.g. build/apps/netsurf/netsurf.debug.app), BASE the address the
loader placed the image at (the process arena, or the module base from lsmod). The section
layout is recomputed the way the kernel's elf_layout() does it: code first, then data, then
bss, each section at its alignment, in section-header order. A program with fast code
(sections ".fast*", crtos_app FAST) that the kernel put into on-chip memory has them in a
second block at FBASE (the fault report's "fast"; the kernel log at the start), and veneer
pools after the code of each block (elf_split_fast()).

A program that runs in place (ELF ET_DYN, crtos_app XIP) has its final addresses already:
BASE is where its text is (the fault report's "text"), and an address is BASE + its address in
the file.
"""
import shutil
import subprocess
import sys

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection

SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4


def _block(elf, keep, pool=0):
    """offsets of the sections @keep(index, section) says are in the block, and its size"""
    addr = {}
    off = 0
    for pass_ in range(3):
        for i, s in enumerate(elf.iter_sections()):
            flags = s['sh_flags']
            if not flags & SHF_ALLOC or not keep(i, s):
                continue
            exe = bool(flags & SHF_EXECINSTR)
            bss = s['sh_type'] == 'SHT_NOBITS'
            if (pass_ == 0 and not exe) or (pass_ == 1 and (exe or bss)) or (pass_ == 2 and not bss):
                continue
            align = s['sh_addralign'] or 1
            off = (off + align - 1) & ~(align - 1)
            addr[i] = off
            off += s['sh_size']
        if pass_ == 0 and pool is not None:
            off = ((off + 3) & ~3) + pool
    return addr, off


def _is_fast(s):
    return bool(s['sh_flags'] & SHF_ALLOC) and s.name.startswith('.fast')


def _veneers(elf):
    """veneers of the split image per block (0 main, 1 fast): the distinct (symbol, addend)
    of branches from one block into the other, as the kernel counts them"""
    import struct
    from elftools.elf.relocation import RelocationSection
    secs = list(elf.iter_sections())
    fast = [_is_fast(s) for s in secs]
    symtab = next(s for s in secs if isinstance(s, SymbolTableSection))
    syms = list(symtab.iter_symbols())
    seen = [set(), set()]
    for rs in secs:
        if not isinstance(rs, RelocationSection) or rs['sh_info'] >= len(secs):
            continue
        t = secs[rs['sh_info']]
        if not t['sh_flags'] & SHF_ALLOC or t['sh_type'] == 'SHT_NOBITS':
            continue
        data = t.data()
        src = fast[rs['sh_info']]
        for r in rs.iter_relocations():
            ty = r['r_info_type']
            if ty not in (10, 30, 51):
                continue
            sh = syms[r['r_info_sym']]['st_shndx']
            dst = fast[sh] if isinstance(sh, int) and 0 < sh < len(secs) else False
            if dst == src:
                continue
            if r.is_RELA():
                a = r['r_addend']
            else:
                hi, lo = struct.unpack_from('<HH', data, r['r_offset'])
                s_ = (hi >> 10) & 1
                j1, j2 = (lo >> 13) & 1, (lo >> 11) & 1
                if ty == 51:
                    v = (s_ << 20) | (j2 << 19) | (j1 << 18) | ((hi & 0x3f) << 12) | ((lo & 0x7ff) << 1)
                    a = v - (1 << 21) if v & (1 << 20) else v
                else:
                    v = (s_ << 24) | ((~(j1 ^ s_) & 1) << 23) | ((~(j2 ^ s_) & 1) << 22) | ((hi & 0x3ff) << 12) | \
                        ((lo & 0x7ff) << 1)
                    a = v - (1 << 25) if v & (1 << 24) else v
            seen[int(src)].add((r['r_info_sym'], a))
    return len(seen[0]) * 8, len(seen[1]) * 8


def layout(elf):
    """offsets of the image's sections in the arena and its size (the main block of a split
    image, without the fast sections)"""
    if not any(_is_fast(s) for s in elf.iter_sections()):
        return _block(elf, lambda i, s: True, None)
    pool, _ = _veneers(elf)
    return _block(elf, lambda i, s: not _is_fast(s), pool)


def fast_layout(elf):
    """offsets of the fast sections in their block and its size ({} and 0: none)"""
    if not any(_is_fast(s) for s in elf.iter_sections()):
        return {}, 0
    _, pool = _veneers(elf)
    return _block(elf, lambda i, s: _is_fast(s), pool)


def addr2line():
    import os
    d = os.environ.get('CRTOS_GCC_BIN')
    if d:
        return os.path.join(d, 'arm-none-eabi-addr2line')
    return shutil.which('arm-none-eabi-addr2line')


def xip(path, base, wanted):
    """A program that runs in place: address - BASE is the address in the file"""
    tool = addr2line()
    with open(path, 'rb') as f:
        elf = ELFFile(f)
        text = max(s['p_vaddr'] + s['p_memsz'] for s in elf.iter_segments()
                   if s['p_type'] == 'PT_LOAD' and not s['p_flags'] & 2)
        syms = []
        for sec in elf.iter_sections():
            if isinstance(sec, SymbolTableSection):
                for sym in sec.iter_symbols():
                    if sym.name and not sym.name.startswith('$') and sym['st_info']['type'] == 'STT_FUNC':
                        syms.append((sym['st_value'] & ~1, sym['st_size'], sym.name))
        syms.sort()
    print('text %08x..%08x (%u bytes, runs in place)' % (base, base + text, text))
    for a in wanted:
        v = a - base
        if not 0 <= v < text:
            print('%08x: outside the text' % a)
            continue
        best = None
        for s in syms:
            if s[0] <= v:
                best = s
            else:
                break
        line = ''
        if tool:
            r = subprocess.run([tool, '-f', '-i', '-C', '-e', path, '%x' % v], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
            line = ' '.join(r.stdout.split())
        print('%08x: %s+0x%x %s' % (a, best[2] if best else '?', v - best[0] if best else v, line))


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    path = sys.argv[1]
    base = int(sys.argv[2], 16)
    args = sys.argv[3:]
    with open(path, 'rb') as f:
        head = f.read(18)
    if head[16] == 3:  # ET_DYN: runs in place
        return xip(path, base, [int(a, 16) for a in args])
    fbase = None
    if len(args) >= 2 and args[0] == '--fast':
        fbase = int(args[1], 16)
        args = args[2:]
    wanted = [int(a, 16) for a in args]
    with open(path, 'rb') as f:
        elf = ELFFile(f)
        addr, size = layout(elf)
        faddr, fsize = fast_layout(elf)
        if fbase is None:
            faddr = {}
        blocks = [(base, size, addr)] + ([(fbase, fsize, faddr)] if faddr else [])
        sections = list(elf.iter_sections())
        syms = []
        for sec in elf.iter_sections():
            if not isinstance(sec, SymbolTableSection):
                continue
            for sym in sec.iter_symbols():
                idx = sym['st_shndx']
                if not isinstance(idx, int):
                    continue
                if sym['st_info']['type'] not in ('STT_FUNC', 'STT_OBJECT', 'STT_NOTYPE'):
                    continue
                if not sym.name or sym.name.startswith('$'):
                    continue
                for b, _, where in blocks:
                    if idx in where:
                        syms.append((b + where[idx] + (sym['st_value'] & ~1), sym['st_size'], sym.name, idx))
        syms.sort()
        tool = addr2line()
        print('image %08x..%08x (%u bytes)' % (base, base + size, size))
        if len(blocks) > 1:
            print('fast code %08x..%08x (%u bytes)' % (fbase, fbase + fsize, fsize))
        for a in wanted:
            best = None
            for s in syms:
                if s[0] <= a:
                    best = s
                else:
                    break
            block = next((b for b in blocks if b[0] <= a < b[0] + b[1]), None)
            if not best or not block:
                print('%08x: outside the image' % a)
                continue
            start, sz, name, idx = best
            sec = sections[idx]
            line = ''
            if tool:
                rel = a - block[0] - block[2][idx]
                r = subprocess.run([tool, '-f', '-i', '-C', '-j', sec.name, '-e', path, '%x' % rel],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                line = ' '.join(r.stdout.split())
            print('%08x: %s+0x%x [%s] %s' % (a, name, a - start, sec.name, line))


if __name__ == '__main__':
    main()
