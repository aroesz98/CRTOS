#!/usr/bin/env python3
"""boardsrc.py - the programs' sources, to be built on the board with its own GCC

    python tools/boardsrc.py [--check] [BUILD_DIR]        (or: crtos src)

For every program of the tree (crtos_app) it writes build/src/<apps|commands|services>/<dir>/:
the program's sources and a Makefile for make on the board (make, make install, make clean),
plus build/src/Makefile (all of them) and README.txt. "crtos src" sends build/src to
/sd/crtos/src.

The flags are those of the tree's own build, read from what CMake wrote: every file's compile
command (compile_commands.json), the link (build.ninja: libraries, -mxip, where the program
goes) and the program header (<name>_appinfo.c: stack, heap). On the board:
  - no -g (the board has no debugger; it only costs time and memory), and no CPU options or
    specs (the board's GCC has them built in); C++ at -O2 where the tree has -O3 (memory);
  - the tree's include directories of libcrtos, the kernel and libgfx are the headers in
    /sd/crtos/usr/include (a program may use only what the toolchain gives programs);
    TFTLIB's are /sd/crtos/usr/include/tftlib;
  - the stack and heap go into the header with crtos-app set, and the debugging information
    of the libraries goes (strip --strip-debug), as in the tree;
  - FAST (hottest code in ITCM, tools/appfast.py) needs Python: left out;
  - sources from outside the program's directory (make's third_party/pdpmake, crtos-app's
    toolchain/crtos-app.c) are copied into a subdirectory named after their directory.
NetSurf is left out (a dozen libraries from third_party, far beyond the board's memory).

--check compiles every source on this computer (arm-crtos-gcc, -fsyntax-only) the way the
board's make would: the same flags, the toolchain's headers, C++14 unless the flags say
otherwise (the board's default).
"""
import glob
import json
import os
import re
import shlex
import shutil
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
BOARD_INC = '/sd/crtos/usr/include'     # the board's headers (the toolchain's include/)
BOARD_SRC = '/sd/crtos/src'
SKIP = ('apps/netsurf',)
# programs too big for the compiler's memory on the board: none since it has emulated memory
# (libcrtosheap, kernel vmem.cpp) and SNES uses no C++ library (30.09.2026)
TOO_BIG = ()
# include directories of the tree that are the board's headers
SYSTEM_INCLUDES = ('system/lib/libcrtos/include', 'kernel/include', 'system/lib/libgfx/include')
MAPPED_INCLUDES = {'system/lib/tftlib': '$(CRTOS_INC)/tftlib'}
KEEP_FLAG = re.compile(r'^(-D.+|-U.+|-O.*|-W.*|-w|-f.+|-std=.+|-mxip)$')
DROP_DEFINES = ('-DCRTOS_USER=1',)      # (the specs give it)
EXTRA_FILES = re.compile(r'^(README|LICEN[CS]E|COPYING|AUTHORS)|\.md$', re.I)
HEADER = re.compile(r'\.(h|hh|hpp|hxx|inc|inl)$', re.I)


class Fail(Exception):
    pass


def norm(path):
    return os.path.normpath(path).replace('\\', '/')


def rel_root(path):
    """@path relative to the tree, or None outside it"""
    r = os.path.relpath(norm(path), norm(ROOT)).replace('\\', '/')
    return None if r.startswith('../') else r


class Program:
    def __init__(self, name, project):
        self.name = name
        self.project = project          # its directory in the tree ("apps/paint")
        self.sources = []               # [(tree path, [flags], [include dirs as in the tree])]
        self.stack = self.heap = None
        self.libs = []
        self.xip = False
        self.dest = None                # on the board
        self.fast = False


def read_programs(bdir):
    with open(os.path.join(bdir, 'compile_commands.json')) as f:
        entries = json.load(f)
    progs = {}
    for e in entries:
        out = os.path.relpath(norm(e['output']), norm(bdir)).replace('\\', '/')
        m = re.match(r'^(.+?)/CMakeFiles/app_(.+?)_objs\.dir/', out)
        if not m:
            continue
        project, name = m.group(1), m.group(2)
        if project.startswith(SKIP):
            continue
        p = progs.setdefault(name, Program(name, project))
        src = norm(e['file'])
        if src.endswith('_appinfo.c'):
            with open(src) as f:
                m = re.search(r'CRTOS_APP_ABI,\s*(\d+)u,\s*(\d+)u', f.read())
            if not m:
                raise Fail('%s: no stack and heap' % src)
            p.stack, p.heap = int(m.group(1)), int(m.group(2))
            continue
        args = shlex.split(e['command'].replace('\\', '/'), posix=True)[1:]
        flags, incs = [], []
        skip_next = False
        for a in args:
            if skip_next:
                skip_next = False
            elif a in ('-o', '-c', '-MT', '-MF'):
                skip_next = True
            elif a.startswith('-I'):
                incs.append(norm(a[2:]))
            elif KEEP_FLAG.match(a) and a not in DROP_DEFINES:
                flags.append(a)
        rel = rel_root(src)
        if rel is None:
            raise Fail('%s: a source outside the tree' % src)
        p.sources.append((rel, flags, incs))
    return progs


