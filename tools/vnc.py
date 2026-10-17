"""The remote desktop of the board on this computer: a VNC (RFB 3.8) client for "crtos desktop".

The board's vncd (system/services/vncd) sends its screen, this window sends back the mouse (left
button, moves - also without it, for the board's tooltips - and wheel), the keys and the clipboard. The window opens maximised with the picture as
large as fits (F11: full screen), scaled sharp with Pillow (without it: by whole numbers
only); it needs Python with Tk (tkinter). Any other VNC viewer works as well: the board
listens on port 5900 and the password is in ~/.crtos/vnc.passwd ("crtos desktop --password"
shows it).

    view(host, password[, zoom, fullscreen, stats])    the window
    shot(host, password, path)                  one picture of the screen as PNG (tests)
"""
import os
import queue
import socket
import struct
import threading
import time
import zlib

PORT = 5900

# ---- DES (FIPS 46-3): all that VNC authentication needs ---------------------------------------

_IP = (58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4, 62, 54, 46, 38, 30, 22, 14, 6,
       64, 56, 48, 40, 32, 24, 16, 8, 57, 49, 41, 33, 25, 17, 9, 1, 59, 51, 43, 35, 27, 19, 11, 3,
       61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7)
_FP = (40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31, 38, 6, 46, 14, 54, 22, 62, 30,
       37, 5, 45, 13, 53, 21, 61, 29, 36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
       34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9, 49, 17, 57, 25)
_E = (32, 1, 2, 3, 4, 5, 4, 5, 6, 7, 8, 9, 8, 9, 10, 11, 12, 13, 12, 13, 14, 15, 16, 17,
      16, 17, 18, 19, 20, 21, 20, 21, 22, 23, 24, 25, 24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1)
_P = (16, 7, 20, 21, 29, 12, 28, 17, 1, 15, 23, 26, 5, 18, 31, 10,
      2, 8, 24, 14, 32, 27, 3, 9, 19, 13, 30, 6, 22, 11, 4, 25)
_PC1 = (57, 49, 41, 33, 25, 17, 9, 1, 58, 50, 42, 34, 26, 18, 10, 2, 59, 51, 43, 35, 27, 19, 11, 3, 60, 52, 44, 36,
        63, 55, 47, 39, 31, 23, 15, 7, 62, 54, 46, 38, 30, 22, 14, 6, 61, 53, 45, 37, 29, 21, 13, 5, 28, 20, 12, 4)
_PC2 = (14, 17, 11, 24, 1, 5, 3, 28, 15, 6, 21, 10, 23, 19, 12, 4, 26, 8, 16, 7, 27, 20, 13, 2,
        41, 52, 31, 37, 47, 55, 30, 40, 51, 45, 33, 48, 44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32)
_SHIFTS = (1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1)
_SBOX = (
    (14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7, 0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
     4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0, 15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13),
    (15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10, 3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
     0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15, 13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9),
    (10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8, 13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
     13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7, 1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12),
    (7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15, 13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
     10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4, 3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14),
    (2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9, 14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
     4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14, 11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3),
    (12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11, 10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
     9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6, 4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13),
    (4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1, 13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
     1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2, 6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12),
    (13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7, 1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
     7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8, 2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11),
)


def _permute(value, table, width):
    out = 0
    for t in table:
        out = (out << 1) | ((value >> (width - t)) & 1)
    return out


def des_encrypt(key, block):
    """One 8-byte block encrypted with an 8-byte key"""
    cd = _permute(int.from_bytes(key, 'big'), _PC1, 64)
    c, d = cd >> 28, cd & 0x0FFFFFFF
    sub = []
    for s in _SHIFTS:
        for _ in range(s):
            c = ((c << 1) | (c >> 27)) & 0x0FFFFFFF
            d = ((d << 1) | (d >> 27)) & 0x0FFFFFFF
        sub.append(_permute((c << 28) | d, _PC2, 56))
    b = _permute(int.from_bytes(block, 'big'), _IP, 64)
    left, right = b >> 32, b & 0xFFFFFFFF
    for k in sub:
        x = _permute(right, _E, 32) ^ k
        f = 0
        for j in range(8):
            six = (x >> (42 - 6 * j)) & 0x3F
            f = (f << 4) | _SBOX[j][(((six >> 4) & 2) | (six & 1)) * 16 + ((six >> 1) & 0xF)]
        left, right = right, left ^ _permute(f, _P, 32)
    return _permute((right << 32) | left, _FP, 64).to_bytes(8, 'big')


