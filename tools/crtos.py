#!/usr/bin/env python3
"""crtos - build CRTOS, put it on the board, run programs.

    crtos doctor                    check the tools, the debug probe and the board
    crtos setup [--netsurf]         install what is missing (--netsurf: the web browser too)
    crtos build [kernel|NAME...]    build everything into build/ (or the kernel, or a program;
                                    --base: only the base system, without apps/, tests/, examples/;
                                    --rtos [DIR]: the RTOS alone with the application in DIR)
    crtos flash [--rtos]            write the kernel (or the RTOS build) into the board's flash
    crtos sdcard DRIVE              copy the system files to an SD card in this computer
    crtos deploy [--full]           send the changed system files to the board (network/probe)
    crtos run NAME [ARGS...]        run a program on the board and show its output
    crtos new NAME [--console|--service]   a new program from a template (default: a window)
    crtos kmon [COMMAND...]         the kernel monitor (without a command: interactive)
    crtos serial                    terminal on the board's serial console (the shell)
    crtos log [--reset]             show what the board prints on the serial console
    crtos shot [FILE.png]           screenshot of the display
    crtos reboot                    restart the board
    crtos scp [-r] SRC... DST       copy files between this computer and the board (board:/path)
    crtos desktop [--fullscreen]    the board's screen in a window here (remote desktop, VNC)
    crtos netbench [--time S]       the network's throughput both ways (TCP, UDP)
    crtos wallpaper PICTURE         a picture of this computer as the board's wallpaper
    crtos find                      boards on the local network
    crtos crash                     the last crash report, with its source lines
    crtos sdk [DIR]                 SDK for programs built outside this tree (build/sdk)
    crtos src                       the programs' sources to /sd/crtos/src, to build on the board
    crtos toolchain [native|install]   arm-crtos-gcc & co. in build/toolchain/arm-crtos (native:
                                    GCC for the board itself, built in WSL/Linux; install: onto
                                    the board, over the network)
    crtos clean [--all]             remove the build results (--all: the whole build/)

Also: kbuild (the kernel with MCUXpresso IDE, into Release/), put LOCAL REMOTE (one file),
swd kmon|deploy|put|shot (through the probe), net setup|deploy|put|reboot|find (network).

Settings (environment variables, all optional): CRTOS_PROBE debug probe id, CRTOS_PORT
serial port, CRTOS_HOST the board's IP address, CRTOS_GCC_BIN the compiler's bin directory,
CRTOS_BUILD the build directory.
"""
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import zlib

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
IN_TREE = os.path.isdir(os.path.join(ROOT, 'kernel'))      # else: an exported SDK
sys.path.insert(0, TOOLS)

WINDOWS = os.name == 'nt'
EXE = '.exe' if WINDOWS else ''
BAUD = int(os.environ.get('CRTOS_BAUD', '1000000'))
TARGET = 'mimxrt1050_hyperflash'
FLASH_ADDR = 0x60000000
TOKEN_REMOTE = '/sd/crtos/etc/deploy.token'
VNC_REMOTE = '/sd/crtos/etc/vnc.passwd'
# where programs live: source directory -> directory on the card (the base system, the optional
# programs, tests, examples)
PROGRAM_DIRS = (('system/apps', 'apps'), ('system/commands', 'bin'), ('system/services', 'sbin'),
                ('apps', 'apps'), ('tests', 'bin'), ('examples', 'apps'), ('examples', 'bin'))
# "crtos new": every new program goes to apps/ (the system's own are not made there), to its card
# directory by kind (DEST in its CMakeLists.txt)
KINDS = {'gui': ('apps', 'apps'), 'console': ('apps', 'bin'), 'service': ('apps', 'sbin')}


class Fail(Exception):
    """An error for the user: printed without a traceback"""


def say(text=''):
    print(text, flush=True)


def rel(path):
    try:
        r = os.path.relpath(path)
        return path if r.startswith('..') else r
    except ValueError:
        return path


def natural(text):
    return [int(t) if t.isdigit() else t.lower() for t in re.split(r'(\d+)', text)]


# ---- where things are ------------------------------------------------------------------------

def build_dir():
    d = os.environ.get('CRTOS_BUILD')
    if d:
        return os.path.abspath(d)
    return os.path.join(ROOT, 'build') if IN_TREE else os.path.abspath('build')


def source_dir():
    return ROOT if IN_TREE else os.getcwd()


def sdcard_dir():
    """build/sdcard: its crtos/ directory is /crtos on the card (/sd/crtos on the board)"""
    return os.path.join(build_dir(), 'sdcard')


def flash0_dir():
    """build/flash0: /flash0 on the board (the flash file system; programs there run in place)"""
    return os.path.join(build_dir(), 'flash0')


# the RTOS build (the kernel's core with one application, "crtos build --rtos") is configured in
# a directory of its own, so the system's build stays as it is
RTOS_SUBDIR = 'rtos'
RTOS_DEFAULT_APP = os.path.join('examples', 'rtos', 'blinky')


def kernel_file(ext, rtos=False):
    return os.path.join(build_dir(), *([RTOS_SUBDIR] if rtos else []), 'kernel', 'crtos' + ext)


def kernel_axf(rtos=False):
    """The kernel's .axf when there is one (the SWD console is found without it too)"""
    p = kernel_file('.axf', rtos)
    return p if os.path.exists(p) else None


def templates_dir():
    return os.path.join(ROOT, 'sdk', 'templates') if IN_TREE else os.path.join(ROOT, 'templates')


# ---- host tools ------------------------------------------------------------------------------

def _script_dirs():
    """Where pip puts programs (cmake, ninja, pyocd), on the PATH or not"""
    import sysconfig
    dirs = [sysconfig.get_path('scripts')]
    for scheme in ('%s_user' % os.name, 'posix_user'):
        try:
            dirs.append(sysconfig.get_path('scripts', scheme))
            break
        except KeyError:
            pass
    return [d for d in dirs if d]


def _winget_dirs(package):
    base = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Microsoft', 'WinGet')
    return [os.path.join(base, 'Links')] + glob.glob(os.path.join(base, 'Packages', package + '_*'))


def find_program(name, *places):
    """@name on the PATH, else in @places (directories or glob patterns, newest first)"""
    p = shutil.which(name)
    if p:
        return p
    for place in places:
        for d in sorted(glob.glob(os.path.expanduser(place)), key=natural, reverse=True):
            c = os.path.join(d, name + EXE)
            if os.path.isfile(c):
                return c
    return None


def cmake_path():
    return find_program('cmake', r'C:\Program Files\CMake\bin', *(_winget_dirs('Kitware.CMake') + _script_dirs()))


def ninja_path():
    return find_program('ninja', *(_winget_dirs('Ninja-build.Ninja') + _script_dirs()))


GCC_PLACES = (
    r'C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\*\bin',
    r'C:\Program Files\Arm GNU Toolchain arm-none-eabi\*\bin',
    r'C:\Program Files (x86)\GNU Arm Embedded Toolchain\*\bin',
    r'C:\nxp\MCUXpressoIDE_*\ide\tools\bin',
    '~/.local/arm-gnu-toolchain*/bin',
    '/opt/arm-gnu-toolchain*/bin',
    '/opt/gcc-arm-none-eabi*/bin',
    '/Applications/ArmGNUToolchain/*/arm-none-eabi/bin',
    '/usr/local/bin',
    '/usr/bin',
)


def gcc_bin():
    """Directory of arm-none-eabi-gcc, searched like cmake/toolchain-arm-none-eabi.cmake does"""
    d = os.environ.get('CRTOS_GCC_BIN')
    if d:
        return d
    p = find_program('arm-none-eabi-gcc', *GCC_PLACES)
    return os.path.dirname(p) if p else None


def mcux_ide():
    p = os.environ.get('CRTOS_MCUX')
    if p:
        return p
    found = sorted(glob.glob(r'C:\nxp\MCUXpressoIDE_*\ide\mcuxpressoidec.exe'), key=natural, reverse=True)
    return found[0] if found else None


def tool_version(cmd, pattern=r'(\d+\.\d+(\.\d+)?)'):
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=20).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    m = re.search(pattern, out)
    return m.group(1) if m else None


def need_module(name, package=None):
    try:
        return __import__(name)
    except ImportError:
        raise Fail('the Python package %s is missing: run "crtos setup"' % (package or name))


def swdcon_module():
    need_module('pyocd')
    import swdcon
    return swdcon


def netdeploy_module():
    import netdeploy
    return netdeploy


# ---- building --------------------------------------------------------------------------------

