#!/bin/bash
# Builds the native CRTOS toolchain (GCC and binutils that run ON the board) in Linux (WSL).
#
#     bash toolchain/native/build.sh [step...]      (or: crtos toolchain native)
#
# Steps (all of them, in this order, when none is given):
#     fetch    download the Arm GNU Toolchain 14.3.Rel1 sources (the same release as the
#              cross compiler on the PC), check the SHA-256, unpack them and apply CRTOS'
#              changes (<repo>/patches/arm-gnu-toolchain/<source directory>/*.patch)
#     cross    stage A: a Linux-hosted arm-none-eabi GCC whose target libraries (newlib, also
#              as newlib-nano, libstdc++, libgcc) are position independent with the data base
#              in r9 - the model of CRTOS programs that run in place from the flash (XIP). (The
#              tree's build makes the same libraries itself for lib/xip: cmake/xiplibs.cmake,
#              from the recipe tools/xiplibs_recipe.py takes from this stage)
#     native   stage B: binutils and GCC built by stage A for the board (host = target =
#              arm-none-eabi), as XIP programs
#     install  the board's files (programs, headers, libraries, specs) into
#              <repo>/build/toolchain/native, as they lie on the board
#
# The work directory (default ~/crtos-toolchain, set CRTOS_TC_WORK) should be on the Linux file
# system: building under /mnt/c is several times slower.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
WORK=${CRTOS_TC_WORK:-$HOME/crtos-toolchain}
JOBS=${CRTOS_TC_JOBS:-$(nproc)}

RELEASE=14.3.rel1
SRC_URL=https://developer.arm.com/-/media/Files/downloads/gnu/$RELEASE/srcrel/arm-gnu-toolchain-src-snapshot-$RELEASE.tar.xz
SRC_SHA256=d8676291e48029ba8814037a8c5daa30728e406d0a1ba89f1081470af9175d69
SRC_TAR=$WORK/download/arm-gnu-toolchain-src-snapshot-$RELEASE.tar.xz
SRC=$WORK/src

CROSS=$WORK/cross                 # stage A, installed
BLD=$WORK/build                   # build directories
# XIP code: every address through the GOT, which lies with the program's data in RAM and whose
# address is in r9 - so code and constants may lie anywhere (in the flash) apart from the data
PIC="-fPIC -msingle-pic-base -mpic-register=r9 -mno-pic-data-is-text-relative"
CPU="--with-cpu=cortex-m7 --with-fpu=fpv5-d16 --with-float=hard --with-mode=thumb"

say() { printf '\n=== %s   [%s]\n' "$*" "$(date +%H:%M:%S)"; }

# configure (once) in $BLD/<name> and stay there
configure_in() {
    local name=$1 src=$2
    shift 2
    mkdir -p "$BLD/$name"
    cd "$BLD/$name"
    if [ ! -f config.status ]; then
        "$src/configure" "$@" > configure.log 2>&1 || { tail -30 configure.log; exit 1; }
    fi
}

# make [targets] with the log in <log>.log; the end of the log on failure
run_make() {
    local log=$1
    shift
    make "$@" > "$log.log" 2>&1 || { tail -40 "$log.log"; exit 1; }
}

