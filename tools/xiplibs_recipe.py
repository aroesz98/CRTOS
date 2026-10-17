#!/usr/bin/env python3
"""The recipe of the runtime libraries of programs that run in place (XIP), from a build of them.

    python3 tools/xiplibs_recipe.py TOOLCHAIN [WORK]
        TOOLCHAIN  the Arm GNU Toolchain the build uses (its root: bin/, lib/, arm-none-eabi/),
                   e.g. "/mnt/c/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.3 rel1"
        WORK       stage A of toolchain/native/build.sh (default ~/crtos-toolchain)

A program that runs in place from /flash0 (crtos_app XIP, gcc -mxip) links position-independent
libraries whose data is addressed through r9: lib/xip/libc.a, libm.a, libgcc.a, libstdc++.a and
libsupc++.a. The build makes them with the Arm GNU Toolchain itself (cmake/xiplibs.cmake), each
object from the newlib and GCC sources of third_party/ (the commits of the toolchain's sources)
with the options newlib's and GCC's own builds give it, the CPU's and the position-independent
ones (-fPIC -msingle-pic-base -mpic-register=r9 -mno-pic-data-is-text-relative).

Those builds need configure and make, so what they do is kept in the repository:
toolchain/xiplibs/recipe.txt (which object of which library, from which source, with which
options) and the few files their configure generates (toolchain/xiplibs/gen/). This script reads
it from such a build - stage A (WORK/build, WORK/src, WORK/cross: a Linux or WSL system) - and is
needed again only for another toolchain release. It does not touch the build trees: make -n runs
in a copy of them (WORK/.xiprecipe).
"""
import hashlib
import os
import re
import shlex
import shutil
import subprocess
import sys
from collections import Counter

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(REPO, 'toolchain', 'xiplibs')
CPU = ['-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard']   # stage A's defaults
GCCVER = '14.3.1'
MULTILIB = 'thumb/v7e-m+dp/hard'

if len(sys.argv) < 2:
    sys.exit(__doc__)
TC = os.path.abspath(sys.argv[1])
WORK = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.expanduser('~/crtos-toolchain'))
SRC, BLD, CROSS = WORK + '/src', WORK + '/build', WORK + '/cross'
COPY = WORK + '/.xiprecipe'
NEWLIB_B = BLD + '/cross-newlib/arm-none-eabi/newlib'
LIBGCC_B = BLD + '/cross-gcc/arm-none-eabi/libgcc'
STDCXX_B = BLD + '/cross-gcc/arm-none-eabi/libstdc++-v3'
GCC_B = BLD + '/cross-gcc/gcc'
SYSINC = CROSS + '/arm-none-eabi/include'
# what the toolchain has the same (@T): GCC's generated headers in plugin/include (stage A's
# differ only in tm.h, the default CPU), libstdc++'s thread headers
TC_DIRS = [('lib/gcc/arm-none-eabi/%s/plugin/include' % GCCVER, GCC_B),
           ('arm-none-eabi/include/c++/%s/arm-none-eabi/%s/bits' % (GCCVER, MULTILIB),
            STDCXX_B + '/include/arm-none-eabi/bits')]
# taken from the toolchain although they differ: only in settings of the compiler itself (LTO,
# plugins, its version string), which the libraries do not use
TC_ANYWAY = {'auto-host.h', 'version.h'}

AREAS = [(NEWLIB_B, '@B/newlib'), (LIBGCC_B, '@B/libgcc'), (STDCXX_B, '@B/libstdc++'),
         (GCC_B, '@B/gcc'), (SYSINC, '@B/sysroot/include')]
