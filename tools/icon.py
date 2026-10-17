#!/usr/bin/env python3
"""icon.py - a program's icon for the board: PNG -> PAM (netpbm, RGB_ALPHA)

    python tools/icon.py SRC.png OUT.pam [--size 64]

"crtos build" runs it for crtos_app(... ICON icon.png) and crtos_icon(): the window manager
shows <name>.pam from /sd/crtos/share/icons in the programs menu, on the task bar and in the
title bars, scaled to what it needs (libgfx: gfx_icon_load). PAM needs no decompressor on the
board. The picture becomes square (transparent margins) and at most SIZE x SIZE: a larger one is
shrunk here, each pixel the average of the area it covers. The PNG is read with Python alone
(zlib): 8 or 16 bits, grey, RGB, palette (with tRNS), each with or without alpha, not interlaced.
A .pam or .ppm source is taken as it is.
"""
import argparse
import os
import struct
import sys
import zlib


class Fail(Exception):
    pass


def read_png(path):
    """(width, height, RGBA bytes)"""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise Fail('%s: not a PNG file' % path)
    pos, ihdr, idat, plte, trns = 8, None, [], None, None
    while pos + 8 <= len(data):
        n, tag = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if tag == b'IHDR':
            ihdr = struct.unpack('>IIBBBBB', body)
        elif tag == b'PLTE':
            plte = body
        elif tag == b'tRNS':
            trns = body
        elif tag == b'IDAT':
            idat.append(body)
        elif tag == b'IEND':
            break
    if not ihdr:
        raise Fail('%s: no IHDR' % path)
    w, h, depth, ctype, _, _, interlace = ihdr
    if interlace:
        raise Fail('%s: interlaced PNG (save it without interlacing)' % path)
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if channels is None or depth not in (1, 2, 4, 8, 16) or (depth < 8 and ctype not in (0, 3)):
        raise Fail('%s: PNG colour type %d with %d bits is not supported' % (path, ctype, depth))
    raw = zlib.decompress(b''.join(idat))
    bpp = max(1, channels * depth // 8)                 # bytes per pixel for the filters
    stride = (w * channels * depth + 7) // 8
    rows, prev, o = [], bytearray(stride), 0
    for _ in range(h):
        ftype, line = raw[o], bytearray(raw[o + 1:o + 1 + stride])
        o += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ftype == 1:
                line[i] = (line[i] + a) & 255
            elif ftype == 2:
                line[i] = (line[i] + b) & 255
            elif ftype == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
            elif ftype:
                raise Fail('%s: bad PNG filter %d' % (path, ftype))
        rows.append(line)
        prev = line
    out = bytearray()
    for line in rows:
        if depth == 16:
            vals = line[0::2]                           # the high byte of each sample
        elif depth == 8:
            vals = line
        else:
            per = 8 // depth
            mask = (1 << depth) - 1
            vals = [(line[i // per] >> (8 - depth * (i % per + 1))) & mask for i in range(w)]
            if ctype == 0:
                vals = [v * 255 // mask for v in vals]
        for x in range(w):
            if ctype == 6:
                out += bytes(vals[x * 4:x * 4 + 4])
            elif ctype == 2:
                r, g, b = vals[x * 3:x * 3 + 3]
                t = trns and depth == 8 and trns[1] == r and trns[3] == g and trns[5] == b
                out += bytes((r, g, b, 0 if t else 255))
            elif ctype == 4:
                out += bytes((vals[x * 2], vals[x * 2], vals[x * 2], vals[x * 2 + 1]))
            elif ctype == 0:
                out += bytes((vals[x], vals[x], vals[x], 255))
            else:
                i = vals[x]
                if not plte or i * 3 + 3 > len(plte):
                    raise Fail('%s: palette index out of range' % path)
                out += plte[i * 3:i * 3 + 3] + bytes((trns[i] if trns and i < len(trns) else 255,))
    return w, h, bytes(out)


def read_netpbm(path):
    """(width, height, RGBA bytes) of a PAM (8 bits, 1-4 channels) or PPM (P6, 8 bits)"""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:2] == b'P6':
        parts, o = [], 2
        while len(parts) < 3:
            while data[o:o + 1].isspace():
                o += 1
            if data[o:o + 1] == b'#':
                o = data.index(b'\n', o)
                continue
            s = o
            while not data[o:o + 1].isspace():
                o += 1
            parts.append(int(data[s:o]))
        w, h, maxval = parts
        depth, o = 3, o + 1
    elif data[:2] == b'P7':
        end = data.index(b'ENDHDR\n')
        head = dict(ln.split(None, 1) for ln in data[2:end].decode('ascii').splitlines()
                    if ln.strip() and not ln.startswith('#'))
        w, h, depth, maxval = (int(head[k]) for k in ('WIDTH', 'HEIGHT', 'DEPTH', 'MAXVAL'))
        o = end + 7
    else:
        raise Fail('%s: not a PAM or PPM file' % path)
    if maxval != 255 or not 1 <= depth <= 4:
        raise Fail('%s: only 8 bits with 1 to 4 channels' % path)
    px = data[o:o + w * h * depth]
    out = bytearray()
    for i in range(w * h):
        p = px[i * depth:(i + 1) * depth]
        out += bytes({1: (p[0], p[0], p[0], 255), 2: (p[0], p[0], p[0], p[1]),
                      3: (p[0], p[1], p[2], 255), 4: tuple(p)}[depth])
    return w, h, bytes(out)


def square(w, h, rgba):
    """Transparent margins around a picture that is not square"""
    if w == h:
        return w, rgba
    n = max(w, h)
    out = bytearray(n * n * 4)
    ox, oy = (n - w) // 2, (n - h) // 2
    for y in range(h):
        out[((oy + y) * n + ox) * 4:((oy + y) * n + ox + w) * 4] = rgba[y * w * 4:(y + 1) * w * 4]
    return n, bytes(out)


def shrink(n, rgba, size):
    """n x n -> size x size, each pixel the average of the area it covers (colours weighted by
    their alpha, as libgfx's gfx_image_scale does)"""
    k = n / size
    out = bytearray(size * size * 4)
    for oy in range(size):
        y0, y1 = oy * k, (oy + 1) * k
        for ox in range(size):
            x0, x1 = ox * k, (ox + 1) * k
            a = r = g = b = area = 0.0
            for sy in range(int(y0), min(n, int(y1 + 0.999999))):
                wy = min(y1, sy + 1) - max(y0, sy)
                if wy <= 0:
                    continue
                for sx in range(int(x0), min(n, int(x1 + 0.999999))):
                    wx = min(x1, sx + 1) - max(x0, sx)
                    if wx <= 0:
                        continue
                    o = (sy * n + sx) * 4
                    wgt = wx * wy
                    pa = rgba[o + 3] * wgt
                    a += pa
                    r += rgba[o] * pa
                    g += rgba[o + 1] * pa
                    b += rgba[o + 2] * pa
                    area += wgt
            o = (oy * size + ox) * 4
            if a > 0:
                out[o:o + 4] = bytes((min(255, round(r / a)), min(255, round(g / a)), min(255, round(b / a)),
                                      min(255, round(a / area))))
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description="a program's icon for the board: PNG -> PAM")
    ap.add_argument('src')
    ap.add_argument('out')
    ap.add_argument('--size', type=int, default=64, help='at most this many pixels a side (default 64)')
    a = ap.parse_args()
    try:
        ext = os.path.splitext(a.src)[1].lower()
        w, h, rgba = read_netpbm(a.src) if ext in ('.pam', '.ppm') else read_png(a.src)
        n, rgba = square(w, h, rgba)
        if n > a.size:
            rgba, n = shrink(n, rgba, a.size), a.size
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        with open(a.out, 'wb') as f:
            f.write(b'P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n' % (n, n) + rgba)
    except (Fail, OSError, ValueError, KeyError, zlib.error) as e:
        print('icon.py: %s' % e, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