step_fetch() {
    mkdir -p "$WORK/download"
    if [ ! -f "$SRC_TAR" ] || ! echo "$SRC_SHA256  $SRC_TAR" | sha256sum -c --status; then
        say "downloading $SRC_URL"
        curl -fL --retry 3 -o "$SRC_TAR.part" "$SRC_URL"
        echo "$SRC_SHA256  $SRC_TAR.part" | sha256sum -c
        mv "$SRC_TAR.part" "$SRC_TAR"
    fi
    if [ ! -f "$SRC/.unpacked" ]; then
        say "unpacking into $SRC"
        rm -rf "$SRC"
        mkdir -p "$SRC"
        tar -xJf "$SRC_TAR" -C "$SRC" --strip-components=1
        touch "$SRC/.unpacked"
    fi
    # CRTOS' changes: <repo>/patches/arm-gnu-toolchain/<source directory>/<name>.patch, each
    # applied once (a stamp)
    local p dir stamp
    for p in "$REPO"/patches/arm-gnu-toolchain/*/*.patch; do
        [ -e "$p" ] || continue
        dir=$(basename "$(dirname "$p")")
        stamp=$SRC/.patched-$dir-$(basename "$p" .patch)
        [ -f "$stamp" ] && continue
        say "patch $dir/$(basename "$p")"
        patch -p1 -d "$SRC/$dir" < "$p"
        touch "$stamp"
    done
    say "sources ready: $(ls "$SRC" | tr '\n' ' ')"
}

step_cross() {
    export PATH=$CROSS/bin:$PATH
    local sysroot=$CROSS/arm-none-eabi
    local tflags="-g -O2 -ffunction-sections -fdata-sections $PIC"
    # GCC's own libraries (gmp, mpfr, mpc) are built along with it
    for l in gmp mpfr mpc; do
        [ -e "$SRC/gcc/$l" ] || ln -s "../$l" "$SRC/gcc/$l"
    done

    say "stage A: binutils"
    configure_in cross-binutils "$SRC/binutils-gdb" --target=arm-none-eabi --prefix="$CROSS" \
        --with-sysroot="$sysroot" --disable-nls --disable-werror --disable-gdb --disable-gdbserver \
        --disable-sim --disable-readline --disable-libdecnumber --disable-gprofng
    run_make make -j"$JOBS"
    run_make install install

    say "stage A: GCC (the compiler)"
    configure_in cross-gcc "$SRC/gcc" --target=arm-none-eabi --prefix="$CROSS" --with-sysroot="$sysroot" \
        --with-native-system-header-dir=/include $CPU --disable-multilib --enable-languages=c,c++ \
        --with-newlib --disable-shared --disable-threads --disable-tls --disable-libssp --disable-libgomp \
        --disable-libquadmath --disable-libstdcxx-pch --disable-libstdcxx-verbose --disable-nls --disable-lto \
        --disable-plugin --without-isl --disable-libcc1 --enable-checking=release --with-gnu-as --with-gnu-ld \
        CFLAGS_FOR_TARGET="$tflags" CXXFLAGS_FOR_TARGET="$tflags"
    run_make make-gcc -j"$JOBS" all-gcc
    run_make install-gcc install-gcc

    say "stage A: newlib (full: 64-bit and C99 printf formats, for the native compiler)"
    configure_in cross-newlib "$SRC/newlib-cygwin" --target=arm-none-eabi --prefix="$CROSS" \
        --disable-newlib-supplied-syscalls --enable-newlib-retargetable-locking \
        --enable-newlib-reent-check-verify --enable-newlib-io-long-long --enable-newlib-io-c99-formats \
        --enable-newlib-register-fini --enable-newlib-mb --disable-nls \
        CFLAGS_FOR_TARGET="$tflags"
    run_make make -j"$JOBS"
    run_make install install

    say "stage A: newlib-nano (for programs)"
    configure_in cross-newlib-nano "$SRC/newlib-cygwin" --target=arm-none-eabi --prefix="$BLD/nano-install" \
        --disable-newlib-supplied-syscalls --enable-newlib-retargetable-locking \
        --enable-newlib-reent-check-verify --enable-newlib-reent-small --disable-newlib-fvwrite-in-streamio \
        --disable-newlib-fseek-optimization --disable-newlib-wide-orient --enable-newlib-nano-malloc \
        --disable-newlib-unbuf-stream-opt --enable-lite-exit --enable-newlib-global-atexit \
        --enable-newlib-nano-formatted-io --disable-nls \
        CFLAGS_FOR_TARGET="-g -Os -ffunction-sections -fdata-sections $PIC"
    run_make make -j"$JOBS"
    run_make install install
    local nano=$BLD/nano-install/arm-none-eabi
    cp "$nano/lib/libc.a" "$sysroot/lib/libc_nano.a"
    cp "$nano/lib/libg.a" "$sysroot/lib/libg_nano.a"
    mkdir -p "$sysroot/include/newlib-nano"
    cp "$nano/include/newlib.h" "$sysroot/include/newlib-nano/newlib.h"

    say "stage A: libgcc and libstdc++"
    cd "$BLD/cross-gcc"
    run_make make-libgcc -j"$JOBS" all-target-libgcc
    run_make install-libgcc install-target-libgcc
    run_make make-libstdcxx -j"$JOBS" all-target-libstdc++-v3
    run_make install-libstdcxx install-target-libstdc++-v3
    cp "$sysroot/lib/libstdc++.a" "$sysroot/lib/libstdc++_nano.a"
    cp "$sysroot/lib/libsupc++.a" "$sysroot/lib/libsupc++_nano.a"
    say "stage A done: GCC $("$CROSS/bin/arm-none-eabi-gcc" -dumpversion), libraries in $sysroot/lib"
}

# ---- stage B: the compiler for the board ------------------------------------------------------
#
# host = target = arm-none-eabi ("crossed native"), built by stage A with crtos.specs -mxip: every
# program is a CRTOS program that runs in place. Where things go on the board:
#   /flash0/gcc/libexec/gcc/arm-none-eabi/14.3.1/   cc1, cc1plus, collect2, as, ld (in place)
#   /flash0/bin/                                    gcc, g++, cpp, ar, nm, ... (.app)
#   /sd/crtos/usr/                                  the prefix: GCC's own include/ and the
#                                                   specs, include/ (CRTOS, libstdc++), the
#                                                   small files (the card)
#   /sd/crtos/usr/arm-none-eabi/                    the sysroot: newlib, the libraries
# The driver's directory is the configured bindir, so the prefixes it derives from where it was
# started (make_relative_prefix) lead to the same places.
TC=${CRTOS_TC_OUT:-$REPO/build/toolchain/arm-crtos}     # the PC toolchain directory (crtos build)
HS=$WORK/host-sysroot                                   # its copy on the Linux file system
DEV_PREFIX=/sd/crtos/usr
DEV_SYSROOT=$DEV_PREFIX/arm-none-eabi
DEV_BINDIR=/flash0/bin
DEV_LIBEXEC=/flash0/gcc/libexec
GCCVER=14.3.1
# uint32_t is unsigned long on arm-none-eabi, and binutils mixes it with unsigned int in its
# function tables (the same in the ABI), which GCC 14 turns into an error
HOSTWARN="-Wno-error=incompatible-pointer-types"

step_native() {
    export PATH=$CROSS/bin:$PATH
    [ -f "$TC/lib/xip/libc.a" ] || { echo "build-native: $TC has no lib/xip (crtos build)" >&2; exit 1; }
    [ -f "$TC/lib/xip/libcrtosheap.a" ] || { echo "build-native: $TC has no libcrtosheap (crtos build)" >&2; exit 1; }
    mkdir -p "$HS"
    cp -r "$TC/lib" "$TC/include" "$HS/"
    # the compilers for the board's programs: stage A with the CRTOS rules, in place
    mkdir -p "$WORK/host-bin"
    for t in gcc g++; do
        cat > "$WORK/host-bin/crtos-$t" <<EOF
#!/bin/sh
exec "$CROSS/bin/arm-none-eabi-$t" -specs="$HS/lib/crtos.specs" -mxip -B"$HS/lib/" -L"$HS/lib/xip" -L"$HS/lib" -isystem "$HS/include" "\$@"
EOF
        chmod +x "$WORK/host-bin/crtos-$t"
    done
    # every program with libcrtosheap's malloc (system/lib/libcrtos/src/heap.c): it fragments far less
    # under GCC than newlib's and grows beyond the arena into shared memory windows; -u pulls
    # it in before anything asks libc for malloc
    local host=(--build=x86_64-pc-linux-gnu --host=arm-none-eabi --target=arm-none-eabi
                CC="$WORK/host-bin/crtos-gcc" CXX="$WORK/host-bin/crtos-g++"
                CFLAGS="-Os -g0 $HOSTWARN" CXXFLAGS="-Os -g0" LDFLAGS="-Wl,-u,malloc -lcrtosheap"
                CC_FOR_BUILD=gcc CXX_FOR_BUILD=g++
                AR=arm-none-eabi-ar RANLIB=arm-none-eabi-ranlib NM=arm-none-eabi-nm
                STRIP=arm-none-eabi-strip OBJCOPY=arm-none-eabi-objcopy OBJDUMP=arm-none-eabi-objdump
                AS=arm-none-eabi-as LD=arm-none-eabi-ld)

    say "stage B: binutils for the board"
    configure_in native-binutils "$SRC/binutils-gdb" "${host[@]}" --prefix=$DEV_PREFIX \
        --with-sysroot=$DEV_SYSROOT --disable-nls --disable-werror --disable-gdb --disable-gdbserver \
        --disable-sim --disable-readline --disable-libdecnumber --disable-gprofng --disable-plugins \
        --disable-shared --enable-static --without-zstd --without-debuginfod
    run_make make -j"$JOBS" all-binutils all-gas all-ld

    say "stage B: GCC for the board (C, C++)"
    configure_in native-gcc "$SRC/gcc" "${host[@]}" --prefix=$DEV_PREFIX --bindir=$DEV_BINDIR \
        --libexecdir=$DEV_LIBEXEC --with-sysroot=$DEV_SYSROOT --with-build-sysroot="$CROSS/arm-none-eabi" \
        --with-native-system-header-dir=/include --with-local-prefix=$DEV_PREFIX \
        $CPU --disable-multilib --enable-languages=c,c++ --with-newlib --disable-shared --disable-threads \
        --disable-tls --disable-lto --disable-plugin --disable-nls --without-isl --without-zstd --disable-libcc1 \
        --disable-libssp --disable-libgomp --disable-libquadmath --enable-checking=release --with-gnu-as \
        --with-gnu-ld --disable-bootstrap --disable-fixincludes --disable-gcov
    run_make make-gcc -j"$JOBS" all-gcc
    say "stage B done: $(ls -l gcc/cc1 gcc/cc1plus | awk '{print $9, $5}' | tr '\n' ' ')"
}

# ---- install: the board's files, into the tree -----------------------------------------------
#
# build/toolchain/native/ mirrors the board ("crtos toolchain install" sends it there):
#   flash0/...        -> /flash0/...        the programs, stripped, with their stack and heap
#   sd/crtos/usr/...  -> /sd/crtos/usr/...  headers, libraries, the specs
#   debug/                                  the programs with symbols (crtos crash, appsym.py)
# The headers and the ordinary (not position-independent) libraries are the PC toolchain's, as
# arm-crtos-gcc uses them, so a program built on the board is the same as one built on the PC.
# Put together on the Linux file system first (Windows' file system under /mnt refuses some
# renames), then copied.
NATIVE_OUT=${CRTOS_NATIVE_OUT:-$REPO/build/toolchain/native}
STAGE=$WORK/install

# program SRC -> DEST, stripped, with its stack and heap in the header (a copy with symbols in
# debug/)
put_prog() {
    local src=$1 dst=$2 stack=$3 heap=$4
    local name
    name=$(basename "$dst" .app)
    cp "$src" "$STAGE/debug/$name.debug.app"
    arm-none-eabi-strip --strip-all -o "$dst" "$src"
    "$WORK/crtos-app" set "$dst" --stack "$stack" --heap "$heap" > /dev/null
    "$WORK/crtos-app" check -q "$dst"
}

step_install() {
    export PATH=$CROSS/bin:$PATH
    local nb=$BLD/native-binutils ng=$BLD/native-gcc/gcc
    [ -f "$ng/cc1plus" ] || { echo "build-native: no stage B yet (step native)" >&2; exit 1; }
    local arm
    arm=$(dirname "$(wslpath -u "$(tr -d '\r\n' < "$TC/lib/gcc-bin.txt")")")
    [ -d "$arm/arm-none-eabi/include" ] || { echo "build-native: no Arm GNU Toolchain at $arm" >&2; exit 1; }
    local ml=thumb/v7e-m+dp/hard            # the PC toolchain's variant for the Cortex-M7
    local fl=$STAGE/flash0 usr=$STAGE/sd/crtos/usr
    local lx=$fl/gcc/libexec/gcc/arm-none-eabi/$GCCVER
    local gl=$usr/lib/gcc/arm-none-eabi/$GCCVER
    local sr=$usr/arm-none-eabi
    chmod -R u+w "$STAGE" 2> /dev/null || true; rm -rf "$STAGE"
    mkdir -p "$lx" "$fl/bin" "$gl" "$sr/lib/xip" "$usr/include" "$STAGE/debug"
    gcc -O2 -o "$WORK/crtos-app" "$REPO/toolchain/crtos-app.c"

    say "install: programs"
    # the compiler proper, where the driver looks for it (in place from the flash). Memory: C
    # takes a few MB (sh.c at -O2: about 5, NES' nes_fast.c at -O3 about 18), C++ with the
    # library's headers more (<string> alone about 11). The arena holds little more than data
    # and bss (2 MB) and the 1 MB stack: 3 MB in 512 KB subregions, so the kernel puts it at an
    # end of the free memory instead of splitting it. The heap is libcrtosheap's shared
    # memory window: one object of nearly all the free memory, covered by up to three MPU
    # regions
    put_prog "$ng/cc1" "$lx/cc1" 1M 64K
    put_prog "$ng/cc1plus" "$lx/cc1plus" 1M 64K
    put_prog "$ng/collect2" "$lx/collect2" 64K 1M
    put_prog "$nb/gas/as-new" "$lx/as" 64K 4M
    put_prog "$nb/ld/ld-new" "$lx/ld" 128K 6M
    # the commands. The driver lives while cc1plus runs and takes about 100 KB of heap: small,
    # so that it (and make, a shell) leave cc1plus' arena and windows the memory
    put_prog "$ng/xgcc" "$fl/bin/gcc.app" 64K 256K
    put_prog "$ng/xg++" "$fl/bin/g++.app" 64K 256K
    put_prog "$ng/cpp" "$fl/bin/cpp.app" 64K 256K
    local t
    for t in ar ranlib nm-new:nm objcopy objdump strip-new:strip size readelf addr2line strings; do
        put_prog "$nb/binutils/${t%%:*}" "$fl/bin/${t##*:}.app" 64K 4M
    done

    say "install: headers and libraries"
    # GCC's own headers, and the specs: crtos.specs with the board's paths
    cp -r "$arm/lib/gcc/arm-none-eabi/$GCCVER/include" "$gl/"
    arm-none-eabi-strip --strip-debug -D -o "$gl/libgcc.a" "$arm/lib/gcc/arm-none-eabi/$GCCVER/$ml/libgcc.a"
    python3 "$HERE/native-specs.py" "$REPO/toolchain/crtos.specs" "$arm/arm-none-eabi/lib/nano.specs" "$gl/specs"
    # the sysroot: newlib's headers, libstdc++'s (the header of its variant where a
    # single-variant compiler looks), CRTOS' startup files and the libraries, the
    # position-independent ones in lib/xip
    cp -r "$arm/arm-none-eabi/include" "$sr/"
    chmod -R u+w "$sr/include"
    # libstdc++'s headers where a compiler with host = target looks: <prefix>/include/c++
    mv "$sr/include/c++" "$usr/include/"
    local cxx=$usr/include/c++/$GCCVER/arm-none-eabi
    mv "$cxx" "$cxx.all"
    cp -r "$cxx.all/$ml" "$cxx"
    rm -rf "$cxx.all"
    # (only what the specs link: -lc and -lstdc++ become their _nano variants, -lg is never
    # used; lib/xip's libstdc++_nano is its libstdc++)
    local l
    for l in libc_nano.a libm.a libstdc++_nano.a libsupc++_nano.a; do
        arm-none-eabi-strip --strip-debug -D -o "$sr/lib/$l" "$arm/arm-none-eabi/lib/$ml/$l"
    done
    cp "$arm/arm-none-eabi/lib/nano.specs" "$sr/lib/"
    cp "$TC/lib"/crtos-*.o "$TC/lib"/crtos-*.ld "$TC/lib"/lib*.a "$sr/lib/"
    cp "$TC/lib/xip"/* "$sr/lib/xip/"
    rm -f "$sr/lib/xip"/libg*.a "$sr/lib/xip/libstdc++.a" "$sr/lib/xip/libsupc++.a"
    cp -r "$TC/include"/* "$usr/include/"

    chmod -R u+w "$STAGE" # (what came from Windows' Program Files may be read-only)
    # every path must fit the kernel's (VFS_PATH_MAX 128 with the NUL), with ".part" while
    # deployd writes it
    local long
    long=$(cd "$STAGE/sd" && find . -type f | sed 's|^\.|/sd|' | awk 'length($0) > 122')
    [ -z "$long" ] || { echo "build-native: paths too long for the board:" >&2; echo "$long" >&2; exit 1; }
    say "install: into $NATIVE_OUT"
    chmod -R u+w "$NATIVE_OUT" 2> /dev/null || true; rm -rf "$NATIVE_OUT"
    mkdir -p "$NATIVE_OUT"
    cp -r "$STAGE"/. "$NATIVE_OUT/"
    say "install: /flash0 $(du -sh "$fl" | cut -f1), /sd/crtos/usr $(du -sh "$usr" | cut -f1) ($(find "$usr" -type f | wc -l) files)"
    ls -l "$lx" "$fl/bin" | awk 'NF > 5 {printf "  %-12s %6d KB\n", $9, $5 / 1024}'
}

steps=("$@")
[ ${#steps[@]} -eq 0 ] && steps=(fetch cross native install)
for s in "${steps[@]}"; do
    case $s in
    fetch) step_fetch ;;
    cross) step_cross ;;
    native) step_native ;;
    install) step_install ;;
    *) echo "build-native: unknown step '$s'" >&2; exit 2 ;;
    esac
done