def vnc_response(password, challenge):
    """The answer to a VNC challenge: DES with the password (8 characters, each byte's bits
    mirrored) as the key"""
    key = bytes(int('{:08b}'.format(b)[::-1], 2) for b in password.encode('latin-1', 'replace')[:8].ljust(8, b'\0'))
    return des_encrypt(key, challenge[:8]) + des_encrypt(key, challenge[8:16])


# ---- RFB ----------------------------------------------------------------------------------------

class VncError(Exception):
    pass


class Rfb:
    """A connection to a VNC server with the picture in fb: width x height pixels of 2 bytes,
    RGB565 little endian - the board's own format, which it sends as it is. Encodings asked
    for: Zlib (a deflate stream; the board's desktop arrives in 1-2 % of its bytes, a game
    picture in about 10 %), then Raw and Hextile."""

    def __init__(self, host, password, port=PORT, timeout=10.0):
        self.received = 0                           # bytes read (statistics)
        self.s = socket.create_connection((host, port), timeout=timeout)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.f = self.s.makefile('rb')
        self.send_lock = threading.Lock()
        ver = self.read(12)
        if not ver.startswith(b'RFB 003.'):
            raise VncError('not a VNC server: %r' % ver)
        self.s.sendall(b'RFB 003.008\n')
        n = self.read(1)[0]
        if not n:
            raise VncError(self.reason())
        types = self.read(n)
        if 2 not in types:
            raise VncError('the server does not offer VNC authentication (%s)' % list(types))
        self.s.sendall(b'\x02')
        self.s.sendall(vnc_response(password, self.read(16)))
        if struct.unpack('>I', self.read(4))[0]:
            raise VncError('%s (the password is in ~/.crtos/vnc.passwd)' % self.reason())
        self.s.sendall(b'\x01')                     # shared
        self.width, self.height = struct.unpack('>HH', self.read(4))
        self.read(16)                               # its pixel format: we ask for ours
        self.name = self.read(struct.unpack('>I', self.read(4))[0]).decode('latin-1')
        # RGB565, little endian
        self.s.sendall(struct.pack('>BxxxBBBBHHHBBBxxx', 0, 16, 16, 0, 1, 31, 63, 31, 11, 5, 0))
        self.s.sendall(struct.pack('>BxHiii', 2, 3, 6, 0, 5))   # Zlib, Raw, Hextile
        self.fb = bytearray(self.width * self.height * 2)
        self.z = zlib.decompressobj()               # the server's one stream for all rectangles
        self.s.settimeout(None)

    def read(self, n):
        data = self.f.read(n)
        if data is None or len(data) < n:
            raise VncError('the connection closed')
        self.received += n
        return data

    def reason(self):
        return self.read(struct.unpack('>I', self.read(4))[0]).decode('latin-1', 'replace')

    def send(self, data):
        with self.send_lock:
            self.s.sendall(data)

    def request(self, incremental=True):
        self.send(struct.pack('>BBHHHH', 3, 1 if incremental else 0, 0, 0, self.width, self.height))

    def pointer(self, mask, x, y):
        self.send(struct.pack('>BBHH', 5, mask, max(0, x), max(0, y)))

    def key(self, down, keysym):
        self.send(struct.pack('>BBxxI', 4, 1 if down else 0, keysym))

    def cut(self, text):
        data = text.replace('\r\n', '\n').encode('latin-1', 'replace')
        self.send(struct.pack('>BxxxI', 6, len(data)) + data)

    def close(self):
        try:
            self.s.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.s.close()

    # the picture

    def _fill(self, x, y, w, h, colour):
        row = colour * w
        stride = self.width * 2
        o = (y * self.width + x) * 2
        for _ in range(h):
            self.fb[o:o + w * 2] = row
            o += stride

    def _blit(self, x, y, w, h, data):
        stride, n = self.width * 2, w * 2
        o = (y * self.width + x) * 2
        if x == 0 and w == self.width:              # whole rows: one copy
            self.fb[o:o + n * h] = data[:n * h]
            return
        for r in range(h):
            self.fb[o:o + n] = data[r * n:(r + 1) * n]
            o += stride

    def _hextile(self, x0, y0, w, h):
        bg = fg = b'\0\0'
        for ty in range(y0, y0 + h, 16):
            th = min(16, y0 + h - ty)
            for tx in range(x0, x0 + w, 16):
                tw = min(16, x0 + w - tx)
                flags = self.read(1)[0]
                if flags & 1:
                    self._blit(tx, ty, tw, th, self.read(tw * th * 2))
                    continue
                if flags & 2:
                    bg = self.read(2)
                self._fill(tx, ty, tw, th, bg)
                if flags & 4:
                    fg = self.read(2)
                if flags & 8:
                    n = self.read(1)[0]
                    if flags & 16:
                        data = self.read(n * 4)
                        for i in range(n):
                            xy, wh = data[i * 4 + 2], data[i * 4 + 3]
                            self._fill(tx + (xy >> 4), ty + (xy & 15), (wh >> 4) + 1, (wh & 15) + 1,
                                       data[i * 4:i * 4 + 2])
                    else:
                        data = self.read(n * 2)
                        for i in range(n):
                            xy, wh = data[i * 2], data[i * 2 + 1]
                            self._fill(tx + (xy >> 4), ty + (xy & 15), (wh >> 4) + 1, (wh & 15) + 1, fg)

    def message(self, lock=None, on_update=None):
        """One message from the server: ('update', [rects]), ('cut', text) or (kind, None).
        on_update() is called as soon as an update's header is in, before its pixels: the place
        to ask for the next one, which the server then prepares while these are decoded."""
        kind = self.read(1)[0]
        if kind == 0:
            n = struct.unpack('>xH', self.read(3))[0]
            if on_update:
                on_update()
            rects = []
            for _ in range(n):
                x, y, w, h, enc = struct.unpack('>HHHHi', self.read(12))
                if enc == 6:                        # (read and inflated outside the lock)
                    data = self.z.decompress(self.read(struct.unpack('>I', self.read(4))[0]))
                elif enc == 0:
                    data = self.read(w * h * 2)
                if lock:
                    lock.acquire()
                try:
                    if enc in (0, 6):
                        self._blit(x, y, w, h, data)
                    elif enc == 5:
                        self._hextile(x, y, w, h)
                    else:
                        raise VncError('encoding %d not asked for' % enc)
                finally:
                    if lock:
                        lock.release()
                rects.append((x, y, w, h))
            return 'update', rects
        if kind == 1:                               # colour map: not for true colour
            first, n = struct.unpack('>xHH', self.read(5))
            self.read(n * 6)
            return 'colours', None
        if kind == 2:
            return 'bell', None
        if kind == 3:
            n = struct.unpack('>xxxI', self.read(7))[0]
            return 'cut', self.read(n).decode('latin-1', 'replace')
        raise VncError('unknown message %d' % kind)

    def rows(self, x, y, w, h):
        """A part of the picture as its own RGB565 rows, one after another"""
        stride, n = self.width * 2, w * 2
        o = (y * self.width + x) * 2
        if x == 0 and w == self.width:
            return bytes(self.fb[o:o + n * h])
        return b''.join(self.fb[o + r * stride:o + r * stride + n] for r in range(h))

    def rgb_rows(self, x, y, w, h):
        """A part of the picture as rows of red, green and blue bytes"""
        data = self.rows(x, y, w, h)
        try:
            from PIL import Image
            return Image.frombuffer('RGB', (w, h), data, 'raw', 'BGR;16', 0, 1).tobytes()
        except ImportError:
            pass
        out = bytearray(w * h * 3)                  # (slowly, without Pillow)
        for i in range(w * h):
            v = data[2 * i] | (data[2 * i + 1] << 8)
            r, g, b = v >> 11, (v >> 5) & 63, v & 31
            out[3 * i:3 * i + 3] = bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
        return bytes(out)

    def rgb(self, x, y, w, h):
        """A part of the picture as PPM"""
        return b'P6 %d %d 255\n' % (w, h) + self.rgb_rows(x, y, w, h)


