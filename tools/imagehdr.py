#!/usr/bin/env python3
"""Fill in the image header of a kernel for the STM32H7S78-DK (kernel/platform/stm32h7s78dk/
board/image.h): the image size and its CRC-32 (zlib), computed with the crc32 field as 0. The
boot loader checks both before it starts the kernel, and a kernel update before it writes.

    imagehdr.py build/kernel/crtos.bin          fill in (the build does this)
    imagehdr.py --check crtos.bin               0 if the header is right
"""
import struct
import sys
import zlib

HDR_OFF = 0x400
MAGIC = 0x4E4B5243          # "CRKN"
HDR_SIZE = 48
MAX_SIZE = 2 << 20


def check(data):
    """None, or what is wrong with the image"""
    if len(data) < HDR_OFF + HDR_SIZE:
        return 'too short'
    magic, hsize, size, crc = struct.unpack_from('<IIII', data, HDR_OFF)
    if magic != MAGIC or hsize != HDR_SIZE:
        return 'no CRTOS kernel header at 0x%x' % HDR_OFF
    if size != len(data) or size > MAX_SIZE:
        return 'size %d in the header, %d in the file (at most %d)' % (size, len(data), MAX_SIZE)
    zeroed = data[:HDR_OFF + 12] + b'\0\0\0\0' + data[HDR_OFF + 16:]
    if zlib.crc32(zeroed) & 0xFFFFFFFF != crc:
        return 'CRC-32 does not match'
    return None


def fill(path):
    with open(path, 'rb') as f:
        data = bytearray(f.read())
    if len(data) > MAX_SIZE:
        sys.exit('imagehdr: %s is %d bytes, more than the kernel partition (%d)' % (path, len(data), MAX_SIZE))
    magic, hsize = struct.unpack_from('<II', data, HDR_OFF)
    if magic != MAGIC or hsize != HDR_SIZE:
        sys.exit('imagehdr: %s has no kernel header at 0x%x' % (path, HDR_OFF))
    struct.pack_into('<II', data, HDR_OFF + 8, len(data), 0)
    struct.pack_into('<I', data, HDR_OFF + 12, zlib.crc32(bytes(data)) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(data)


def main():
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == '--check':
        with open(args[1], 'rb') as f:
            problem = check(f.read())
        if problem:
            print('%s: %s' % (args[1], problem))
            return 1
        return 0
    if len(args) != 1:
        sys.exit(__doc__)
    fill(args[0])
    return 0


if __name__ == '__main__':
    sys.exit(main())