LIBS = {     # library: (where stage A installed it, the commands that make its objects)
    'libc': (CROSS + '/arm-none-eabi/lib/libc.a', 'newlib'),
    'libm': (CROSS + '/arm-none-eabi/lib/libm.a', 'newlib'),
    'libgcc': (CROSS + '/lib/gcc/arm-none-eabi/%s/libgcc.a' % GCCVER, 'libgcc'),
    'libsupc++': (CROSS + '/arm-none-eabi/lib/libsupc++.a', 'libstdcxx'),
    'libstdc++': (CROSS + '/arm-none-eabi/lib/libstdc++.a', 'libstdcxx'),
}
STDCXX_DIRS = ('libsupc++', 'src/c++98', 'src/c++11', 'src/c++17', 'src/c++20', 'src/c++23',
               'src/c++26', 'src/filesystem', 'src/experimental', 'src/libbacktrace', 'src')


def run(cmd, cwd=None, env=None):
    return subprocess.run(cmd, cwd=cwd, env=env, shell=isinstance(cmd, str), capture_output=True,
                          text=True).stdout


# ---- the commands, from make -n in a copy of the build trees ----------------------------------

ENV = dict(os.environ, PATH=CROSS + '/bin:' + os.environ['PATH'])


def make_copy():
    shutil.rmtree(COPY, ignore_errors=True)
    os.makedirs(COPY + '/build/cross-newlib')
    os.makedirs(COPY + '/build/cross-gcc/arm-none-eabi')
    os.symlink(SRC, COPY + '/src')
    os.symlink(CROSS, COPY + '/cross')
    os.symlink(GCC_B, COPY + '/build/cross-gcc/gcc')
    run(['cp', '-a', BLD + '/cross-newlib/arm-none-eabi', COPY + '/build/cross-newlib/'])
    run(['cp', '-a', LIBGCC_B, STDCXX_B, COPY + '/build/cross-gcc/arm-none-eabi/'])
    # without the objects and archives make -n lists how each is made (the time stamps of the
    # copy keep the makefiles from being made again)
    run("find %s/build -type f \\( -name '*.o' -o -name '*.lo' -o -name '*.la' -o -name '*.a' \\) -delete" % COPY)
    run("find %s/build -type d -name .libs -prune -exec rm -rf {} +" % COPY)


def make_n(real, target='all'):
    """[(directory in the real tree, command)] of make -n in the copy"""
    text = run(['make', '-n', '-k', target], cwd=real.replace(WORK, COPY), env=ENV)
    text = text.replace('\\\n', ' ')                       # a command continued on the next line
    return [(real, line) for line in text.splitlines()]


def parse(cwd, line):
    """(object path, source path, [options]) of a compile command, else None"""
    line = line.strip()
    if ';' in line and line.startswith('echo'):            # newlib: echo "  CC  " x.o;gcc ...
        line = line.split(';', 1)[1]
    m = re.search(r"`test -f '([^']*)' \|\| echo '([^']*)'`(\S+)", line)
    if m:                                                   # automake's source lookup
        rel = m.group(1)
        src = rel if os.path.exists(os.path.join(cwd, rel)) else m.group(2) + m.group(3)
        line = line[:m.start()] + src + line[m.end():]
    if '--mode=compile' in line:                            # libtool
        line = line.split('--mode=compile', 1)[1]
    try:
        argv = shlex.split(line)
    except ValueError:
        return None
    if not argv or not argv[0].endswith(('gcc', 'xgcc')) or not ('-c' in argv or '-S' in argv):
        return None
    obj = src = None
    opts = []
    i = 1
    while i < len(argv):
        a = argv[i]
        if a in ('-o', '-MT', '-MF', '-MQ'):
            if a == '-o':
                obj = argv[i + 1]
            i += 2
            continue
        if a in ('-c', '-S', '-MD', '-MP', '-shared-libgcc') or a.startswith(('-B', '-L')):
            i += 1
            continue
        if a.startswith('-g') or (a.startswith('-W') and not a.startswith(('-Wa,', '-Wp,', '-Wabi'))):
            # no debug data, no warnings (but -Wabi=<n>: it sets the ABI version of the
            # compatibility aliases of mangled names, so it changes the objects)
            i += 1
            continue
        if not a.startswith('-') and re.search(r'\.(c|cc|S|s|cpp)$', a):
            src = a
            i += 1
            continue
        if a in ('-I', '-isystem', '-idirafter', '-include', '-iquote'):
            opts += ['-I' + argv[i + 1]] if a == '-I' else [a, argv[i + 1]]
            i += 2
            continue
        opts.append(a)
        i += 1
    if src and not obj:                                     # libtool: the object of the source
        obj = os.path.splitext(os.path.basename(src))[0] + '.o'
    if not obj or not src:
        return None
    if obj.endswith('.lo'):
        obj = obj[:-3] + '.o'
    return os.path.normpath(os.path.join(cwd, obj)), os.path.normpath(os.path.join(cwd, src)), opts