class BuildLock:
    """One crtos build at a time in a build directory: two Ninja runs there at once trip over
    each other's files ("ar: could not create temporary file ... Permission denied"). The
    lock goes away with the process, also when it is killed."""

    def __init__(self, bdir):
        self.path = os.path.join(bdir, '.crtos-build.lock')
        self.f = None

    def _try(self):
        try:
            if WINDOWS:
                import msvcrt
                self.f.seek(0)
                msvcrt.locking(self.f.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except OSError:
            return False

    def __enter__(self):
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        self.f = open(self.path, 'a+')
        if not self._try():
            say('build: another build is running in %s - waiting for it to finish' % rel(os.path.dirname(self.path)))
            while not self._try():
                time.sleep(0.5)
        return self

    def __exit__(self, *exc):
        try:
            if WINDOWS:
                import msvcrt
                self.f.seek(0)
                msvcrt.locking(self.f.fileno(), msvcrt.LK_UNLCK, 1)
        except OSError:
            pass
        self.f.close()


def run_cmake(args):
    exe = cmake_path()
    if not exe:
        raise Fail('CMake not found: run "crtos setup"')
    return subprocess.run([exe] + args).returncode


# the optional parts of the tree ("crtos build --base" leaves them out)
OPTIONAL_PARTS = ('CRTOS_BUILD_APPS', 'CRTOS_BUILD_TESTS', 'CRTOS_BUILD_EXAMPLES')


def cache_value(bdir, name):
    """A variable of the build directory's CMakeCache.txt (None: not there)"""
    try:
        with open(os.path.join(bdir, 'CMakeCache.txt'), encoding='utf-8', errors='replace') as f:
            for line in f:
                key, _, value = line.rstrip('\n').partition('=')
                if key.split(':')[0] == name:
                    return value
    except OSError:
        pass
    return None


def configure(bdir, fresh=False, options=None):
    """@options: {CMake variable: value} the configuration must have - configured again when one
    differs"""
    options = options or {}
    if not fresh and os.path.exists(os.path.join(bdir, 'build.ninja')) and \
            all(cache_value(bdir, k) == v for k, v in options.items()):
        return          # configured (CMake itself configures again when a CMakeLists.txt changes)
    ninja = ninja_path()
    if not ninja:
        raise Fail('Ninja not found: run "crtos setup"')
    gcc = gcc_bin()
    if not gcc:
        raise Fail('the Arm GNU Toolchain (arm-none-eabi-gcc) was not found: run "crtos setup"')
    args = ['-S', source_dir(), '-B', bdir, '-G', 'Ninja', '-DCMAKE_MAKE_PROGRAM=' + ninja,
            '-DCRTOS_GCC_BIN=' + gcc]
    if not IN_TREE:
        if not os.path.exists(os.path.join(source_dir(), 'CMakeLists.txt')):
            raise Fail('no CMakeLists.txt here: run "crtos build" in the directory of your program '
                       '("crtos new NAME" makes one)')
        args.append('-DCRTOS_SDK=' + ROOT)
    args += ['-D%s=%s' % kv for kv in sorted(options.items())]
    if fresh:
        args.append('--fresh')
    if run_cmake(args):
        raise Fail('configuration failed (see above)')


def target_name(name):
    """What the user calls a thing -> the build target"""
    if name in ('kernel', 'all', 'clean', 'sdk'):
        return name
    if IN_TREE:
        for src, _ in PROGRAM_DIRS:
            if os.path.isdir(os.path.join(ROOT, src, name)):
                return 'app_' + name
    return name if IN_TREE else 'app_' + name


def cmd_build(targets, jobs=None, verbose=False, fresh=False, base=False, rtos=None):
    """@base: the base system only (kernel, drivers, system/), without apps/, tests/, examples/;
    @rtos: the directory of an RTOS application - the kernel's core with it, in build/rtos"""
    bdir = build_dir()
    options = {k: 'OFF' if base else 'ON' for k in OPTIONAL_PARTS} if IN_TREE else {}
    if rtos is not None:
        if not IN_TREE:
            raise Fail('the RTOS build is made in the CRTOS source tree')
        app = os.path.abspath(rtos)
        if not os.path.exists(os.path.join(app, 'CMakeLists.txt')):
            raise Fail('%s: no CMakeLists.txt with crtos_rtos_app(SOURCES ...) there' % rtos)
        bdir = os.path.join(bdir, RTOS_SUBDIR)
        options = {'CRTOS_PROFILE': 'rtos', 'CRTOS_RTOS_APP': app.replace('\\', '/')}
        targets = [t for t in targets if t != 'kernel']
        if targets:
            raise Fail('the RTOS build has the kernel only (with its application)')
    args = ['--build', bdir]
    if targets:
        args += ['--target'] + [target_name(t) for t in targets]
    if jobs:
        args += ['-j', str(jobs)]
    if verbose:
        args.append('-v')
    t0 = time.time()
    with BuildLock(bdir):
        configure(bdir, fresh, options)
        if run_cmake(args):
            raise Fail('the build failed (the first error is above)')
    say('build: done in %.1f s' % (time.time() - t0))
    if rtos is not None:
        binf = kernel_file('.bin', True)
        say('  RTOS:     %s (%d KB) with %s' % (rel(binf), os.path.getsize(binf) // 1024, rel(os.path.abspath(rtos))))
        say('next: "crtos flash --rtos" writes it (the system\'s kernel is replaced until "crtos flash")')
        return
    binf = kernel_file('.bin')
    if IN_TREE and os.path.exists(binf):
        say('  kernel:   %s (%d KB)' % (rel(binf), os.path.getsize(binf) // 1024))
    files = sdcard_files()
    if files:
        size = sum(os.path.getsize(f) for f, _ in files)
        say('  SD card:  %s (%d file(s), %.1f MB)' % (rel(os.path.join(sdcard_dir(), 'crtos')), len(files),
                                                     size / 1048576.0))
    files = flash0_files()
    if files:
        size = sum(os.path.getsize(f) for f, _ in files)
        say('  /flash0:  %s (%d file(s), %.1f MB)' % (rel(flash0_dir()), len(files), size / 1048576.0))
    if not IN_TREE:
        say('next: "crtos deploy" sends it to the board, "crtos run NAME" runs it')
    elif not targets:
        say('next: "crtos flash" writes the kernel, "crtos deploy" sends the files to the board')


def cmd_clean(everything=False):
    bdir = build_dir()
    if everything:
        if os.path.isdir(bdir):
            with BuildLock(bdir):       # not in the middle of a build
                pass
            shutil.rmtree(bdir)
        say('clean: %s removed' % rel(bdir))
        return
    if not os.path.exists(os.path.join(bdir, 'CMakeCache.txt')):
        say('clean: nothing built yet')
        return
    with BuildLock(bdir):
        if run_cmake(['--build', bdir, '--target', 'clean']):
            raise Fail('clean failed')
    say('clean: done ("crtos clean --all" also removes the configuration)')


# ---- the toolchain: arm-crtos-gcc & co. --------------------------------------------------------

# arm-crtos-<tool> for every arm-none-eabi-<tool> there is (toolchain/launcher.c)
TOOLCHAIN_TOOLS = ('gcc', 'g++', 'c++', 'cpp', 'as', 'ld', 'ar', 'nm', 'ranlib', 'objcopy', 'objdump', 'readelf',
                   'size', 'strip', 'strings', 'addr2line', 'gdb')


def toolchain_dir():
    """The toolchain directory: build/toolchain/arm-crtos in the tree, the SDK itself outside"""
    return os.path.join(build_dir(), 'toolchain', 'arm-crtos') if IN_TREE else ROOT


def host_cc():
    """A C compiler for this computer (for the launchers): CC, gcc, clang, cc or MSVC's cl"""
    cc = os.environ.get('CC')
    if cc:
        return cc
    places = (r'C:\mingw64\bin', r'C:\msys64\ucrt64\bin', r'C:\msys64\mingw64\bin', r'C:\msys64\clang64\bin') if WINDOWS else ()
    for name in ('gcc', 'clang', 'cc', 'cl'):
        p = find_program(name, *places)
        if p:
            return p
    return None


def host_compile(cc, src, out):
    if os.path.basename(cc).lower() in ('cl', 'cl.exe'):
        cmd = [cc, '/nologo', '/O2', '/W3', '/D_CRT_SECURE_NO_WARNINGS', src, '/Fe:' + out]
        cwd = os.path.dirname(out)      # (cl drops its .obj files into the current directory)
    else:
        cmd = [cc, '-O2', '-Wall', '-o', out, src]
        if WINDOWS:
            cmd.append('-static')      # (no dependency on the compiler's run-time DLLs)
        cwd = None
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    if r.returncode:
        raise Fail('compiling %s failed:\n%s' % (os.path.basename(src), (r.stdout + r.stderr).strip()))


def build_host_tools(root, required=True):
    """bin/arm-crtos-* and bin/crtos-app for this computer, lib/gcc-bin.txt (where the Arm
    toolchain is). False when there is no C compiler and @required is False."""
    cc = host_cc()
    if not cc:
        if required:
            raise Fail('no C compiler for this computer (gcc, clang or cl) to build arm-crtos-gcc: install one '
                       '(Windows: e.g. "winget install BrechtSanders.WinLibs.POSIX.UCRT") or set CC')
        return False
    src = os.path.join(ROOT, 'toolchain') if IN_TREE else os.path.join(ROOT, 'src')
    gbin = gcc_bin()
    if not gbin:
        raise Fail('no Arm GNU Toolchain found ("crtos setup" installs it)')
    bindir = os.path.join(root, 'bin')
    os.makedirs(bindir, exist_ok=True)
    launcher = os.path.join(bindir, 'arm-crtos-gcc' + EXE)
    host_compile(cc, os.path.join(src, 'launcher.c'), launcher)
    made = ['gcc']
    for t in TOOLCHAIN_TOOLS[1:]:
        if os.path.exists(os.path.join(gbin, 'arm-none-eabi-' + t + EXE)):
            shutil.copy2(launcher, os.path.join(bindir, 'arm-crtos-' + t + EXE))
            made.append(t)
    host_compile(cc, os.path.join(src, 'crtos-app.c'), os.path.join(bindir, 'crtos-app' + EXE))
    for leftover in glob.glob(os.path.join(bindir, '*.obj')):
        os.remove(leftover)
    with open(os.path.join(root, 'lib', 'gcc-bin.txt'), 'w', encoding='utf-8') as f:
        f.write(gbin + '\n')
    say('toolchain: arm-crtos-{%s} and crtos-app built with %s' % (','.join(made), cc))
    return True


def cmd_toolchain(action='build'):
    root = toolchain_dir()
    if IN_TREE:
        bdir = build_dir()
        with BuildLock(bdir):
            configure(bdir)
            if run_cmake(['--build', bdir, '--target', 'crtos_toolchain']):
                raise Fail('the build failed (the first error is above)')
    build_host_tools(root)
    gcc = os.path.join(root, 'bin', 'arm-crtos-gcc' + EXE)
    r = subprocess.run([gcc, '-dumpversion'], capture_output=True, text=True)
    if r.returncode:
        raise Fail('arm-crtos-gcc does not work:\n%s' % (r.stdout + r.stderr).strip())
    say('toolchain: %s (GCC %s, Arm GNU Toolchain in %s)' % (rel(root), r.stdout.strip(), gcc_bin()))
    say('  add %s to the PATH, then e.g.:' % rel(os.path.join(root, 'bin')))
    say('    arm-crtos-gcc -O2 hello.c -o hello.app       (C++: arm-crtos-g++)')
    say('    crtos-app info hello.app                     (the program header, memory)')
    say('    crtos put hello.app /sd/crtos/bin/hello.app  then "hello" in the board\'s shell')


def native_dir():
    """build/toolchain/native: the compiler for the board as the board's files (flash0/, sd/)"""
    return os.path.join(build_dir(), 'toolchain', 'native')


def linux_path(path):
    """A path as Linux (WSL) sees it: C:\\x -> /mnt/c/x"""
    p = os.path.abspath(path).replace('\\', '/')
    if WINDOWS and len(p) > 1 and p[1] == ':':
        p = '/mnt/%s%s' % (p[0].lower(), p[2:])
    return p


def cmd_toolchain_native(steps):
    """GCC and binutils for the board: toolchain/native/build.sh, in Linux (WSL on Windows).
    The first run downloads the sources and builds a Linux cross compiler too (an hour or
    more); later runs redo only what is missing."""
    if not IN_TREE:
        raise Fail('the compiler for the board is built in the CRTOS tree')
    cmd_toolchain('build') # the PC toolchain: its libraries go into stage B and onto the board
    script = os.path.join(ROOT, 'toolchain', 'native', 'build.sh')
    env = ['CRTOS_TC_OUT=' + linux_path(toolchain_dir()), 'CRTOS_NATIVE_OUT=' + linux_path(native_dir())]
    if WINDOWS:
        if not shutil.which('wsl'):
            raise Fail('no WSL: the compiler for the board is built in Linux ("wsl --install -d Ubuntu-22.04")')
        distro = ['-d', os.environ['CRTOS_WSL']] if os.environ.get('CRTOS_WSL') else []
        # --exec: the arguments as they are, without a Linux shell parsing them again
        cmd = ['wsl'] + distro + ['--exec', 'env'] + env + ['bash', linux_path(script)] + steps
    else:
        cmd = ['env'] + env + ['bash', script] + steps
    say('toolchain native: %s (the logs are in the work directory, ~/crtos-toolchain)' %
        (' '.join(steps) if steps else 'all steps'))
    if subprocess.call(cmd):
        raise Fail('the build of the compiler for the board failed (the end of its log is above)')
    if not steps or 'install' in steps:
        say('toolchain native: %s - "crtos toolchain install" puts it on the board' % rel(native_dir()))


def native_files():
    """[(local path, manifest key, path on the board)] of build/toolchain/native"""
    files = []
    for sub, board in (('flash0', '/flash0/'), ('sd', '/sd/')):
        top = os.path.join(native_dir(), sub)
        for dirpath, _, names in os.walk(top):
            for n in names:
                local = os.path.join(dirpath, n)
                remote = board + os.path.relpath(local, top).replace('\\', '/')
                files.append((local, remote[len('/sd/'):] if sub == 'sd' else remote[1:], remote))
    return sorted(files, key=lambda f: f[2])


def cmd_src(host=None, full=False, dry_run=False, check=False):
    """The programs' sources with Makefiles for the board's own GCC (tools/boardsrc.py) into
    build/src, then to /sd/crtos/src over the network (changed files only, like deploy)"""
    if not IN_TREE:
        raise Fail('the sources are those of the CRTOS tree')
    cmd_build([])       # (the flags come from what CMake wrote)
    import boardsrc
    bdir = build_dir()
    try:
        out, projects = boardsrc.generate(bdir)
        if check and boardsrc.check(bdir, out, projects):
            raise Fail('some sources do not compile the way the board would build them (above)')
    except boardsrc.Fail as e:
        raise Fail(str(e))
    files = []
    for dirpath, _, names in os.walk(out):
        for n in names:
            local = os.path.join(dirpath, n)
            path = os.path.relpath(local, out).replace('\\', '/')
            files.append((local, 'crtos/src/' + path, '/sd/crtos/src/' + path))
    say('src: %d program(s) in %s' % (sum(len(p.progs) for p in projects), rel(out)))
    todo, done = changed_files(sorted(files, key=lambda f: f[2]), full)
    if not todo:
        say('src: the board has them already ("--full" sends everything again)')
        return
    say('src: %d file(s), %d KB to /sd/crtos/src' % (len(todo), sum(os.path.getsize(l) for l, _ in todo) // 1024))
    if dry_run:
        for local, remote in todo:
            say('  %s' % remote)
        return
    upload(todo, 'net', host, on_done=done)
    say('src: done - on the board: cd /sd/crtos/src/apps/paint; make (README.txt there)')


def cmd_toolchain_install(host=None, full=False, dry_run=False):
    """The compiler onto the board, over the network only: tens of MB (the probe would take an
    hour). Changed files only, like deploy (build/deployed.json)."""
    files = native_files()
    if not files:
        raise Fail('no compiler for the board yet: "crtos toolchain native" builds it (in WSL/Linux)')
    todo, done = changed_files(files, full)
    if not todo:
        say('toolchain install: the board has it already ("--full" sends everything again)')
        return
    flash = sum(os.path.getsize(l) for l, r in todo if r.startswith('/flash0/'))
    card = sum(os.path.getsize(l) for l, r in todo if not r.startswith('/flash0/'))
    say('toolchain install: %d file(s): %.1f MB to /flash0, %.1f MB to /sd/crtos/usr' %
        (len(todo), flash / 1048576.0, card / 1048576.0))
    if dry_run:
        for local, remote in todo:
            say('  %s (%d bytes)' % (remote, os.path.getsize(local)))
        return
    if flash:
        say('  writing the flash stops the board for up to a second per 256 KB erased (a few minutes in all)')
    t0 = time.time()
    upload(todo, 'net', host, on_done=done)
    say('toolchain install: done in %.0f s - on the board: gcc hello.c -o hello.app' % (time.time() - t0))


def cmd_sdk(dest=None):
    if not IN_TREE:
        raise Fail('this is the SDK already')
    bdir = build_dir()
    out = os.path.join(bdir, 'sdk')
    with BuildLock(bdir):
        configure(bdir)
        if run_cmake(['--build', bdir, '--target', 'sdk_parts']):
            raise Fail('the build failed')
    if not build_host_tools(toolchain_dir(), required=False):
        say('sdk: no C compiler for this computer - the SDK has no arm-crtos-gcc (CMake builds still work)')
    with BuildLock(bdir):
        if run_cmake(['--install', bdir, '--prefix', out, '--component', 'sdk']):
            raise Fail('SDK export failed')
    if dest:
        shutil.copytree(out, dest, dirs_exist_ok=True)
        out = dest
    say('sdk: %s' % os.path.abspath(out))
    say('     programs outside this tree: see README.md there ("crtos new NAME" in any directory)')


# ---- the kernel with MCUXpresso IDE ----------------------------------------------------------

def cmd_kbuild(config='Release'):
    """Headless build of the IDE project (the kernel) in a temporary workspace, so an open
    IDE does not get in the way"""
    ide = mcux_ide()
    if not ide:
        raise Fail('MCUXpresso IDE not found (CRTOS_MCUX=<mcuxpressoidec.exe>); "crtos build kernel" '
                   'builds the kernel without it')
    ws = tempfile.mkdtemp(prefix='crtos_hws_')
    t_start = time.time() - 2
    try:
        cmd = [ide, '-nosplash', '--launcher.suppressErrors',
               '-application', 'org.eclipse.cdt.managedbuilder.core.headlessbuild',
               '-data', ws, '-import', ROOT, '-build', 'crtos/' + config]
        proc = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    finally:
        shutil.rmtree(ws, ignore_errors=True)
    out = proc.stdout + proc.stderr
    os.makedirs(os.path.join(ROOT, config), exist_ok=True)
    with open(os.path.join(ROOT, config, 'kbuild.log'), 'w', encoding='utf-8', errors='replace') as f:
        f.write(out)
    keys = (' error:', 'undefined reference', 'multiple definition', 'ld.exe:', 'will not fit',
            'overflowed', 'warning:')
    for line in out.splitlines():
        if any(k in line for k in keys) or 'Error ' in line[:6]:
            say(line)
    summary = [l for l in out.splitlines() if 'Build Finished' in l]
    say(summary[-1] if summary else out[-2000:])
    subprocess.run([sys.executable, os.path.join(TOOLS, 'stackcheck.py'), os.path.join(ROOT, config),
                    '--since', str(t_start)])
    ok = (proc.returncode == 0 and os.path.exists(os.path.join(ROOT, config, 'crtos.axf'))
          and bool(summary) and 'Build Finished. 0 errors' in summary[-1])
    if not ok:
        raise Fail('kbuild failed (%s)' % rel(os.path.join(ROOT, config, 'kbuild.log')))


# ---- flashing --------------------------------------------------------------------------------

def linkserver():
    p = os.environ.get('CRTOS_LINKSERVER')
    if p:
        return p
    found = sorted(glob.glob(r'C:\nxp\LinkServer_*\LinkServer.exe'), key=natural, reverse=True)
    return found[0] if found else None


def to_binary(axf):
    gcc = gcc_bin()
    objcopy = os.path.join(gcc, 'arm-none-eabi-objcopy') if gcc else 'arm-none-eabi-objcopy'
    binf = os.path.splitext(axf)[0] + '.bin'
    subprocess.run([objcopy, '-O', 'binary', axf, binf], check=True)
    return binf


def cmd_flash(path=None, net=False, host=None, rtos=False):
    if rtos and net:
        raise Fail('the RTOS build is written through the debug probe (it has no network to come back on)')
    binf = path or kernel_file('.bin', rtos)
    if not os.path.exists(binf):
        raise Fail('%s not found: run "crtos build%s" first' % (rel(binf), ' --rtos' if rtos else ''))
    if binf.endswith('.axf'):
        binf = to_binary(binf)
    if net:
        # through the running system: deployd writes it with /dev/mtd0 (flexspi-mtd) and restarts
        netdeploy = netdeploy_module()
        say('flash: %s (%d KB) -> the board\'s flash over the network' % (rel(binf), os.path.getsize(binf) // 1024))
        t0 = time.time()
        try:
            ok = netdeploy.Net(host).flash(binf)
        except netdeploy.DeployError as e:
            raise Fail('flash over the network: %s' % e)
        if not ok:
            raise Fail('the board did not come back - "crtos flash" through the debug probe repairs it')
        say('flash: done in %.0f s' % (time.time() - t0))
        return
    swdcon = swdcon_module()
    try:
        probe = swdcon.find_probe()
    except RuntimeError as e:
        raise Fail(str(e))
    say('flash: %s (%d KB) -> the board\'s flash, probe %s' % (rel(binf), os.path.getsize(binf) // 1024, probe))
    t0 = time.time()
    pyocd = [sys.executable, '-m', 'pyocd']
    # (sector erase: only the kernel's own blocks; the rest of the flash is /flash0)
    proc = subprocess.run(pyocd + ['flash', '-u', probe, '-t', TARGET, '-f', '12000000', '-e', 'sector', '-a', hex(FLASH_ADDR), binf],
                          capture_output=True, text=True, errors='replace')
    if proc.returncode == 0:
        try:
            swdcon.reset(probe, 'hw')       # pyOCD's own reset (VECTRESET) does nothing on the M7
        except Exception as e:
            say('flash: done, but the reset failed (%s) - press the board\'s reset button' % e)
            return
        say('flash: done in %.0f s, the board restarts' % (time.time() - t0))
        return
    say((proc.stdout + proc.stderr).strip()[-1500:])
    ls = linkserver()
    if not ls:
        raise Fail('flashing failed (is the board powered and the probe free - no debug session running?)')
    # LinkServer takes the raw image as well (its ELF loader fails on some images)
    say('flash: pyOCD failed, trying LinkServer')
    device = os.environ.get('CRTOS_DEVICE', 'MIMXRT1052B:EVKB-IMXRT1050')
    proc = subprocess.run([ls, 'flash', device, 'load', '%s:0x%08x' % (binf, FLASH_ADDR)], capture_output=True,
                          text=True, errors='replace')
    if proc.returncode == 0 and 'Finished writing Flash successfully' in proc.stdout:
        say('flash: done (LinkServer)')
        return
    say((proc.stdout + proc.stderr).strip()[-1500:])
    raise Fail('flashing failed')


# ---- the serial console ----------------------------------------------------------------------

def serial_port():
    """CRTOS_PORT, else the serial port of the board's DAPLink probe"""
    p = os.environ.get('CRTOS_PORT')
    if p:
        return p
    need_module('serial', 'pyserial')
    from serial.tools import list_ports
    ports = [p for p in list_ports.comports() if p.vid == 0x0D28]     # Arm (DAPLink)
    if len(ports) == 1:
        return ports[0].device
    if not ports:
        raise Fail('no DAPLink serial port: is the board connected (USB in its debug port, J28)? '
                   '(or name the port: CRTOS_PORT=COM5)')
    probe = swdcon_module().find_probe().lower()
    for p in ports:
        if (p.serial_number or '').lower() == probe:
            return p.device
    raise Fail('several DAPLink serial ports (%s): choose one with CRTOS_PORT' % ', '.join(p.device for p in ports))


def open_serial(wait=5.0):
    """The console port; gives up after @wait seconds (a terminal program may hold it)"""
    import serial
    port = serial_port()
    t0 = time.time()
    while True:
        try:
            return serial.Serial(port, BAUD, timeout=0.05)
        except Exception as e:
            if time.time() - t0 > wait:
                raise Fail('%s: %s (is a terminal program using it?)' % (port, e))
            time.sleep(0.05)


def cmd_serial():
    need_module('serial', 'pyserial')
    port = serial_port()
    say('serial console %s, %d baud. Ctrl-] switches to the kernel monitor (its "exit" switches back); '
        'Ctrl-X ends.' % (port, BAUD))
    return subprocess.call([sys.executable, '-m', 'serial.tools.miniterm', '--exit-char', '24', port, str(BAUD)])


def cmd_log(seconds=None, reset=False, out=None, until=None):
    """Show the console output (until Ctrl-C or @seconds); @reset restarts the board first
    (the DAPLink resets it on a BREAK)"""
    s = open_serial()
    s.reset_input_buffer()
    if reset:
        try:
            s.send_break(0.3)
        except Exception:
            pass
    data = bytearray()
    t0 = time.time()
    try:
        while seconds is None or time.time() - t0 < seconds:
            try:
                chunk = s.read(4096)
            except Exception:
                s.close()
                time.sleep(0.05)
                s = open_serial()
                continue
            if chunk:
                data += chunk
                sys.stdout.write(chunk.decode('utf-8', 'replace'))
                sys.stdout.flush()
                if until and until.encode() in data:
                    break
    except KeyboardInterrupt:
        pass
    finally:
        s.close()
        if out:
            with open(out, 'wb') as f:
                f.write(data)
    return bytes(data)


def kmon_serial(cmds, reset=False, timeout=30.0, boot_wait=10.0, out=None):
    """Kernel monitor commands on the serial console; each waits for the next prompt"""
    s = open_serial()
    s.reset_input_buffer()
    data = bytearray()
    prompt = b'kmon> '

    def wait_for(start, texts, tmo):
        t0 = time.time()
        while time.time() - t0 < tmo:
            chunk = s.read(4096)
            if chunk:
                data.extend(chunk)
                sys.stdout.write(chunk.decode('utf-8', 'replace'))
                sys.stdout.flush()
            if any(t in data[start:] for t in texts):
                return True
        return False

    ok = True
    try:
        if reset:
            try:
                s.send_break(0.3)
            except Exception:
                pass
            # once init runs, the shell owns the console: wait for it (or the end of the boot)
            wait_for(0, [b'boot: done'], boot_wait)
            wait_for(0, [b'crtos:', b'no /sd/crtos/sbin/init.app', b'safe mode'], 3.0)
        mark = len(data)
        s.write(b'\x1d\r')      # Ctrl-]: the console input goes to kmon
        if not wait_for(mark, [prompt], 3.0):
            say('\n[crtos: no kmon prompt]')
            ok = False
        for c in cmds:
            mark = len(data)
            s.write(c.encode() + b'\r')
            if not wait_for(mark, [prompt], timeout):
                say('\n[crtos: timeout waiting for "%s"]' % c)
                ok = False
                break
    finally:
        s.close()
        if out:
            with open(out, 'wb') as f:
                f.write(data)
    return ok


class _Console:
    """Reader over the serial console that never loses already received text"""

    def __init__(self, s):
        self.s = s
        self.data = bytearray()
        self.pos = 0

    def until(self, tokens, timeout):
        """Wait for the earliest of @tokens after the cursor; return (token, rest of its line)"""
        t0 = time.time()
        while True:
            best = None
            for t in tokens:
                i = self.data.find(t, self.pos)
                if i >= 0 and (best is None or i < best[0]):
                    best = (i, t)
            if best:
                i, t = best
                j = self.data.find(b'\n', i)
                if j >= 0 or t == b'kmon> ':
                    end = j if j >= 0 else i + len(t)
                    self.pos = end + (1 if j >= 0 else 0)
                    return t, bytes(self.data[i:end]).rstrip(b'\r')
            if time.time() - t0 > timeout:
                return None, bytes(self.data[self.pos:self.pos + 80])
            chunk = self.s.read(4096)
            if chunk:
                self.data.extend(chunk)


def put_serial(pairs, timeout=10.0, on_done=None):
    """Upload (local, remote) files with the kmon 'put' command over the serial console
    (slow: for a board without network and without the SWD console)"""
    s = open_serial()
    s.reset_input_buffer()
    con = _Console(s)
    s.write(b'\x1d\r')
    if not con.until([b'kmon> '], 3.0)[0]:
        s.close()
        raise Fail('put: no kmon prompt on the serial console')
    try:
        for local, remote in pairs:
            blob = open(local, 'rb').read()
            crc = zlib.crc32(blob) & 0xFFFFFFFF
            t0 = time.time()
            s.write(('put %s %d %08x\r' % (remote, len(blob), crc)).encode())
            tok, line = con.until([b'READY', b'ERR'], timeout)
            if tok != b'READY':
                raise Fail('put %s: not accepted: %s' % (remote, line))
            chunk, sent, acked = 1024, 0, 0     # stop-and-wait: the DAPLink bridge stalls under load
            while acked < len(blob):
                if sent == acked and sent < len(blob):
                    s.write(blob[sent:sent + chunk])
                    sent = min(len(blob), sent + chunk)
                tok, line = con.until([b'ACK ', b'ERR'], timeout)
                if tok != b'ACK ':
                    raise Fail('put %s: transfer failed: %s' % (remote, line))
                acked = int(line.split()[1])
            tok, line = con.until([b'OK ', b'ERR'], timeout)
            if tok != b'OK ':
                raise Fail('put %s: %s' % (remote, line))
            dt = time.time() - t0
            say('put %s: %d bytes, %.1f KB/s' % (remote, len(blob), len(blob) / 1024.0 / max(dt, 0.001)))
            con.until([b'kmon> '], 3.0)
            if on_done:
                on_done(local, remote, crc)
    finally:
        s.close()


# ---- files for the board ---------------------------------------------------------------------

def sdcard_files():
    """[(local path, path relative to build/sdcard)] - 'crtos/apps/x.app' is /sd/crtos/apps/x.app"""
    top = sdcard_dir()
    files = []
    for dirpath, _, names in os.walk(top):
        for n in names:
            local = os.path.join(dirpath, n)
            files.append((local, os.path.relpath(local, top).replace('\\', '/')))
    return sorted(files, key=lambda f: f[1])


def flash0_files():
    """[(local path, path relative to build/flash0)]: 'bin/x.app' is /flash0/bin/x.app"""
    top = flash0_dir()
    files = []
    for dirpath, _, names in os.walk(top):
        for n in names:
            local = os.path.join(dirpath, n)
            files.append((local, os.path.relpath(local, top).replace('\\', '/')))
    return sorted(files, key=lambda f: f[1])


def deploy_files():
    """[(local path, manifest key, path on the board)] of both roots"""
    return ([(l, r, '/sd/' + r) for l, r in sdcard_files()] +
            [(l, 'flash0/' + r, '/flash0/' + r) for l, r in flash0_files()])


def file_crc(path):
    with open(path, 'rb') as f:
        return zlib.crc32(f.read()) & 0xFFFFFFFF


def manifest_path():
    return os.path.join(build_dir(), 'deployed.json')


def load_manifest():
    try:
        with open(manifest_path()) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def changed_files(files, full):
    """The files (from deploy_files) that differ from what was sent last time
    (build/deployed.json) and a callback recording each finished upload"""
    manifest = load_manifest()
    todo = [(local, remote) for local, key, remote in files if full or manifest.get(key) != '%08x' % file_crc(local)]

    def done(local, remote, crc):
        if remote == TOKEN_REMOTE:
            return
        if remote.startswith('/sd/crtos/'):
            key = remote[len('/sd/'):]
        elif remote.startswith('/flash0/'):
            key = remote[1:]
        else:
            return
        manifest[key] = '%08x' % crc
        with open(manifest_path(), 'w') as f:
            json.dump(manifest, f, indent=1, sort_keys=True)

    return todo, done


def board_path(remote):
    """A path on the board as typed: Git Bash turns "/sd/x" into "C:/Program Files/Git/sd/x"
    when it starts a Windows program - undo that"""
    if not remote.startswith('/'):
        path = remote.replace('\\', '/')
        for top in ('/sd/', '/ram/', '/dev/', '/flash0/'):
            i = path.find(top)
            if i > 0:
                return path[i:]
    return remote


def token_path(create=False):
    net = netdeploy_module().Net()
    if create:
        net.token(create=True)
    return net.token_path


def upload(pairs, how='auto', host=None, on_done=None, install_token=False):
    """Send (local, remote) files: over the network when the board answers there (deployd),
    else through the debug probe. The probe also installs the deploy token when the network
    did not accept ours, so the next time the network works."""
    netdeploy = netdeploy_module()
    if how in ('auto', 'net'):
        net = netdeploy.Net(host)
        if how == 'net' or os.path.exists(net.token_path):
            try:
                net.resolve()
                if not pairs:
                    return 'net'
                net.upload(pairs, on_done=on_done)
                return 'net'
            except (netdeploy.DeployError, OSError) as e:
                if how == 'net':
                    raise Fail('network: %s' % e)
                if 'token' in str(e):
                    install_token = True
                say('network: %s - using the debug probe' % e)
    if how == 'serial':
        put_serial(pairs, on_done=on_done)
        return 'serial'
    if not os.path.exists(token_path()):
        install_token = True
    # the probe is slow (~45 KB/s): small files first, so the system is complete early on
    pairs = sorted(pairs, key=lambda p: os.path.getsize(p[0]))
    if install_token:
        pairs = [(token_path(create=True), TOKEN_REMOTE)] + pairs
    if not pairs:
        return 'swd'
    size = sum(os.path.getsize(l) for l, _ in pairs)
    if size > 512 * 1024:
        say('through the debug probe: about %d s for %d KB (over Ethernet it takes seconds)' %
            (size // (45 * 1024) + 3, size // 1024))
    swdcon = swdcon_module()
    try:
        if not swdcon.upload_files(kernel_axf(), pairs, on_done=on_done):
            raise Fail('the kernel monitor on the SWD console does not answer')
    except RuntimeError as e:
        raise Fail(str(e))
    if install_token:
        say('deploy: token installed - from now on "crtos deploy" also works over Ethernet')
    return 'swd'


def cmd_deploy(how='auto', full=False, host=None, dry_run=False):
    files = deploy_files()
    if not files:
        raise Fail('nothing to send: run "crtos build" first')
    todo, done = changed_files(files, full)
    t0 = time.time()
    if not todo:
        say('deploy: the board is up to date ("crtos deploy --full" sends everything again)')
        return
    size = sum(os.path.getsize(l) for l, _ in todo)
    say('deploy: %d file(s), %d KB' % (len(todo), size // 1024))
    if dry_run:
        for local, remote in todo:
            say('  %s (%d bytes)' % (remote, os.path.getsize(local)))
        return
    via = upload(todo, how, host, on_done=done)
    say('deploy: done in %.1f s (%s)' % (time.time() - t0, {'net': 'network', 'swd': 'debug probe',
                                                             'serial': 'serial console'}[via]))
    if any('/drivers/' in r or '/boot/' in r or '/sbin/' in r or '/etc/' in r for _, r in todo):
        say('        new drivers, services or settings take effect after "crtos reboot"')


def cmd_put(local, remote, how='auto', host=None):
    if not os.path.isfile(local):
        raise Fail('%s: no such file' % local)
    upload([(local, board_path(remote))], how, host)


# ---- files and the screen over the network -------------------------------------------------------

REMOTE_RE = re.compile(r'^(board|\d{1,3}(?:\.\d{1,3}){3})?:(.*)$')


def remote_spec(arg):
    """(host or None, path) of a board path - board:/path, :/path or IP:/path (a relative path
    is in /sd/crtos) - or None for a path of this computer (C:\\x and C:/x are local)"""
    m = REMOTE_RE.match(arg)
    if not m:
        return None
    path = board_path(m.group(2)) if m.group(2) else '/sd/crtos'
    if not path.startswith('/'):
        path = '/sd/crtos/' + path
    host = m.group(1) if m.group(1) and m.group(1) != 'board' else None
    return host, path


def remote_join(a, b):
    return (a.rstrip('/') or '') + '/' + b


def _scp_put(s, local, remote, stats):
    data = open(local, 'rb').read()
    t = time.time()
    s.put(data, remote)
    say('  %s -> board:%s (%d bytes, %.0f KB/s)' % (local, remote, len(data), len(data) / 1024.0 / max(time.time() - t, 1e-3)))
    stats[0] += 1
    stats[1] += len(data)


def _scp_get(s, remote, local, stats):
    t = time.time()
    data = s.get(remote)
    with open(local, 'wb') as f:
        f.write(data)
    say('  board:%s -> %s (%d bytes, %.0f KB/s)' % (remote, local, len(data), len(data) / 1024.0 / max(time.time() - t, 1e-3)))
    stats[0] += 1
    stats[1] += len(data)


def _scp_get_tree(s, remote, local, stats):
    os.makedirs(local, exist_ok=True)
    for kind, _size, _mtime, name in s.list(remote):
        if kind == 'd':
            _scp_get_tree(s, remote_join(remote, name), os.path.join(local, name), stats)
        else:
            _scp_get(s, remote_join(remote, name), os.path.join(local, name), stats)


def cmd_scp(paths, recursive=False, host=None):
    """Files from this computer to the board (deployd writes only under /sd/crtos, /flash0 and
    /ram) or from the board (anything on /sd, /flash0, /ram); as scp: into DST when it is a
    directory (or several sources), else as DST"""
    if len(paths) < 2:
        raise Fail('usage: crtos scp [-r] SRC... DST (a board path: board:/sd/crtos/...)')
    specs = [remote_spec(p) for p in paths]
    srcs, dst = specs[:-1], specs[-1]
    hosts = {sp[0] for sp in specs if sp and sp[0]}
    if len(hosts) > 1:
        raise Fail('scp: one board at a time')
    up = dst is not None and all(sp is None for sp in srcs)
    down = dst is None and all(sp is not None for sp in srcs)
    if not up and not down:
        raise Fail('scp: from this computer to the board or back - one side is board:PATH')
    netdeploy = netdeploy_module()
    net = netdeploy.Net(host or (next(iter(hosts)) if hosts else None))
    try:
        board, s = net.session()
    except (netdeploy.DeployError, OSError) as e:
        raise Fail('network: %s' % e)
    stats = [0, 0]
    t0 = time.time()
    try:
        if up:
            target = dst[1]
            st = s.stat(target)
            into = target.endswith('/') or len(srcs) > 1 or (st is not None and st[0] == 'd')
            for local in paths[:-1]:
                if not os.path.exists(local):
                    raise Fail('%s: no such file' % local)
                name = os.path.basename(os.path.normpath(local))
                where = remote_join(target, name) if into else target.rstrip('/')
                if not os.path.isdir(local):
                    _scp_put(s, local, where, stats)
                    continue
                if not recursive:
                    raise Fail('%s is a directory: -r copies it with what it holds' % local)
                for root, _dirs, files in os.walk(local):
                    rel_dir = os.path.relpath(root, local)
                    rdir = where if rel_dir == '.' else remote_join(where, rel_dir.replace(os.sep, '/'))
                    s.mkdir(rdir)
                    for f in sorted(files):
                        _scp_put(s, os.path.join(root, f), remote_join(rdir, f), stats)
        else:
            local_dst = paths[-1]
            into = os.path.isdir(local_dst) or len(srcs) > 1 or local_dst.endswith(('/', '\\'))
            if into:
                os.makedirs(local_dst, exist_ok=True)
            for _h, remote in srcs:
                st = s.stat(remote)
                if st is None:
                    raise Fail('board:%s: no such file (or not readable: /sd, /flash0, /ram)' % remote)
                name = remote.rstrip('/').rsplit('/', 1)[-1]
                where = os.path.join(local_dst, name) if into else local_dst
                if st[0] == 'd':
                    if not recursive:
                        raise Fail('board:%s is a directory: -r copies it with what it holds' % remote)
                    _scp_get_tree(s, remote.rstrip('/'), where, stats)
                else:
                    _scp_get(s, remote, where, stats)
    except netdeploy.DeployError as e:
        raise Fail('scp: %s' % e)
    finally:
        s.close()
    dt = time.time() - t0
    say('scp: %d file(s), %d bytes in %.1f s (%.0f KB/s, %s)' % (stats[0], stats[1], dt, stats[1] / 1024.0 / max(dt, 1e-3),
                                                                   board))


def vnc_password(net):
    """The password of the board's remote desktop: ~/.crtos/vnc.passwd (made at the first use),
    put on the board (through deployd) when it has another one or none. Returns (host, pw)."""
    import secrets
    import string
    path = os.path.join(net.state, 'vnc.passwd')
    if not os.path.exists(path):
        os.makedirs(net.state, exist_ok=True)
        with open(path, 'w') as f:
            f.write(''.join(secrets.choice(string.ascii_letters + string.digits) for _ in range(8)) + '\n')
    pw = open(path).read().strip()
    data = (pw + '\n').encode()
    netdeploy = netdeploy_module()
    try:
        board, s = net.session()
    except (netdeploy.DeployError, OSError) as e:
        say('desktop: the password could not be checked on the board (%s)' % e)
        return net.resolve(), pw
    try:
        try:
            size, crc = s.crc(VNC_REMOTE)
            same = size == len(data) and crc == zlib.crc32(data) & 0xFFFFFFFF
        except netdeploy.DeployError:
            same = False
        if not same:
            s.put(data, VNC_REMOTE)
            say('desktop: the password is now on the board (%s)' % VNC_REMOTE)
    finally:
        s.close()
    return board, pw


def cmd_desktop(host=None, zoom=None, show_password=False, shot=None, fullscreen=False, stats=False):
    netdeploy = netdeploy_module()
    net = netdeploy.Net(host)
    try:
        board, pw = vnc_password(net)
    except (netdeploy.DeployError, OSError) as e:
        raise Fail('network: %s' % e)
    import vnc
    if show_password:
        say('desktop: VNC at %s:%d, password %s (~/.crtos/vnc.passwd)' % (board, vnc.PORT, pw))
        return 0
    try:
        if shot:
            w, h, dt = vnc.shot(board, pw, shot)
            say('desktop: %s (%dx%d) in %.1f s' % (shot, w, h, dt))
        else:
            say('desktop: %s:%d - close the window to end' % (board, vnc.PORT))
            try:
                import PIL  # noqa: F401 (the picture scaled to any size)
            except ImportError:
                say('desktop: without Pillow the picture grows only by whole numbers ("crtos setup" installs it)')
            vnc.view(board, pw, max(1, zoom) if zoom else None, fullscreen, stats=stats)
    except (vnc.VncError, OSError) as e:
        raise Fail('desktop: %s' % e)
    return 0


def _busy_threads(ps_text):
    """The threads of a "kmon ps" that used the processor (CPU%), busiest first"""
    rows = []
    for line in ps_text.splitlines():
        f = line.split()
        if len(f) >= 5 and f[0].isdigit():
            try:
                cpu = float(f[4])
            except ValueError:
                continue
            if cpu >= 1.0:
                rows.append((cpu, f[1]))
    return ', '.join('%s %.0f%%' % (name, cpu) for cpu, name in sorted(rows, reverse=True))


def _net_counters(text):
    m = re.search(r'rx (\d+) \((\d+) B, (\d+) dropped, (\d+) errors\)\s+tx (\d+) \((\d+) B, (\d+) dropped, (\d+) errors\)', text)
    return [int(x) for x in m.groups()] if m else None


def cmd_netbench(host=None, seconds=5, only=None, rate=95.0):
    """Throughput of the board's network, both ways, over TCP and UDP: the board runs "nettest"
    (started through the kernel monitor on the SWD console, which also shows the processor load
    halfway through each test)."""
    import threading
    import netbench
    tests = [t for t in netbench.TESTS if not only or t[0] in only]
    if not tests:
        raise Fail('netbench: no such test (%s)' % ', '.join(t[0] for t in netbench.TESTS))
    netdeploy = netdeploy_module()
    try:
        board = netdeploy.Net(host).resolve()
    except (netdeploy.DeployError, OSError) as e:
        raise Fail('network: %s' % e)
    remote = program_path('nettest')
    local = os.path.join(sdcard_dir(), remote[len('/sd/'):])
    if not os.path.exists(local):
        raise Fail('netbench: build nettest first ("crtos build nettest", "crtos deploy")')
    if load_manifest().get(remote[len('/sd/'):]) != '%08x' % file_crc(local):
        say('note: nettest was built after the last "crtos deploy" - the board runs the old one')
    swdcon, con = swd_console()
    try:
        _, text = con.command('net', 10.0)
        before = _net_counters(text)
        ok, text = con.command('run %s -n %d -t 30' % (remote, len(tests)), 10.0)
        if 'started' not in text:
            raise Fail('netbench: nettest did not start: %s' % text.strip())
        if not netbench.wait_server(board):
            raise Fail('netbench: nettest does not answer on %s:%d' % (board, netbench.PORT))
        say('netbench: %s, %d s per test' % (board, seconds))
        for key, title, fn in tests:
            box = {}

            def work():
                try:
                    box['r'] = fn(board, seconds, rate) if key == 'udp-rx' else fn(board, seconds)
                except (netbench.BenchError, OSError) as e:
                    box['e'] = e
            th = threading.Thread(target=work, daemon=True)
            th.start()
            th.join(1.0)
            load = ''
            if th.is_alive():   # "ps" counts the processor's time since the "ps" before
                con.command('ps', 10.0)
                th.join(max(1.0, seconds - 2.5))
                if th.is_alive():
                    _, text = con.command('ps', 10.0)
                    load = _busy_threads(text)
            th.join(seconds + 30)
            if 'e' in box or 'r' not in box:
                say('  %-20s failed: %s' % (title, box.get('e', 'no result')))
                continue
            r = box['r']
            extra = ''
            if 'lost' in r:
                extra = ', %d of %d datagrams lost (%.2f%%)' % (r['lost'], r['sent'], 100.0 * r['lost'] / max(1, r['sent']))
            say('  %-20s %6.1f Mbit/s%s' % (title, r['mbit'], extra))
            if load:
                say('  %-20s CPU: %s' % ('', load))
        _, text = con.command('net', 10.0)
        after = _net_counters(text)
        if before and after:
            d = [b - a for a, b in zip(before, after)]
            say('netbench: eth0 received %d frames (%d dropped, %d damaged), sent %d (%d dropped, %d errors)' %
                (d[0], d[2], d[3], d[4], d[6], d[7]))
    finally:
        con.close()
    return 0


WALLPAPER_DIR = '/sd/crtos/share/wallpapers'
FITS = ('fill', 'fit', 'stretch', 'center', 'tile')


def cmd_wallpaper(picture, name=None, size='800x480', fit='fill', set_it=True, host=None):
    """A picture of this computer as the board's wallpaper: made smaller to cover SIZE (its
    proportions kept), saved as PPM - the board reads no JPEG or PNG - and put into
    /sd/crtos/share/wallpapers; then set through "appearance" (the kernel monitor on SWD)"""
    try:
        from PIL import Image
    except ImportError:
        raise Fail('wallpaper: needs Pillow ("crtos setup" installs it)')
    m = re.fullmatch(r'(\d+)x(\d+)', size)
    if not m:
        raise Fail('wallpaper: --size WIDTHxHEIGHT, e.g. 800x480')
    tw, th = int(m.group(1)), int(m.group(2))
    try:
        img = Image.open(picture).convert('RGB')
    except OSError as e:
        raise Fail('wallpaper: %s: %s' % (picture, e))
    k = max(tw / img.width, th / img.height)
    if k < 1:
        img = img.resize((max(1, round(img.width * k)), max(1, round(img.height * k))), Image.LANCZOS)
    stem = name or os.path.splitext(os.path.basename(picture))[0]
    stem = re.sub(r'[^A-Za-z0-9_-]+', '-', stem).strip('-')[:40] or 'wallpaper'
    local = os.path.join(build_dir(), 'wallpaper', stem + '.ppm')
    os.makedirs(os.path.dirname(local), exist_ok=True)
    img.save(local, 'PPM')
    remote = '%s/%s.ppm' % (WALLPAPER_DIR, stem)
    say('wallpaper: %s -> %dx%d, %s' % (picture, img.width, img.height, remote))
    cmd_scp([local, 'board:' + remote], host=host)
    if not set_it:
        return 0
    swdcon, con = swd_console()
    try:
        ok, text = con.command('run -w /sd/crtos/bin/appearance.app wallpaper %s fit %s' % (remote, fit), 30.0)
    finally:
        con.close()
    if 'run: ' in text or not ok:
        raise Fail('wallpaper: it is on the card, but "appearance" did not set it (deployed? "crtos deploy"): %s'
                   % text.strip())
    say('wallpaper: set (%s)' % fit)
    return 0


def cmd_sdcard(drive, full=False):
    """Copy build/sdcard/crtos to <drive>/crtos: only that directory is written"""
    src = os.path.join(sdcard_dir(), 'crtos')
    if not os.path.isdir(src):
        raise Fail('nothing to copy: run "crtos build" first')
    top = drive
    if WINDOWS and re.fullmatch(r'[A-Za-z]:?[\\/]?', drive):
        top = drive[0].upper() + ':\\'
    if not os.path.isdir(top):
        raise Fail('no such drive or directory: %s (is the card in the reader?)' % drive)
    if WINDOWS and os.path.abspath(top).rstrip('\\').upper() == os.environ.get('SystemDrive', 'C:').upper():
        raise Fail('%s is the system drive, not an SD card' % drive)
    dest = os.path.join(top, 'crtos')
    copied = 0
    for local, r in sdcard_files():
        target = os.path.join(top, r)
        if not full and os.path.exists(target) and os.path.getsize(target) == os.path.getsize(local) \
                and file_crc(target) == file_crc(local):
            continue
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copyfile(local, target)
        copied += 1
    tok = os.path.join(dest, 'etc', 'deploy.token')
    if not os.path.exists(tok):
        os.makedirs(os.path.dirname(tok), exist_ok=True)
        shutil.copyfile(token_path(create=True), tok)
        copied += 1
    say('sdcard: %d file(s) copied to %s (nothing outside it was touched)' % (copied, dest))


# ---- the board -------------------------------------------------------------------------------

def swd_console():
    swdcon = swdcon_module()
    try:
        con = swdcon.SwdCon(kernel_axf())
    except RuntimeError as e:
        raise Fail(str(e))
    if not con.sync():
        con.close()
        raise Fail('the kernel monitor on the SWD console does not answer')
    return swdcon, con


def program_path(name):
    """The board path of a program named on the command line"""
    if name.startswith('/') or '/' in name.replace('\\', '/'):
        return board_path(name)
    base = name[:-4] if name.endswith('.app') else name
    for _, card in PROGRAM_DIRS:
        local = os.path.join(sdcard_dir(), 'crtos', card, base + '.app')
        if os.path.exists(local):
            return '/sd/crtos/%s/%s.app' % (card, base)
    return '/sd/crtos/bin/%s.app' % base


class _OutputFilter:
    """Program output of "run -w" without the monitor's own lines"""

    def __init__(self, hide):
        self.hide = hide
        self.buf = ''

    def feed(self, text):
        self.buf += text
        while '\n' in self.buf:
            line, self.buf = self.buf.split('\n', 1)
            bare = line.rstrip('\r')
            if not any(bare.startswith(h) for h in self.hide) and not re.fullmatch(r'\(\d+ ms\)', bare):
                sys.stdout.write(line + '\n')
        sys.stdout.flush()

    def flush(self):
        if self.buf and not self.buf.startswith('kmon> '):
            sys.stdout.write(self.buf)
        self.buf = ''
        sys.stdout.flush()


def cmd_run(name, args, seconds=None):
    """Run a program on the board through the kernel monitor on the SWD console; its output
    comes here, Ctrl-C ends it. Returns its exit code."""
    for a in args:
        if not a or any(c in a for c in ' \t'):
            raise Fail('arguments with spaces cannot be passed to the kernel monitor')
    remote = program_path(name)
    if remote.startswith('/sd/crtos/'):
        local = os.path.join(sdcard_dir(), remote[len('/sd/'):])
        if os.path.exists(local) and load_manifest().get(remote[len('/sd/'):]) != '%08x' % file_crc(local):
            say('note: %s was built after the last "crtos deploy" - the board runs the old one' %
                os.path.basename(local))
    swdcon, con = swd_console()
    line = ' '.join(['run', '-w', remote] + list(args))
    out = _OutputFilter([line, 'started %s as pid' % remote])
    text = ''
    try:
        con.write(line.encode() + b'\r')
        try:
            ok, data = con.until(swdcon.PROMPT, seconds if seconds else 1e9, stream=out.feed)
            text = data.decode('utf-8', 'replace')
            if not ok:
                con.write(b'\x03')
                _, data = con.until(swdcon.PROMPT, 5.0, stream=out.feed)
                text += data.decode('utf-8', 'replace')
        except KeyboardInterrupt:
            con.write(b'\x03')
            _, data = con.until(swdcon.PROMPT, 5.0, stream=out.feed)
            text += data.decode('utf-8', 'replace')
    finally:
        out.flush()
        con.close()
    m = re.search(r'pid \d+ exited with (-?\d+)', text)
    if m:
        code = int(m.group(1))
        return code if 0 <= code <= 255 else 1      # negative: killed (e.g. -14 after a crash)
    m = re.search(r'run: (\S+): (-?\d+)', text)
    if m:
        raise Fail('%s could not be started (error %s) - is it on the card? ("crtos deploy")' % (m.group(1), m.group(2)))
    return 1


def cmd_kmon(cmds, serial=False, reset=False, timeout=60.0, out=None):
    if serial:
        if not cmds:
            return cmd_serial()
        return 0 if kmon_serial(cmds, reset, timeout, out=out) else 1
    swdcon, con = swd_console()
    try:
        if cmds:
            ok = True
            for c in cmds:
                done, _ = con.command(c, timeout, stream=lambda s: (sys.stdout.write(s), sys.stdout.flush()))
                ok = ok and done
            say()
            return 0 if ok else 1
        return kmon_interactive(con)
    finally:
        con.close()


def kmon_interactive(con):
    """Type monitor commands; the board's output shows up as it comes"""
    import threading
    lock = threading.Lock()
    stop = threading.Event()
    last_output = [time.time()]

    def reader():
        while not stop.is_set():
            try:
                with lock:
                    data = con.read()
            except Exception as e:
                sys.stdout.write('\n[crtos: %s]\n' % e)
                stop.set()
                break
            if data:
                last_output[0] = time.time()
                sys.stdout.write(data.decode('utf-8', 'replace'))
                sys.stdout.flush()
            else:
                time.sleep(0.03)

    say('kernel monitor on the SWD console: "help" lists the commands; Ctrl-C stops a running program, '
        'twice ends')
    th = threading.Thread(target=reader, daemon=True)
    th.start()
    with lock:
        con.write(b'\r')
    last_int = 0.0
    while not stop.is_set():
        try:
            line = sys.stdin.readline()
            if not line:
                # end of the input (commands from a pipe or a file): let the output finish
                t0 = time.time()
                while time.time() - max(last_output[0], t0) < 0.6 and time.time() - t0 < 20 and not stop.is_set():
                    time.sleep(0.05)
                break
            with lock:
                con.write(line.rstrip('\r\n').encode() + b'\r')
        except KeyboardInterrupt:
            if time.time() - last_int < 2.0:
                break
            last_int = time.time()
            with lock:
                con.write(b'\x03')
    stop.set()
    th.join(1.0)
    say()
    return 0


def cmd_shot(path=None):
    swdcon = swdcon_module()
    need_module('PIL', 'pillow')
    path = path or time.strftime('screenshot-%Y%m%d-%H%M%S.png')
    try:
        swdcon.screenshot(kernel_axf(), path)
    except RuntimeError as e:
        raise Fail(str(e))


BENCH_UNITS_LOWER_BETTER = ('ns', 'us', 'ms', 'cycles')


def bench_dir():
    return os.path.join(build_dir(), 'bench')


def bench_collect(con, gfx=True):
    """Run the measurements on the board; returns {key: (value, unit, what)}"""
    res = {}

    def run(cmd, timeout=300.0):
        say('  %s' % cmd)
        ok, text = con.command(cmd, timeout)
        if not ok:
            say('    (did not finish)')
        return text

    # the bench program: system calls, switching, IPC, memory, SD card, starting programs
    text = run('run -w /sd/crtos/bin/bench.app')
    if 'run: ' in text and 'bench:' not in text:
        raise Fail('the board has no bench.app - "crtos build" and "crtos deploy" first')
    for m in re.finditer(r'bench: (\S+)\s+([\d.]+) (\S+)\s+(.*)', text):
        name, value, unit, what = m.group(1), float(m.group(2)), m.group(3), m.group(4).strip()
        key = name if name not in ('memcpy', 'memset') else '%s %s' % (name, what.split()[0] + 'K')
        res[key] = (value, unit, what)
    # the kernel's own tests: switching between kernel threads, interrupts, the heap
    text = run('test sem')
    m = re.search(r'(\d+) cycles per switch', text)
    if m:
        res['k-switch'] = (float(m.group(1)), 'cycles', 'kernel threads: switch incl. semaphore operations')
    text = run('test irq')
    m = re.search(r'pend -> handler: min (\d+) avg (\d+) max (\d+) cycles', text)
    if m:
        res['irq-avg'] = (float(m.group(2)), 'cycles', 'interrupt latency, average')
        res['irq-max'] = (float(m.group(3)), 'cycles', 'interrupt latency, worst')
    text = run('test heap')
    m = re.search(r'kmalloc avg (\d+) cycles, kfree avg (\d+) cycles', text)
    if m:
        res['kmalloc'] = (float(m.group(1)), 'cycles', 'kernel heap: allocate')
        res['kfree'] = (float(m.group(2)), 'cycles', 'kernel heap: free')
    # start-up (from the kernel log, while it still holds the start)
    text = run('dmesg 64000')
    m = re.search(r'boot: done in (\d+) ms', text)
    if m:
        res['boot'] = (float(m.group(1)), 'ms', 'kernel start to drivers loaded')
    m = re.search(r'\[\s*(\d+\.\d+)\] init started', text)
    if m:
        res['init'] = (float(m.group(1)) * 1000.0, 'ms', 'kernel start to the first process')
    # graphics: an animated window, alone and with another window being drawn in
    if gfx:
        text = run('run -w /sd/crtos/bin/gfxinfo.app -b 2', 60.0)
        for m in re.finditer(r'bench (.+?): (\d+\.\d) fps.*?\((\d+) us each\)', text):
            what = m.group(1).strip()
            res['fps ' + what.split()[0]] = (float(m.group(2)), 'fps', 'animation: ' + what)
            res['compose ' + what.split()[0]] = (float(m.group(3)), 'us', 'gfxd: composing a frame, ' + what)
    return res


def bench_print(res, old=None):
    if old is not None:
        say('%-12s %12s %12s %8s   %s' % ('', 'before', 'now', 'change', ''))
    for key in sorted(res, key=lambda k: list(res).index(k)):
        value, unit, what = res[key]
        if old is not None and key in old:
            ov = old[key][0]
            ch = (value - ov) / ov * 100.0 if ov else 0.0
            better = (ch < 0) == (unit in BENCH_UNITS_LOWER_BETTER)
            mark = '' if abs(ch) < 3 else (' better' if better else ' WORSE')
            say('%-12s %9.1f %-3s %8.1f %-3s %+7.0f%%%s  %s' % (key, ov, unit[:3], value, unit[:3], ch, mark, what))
        else:
            say('%-12s %9.1f %-6s %s' % (key, value, unit, what))


def cmd_bench(save=None, compare=None, gfx=True):
    """Measure the board and show the results; @save keeps them under a name, @compare shows
    them next to a saved run"""
    old = None
    if compare:
        path = os.path.join(bench_dir(), compare + '.json')
        if not os.path.exists(path):
            raise Fail('no saved results "%s" (%s)' % (compare, path))
        old = {k: tuple(v) for k, v in json.load(open(path, encoding='utf-8'))['results'].items()}
    swdcon, con = swd_console()
    try:
        say('bench: measuring on the board (about a minute)')
        res = bench_collect(con, gfx)
    finally:
        con.close()
    say()
    bench_print(res, old)
    if save:
        os.makedirs(bench_dir(), exist_ok=True)
        path = os.path.join(bench_dir(), save + '.json')
        with open(path, 'w', encoding='utf-8') as f:
            json.dump({'date': time.strftime('%Y-%m-%d %H:%M:%S'), 'results': res}, f, indent=1)
        say('bench: saved as "%s" (%s)' % (save, rel(path)))
    return 0


def wait_restarted(swdcon, t_reset, timeout=20.0):
    """After a reset: (True, uptime) once the kernel answers with an uptime that started after
    the reset, (False, uptime) if it answers with an older one, (None, None) if it is silent"""
    up = None
    while time.time() - t_reset < timeout:
        up = swdcon.board_uptime(kernel_axf())
        if up is not None:
            return up < time.time() - t_reset + 1.0, up
        time.sleep(0.5)
    return None, None


def cmd_reboot(how='auto', host=None):
    if how in ('auto', 'swd'):
        try:
            swdcon = swdcon_module()
            swdcon.find_probe()
        except Exception as e:
            if how == 'swd':
                raise Fail(str(e))
            say('reboot: no debug probe (%s), asking over the network' % e)
        else:
            # the reset line first (also restarts a system that hangs), then the system reset the
            # kernel itself uses; each is checked with the kernel's uptime
            for method in ('hw', 'sysresetreq'):
                t0 = time.time()
                try:
                    swdcon.reset(method=method)
                except Exception as e:
                    say('reboot: %s reset failed: %s' % (method, e))
                    continue
                restarted, up = wait_restarted(swdcon, t0)
                if restarted:
                    say('reboot: done, the kernel is up again (%.1f s since the reset)' % up)
                    return
                if restarted is None:
                    raise Fail('the board was reset, but CRTOS does not answer on the SWD console '
                               '("crtos log --reset" shows what it prints while starting)')
                say('reboot: the %s reset did not restart the board (up %.0f s)' % (method, up))
            if how == 'swd':
                raise Fail('the board did not restart')
            say('reboot: asking over the network')
    netdeploy = netdeploy_module()
    try:
        if not netdeploy.Net(host).reboot():
            raise Fail('the board did not come back')
    except (netdeploy.DeployError, OSError) as e:
        raise Fail('reboot: %s' % e)


REPORT_RE = re.compile(r"\*\*\* ([^\n]*)\n\s*task (\d+) '([^']*)'(?: \(process (\d+) '([^']*)', (?:arena|text) ([0-9a-f]{8})"
                       r"(?:, fast ([0-9a-f]{8}))?[^)]*\))?"
                       r"(?:(?!\*\*\*).)*?pc=([0-9a-f]{8}) lr=([0-9a-f]{8})", re.S)


def find_debug_file(name):
    """The unstripped program or module (build/.../<name>.debug.app or .debug.ko)"""
    for ext in ('app', 'ko'):
        found = glob.glob(os.path.join(build_dir(), '**', '%s.debug.%s' % (name, ext)), recursive=True)
        if found:
            return found[0]
    return None


def cmd_crash():
    """The last crash report in the kernel log, with its addresses as source lines"""
    swdcon, con = swd_console()
    try:
        _, text = con.command('dmesg 16000', 30.0)
    finally:
        con.close()
    text = re.sub(r'\[\s*\d+\.\d+\] ', '', text.replace('\r', ''))
    reports = list(REPORT_RE.finditer(text))
    if not reports:
        say('crash: no crash report in the kernel log (since the last start)')
        return 0
    m = reports[-1]
    reason, task, tname, pid, pname, arena, fast, pc, lr = m.groups()
    say(text[m.start():m.end()].rstrip())
    say()
    env = dict(os.environ, CRTOS_GCC_BIN=gcc_bin() or '')
    if arena:
        dbg = find_debug_file(pname)
        if not dbg:
            raise Fail('no build/.../%s.debug.app: build the program ("crtos build %s")' % (pname, pname))
        say('in %s (loaded at %s):' % (rel(dbg), arena))
        subprocess.run([sys.executable, os.path.join(TOOLS, 'appsym.py'), dbg, arena] +
                       (['--fast', fast] if fast else []) + [pc, lr], env=env)
        return 0
    if 0x60000000 <= int(pc, 16) < 0x64000000 and kernel_axf():
        say('in the kernel (%s):' % rel(kernel_axf()))
        tool = os.path.join(gcc_bin() or '', 'arm-none-eabi-addr2line')
        subprocess.run([tool, '-f', '-C', '-i', '-p', '-e', kernel_axf(), pc, lr])
        return 0
    say('pc %s is outside the kernel image: in a driver module? "crtos kmon lsmod" shows where the modules are, '
        'then: python tools/appsym.py build/drivers/<module>.debug.ko <address> %s %s' % (pc, pc, lr))
    return 0


def cmd_find():
    boards = netdeploy_module().find()
    for ip, mac in sorted(boards.items()):
        say('%s  %s' % (ip, mac))
    if not boards:
        raise Fail('no board answered (is it on the same network? is deployd running?)')


# ---- new programs ----------------------------------------------------------------------------

def cmd_new(name, kind='gui', title=None):
    if not re.fullmatch(r'[a-z][a-z0-9_]{0,30}', name):
        raise Fail('a program name: small letters, digits and "_", starting with a letter (e.g. mytool)')
    tdir = os.path.join(templates_dir(), kind)
    src_dir, card_dir = KINDS[kind]
    title = title or name.replace('_', ' ').capitalize()
    if IN_TREE:
        for d in [s for s, _ in PROGRAM_DIRS] + ['drivers']:
            if os.path.exists(os.path.join(ROOT, d, name)):
                raise Fail('%s/%s exists already' % (d, name))
        dest = os.path.join(ROOT, src_dir, name)
    else:
        dest = os.path.abspath(name)
        if os.path.exists(dest):
            raise Fail('%s exists already' % dest)
    subst = {'@NAME@': name, '@TITLE@': title, '@DEST@': card_dir}
    os.makedirs(dest)
    for fn in sorted(os.listdir(tdir)):
        if fn.endswith('.png'):         # the icon (gui): as it is
            shutil.copyfile(os.path.join(tdir, fn), os.path.join(dest, fn))
            continue
        with open(os.path.join(tdir, fn), encoding='utf-8') as f:
            text = f.read()
        if fn == 'CMakeLists.txt' and not IN_TREE:
            with open(os.path.join(templates_dir(), 'sdk-project.cmake'), encoding='utf-8') as f:
                text = f.read() + '\n' + text
        for k, v in subst.items():
            text = text.replace(k, v)
        out = fn.replace('app.', name + '.', 1) if fn.startswith('app.') else fn
        with open(os.path.join(dest, out), 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
    say('new: %s' % rel(dest))
    for fn in sorted(os.listdir(dest)):
        say('       %s' % fn)
    remote = '/sd/crtos/%s/%s.app' % (card_dir, name)
    if kind == 'gui' and IN_TREE:
        cfg = os.path.join(ROOT, 'rootfs', 'etc', 'launcher.cfg')
        with open(cfg, 'a', encoding='utf-8', newline='\n') as f:
            f.write('%s: %s\n' % (title, remote))
        say('     programs menu: "%s" added to %s' % (title, rel(cfg)))
    if kind == 'service':
        say('     to start it at boot add to %s:' % ('rootfs/etc/init.cfg' if IN_TREE else '/sd/crtos/etc/init.cfg'))
        say('         service %s respawn %s' % (name, remote))
    say('next:  crtos build%s   crtos deploy   crtos run %s' % ('' if IN_TREE else ' (in %s)' % name, name))


# ---- checks and installation -----------------------------------------------------------------

def _check_rows():
    """[(state, what, detail)]: state 'ok', 'missing' (needed) or 'optional'"""
    rows = []
    v = sys.version_info
    rows.append(('ok' if v >= (3, 8) else 'missing', 'Python %d.%d.%d' % v[:3], sys.executable))
    exe = cmake_path()
    ver = tool_version([exe, '--version']) if exe else None
    rows.append(('ok' if ver and natural(ver) >= natural('3.20') else 'missing', 'CMake %s' % (ver or ''),
                 exe or 'not found (3.20 or newer)'))
    exe = ninja_path()
    rows.append(('ok' if exe else 'missing', 'Ninja %s' % (tool_version([exe, '--version']) if exe else ''),
                 exe or 'not found'))
    d = gcc_bin()
    ver = tool_version([os.path.join(d, 'arm-none-eabi-gcc'), '-dumpversion']) if d else None
    rows.append(('ok' if ver else 'missing', 'Arm GCC %s' % (ver or ''), d or 'arm-none-eabi-gcc not found'))
    if IN_TREE:
        # the build fetches third_party/ (third_party/sources.txt) with git
        exe = shutil.which('git')
        ver = tool_version([exe, '--version']) if exe else None
        rows.append(('ok' if ver else 'missing', 'Git %s' % (ver or ''),
                     exe or 'not found (the build fetches third_party/ with it)'))
    for mod, pkg, what in (('pyocd', 'pyocd', 'debug probe'), ('serial', 'pyserial', 'serial console'),
                           ('PIL', 'pillow', 'screenshots, desktop scaling')):
        try:
            m = __import__(mod)
            rows.append(('ok', '%s %s' % (pkg, getattr(m, '__version__', '')), what))
        except ImportError:
            rows.append(('missing', pkg, 'not installed (%s)' % what))
    return rows


def _board_rows():
    rows = []
    try:
        swdcon = swdcon_module()
        probe = swdcon.find_probe()
        rows.append(('ok', 'debug probe', probe))
    except Exception as e:
        rows.append(('optional', 'debug probe', str(e)))
        probe = None
    try:
        rows.append(('ok', 'serial port', serial_port()))
    except Fail as e:
        rows.append(('optional', 'serial port', str(e)))
    if probe:
        try:
            con = swdcon.SwdCon(kernel_axf())
            try:
                rows.append(('ok' if con.sync() else 'optional', 'CRTOS on the board',
                             'SWD console at %08x' % con.base))
            finally:
                con.close()
        except Exception as e:
            rows.append(('optional', 'CRTOS on the board', str(e)))
    netdeploy = netdeploy_module()
    net = netdeploy.Net()
    if os.path.exists(net.token_path):
        try:
            rows.append(('ok', 'network deploy', net.resolve()))
        except (netdeploy.DeployError, OSError) as e:
            rows.append(('optional', 'network deploy', str(e)))
    else:
        rows.append(('optional', 'network deploy', 'not set up yet (the first "crtos deploy" does it)'))
    return rows


def _netsurf_rows():
    rows = []
    if not IN_TREE:
        return rows
    have = os.path.isdir(os.path.join(ROOT, 'third_party', 'netsurf', 'content'))
    rows.append(('ok' if have else 'optional', 'NetSurf sources',
                 'third_party/netsurf*' if have else 'not fetched ("crtos setup --netsurf")'))
    if have:
        import netsurf_prepare
        for name in ('perl', 'gperf', 'gcc'):
            p = netsurf_prepare.find_host_tool(name)
            rows.append(('ok' if p else 'optional', 'host %s (NetSurf)' % name, p or 'not found'))
    return rows


def print_rows(rows):
    mark = {'ok': ' ok ', 'missing': ' !! ', 'optional': ' -- '}
    for state, what, detail in rows:
        say('%s %-22s %s' % (mark[state], what, detail))


def cmd_doctor():
    say('Tools:')
    rows = _check_rows()
    print_rows(rows)
    missing = [w for s, w, _ in rows if s == 'missing']
    if IN_TREE:
        ide = mcux_ide()
        print_rows([('ok' if ide else 'optional', 'MCUXpresso IDE', ide or 'not found (optional)')])
    say('Board:')
    print_rows(_board_rows())
    ns = _netsurf_rows()
    if ns:
        say('Web browser (optional):')
        print_rows(ns)
    say()
    if missing:
        say('missing: %s - "crtos setup" installs them' % ', '.join(missing))
        return 1
    say('everything needed for building is there' + ('' if IN_TREE else ' ("crtos new NAME" starts a program)'))
    return 0


def _pip_install(packages):
    say('setup: pip install %s' % ' '.join(packages))
    return subprocess.call([sys.executable, '-m', 'pip', 'install', '--upgrade', '--disable-pip-version-check']
                           + packages) == 0


def _winget_install(package):
    if not WINDOWS or not shutil.which('winget'):
        return False
    say('setup: winget install %s' % package)
    return subprocess.call(['winget', 'install', '-e', '--id', package, '--accept-package-agreements',
                            '--accept-source-agreements']) == 0


def cmd_setup(netsurf=False):
    ok = _pip_install(['-r', os.path.join(TOOLS, 'requirements.txt')])
    extra = []
    if not cmake_path():
        extra.append('cmake')
    if not ninja_path():
        extra.append('ninja')
    if extra:
        ok = _pip_install(extra) and ok
    if not gcc_bin():
        if not _winget_install('Arm.GnuArmEmbeddedToolchain'):
            say('setup: install the Arm GNU Toolchain (arm-none-eabi) yourself:')
            say('         https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads')
            say('         (Debian/Ubuntu: sudo apt install gcc-arm-none-eabi; macOS: brew install --cask gcc-arm-embedded)')
            ok = False
    if netsurf and IN_TREE:
        import netsurf_prepare
        if not netsurf_prepare.find_host_tool('gperf') and not _winget_install('oss-winget.gperf'):
            say('setup: NetSurf needs gperf (Debian/Ubuntu: sudo apt install gperf)')
        if not netsurf_prepare.find_host_tool('gcc') and not _winget_install('BrechtSanders.WinLibs.POSIX.UCRT'):
            say('setup: NetSurf needs a C compiler for the build computer (Debian/Ubuntu: sudo apt install gcc)')
    if IN_TREE:
        # the third-party sources (the build would fetch the base ones anyway)
        say('setup: third-party sources (third_party/sources.txt)')
        if not shutil.which('git') and not _winget_install('Git.Git'):
            say('setup: install Git yourself (https://git-scm.com; Debian/Ubuntu: sudo apt install git)')
            ok = False
        elif subprocess.call([sys.executable, os.path.join(TOOLS, 'thirdparty.py')] + (['netsurf'] if netsurf else [])):
            ok = False
    say()
    rc = cmd_doctor()
    if not ok or rc:
        raise Fail('setup is not complete (see above)')


# ---- the old command forms -------------------------------------------------------------------

def legacy_swd(action, args, full=False, timeout=60.0):
    if action == 'kmon':
        return cmd_kmon(args, timeout=timeout)
    if action == 'put':
        if len(args) != 2:
            raise Fail('usage: crtos swd put LOCAL REMOTE')
        return cmd_put(args[0], args[1], 'swd')
    if action == 'shot':
        return cmd_shot(args[0] if args else None)
    if action == 'deploy':
        return cmd_deploy('swd', full)


def legacy_net(action, args, host=None, full=False):
    if action == 'find':
        return cmd_find()
    if action == 'setup':
        tok = token_path(create=True)
        say('net setup: installing %s as %s' % (tok, TOKEN_REMOTE))
        return upload([(tok, TOKEN_REMOTE)], 'swd')
    if action == 'put':
        if len(args) != 2:
            raise Fail('usage: crtos net put LOCAL REMOTE')
        return cmd_put(args[0], args[1], 'net', host)
    if action == 'deploy':
        return cmd_deploy('net', full, host)
    if action == 'reboot':
        return cmd_reboot('net', host)


# ---- command line ----------------------------------------------------------------------------

def main():
    try:
        sys.stdout.reconfigure(errors='replace', line_buffering=True)   # progress shows as it happens
    except AttributeError:
        pass
    ap = argparse.ArgumentParser(prog='crtos', description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', metavar='COMMAND')

    def cmd(name, text):
        return sub.add_parser(name, help=text, description=text)

    cmd('doctor', 'check the tools, the debug probe and the board')
    p = cmd('setup', 'install what is missing')
    p.add_argument('--netsurf', action='store_true', help='also fetch the NetSurf web browser and its tools')
    p = cmd('build', 'build everything, or the named parts (kernel, program names)')
    p.add_argument('targets', nargs='*', metavar='TARGET')
    p.add_argument('-j', '--jobs', type=int)
    p.add_argument('-v', '--verbose', action='store_true', help='show the compiler command lines')
    p.add_argument('--fresh', action='store_true', help='configure again from scratch')
    p.add_argument('--base', action='store_true',
                   help='only the base system: kernel, drivers and system/ (no apps/, tests/, examples/)')
    p.add_argument('--rtos', nargs='?', const=RTOS_DEFAULT_APP, metavar='DIR',
                   help='the RTOS alone (the kernel\'s core) with the application in DIR (default: %s), '
                        'in build/rtos' % RTOS_DEFAULT_APP)
    p = cmd('clean', 'remove the build results')
    p.add_argument('--all', action='store_true', help='remove the whole build directory')
    p = cmd('flash', 'write the kernel into the board\'s flash memory')
    p.add_argument('file', nargs='?', help='image (.bin or .axf; default: build/kernel/crtos.bin)')
    p.add_argument('--net', action='store_true', help='over the network, through the running system (no probe)')
    p.add_argument('--host', help='(--net) the board\'s IP address')
    p.add_argument('--rtos', action='store_true', help='the RTOS build (build/rtos/kernel/crtos.bin, "crtos build --rtos")')
    p = cmd('sdcard', 'copy the system files to an SD card in this computer (only its /crtos)')
    p.add_argument('drive', help='the card: a drive letter (E:) or a directory')
    p.add_argument('--full', action='store_true', help='copy every file, also unchanged ones')
    p = cmd('deploy', 'send the changed system files to the board')
    p.add_argument('--full', action='store_true', help='send every file')
    g = p.add_mutually_exclusive_group()
    g.add_argument('--net', dest='how', action='store_const', const='net', help='only over the network')
    g.add_argument('--swd', dest='how', action='store_const', const='swd', help='only through the debug probe')
    g.add_argument('--serial', dest='how', action='store_const', const='serial', help='over the serial console (slow)')
    p.add_argument('--host', help='the board\'s IP address')
    p.add_argument('-n', '--dry-run', action='store_true', help='only list what would be sent')
    p = cmd('run', 'run a program on the board and show its output')
    p.add_argument('name', help='program name (hello, paint...) or path on the board')
    p.add_argument('args', nargs=argparse.REMAINDER)
    p.add_argument('--time', type=float, help='end it after this many seconds')
    p = cmd('new', 'a new program from a template')
    p.add_argument('name')
    g = p.add_mutually_exclusive_group()
    g.add_argument('--gui', dest='kind', action='store_const', const='gui', help='with a window (default)')
    g.add_argument('--console', dest='kind', action='store_const', const='console', help='a command-line program')
    g.add_argument('--service', dest='kind', action='store_const', const='service', help='a background service')
    p.add_argument('--title', help='name in the programs menu')
    p = cmd('kmon', 'the kernel monitor (interactive without commands)')
    p.add_argument('commands', nargs='*')
    p.add_argument('--serial', action='store_true', help='on the serial console instead of the debug probe')
    p.add_argument('--reset', action='store_true', help='(--serial) restart the board first')
    p.add_argument('--timeout', type=float, default=60, help='seconds per command')
    p.add_argument('--out', help='(--serial) save the output to a file')
    cmd('serial', 'terminal on the board\'s serial console')
    p = cmd('log', 'show the serial console output (Ctrl-C ends)')
    p.add_argument('--reset', action='store_true', help='restart the board first')
    p.add_argument('--time', type=float, help='stop after this many seconds')
    p.add_argument('--out', help='also save it to a file')
    p.add_argument('--until', help='stop when this text shows up')
    p = cmd('shot', 'screenshot of the display')
    p.add_argument('file', nargs='?')
    p = cmd('reboot', 'restart the board')
    p.add_argument('--host')
    p = cmd('scp', 'copy files between this computer and the board (a board path: board:/path)')
    p.add_argument('paths', nargs='+', metavar='PATH', help='SRC... DST; e.g. notes.txt board:/sd/crtos/home/, '
                                                            'board:/sd/crtos/var/log.txt .')
    p.add_argument('-r', '--recursive', action='store_true', help='directories with what they hold')
    p.add_argument('--host', help="the board's IP address")
    p = cmd('desktop', "the board's screen in a window of this computer (remote desktop, VNC on port 5900)")
    p.add_argument('--zoom', type=int, help="a window this many times the board's screen (default: maximised, "
                                            "the picture as large as fits)")
    p.add_argument('--fullscreen', action='store_true', help='start in full screen (F11 switches it)')
    p.add_argument('--password', action='store_true', help='only show the address and the password (other VNC viewers)')
    p.add_argument('--shot', metavar='FILE.png', help='only save one picture of the screen')
    p.add_argument('--stats', action='store_true', help='print updates and frames drawn per second')
    p.add_argument('--host', help="the board's IP address")
    p = cmd('wallpaper', "a picture of this computer as the board's wallpaper (any format Pillow reads)")
    p.add_argument('picture', metavar='PICTURE')
    p.add_argument('--name', help='its name on the board (default: the file name)')
    p.add_argument('--size', default='800x480', metavar='WxH', help='made smaller to cover this (default 800x480)')
    p.add_argument('--fit', choices=FITS, default='fill', help='how it covers the screen (default fill)')
    p.add_argument('--no-set', dest='set_it', action='store_false', help='only put it on the card')
    p.add_argument('--host', help="the board's IP address")
    p = cmd('netbench', "the network's throughput both ways, TCP and UDP (the board runs nettest)")
    p.add_argument('--time', type=int, default=5, metavar='S', help='seconds per test (default 5)')
    p.add_argument('--only', metavar='TESTS', help='some of: tcp-rx,tcp-tx,udp-rx,udp-tx (rx: to the board)')
    p.add_argument('--rate', type=float, default=95.0, metavar='MBIT',
                   help='how fast UDP is sent to the board (default 95 Mbit/s)')
    p.add_argument('--host', help="the board's IP address")
    cmd('find', 'boards on the local network')
    cmd('crash', 'the last crash report of the board, with source lines')
    p = cmd('bench', 'measure the board: system calls, switching, IPC, memory, SD card, start-up, graphics')
    p.add_argument('--save', metavar='NAME', help='keep the results under this name (build/bench)')
    p.add_argument('--compare', metavar='NAME', help='show them next to results saved earlier')
    p.add_argument('--no-gfx', dest='gfx', action='store_false', help='without the graphics test (it opens windows)')
    p = cmd('src', "the programs' sources with Makefiles to /sd/crtos/src, to build them on the board")
    p.add_argument('--host')
    p.add_argument('--full', action='store_true', help='send every file again')
    p.add_argument('--dry-run', action='store_true', help='only list what would be sent')
    p.add_argument('--check', action='store_true', help='first compile every source here as the board would')
    p = cmd('sdk', 'export the SDK for programs built outside this tree')
    p.add_argument('dir', nargs='?', help='also copy it there')
    p = cmd('toolchain', 'arm-crtos-gcc & co. for this computer (native: the compiler for the board, '
                         'built in WSL/Linux; install: onto the board)')
    p.add_argument('action', nargs='?', default='build', choices=['build', 'native', 'install'])
    p.add_argument('steps', nargs='*', help='native: only these steps of toolchain/native/build.sh '
                                            '(fetch cross native install)')
    p.add_argument('--host')
    p.add_argument('--full', action='store_true', help='install: send every file again')
    p.add_argument('--dry-run', action='store_true', help='install: only list what would be sent')
    p = cmd('kbuild', 'build the kernel with MCUXpresso IDE (Release/)')
    p.add_argument('--config', default='Release')
    p = cmd('put', 'upload one file to the board')
    p.add_argument('local')
    p.add_argument('remote', help='e.g. /sd/crtos/etc/network.cfg')
    p.add_argument('--host')
    p = cmd('swd', 'through the debug probe only: kmon, deploy, put, shot')
    p.add_argument('action', choices=['kmon', 'deploy', 'put', 'shot'])
    p.add_argument('args', nargs='*')
    p.add_argument('--full', action='store_true')
    p.add_argument('--timeout', type=float, default=60)
    p = cmd('net', 'over the network only: setup, deploy, put, reboot, find')
    p.add_argument('action', choices=['setup', 'deploy', 'put', 'reboot', 'find'])
    p.add_argument('args', nargs='*')
    p.add_argument('--full', action='store_true')
    p.add_argument('--host')
    a = ap.parse_args()
    if not a.cmd:
        ap.print_help()
        return 0

    if not IN_TREE and a.cmd in ('kbuild', 'sdk', 'flash'):
        raise Fail('"%s" works in the CRTOS source tree, not in the SDK' % a.cmd)
    c = a.cmd
    if c == 'doctor':
        return cmd_doctor()
    if c == 'setup':
        return cmd_setup(a.netsurf)
    if c == 'build':
        return cmd_build(a.targets, a.jobs, a.verbose, a.fresh, a.base, a.rtos)
    if c == 'clean':
        return cmd_clean(a.all)
    if c == 'flash':
        return cmd_flash(a.file, a.net, a.host, a.rtos)
    if c == 'sdcard':
        return cmd_sdcard(a.drive, a.full)
    if c == 'deploy':
        return cmd_deploy(a.how or 'auto', a.full, a.host, a.dry_run)
    if c == 'run':
        return cmd_run(a.name, a.args, a.time)
    if c == 'new':
        return cmd_new(a.name, a.kind or 'gui', a.title)
    if c == 'kmon':
        return cmd_kmon(a.commands, a.serial, a.reset, a.timeout, a.out)
    if c == 'serial':
        return cmd_serial()
    if c == 'log':
        cmd_log(a.time, a.reset, a.out, a.until)
        return 0
    if c == 'shot':
        return cmd_shot(a.file)
    if c == 'reboot':
        return cmd_reboot('auto', a.host)
    if c == 'scp':
        return cmd_scp(a.paths, a.recursive, a.host)
    if c == 'desktop':
        return cmd_desktop(a.host, a.zoom, a.password, a.shot, a.fullscreen, a.stats)
    if c == 'wallpaper':
        return cmd_wallpaper(a.picture, a.name, a.size, a.fit, a.set_it, a.host)
    if c == 'netbench':
        return cmd_netbench(a.host, max(1, min(60, a.time)), a.only.split(',') if a.only else None, a.rate)
    if c == 'find':
        return cmd_find()
    if c == 'crash':
        return cmd_crash()
    if c == 'bench':
        return cmd_bench(a.save, a.compare, a.gfx)
    if c == 'src':
        return cmd_src(a.host, a.full, a.dry_run, a.check)
    if c == 'sdk':
        return cmd_sdk(a.dir)
    if c == 'kbuild':
        return cmd_kbuild(a.config)
    if c == 'toolchain':
        if a.action == 'native':
            return cmd_toolchain_native(a.steps)
        if a.action == 'install':
            return cmd_toolchain_install(a.host, a.full, a.dry_run)
        return cmd_toolchain(a.action)
    if c == 'put':
        return cmd_put(a.local, a.remote, 'auto', a.host)
    if c == 'swd':
        return legacy_swd(a.action, a.args, a.full, a.timeout)
    if c == 'net':
        return legacy_net(a.action, a.args, a.host, a.full)
    return 0


if __name__ == '__main__':
    try:
        rc = main()
    except Fail as e:
        print('crtos: %s' % e, file=sys.stderr, flush=True)
        rc = 1
    except KeyboardInterrupt:
        print('', flush=True)
        rc = 130
    sys.exit(rc if isinstance(rc, int) else 0)