def read_links(bdir, progs):
    """build.ninja: the link of each program (libraries, -mxip, FAST) and where it goes. (A
    command too long for Windows' cmd.exe is in a script of its own, CMakeFiles/<x>.app-*.bat.)"""
    texts = []
    for path in [os.path.join(bdir, 'build.ninja')] + glob.glob(os.path.join(bdir, '**', 'CMakeFiles', '*.app-*.bat'),
                                                               recursive=True):
        with open(path, encoding='utf-8', errors='replace') as f:
            texts.append(f.read().replace('\\', '/').replace('\r', ''))
    ninja = '\n'.join(texts)
    sd = norm(os.path.join(bdir, 'sdcard', 'crtos')) + '/'
    fl = norm(os.path.join(bdir, 'flash0')) + '/'
    for p in progs.values():
        name = re.escape(p.name)
        # the link: "gcc ... -o <dir>/<name>.debug.app <objects> -Wl,--start-group <libs> ..."
        m = re.search(r'[^\n]*gcc[^\n]* -o [^ \n]*/%s\.debug\.app [^\n]*' % name, ninja)
        if not m:
            raise Fail('%s: no link command in build.ninja' % p.name)
        cmd = m.group(0)
        link = cmd[:cmd.index('.debug.app')]
        p.xip = ' -mxip ' in link
        group = re.search(r'-Wl,--start-group (.*?) -Wl,--end-group', cmd[cmd.index('.debug.app'):])
        for lib in (shlex.split(group.group(1)) if group else []):
            base = os.path.basename(lib)
            mm = re.match(r'^lib(.+)\.a$', base)
            if not mm:
                raise Fail('%s: a library the board cannot link: %s' % (p.name, lib))
            if mm.group(1) != 'crtos':      # (the specs link it)
                p.libs.append('-l' + mm.group(1))
        p.fast = re.search(r'appfast\.py [^ \n]*/%s\.debug\.app' % name, ninja) is not None
        out = re.search(r'--strip-debug -o ("?)([^ "\n]*/%s\.app)\1 [^ \n]*/%s\.debug\.app' % (name, name), ninja)
        if not out:
            raise Fail('%s: where does it go?' % p.name)
        target = norm(out.group(2))
        if target.startswith(sd):
            p.dest = '/sd/crtos/' + os.path.dirname(target[len(sd):])
        elif target.startswith(fl):
            p.dest = '/flash0/' + os.path.dirname(target[len(fl):])
        else:
            raise Fail('%s: %s is neither on the card nor on /flash0' % (p.name, target))