# ---- paths: the sources (@N newlib, @G GCC), the toolchain (@T), the build directory (@B) -------

GEN = {}            # @X file (generated, kept in the repository) -> its content
COPIES = {}         # @B file -> where it comes from (@N, @G, @T or @X)
GENSTEPS = []       # recipe lines: files the build makes before compiling
_blob_index = {}


def area_of(path):
    for real, name in AREAS:
        if path == real or path.startswith(real + '/'):
            return real, name
    return None, None


def src_name(path):
    path = os.path.realpath(path)
    for real, name in ((SRC + '/newlib-cygwin', '@N'), (SRC + '/gcc', '@G')):
        if path.startswith(real + '/'):
            return name + path[len(real):]
    return None


def toolchain_name(path):
    for rel, real in TC_DIRS:
        if os.path.dirname(path) == real:
            p = os.path.join(TC, rel, os.path.basename(path))
            if os.path.isfile(p) and (os.path.basename(path) in TC_ANYWAY or
                                      open(p, 'rb').read() == open(path, 'rb').read()):
                return '@T/%s/%s' % (rel, os.path.basename(path))
    return None


def keep(path, dest):
    """A generated file kept in the repository (once for the same content)"""
    data = open(path, 'rb').read()
    # its #includes of sources by absolute path (libgcc_tm.h): relative to the source
    # directories the compiles have (-I .../libgcc, -I .../gcc)
    for d in ('/gcc/libgcc/', '/gcc/gcc/'):
        data = data.replace((SRC + d).encode(), b'')
    if WORK.encode() in data:
        raise SystemExit('xiplibs_recipe: %s names a path of the build: %s' % (path, WORK))
    for x, d in GEN.items():
        if d == data and os.path.basename(x) == os.path.basename(dest):
            return x
    x = '@X/' + dest[3:]
    GEN[x] = data
    return x


def need_file(path):
    """A file of a build directory the compiles read: copied into @B from where it comes"""
    real, name = area_of(path)
    dest = name + path[len(real):]
    if dest in COPIES or any(l.split()[1] == dest for l in GENSTEPS):
        return dest
    s = src_name(path) or toolchain_name(path)
    if s is None and not os.path.islink(path):
        s = same_source(path)
    if s is None and generated_source(path, dest):
        return dest
    COPIES[dest] = s or keep(path, dest)
    return dest


def same_source(path):
    """A newlib source file with the same content and name as a copied header (targ-include,
    the installed headers), else None"""
    if not _blob_index:
        for dp, dns, fns in os.walk(SRC + '/newlib-cygwin/newlib/libc'):
            for fn in fns:
                if fn.endswith('.h'):
                    p = os.path.join(dp, fn)
                    _blob_index.setdefault(hashlib.sha1(open(p, 'rb').read()).hexdigest(), []).append(p)
    cands = _blob_index.get(hashlib.sha1(open(path, 'rb').read()).hexdigest(), [])
    tail = '/'.join(path.split('/')[-2:])
    for c in sorted(cands, key=lambda c: (not c.endswith('/' + tail), len(c))):
        if os.path.basename(c) == os.path.basename(path):
            return src_name(c)
    return None