def png(path, width, height, rgb):
    """RGB rows as a PNG file"""
    raw = b''.join(b'\0' + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def shot(host, password, path, timeout=10.0):
    """The whole screen once, as PNG; returns (width, height, seconds)"""
    t0 = time.time()
    c = Rfb(host, password)
    try:
        c.s.settimeout(timeout)
        c.request(incremental=False)
        got = set()
        # the first update may come in several messages: until every tile arrived
        while len(got) < c.width * c.height:
            kind, rects = c.message()
            if kind != 'update':
                continue
            for x, y, w, h in rects:
                for yy in range(y, y + h):
                    got.update(range(yy * c.width + x, yy * c.width + x + w))
            if len(got) < c.width * c.height:
                c.request(incremental=False)
        png(path, c.width, c.height, c.rgb_rows(0, 0, c.width, c.height))
        return c.width, c.height, time.time() - t0
    finally:
        c.close()


# ---- the window ----------------------------------------------------------------------------------

_SPECIAL = {'Win_L': 0xFFEB, 'Win_R': 0xFFEC, 'App': 0xFF67, 'Super_L': 0xFFEB, 'Super_R': 0xFFEC}


class _Picture:
    """The board's picture in the window at any size. With Pillow it is scaled "sharp": each
    pixel repeated (nearest) up to the next whole multiple of the size, then averaged down (box)
    to it - edges stay crisp at any scale, without the uneven pixels of nearest scaling alone;
    a changed rectangle is scaled by itself (Pillow's resize with a box gives the pixels of
    scaling all of it). Without Pillow: the largest whole zoom that fits (Tk's photo copy)."""

    def __init__(self, rfb, shown):
        self.c, self.shown = rfb, shown
        self.fw, self.fh, self.n, self.big = rfb.width, rfb.height, 1, None
        try:
            from PIL import Image, ImageTk
            self.Image, self.ImageTk = Image, ImageTk
        except ImportError:
            import tkinter as tk
            self.Image = None
            self.base = tk.PhotoImage(width=rfb.width, height=rfb.height)

    def resize(self, avail_w, avail_h, zoom=None):
        """As large as fits into avail_w x avail_h, proportions kept (or @zoom times); the
        size taken"""
        w, h = self.c.width, self.c.height
        if zoom:
            fw, fh = w * zoom, h * zoom
        elif self.Image:
            k = min(avail_w / w, avail_h / h)
            fw, fh = max(1, int(w * k)), max(1, int(h * k))
        else:
            z = max(1, min(avail_w // w, avail_h // h))
            fw, fh = w * z, h * z
        self.fw, self.fh = fw, fh
        self.shown.configure(width=fw, height=fh)
        if self.Image:
            self.n = max(1, -(-fw // w), -(-fh // h))
            self.big = self.Image.new('RGB', (w * self.n, h * self.n))
        self.update(0, 0, w, h)
        return fw, fh

    def update(self, x0, y0, x1, y1):
        """The board's rectangle (x0, y0)-(x1, y1) changed (its pixels in the Rfb's picture)"""
        w, h = self.c.width, self.c.height
        if self.Image and self.big is None:
            return                          # (before the first resize(), which draws it all)
        if not self.Image:
            z = self.fw // w
            self.base.put(self.c.rgb(x0, y0, x1 - x0, y1 - y0), to=(x0, y0))
            self.shown.tk.call(self.shown.name, 'copy', self.base.name, '-from', x0, y0, x1, y1,
                               '-to', x0 * z, y0 * z, '-zoom', z, z)
            return
        n, Image = self.n, self.Image
        part = Image.frombuffer('RGB', (x1 - x0, y1 - y0), self.c.rows(x0, y0, x1 - x0, y1 - y0), 'raw', 'BGR;16', 0, 1)
        self.big.paste(part.resize(((x1 - x0) * n, (y1 - y0) * n), Image.NEAREST), (x0 * n, y0 * n))
        kx, ky = self.fw / w, self.fh / h
        # the window's pixels it touches, and one around (the box filter reaches a little over)
        dx0, dy0 = max(0, int(x0 * kx) - 1), max(0, int(y0 * ky) - 1)
        dx1, dy1 = min(self.fw, int(x1 * kx) + 2), min(self.fh, int(y1 * ky) + 2)
        sx, sy = w * n / self.fw, h * n / self.fh
        patch = self.big.resize((dx1 - dx0, dy1 - dy0), Image.BOX, box=(dx0 * sx, dy0 * sy, dx1 * sx, dy1 * sy))
        photo = self.ImageTk.PhotoImage(patch)
        self.shown.tk.call(self.shown.name, 'copy', str(photo), '-to', dx0, dy0)

    def board(self, x, y):
        """The board's pixel under the window point (x, y) of the picture"""
        bx, by = int(x * self.c.width / self.fw), int(y * self.c.height / self.fh)
        return max(0, min(self.c.width - 1, bx)), max(0, min(self.c.height - 1, by))


def _regions(rects, limit=12):
    """Changed rectangles (x, y, w, h) as a few regions (x0, y0, x1, y1) to redraw: those 16
    pixels apart or closer joined - not one box around all, which for the mouse pointer in one
    corner and the clock in another is the whole screen. Too many: that one box after all."""
    regs = []
    for x, y, w, h in rects:
        regs.append([x, y, x + w, y + h])
    changed = True
    while changed and len(regs) > 1:
        changed = False
        out = []
        for r in sorted(regs, key=lambda r: (r[1], r[0])):
            for o in out:
                if r[0] <= o[2] + 16 and o[0] <= r[2] + 16 and r[1] <= o[3] + 16 and o[1] <= r[3] + 16:
                    o[0], o[1], o[2], o[3] = min(o[0], r[0]), min(o[1], r[1]), max(o[2], r[2]), max(o[3], r[3])
                    changed = True
                    break
            else:
                out.append(r)
        regs = out
    if len(regs) > limit:
        return [(min(r[0] for r in regs), min(r[1] for r in regs), max(r[2] for r in regs), max(r[3] for r in regs))]
    return [tuple(r) for r in regs]


def _fine_timer(on):
    """Windows: timers to the millisecond while the window is open (by default Tk's 5 ms wait
    takes 15.6 ms, too coarse for 60 frames a second)"""
    if os.name != 'nt':
        return
    import ctypes
    try:
        (ctypes.windll.winmm.timeBeginPeriod if on else ctypes.windll.winmm.timeEndPeriod)(1)
    except (AttributeError, OSError):
        pass


def _dpi_aware():
    """Windows: the window in real pixels (on a scaled screen Windows would stretch it, blurred)"""
    if os.name != 'nt':
        return
    import ctypes
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except (AttributeError, OSError):
        try:
            ctypes.windll.user32.SetProcessDPIAware()
        except (AttributeError, OSError):
            pass


def view(host, password, zoom=None, fullscreen=False, title=None, stats=False, seconds=None):
    """The remote desktop in a Tk window until it is closed: maximised, the picture as large as
    fits (proportions kept, black around it), again at every size change; F11 switches full
    screen. With @zoom a window of that many times the board's screen instead. @stats prints
    once a second the updates that came, the frames drawn and what that took; @seconds closes
    the window after that long (tests).

    Two threads: the network one reads updates into the picture and asks for the next one as
    soon as an update's header is in (the board prepares it meanwhile, never more than one
    ahead, so the picture does not lag behind); the window draws what changed every 5 ms -
    updates that came meanwhile together, only the regions they touched."""
    import tkinter as tk

    _dpi_aware()
    _fine_timer(True)
    c = Rfb(host, password)
    lock = threading.Lock()
    events = queue.Queue()
    root = tk.Tk()
    root.title(title or 'CRTOS %s - %s (F11: full screen)' % (c.name, host))
    root.configure(background='black')
    shown = tk.PhotoImage(width=c.width, height=c.height)
    picture = _Picture(c, shown)
    width, height = (c.width * zoom, c.height * zoom) if zoom else (c.width * 2, c.height * 2)
    canvas = tk.Canvas(root, width=width, height=height, highlightthickness=0, cursor='arrow', background='black')
    item = canvas.create_image(0, 0, image=shown, anchor='nw')
    canvas.pack(fill='both', expand=True)
    state = {'mask': 0, 'x': 0, 'y': 0, 'keys': set(), 'clip': None, 'alive': True, 'wheel': 0,
             'ox': 0, 'oy': 0, 'size': None, 'pending': None}
    st = {'t': time.perf_counter(), 'updates': 0, 'frames': 0, 'draw': 0.0, 'bytes': 0}

    def layout():
        state['pending'] = None
        w, h = canvas.winfo_width(), canvas.winfo_height()
        if w < 2 or h < 2 or (w, h) == state['size']:
            return
        state['size'] = (w, h)
        with lock:
            fw, fh = picture.resize(w, h, zoom)
        state.update(ox=max(0, (w - fw) // 2), oy=max(0, (h - fh) // 2))
        canvas.coords(item, state['ox'], state['oy'])

    def resized(_ev):
        if state['pending'] is None:        # a burst of sizes while dragging: the last one
            state['pending'] = root.after(60, layout)

    def full_screen(on=None):
        on = not root.attributes('-fullscreen') if on is None else on
        root.attributes('-fullscreen', on)

    def network():
        try:
            c.request(incremental=False)
            while state['alive']:
                kind, data = c.message(lock, on_update=c.request)
                if kind == 'update':
                    events.put(('update', data))
                elif kind == 'cut':
                    events.put(('cut', data))
        except (VncError, OSError) as e:
            events.put(('closed', str(e)))

    def refresh():
        dirty = []
        try:
            while True:
                kind, data = events.get_nowait()
                if kind == 'update':
                    dirty.extend(data)
                    st['updates'] += 1
                elif kind == 'cut':
                    state['clip'] = data
                    root.clipboard_clear()
                    root.clipboard_append(data)
                elif kind == 'closed':
                    if state['alive']:
                        root.title('%s - disconnected: %s' % (root.title(), data))
                    state['alive'] = False
        except queue.Empty:
            pass
        if dirty:
            t0 = time.perf_counter()
            for x0, y0, x1, y1 in _regions(dirty):
                with lock:
                    picture.update(x0, y0, x1, y1)
            st['draw'] += time.perf_counter() - t0
            st['frames'] += 1
        now = time.perf_counter()
        if stats and now - st['t'] >= 1.0:
            dt = now - st['t']
            print('desktop: %.0f updates/s, %.0f frames/s drawn, %.1f ms to draw one, %.0f KB/s' % (
                st['updates'] / dt, st['frames'] / dt, 1000 * st['draw'] / max(1, st['frames']),
                (c.received - st['bytes']) / dt / 1024), flush=True)
            st.update(t=now, updates=0, frames=0, draw=0.0, bytes=c.received)
        root.after(5, refresh)

    def at(ev):
        """The board's pixel under the mouse (off the picture: the nearest one on its edge)"""
        return picture.board(ev.x - state['ox'], ev.y - state['oy'])

    def inside(ev):
        """The mouse is over the picture, not over the black around it"""
        x, y = ev.x - state['ox'], ev.y - state['oy']
        return 0 <= x < picture.fw and 0 <= y < picture.fh

    def pointer(mask=None, ev=None):
        if ev is not None:
            state['x'], state['y'] = at(ev)
        if mask is not None:
            state['mask'] = mask
        if state['alive']:
            c.pointer(state['mask'], state['x'], state['y'])

    def press(bit, ev):
        if inside(ev):                      # a click on the black is not the board's
            pointer(state['mask'] | bit, ev)

    def release(bit, ev):
        if state['mask'] & bit:             # a drag off the picture ends on its edge
            pointer(state['mask'] & ~bit, ev)

    def motion(ev):
        """With a button a drag, without one a hover (the board's tooltips); sent when the
        mouse reaches another pixel of the board"""
        if not state['mask'] and not inside(ev):
            return
        if at(ev) != (state['x'], state['y']):
            pointer(None, ev)

    def wheel(ev):
        """A notch of the wheel: 120 of Windows' delta (finer wheels add up), X11 buttons 4/5"""
        if not inside(ev):
            return
        if ev.num in (4, 5):
            state['wheel'] += 120 if ev.num == 4 else -120
        else:
            state['wheel'] += getattr(ev, 'delta', 0)
        state['x'], state['y'] = at(ev)
        while abs(state['wheel']) >= 120:
            b = 0x08 if state['wheel'] > 0 else 0x10
            state['wheel'] -= 120 if state['wheel'] > 0 else -120
            pointer(state['mask'] | b)
            pointer(state['mask'] & ~b & 0xFF)

    def keysym(ev):
        if ev.keysym in _SPECIAL:
            return _SPECIAL[ev.keysym]
        return ev.keysym_num if 0 < ev.keysym_num < 0x1000000 else None

    def key(ev, down):
        if ev.keysym == 'F11':              # the window's own key, not the board's
            if down:
                full_screen()
            return 'break'
        k = keysym(ev)
        if k is None or not state['alive']:
            return 'break'
        if down:
            state['keys'].add(k)
        else:
            state['keys'].discard(k)
        c.key(down, k)
        return 'break'

    def focus_in(_ev):
        try:
            text = root.clipboard_get()
        except tk.TclError:
            text = None
        if text is not None and text != state['clip'] and state['alive']:
            state['clip'] = text
            c.cut(text)

    def focus_out(_ev):
        for k in list(state['keys']):       # none stays pressed on the board
            c.key(False, k)
        state['keys'].clear()

    canvas.bind('<Motion>', motion)
    canvas.bind('<ButtonPress-1>', lambda e: press(1, e))
    canvas.bind('<ButtonRelease-1>', lambda e: release(1, e))
    canvas.bind('<ButtonPress-3>', lambda e: press(4, e))
    canvas.bind('<ButtonRelease-3>', lambda e: release(4, e))
    canvas.bind('<MouseWheel>', wheel)
    canvas.bind('<Button-4>', wheel)
    canvas.bind('<Button-5>', wheel)
    root.bind('<KeyPress>', lambda e: key(e, True))
    root.bind('<KeyRelease>', lambda e: key(e, False))
    root.bind('<FocusIn>', focus_in)
    root.bind('<FocusOut>', focus_out)
    canvas.bind('<Configure>', resized)
    if fullscreen:
        full_screen(True)
    elif not zoom:
        try:
            root.state('zoomed')            # maximised (Windows, macOS)
        except tk.TclError:
            try:
                root.attributes('-zoomed', True)   # (X11)
            except tk.TclError:
                pass
    threading.Thread(target=network, daemon=True).start()
    root.after(5, refresh)
    if seconds:
        root.after(int(seconds * 1000), root.destroy)
    canvas.focus_set()
    try:
        root.mainloop()
    finally:
        state['alive'] = False
        c.close()
        _fine_timer(False)