class Project:
    """One directory of the tree with its programs, laid out for the board"""

    def __init__(self, path, progs):
        self.path = path
        self.progs = sorted(progs, key=lambda p: p.name)
        self.external = {}              # tree directory -> its subdirectory here
        self.shadow = {}                # system include directory -> subdirectory (see shadows())

    def place(self, tree_path):
        """where a file of the tree lies in the project's directory on the board"""
        if tree_path == self.path or tree_path.startswith(self.path + '/'):
            return os.path.relpath(tree_path, self.path).replace('\\', '/')
        d = os.path.dirname(tree_path)
        sub = self.external.setdefault(d, os.path.basename(d))
        return sub + '/' + os.path.basename(tree_path)

    def include(self, d):
        r = rel_root(d)
        if r is None:
            raise Fail('%s: include directory %s outside the tree' % (self.path, d))
        if r in SYSTEM_INCLUDES:
            return '-I' + self.shadow[r] if r in self.shadow else None
        if r in MAPPED_INCLUDES:
            return '-I' + MAPPED_INCLUDES[r]
        if r == self.path or r.startswith(self.path + '/'):
            return '-I' + (os.path.relpath(r, self.path).replace('\\', '/') or '.')
        if r in self.external:
            return '-I' + self.external[r]
        raise Fail('%s: include directory %s is not one the board has' % (self.path, r))


    def shadows(self, tc_include):
        """On the board the tree's system include directories (libcrtos, the kernel, libgfx) are
        searched after every -I directory, in the tree in their place among them. A header of
        such a directory with a namesake in a directory searched after it (NES and SNES have a
        gfx.h of their own besides libgfx's) is copied - the board's version - into a
        subdirectory given -I in that place. [(the board's header, where it goes here)]"""
        copies = []
        for p in self.progs:
            for _, _, incs in p.sources:
                rels = [rel_root(d) for d in incs]
                for i, r in enumerate(rels):
                    if r not in SYSTEM_INCLUDES:
                        continue
                    own = {n for n in os.listdir(os.path.join(ROOT, r)) if HEADER.search(n)}
                    later = set()
                    for r2 in rels[i + 1:]:
                        if r2 and r2 not in SYSTEM_INCLUDES and os.path.isdir(os.path.join(ROOT, r2)):
                            later |= {n for n in os.listdir(os.path.join(ROOT, r2)) if HEADER.search(n)}
                    for n in sorted(own & later):
                        board = os.path.join(tc_include, n)
                        if not os.path.isfile(board):
                            raise Fail('%s: %s/%s is not a header the board has' % (self.path, r, n))
                        sub = self.shadow.setdefault(r, r.split('/')[2] if r.startswith('system/lib/') else 'kernel')
                        if (board, sub + '/' + n) not in copies:
                            copies.append((board, sub + '/' + n))
        return copies


def board_flags(flags, cxx):
    """C++ at -O3 goes over cc1plus' memory on the board (unrolling and vectorising a big
    function: a peak of more than 17 MB) - -O2 there"""
    if not cxx:
        return flags
    return ['-O2' if f == '-O3' else f for f in flags]


def obj_name(proj, prog, src_place):
    stem = os.path.splitext(src_place)[0]
    return stem + ('-' + prog.name if len(proj.progs) > 1 else '') + '.o'


def makefile(proj, files):
    lines = ['# Makefile - %s on the board: "make" builds, "make install" puts in place, "make clean"'
             % ', '.join(p.name + '.app' for p in proj.progs),
             '# (generated by tools/boardsrc.py from %s/CMakeLists.txt: the flags of the tree\'s build'
             % proj.path, '# without -g, C++ at -O2 where the tree has -O3 - the compiler\'s memory here)', '',
             'CC = gcc', 'CXX = g++', 'CRTOS_INC = ' + BOARD_INC]
    headers = sorted(f for f in files if HEADER.search(f))
    if headers:
        lines.append('HDRS = ' + ' '.join(headers))
    lines += ['', '.PHONY: all install clean', '', 'all: ' + ' '.join(p.name + '.app' for p in proj.progs), '']
    rules, objs_all = [], []
    for p in proj.progs:
        objs = []
        for tree_path, flags, incs in p.sources:
            src = proj.place(tree_path)
            obj = obj_name(proj, p, src)
            objs.append(obj)
            cxx = re.search(r'\.(cpp|cc|cxx)$', src) is not None
            cc = '$(CXX)' if cxx else '$(CC)'
            iflags = [i for i in (proj.include(d) for d in incs) if i]
            cmd = ' '.join([cc] + board_flags(flags, cxx) + iflags + ['-c', src, '-o', obj])
            rules += ['%s: %s%s' % (obj, src, ' $(HDRS)' if headers else ''), '\t' + cmd, '']
        objs_all += objs
        var = re.sub(r'\W', '_', p.name).upper() + '_OBJS'
        lines.append('%s = %s' % (var, ' '.join(objs)))
        link = ['$(CC)'] + (['-mxip'] if p.xip else []) + ['-o', p.name + '.app', '$(%s)' % var]
        if p.libs:
            link += ['-Wl,--start-group'] + p.libs + ['-Wl,--end-group']
        if p.fast:
            lines.append('# (the tree also moves its hottest code to on-chip memory: FAST, not here)')
        lines += ['%s.app: $(%s)' % (p.name, var), '\t' + ' '.join(link),
                  '\tstrip --strip-debug %s.app' % p.name,
                  '\tcrtos-app set %s.app --stack %d --heap %d' % (p.name, p.stack, p.heap), '']
    lines += rules
    lines += ['install: ' + ' '.join(p.name + '.app' for p in proj.progs)]
    lines += ['\tcp %s.app %s/%s.app' % (p.name, p.dest, p.name) for p in proj.progs]
    lines += ['', 'clean:', '\trm -f ' + ' '.join([p.name + '.app' for p in proj.progs] + objs_all), '']
    return '\n'.join(lines)