def generated_source(path, dest):
    """Files libstdc++'s make rules generate from its sources: the build makes them the same way
    (tools/xiplibs.py) instead of a copy here"""
    base = os.path.basename(path)
    if base == 'tzdata.zi.h':
        # tzdata.zi.h: tzdata.zi in a C++ raw string
        GENSTEPS.append('gen %s tzdata %s' % (dest, src_name(os.path.join(os.path.dirname(
            path.replace(STDCXX_B, SRC + '/gcc/libstdc++-v3')), 'tzdata.zi'))))
        return True
    if base == 'cxx11-ios_failure-lt.s':
        # the C++11 ios_failure compiled to assembly with its type_info's vtable renamed
        # ($(rewrite_ios_failure_typeinfo) of src/c++11/Makefile)
        cwd = os.path.dirname(path)
        cmds = [parse(cwd, l) for _, l in make_n_force(cwd, base)]
        cmds = [c for c in cmds if c]
        if len(cmds) != 1:
            raise SystemExit('xiplibs_recipe: %s: %d compile commands' % (base, len(cmds)))
        obj, src, opts = cmds[0]
        GENSTEPS.append(' '.join(['gen', dest, 'iosfail', map_path(src, False)] + CPU + options(cwd, opts)))
        return True
    return False


def make_n_force(real, target):
    """make -n of one file the copy has: without it"""
    c = os.path.join(real.replace(WORK, COPY), target)
    if os.path.exists(c):
        os.remove(c)
    return make_n(real, target)


def map_path(p, is_dir):
    """An absolute path of a command as the recipe has it, None to leave the option out"""
    p = os.path.normpath(p)
    if not is_dir and src_name(p) and \
            os.path.splitext(p)[1] == os.path.splitext(os.path.realpath(p))[1]:
        # a source, also through a symbolic link of a build directory (unless the link gives it
        # another kind: libstdc++'s atomicity.cc is config/.../atomicity.h - copied as .cc)
        return src_name(p)
    if p.startswith(CROSS + '/arm-none-eabi/sys-include') or p.startswith(CROSS + '/lib/gcc/'):
        return None
    if p.startswith(CROSS + '/arm-none-eabi/bin') or p.startswith(CROSS + '/arm-none-eabi/lib'):
        return None
    real, name = area_of(p)
    if real:
        if not is_dir:
            return need_file(p)
        if not os.path.isdir(p):
            return None
        return name + p[len(real):]
    s = src_name(p) if os.path.exists(p) else None
    if s:
        return s
    raise SystemExit('xiplibs_recipe: a path the recipe cannot name: %s' % p)


def options(cwd, opts):
    res = []
    i = 0
    while i < len(opts):
        a = opts[i]
        if a.startswith('-I'):
            d = os.path.normpath(os.path.join(cwd, a[2:]))
            m = map_path(d, True)
            if m:
                res.append('-I' + m)
            # libstdc++ has no dependency files: the headers of its build directories (config.h)
            if m and m.startswith('@B/libstdc++') and not m.startswith('@B/libstdc++/include'):
                for fn in sorted(os.listdir(d)):
                    if fn.endswith('.h') and os.path.isfile(os.path.join(d, fn)):
                        need_file(os.path.join(d, fn))
        elif a in ('-isystem', '-idirafter', '-include', '-iquote'):
            m = map_path(os.path.join(cwd, opts[i + 1]), a != '-include')
            if m:
                res += [a, m]
            i += 1
        else:
            res.append(a)
        i += 1
    for r in res:
        if re.search(r'\s', r) or r == '%':
            raise SystemExit('xiplibs_recipe: an option the recipe cannot hold: %r' % r)
    return res


# ---- the headers of build directories ---------------------------------------------------------

def tree(real, skip=()):
    """Every file of a build-time include directory (symbolic links to sources, generated files)"""
    for dp, dns, fns in os.walk(real):
        dns[:] = [d for d in dns if d not in skip]
        for fn in fns:
            if not fn.startswith('stamp-') and fn != 'Makefile':
                need_file(os.path.join(dp, fn))


