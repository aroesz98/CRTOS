#!/usr/bin/env python3
"""List kernel functions whose stack frame is larger than the MPU stack guard.

    python tools/stackcheck.py <object directory> [guard bytes] [--since <unix time>]

A frame bigger than the guard region (CONFIG_STACK_GUARD, 256 bytes) can step over it, so a
stack overflow in such a function is not caught. GCC writes the frame sizes to .su files
(-fstack-usage); this reads all of them under the directory (with --since: only those written
since then, e.g. by the build that just ran). It only warns.
"""
import os
import sys

SKIP = ('tests.cpp',)   # self tests overflow stacks on purpose


def main():
    args = sys.argv[1:]
    since = 0.0
    if '--since' in args:
        i = args.index('--since')
        since = float(args[i + 1])
        del args[i:i + 2]
    top = args[0]
    guard = int(args[1]) if len(args) > 1 else 256
    big = []
    for dirpath, _, files in os.walk(top):
        for fn in files:
            path = os.path.join(dirpath, fn)
            if not fn.endswith('.su') or os.path.getmtime(path) < since:
                continue
            for line in open(path, encoding='utf-8', errors='replace'):
                parts = line.rstrip('\n').split('\t')
                if len(parts) >= 2 and parts[1].isdigit() and int(parts[1]) > guard:
                    if not any(s in parts[0] for s in SKIP):
                        big.append((int(parts[1]), parts[0]))
    for size, where in sorted(big, reverse=True):
        print('stack: %5d bytes > guard %d: %s' % (size, guard, where))


if __name__ == '__main__':
    main()