def write_project(proj, out, tc_include):
    """copies the project's files, returns their names (relative to its directory)"""
    dest = os.path.join(out, proj.path)
    files = []
    top = os.path.join(ROOT, proj.path)
    for dirpath, dirnames, names in os.walk(top):
        dirnames[:] = sorted(d for d in dirnames if not d.startswith('.'))
        for n in sorted(names):
            if n == 'CMakeLists.txt' or n.startswith('.'):
                continue
            rel = os.path.relpath(os.path.join(dirpath, n), top).replace('\\', '/')
            files.append(rel)
    # files from other directories of the tree: the sources used, the headers and notes beside
    used = {tp for p in proj.progs for tp, _, _ in p.sources}
    for tp in used:
        proj.place(tp)
    for p in proj.progs:                    # (include directories may add more)
        for _, _, incs in p.sources:
            for d in incs:
                r = rel_root(d)
                if r and r not in SYSTEM_INCLUDES and r not in MAPPED_INCLUDES and not (
                        r == proj.path or r.startswith(proj.path + '/')):
                    proj.external.setdefault(r, os.path.basename(r))
    external = []
    for d, sub in sorted(proj.external.items()):
        for n in sorted(os.listdir(os.path.join(ROOT, d))):
            tp = d + '/' + n
            if os.path.isfile(os.path.join(ROOT, tp)) and (tp in used or HEADER.search(n) or EXTRA_FILES.search(n)):
                external.append((os.path.join(ROOT, tp), sub + '/' + n))
    external += proj.shadows(tc_include)
    os.makedirs(dest, exist_ok=True)
    for rel in files:
        os.makedirs(os.path.dirname(os.path.join(dest, rel)), exist_ok=True)
        shutil.copyfile(os.path.join(top, rel), os.path.join(dest, rel))
    for src, rel in external:
        os.makedirs(os.path.dirname(os.path.join(dest, rel)), exist_ok=True)
        shutil.copyfile(src, os.path.join(dest, rel))
    all_files = files + [rel for _, rel in external]
    with open(os.path.join(dest, 'Makefile'), 'w', newline='\n') as f:
        f.write(makefile(proj, all_files))
    return all_files


README = '''Programy CRTOS do zbudowania na płytce
=====================================

Każdy katalog to jeden program (albo kilka z jednego katalogu) ze źródłami i plikiem
Makefile. Makefile ma te same opcje kompilacji co budowanie drzewa na komputerze
(crtos build), bez informacji dla debugera.

    cd /sd/crtos/src/apps/paint
    make                  zbudowanie paint.app w tym katalogu
    ./paint.app           uruchomienie
    make install          zastąpienie programu systemu (/sd/crtos/apps/paint.app)
    make clean            usunięcie wyników

W /sd/crtos/src: make (wszystko), make system (system bazowy), make apps (dodatki), make tests,
make examples, make clean.
make -k buduje dalej mimo błędów.

Uwagi:
- make install zastępuje program systemu. Usługę (system/services/) uruchomi od nowa dopiero
  restart płytki albo init po jej zakończeniu.
- Program z FAST (snes) buduje się tu bez przeniesienia gorącego kodu do ITCM, więc
  działa wolniej niż zbudowany na komputerze.
- C++ kompiluje się tu z -O2 tam, gdzie drzewo ma -O3 (voxel, tftdemo, snes): -O3 w dużych
  funkcjach potrzebuje więcej pamięci, niż ma kompilator na płytce.
- Emulator NES buduje się tu w całości (ok. 8 min; nes_fast.c z -O3 zajmuje ok. 18 MB).
- Emulator SNES też (ok. 70 min): 7 z 29 plików potrzebuje 19-25 MB i część sterty
  kompilatora trafia do pamięci emulowanej (plik wymiany na karcie), co jest wolne.
- Kompilator bierze na czas kompilacji prawie całą wolną pamięć (ok. 21 MB przy działającym
  pulpicie, zostawia 1 MB). W tym czasie nowe okno na pulpicie może się nie otworzyć. Im
  więcej programów działa, tym mniej ma kompilator. Gdy pamięci zabraknie, sterta rośnie
  w pamięci emulowanej (/sd/crtos/var/swap, 64 MB): kompilacja trwa wtedy kilka razy
  dłużej, ale się kończy. export CRTOS_HEAP_STATS=1 pokazuje zużycie pamięci.
- Pliki tymczasowe kompilatora (m.in. wynik preprocesora, osobnego przebiegu) trafiają do
  /sd/crtos/tmp.
- NetSurf się tu nie buduje (kilkanaście bibliotek zewnętrznych).
- Pliki wygenerował tools/boardsrc.py (crtos src). Zmiany wprowadzone tutaj nie wracają
  same do drzewa źródeł na komputerze.
'''


