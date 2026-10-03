#!/usr/bin/env python3
"""The files libstdc++'s make rules generate before it is compiled, for the build of the
position-independent runtime libraries (cmake/xiplibs.cmake, toolchain/xiplibs/recipe.txt):

    python tools/xiplibs.py tzdata  <tzdata.zi> <tzdata.zi.h>
        the time zone data in a C++ raw string (src/c++20/Makefile, tzdata.zi.h)
    python tools/xiplibs.py iosfail <in.s> <out.s>
        the assembly of the C++11 ios_failure with its type_info's vtable renamed
        (src/c++11/Makefile, $(rewrite_ios_failure_typeinfo))
"""
import re
import sys


def write(path, data):
    with open(path, 'wb') as f:
        f.write(data)


def tzdata(src, out):
    data = open(src, 'rb').read()
    write(out, b'static const char tzdata_chars[] = R"__libstdcxx__(\n' + data + b')__libstdcxx__";\n')


def iosfail(src, out):
    # sed -e '/^_*_ZTISt13__ios_failure:/,/_ZTVN10__cxxabiv120__si_class_type_infoE/
    #          s/_ZTVN10__cxxabiv120__si_class_type_infoE/_ZTVSt19__iosfail_type_info/'
    start = re.compile(rb'^_*_ZTISt13__ios_failure:')
    old, new = b'_ZTVN10__cxxabiv120__si_class_type_infoE', b'_ZTVSt19__iosfail_type_info'
    lines = open(src, 'rb').read().split(b'\n')
    inside = False
    for i, line in enumerate(lines):
        if not inside:
            if start.search(line):
                inside = True
                lines[i] = line.replace(old, new, 1)    # (sed checks the end from the next line)
            continue
        ends = old in line
        lines[i] = line.replace(old, new, 1)
        if ends:
            inside = False
    write(out, b'\n'.join(lines))


if __name__ == '__main__':
    steps = {'tzdata': tzdata, 'iosfail': iosfail}
    if len(sys.argv) != 4 or sys.argv[1] not in steps:
        sys.exit(__doc__)
    steps[sys.argv[1]](sys.argv[2], sys.argv[3])