def depfiles():
    """The files of build directories the compiles read (their dependency files): into @B"""
    for real in (NEWLIB_B, LIBGCC_B):
        for dp, dns, fns in os.walk(real):
            for fn in fns:
                if not fn.endswith(('.Po', '.dep')):
                    continue
                cwd = real if real == NEWLIB_B or os.path.basename(dp) != '.deps' else os.path.dirname(dp)
                text = open(os.path.join(dp, fn), errors='replace').read().replace('\\\n', ' ')
                for tok in text.split():
                    if tok.endswith(':') or tok.startswith('#'):
                        continue
                    p = os.path.normpath(os.path.join(cwd, tok))
                    real_a, _ = area_of(p)
                    if not real_a or p.startswith(GCC_B + '/include/') or p.endswith(('.o', '.dep')):
                        continue        # (GCC's own headers come with the compiler)
                    if os.path.islink(p) or (os.path.isfile(p) and not src_name(p)):
                        need_file(p)


# ---- the recipe -------------------------------------------------------------------------------

def best_sets(lists, min_use=3):
    """Option sequences that many lists begin with, chosen by what they save (length times uses)"""
    sets = []
    rest = [tuple(l) for l in lists if len(l) >= 2]
    while rest:
        count = Counter(l[:n] for l in rest for n in range(2, len(l) + 1))
        s, uses = max(count.items(), key=lambda kv: ((len(kv[0]) - 1) * kv[1], len(kv[0])))
        if uses < min_use:
            break
        sets.append(s)
        rest = [l for l in rest if l[:len(s)] != s]
    return sets


def library(lib, archive, records):
    members = run(['%s/bin/arm-none-eabi-ar' % CROSS, 't', archive]).split()
    objs = []
    for m in members:
        cand = records.get(m, [])
        if len(cand) != 1:
            # several objects of that name (libtool renames all but one lt<N>-<name> in the
            # archive): the one whose object file is the archive's member, byte for byte
            base = re.sub(r'^lt\d+-', '', m)
            data = subprocess.run(['%s/bin/arm-none-eabi-ar' % CROSS, 'p', archive, m],
                                  capture_output=True).stdout
            cand = [c for c in records.get(base, [])
                    if os.path.isfile(c[1][0]) and open(c[1][0], 'rb').read() == data]
        if len(cand) != 1:
            raise SystemExit('xiplibs_recipe: %s(%s): %d commands make it' % (lib, m, len(cand)))
        cwd, (obj, src, opts) = cand[0]
        objs.append((m, map_path(src, False), CPU + options(cwd, opts)))
    # an object's options: those its set begins with, its own (-DL_<function>, -frandom-seed...)
    # where the set has %, those the set ends with
    heads = best_sets([o for _, _, o in objs])
    split = []
    for m, s, o in objs:
        h = max((x for x in heads if tuple(o[:len(x)]) == x), key=len, default=())
        split.append((m, s, h, tuple(o[len(h):])))
    tails = [tuple(reversed(x)) for x in best_sets([tuple(reversed(r)) for _, _, _, r in split])]
    short = lib.replace('lib', '').replace('+', 'x')
    names = {}
    lines = []
    cur = None
    for m, s, h, r in split:
        t = max((x for x in tails if len(x) <= len(r) and r[len(r) - len(x):] == x), key=len, default=())
        mid = list(r[:len(r) - len(t)])
        words = ['cc', m]
        if s.startswith('@B'):
            words.append(s)
        else:
            d, f = s.rsplit('/', 1)
            if d != cur:
                lines.append('dir %s' % d)
                cur = d
            words.append(f)
        if h or t:
            if (h, t) not in names:
                names[(h, t)] = '%s%d' % (short, len(names) + 1)
            words.append('$' + names[(h, t)])
        lines.append(' '.join(words + mid))
    out = ['', 'lib %s' % lib]
    for (h, t), n in names.items():
        out.append('set %s %s' % (n, ' '.join(list(h) + ['%'] + list(t))))
    print('%-10s %4d objects, %d option sets' % (lib, len(members), len(names)))
    return out + lines