def top_makefile(projects):
    groups = {}
    for pr in projects:
        if pr.path not in TOO_BIG:
            groups.setdefault(pr.path.split('/')[0], []).append(pr.path)
    lines = ['# Makefile - every program of /sd/crtos/src (tools/boardsrc.py): make [system|apps|tests|examples]',
             '# ["make install", "make clean" in each directory; make -k goes on after an error]']
    if TOO_BIG:
        lines.append('# Not in the lists: %s - too big for the compiler\'s memory here (README.txt)' % ', '.join(TOO_BIG))
    lines += ['',
             '.PHONY: all clean ' + ' '.join(sorted(groups)), '',   # (apps/ etc. are directories)
             'all: ' + ' '.join(sorted(groups)), '']
    for g, dirs in sorted(groups.items()):
        lines += [g + ':'] + ['\tcd %s && make' % d for d in dirs] + ['']
    lines += ['clean:'] + ['\tcd %s && make clean' % pr.path for pr in projects] + ['']
    return '\n'.join(lines)


def generate(bdir):
    progs = read_programs(bdir)
    read_links(bdir, progs)
    by_dir = {}
    for p in progs.values():
        if p.stack is None:
            raise Fail('%s: no program header (%s_appinfo.c)' % (p.name, p.name))
        by_dir.setdefault(p.project, []).append(p)
    out = os.path.join(bdir, 'src')
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    projects = []
    for d in sorted(by_dir):
        proj = Project(d, by_dir[d])
        write_project(proj, out, os.path.join(bdir, "toolchain", "arm-crtos", "include"))
        projects.append(proj)
    with open(os.path.join(out, 'Makefile'), 'w', newline='\n') as f:
        f.write(top_makefile(projects))
    with open(os.path.join(out, 'README.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(README)
    return out, projects


def check(bdir, out, projects):
    """-fsyntax-only of every source as the board's make would compile it"""
    tc = os.path.join(bdir, 'toolchain', 'arm-crtos')
    exe = '.exe' if os.name == 'nt' else ''
    bad = 0
    for proj in projects:
        cwd = os.path.join(out, proj.path)
        for p in proj.progs:
            for tp, flags, incs in p.sources:
                src = proj.place(tp)
                cxx = bool(re.search(r'\.(cpp|cc|cxx)$', src))
                cc = os.path.join(tc, 'bin', ('arm-crtos-g++' if cxx else 'arm-crtos-gcc') + exe)
                iflags = [i.replace('$(CRTOS_INC)', os.path.join(tc, 'include').replace('\\', '/'))
                          for i in (proj.include(d) for d in incs) if i]
                std = [] if not cxx or any(f.startswith('-std=') for f in flags) else ['-std=gnu++14']
                r = subprocess.run([cc] + board_flags(flags, cxx) + std + iflags + ['-fsyntax-only', src], cwd=cwd,
                                   capture_output=True, text=True)
                if r.returncode:
                    bad += 1
                    print('%s/%s:\n%s' % (proj.path, src, (r.stdout + r.stderr).strip()[:1500]))
    return bad


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    bdir = os.path.abspath(args[0] if args else os.path.join(ROOT, 'build'))
    try:
        out, projects = generate(bdir)
        n = sum(len(pr.progs) for pr in projects)
        print('boardsrc: %d program(s) in %d director(ies): %s' % (n, len(projects), out))
        if '--check' in sys.argv:
            bad = check(bdir, out, projects)
            print('boardsrc: check: %s' % ('%d file(s) do not compile' % bad if bad else 'every file compiles'))
            return 1 if bad else 0
    except Fail as e:
        sys.exit('boardsrc: %s' % e)
    return 0


if __name__ == '__main__':
    sys.exit(main())