def newlib_version():
    v = dict(re.findall(r'NEWLIB_(MAJOR|MINOR|PATCHLEVEL)_VERSION\],\[(\d+)\]',
                        open(SRC + '/newlib-cygwin/newlib/acinclude.m4').read()))
    return '%s.%s.%s' % (v['MAJOR'], v['MINOR'], v['PATCHLEVEL'])


def main():
    make_copy()
    cmds = {'newlib': make_n(NEWLIB_B), 'libgcc': make_n(LIBGCC_B), 'libstdcxx': []}
    for d in STDCXX_DIRS:
        if os.path.isdir(STDCXX_B + '/' + d):
            cmds['libstdcxx'] += make_n(STDCXX_B + '/' + d)
    records = {}
    for key, lines in cmds.items():
        for cwd, line in lines:
            r = parse(cwd, line)
            if r:
                records.setdefault(key, {}).setdefault(os.path.basename(r[0]), []).append((cwd, r))
    # the include trees the compiles use as a whole, the files their dependency files name
    tree(STDCXX_B + '/include')
    tree(SYSINC, skip=('c++', 'newlib-nano'))
    tree(NEWLIB_B + '/targ-include')
    depfiles()
    lines = []
    for lib, (archive, key) in LIBS.items():
        lines += library(lib, archive, records[key])
    lines += ['', 'alias libstdc++_nano libstdc++', 'alias libsupc++_nano libsupc++']
    shutil.rmtree(COPY, ignore_errors=True)

    head = [
        '# The position-independent (XIP) runtime libraries of newlib %s and GCC %s: generated by'
        % (newlib_version(), GCCVER),
        '# tools/xiplibs_recipe.py from a build of them (stage A of toolchain/native/build.sh), built by',
        '# cmake/xiplibs.cmake. Paths: @N third_party/newlib, @G third_party/gcc, @X toolchain/xiplibs/gen,',
        '# @T the Arm GNU Toolchain (its root), @B the build directory (build/xiplibs).',
        '#   copy <@B file> <file>             before any compile (copy <dir>/ <dir>/ <name...>: those',
        '#                                     files of a directory)',
        '#   gen <@B file> <step> <source>...   made by tools/xiplibs.py <step> before any compile',
        '#   lib <name>                        lib/xip/<name>.a, of the "cc" lines that follow',
        "#   set <name> <option...>            options that $<name> stands for, the object's own at %",
        '#   dir <directory>                   where the sources of the "cc" lines that follow are',
        '#   cc <object> <source> [$set] [own option...]   one object (CPU options included)',
        '#   alias <name> <lib>                lib/xip/<name>.a is a copy of <lib>.a',
        'version %s' % GCCVER,
        '',
    ]
    # copies: the files of one directory that come from one directory under their names on a line
    groups = {}
    copies = []
    for d, s in sorted(COPIES.items()):
        if os.path.basename(d) == os.path.basename(s):
            groups.setdefault((os.path.dirname(d), os.path.dirname(s)), []).append(os.path.basename(d))
        else:
            copies.append('copy %s %s' % (d, s))
    for (dd, sd), names in sorted(groups.items()):
        for i in range(0, len(names), 12):
            copies.append('copy %s/ %s/ %s' % (dd, sd, ' '.join(names[i:i + 12])))
    gen = os.path.join(OUT, 'gen')
    shutil.rmtree(gen, ignore_errors=True)
    for x, data in GEN.items():
        p = os.path.join(gen, x[3:])
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as f:
            f.write(data)
    with open(os.path.join(OUT, 'recipe.txt'), 'w', newline='\n') as f:
        f.write('\n'.join(head + copies + GENSTEPS + lines) + '\n')
    print('%d copies (%d files kept in gen/), %d generation steps' % (len(COPIES), len(GEN), len(GENSTEPS)))


if __name__ == '__main__':
    main()
