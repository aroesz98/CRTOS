"""Kernel monitor and fast uploads through the debug probe (SWD) - see kernel/rtos/swdcon.cpp.

The kernel keeps a control block (g_swd_chan) with an output ring (up) and an input ring
(down); a second kmon instance serves it. This module attaches to the running target without
halting it, so the UART console stays free and the probe's USB-serial bridge is not needed.
Uploads write the file straight into a RAM buffer (kmon "stage"), then have it saved (kmon
"savestage"), which is far faster than the serial line.

The control block is found in the running kernel itself (it is among the first initialised
data in DTCM), so no .axf file is needed; the probe is the board's CMSIS-DAP (DAPLink) probe
unless CRTOS_PROBE names one.
"""
import logging
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

TARGET = os.environ.get('CRTOS_TARGET', 'mimxrt1050_hyperflash')
MAGIC = 0x57535243
PROMPT = b'kmon> '
DTCM = 0x20000000
SCAN_BYTES = 0x8000             # where the kernel's initialised data starts

_probe = None


def find_probe():
    """Unique id of the board's debug probe: CRTOS_PROBE, else the only CMSIS-DAP probe
    (the board's DAPLink), else the only probe there is"""
    global _probe
    if _probe:
        return _probe
    _probe = os.environ.get('CRTOS_PROBE')
    if _probe:
        return _probe
    logging.getLogger('pyocd').setLevel(logging.ERROR)
    from pyocd.core.helpers import ConnectHelper
    probes = ConnectHelper.get_all_connected_probes(blocking=False, print_wait_message=False)
    dap = [p for p in probes if type(p).__name__ == 'CMSISDAPProbe']
    found = dap or probes
    if not found:
        raise RuntimeError('no debug probe found: is the board connected to this computer '
                           '(USB cable in the board\'s debug port, J28)?')
    if len(found) > 1:
        raise RuntimeError('several debug probes: %s - choose one with CRTOS_PROBE=<id>' %
                           ', '.join('%s (%s)' % (p.unique_id, p.description) for p in found))
    _probe = found[0].unique_id
    return _probe


def open_session(probe=None):
    """A pyOCD session attached to the running target (it keeps running)"""
    logging.getLogger('pyocd').setLevel(logging.ERROR)
    from pyocd.core.helpers import ConnectHelper
    probe = probe or find_probe()
    session = ConnectHelper.session_with_chosen_probe(
        unique_id=probe, target_override=TARGET, connect_mode='attach', frequency=12000000,
        blocking=False)
    if session is None:
        raise RuntimeError('debug probe %s not found' % probe)
    session.open()
    return session


def symbol_address(axf, name):
    d = os.environ.get('CRTOS_GCC_BIN')
    nm = os.path.join(d, 'arm-none-eabi-nm') if d else (shutil.which('arm-none-eabi-nm') or 'arm-none-eabi-nm')
    out = subprocess.run([nm, axf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    raise RuntimeError('%s: no symbol %s (kernel without the SWD console?)' % (axf, name))


def _plausible_ring(size):
    return 64 <= size <= 65536 and not size & (size - 1)


def find_channel(t, axf=None):
    """Address of the SWD console control block of the running kernel: it is near the start
    of DTCM, which is read in growing pieces (the probe moves only ~50 KB/s)"""
    key = struct.pack('<I', MAGIC)
    data = b''
    step = 1024
    while len(data) < SCAN_BYTES:
        data += bytes(t.read_memory_block8(DTCM + len(data), min(step, SCAN_BYTES - len(data))))
        step *= 2
        i = data.find(key)
        while 0 <= i <= len(data) - 16:
            if not i % 4:
                ver, up, down = struct.unpack_from('<III', data, i + 4)
                if ver == 1 and _plausible_ring(up) and _plausible_ring(down):
                    return DTCM + i
            i = data.find(key, i + 1)
    if axf and os.path.exists(axf):
        addr = symbol_address(axf, 'g_swd_chan')
        if t.read32(addr) == MAGIC:
            return addr
    raise RuntimeError('no SWD console in the running kernel: is CRTOS running on the board?')


class SwdCon:
    def __init__(self, axf=None, probe=None):
        self.session = open_session(probe)
        self.t = self.session.target
        try:
            self.base = find_channel(self.t, axf)
        except Exception:
            self.close()
            raise
        magic, _ver, self.up_size, self.down_size = self.t.read_memory_block32(self.base, 4)
        self.o_up_head, self.o_up_tail = self.base + 16, self.base + 20
        self.o_dn_head, self.o_dn_tail = self.base + 24, self.base + 28
        self.o_up, self.o_dn = self.base + 32, self.base + 32 + self.up_size
        self.pending = bytearray()

    def close(self):
        try:
            self.session.close()
        except Exception:
            pass

    def read(self):
        head = self.t.read32(self.o_up_head)
        tail = self.t.read32(self.o_up_tail)
        n = (head - tail) & 0xFFFFFFFF
        if not n:
            return b''
        if n > self.up_size:  # the target restarted under us (tail from the old run): start over
            self.t.write32(self.o_up_tail, head)
            return b''
        start = tail % self.up_size
        first = min(n, self.up_size - start)
        data = bytes(self.t.read_memory_block8(self.o_up + start, first))
        if n > first:
            data += bytes(self.t.read_memory_block8(self.o_up, n - first))
        self.t.write32(self.o_up_tail, (tail + n) & 0xFFFFFFFF)
        return data

    def write(self, data, timeout=5.0):
        t0 = time.time()
        while data:
            head = self.t.read32(self.o_dn_head)
            tail = self.t.read32(self.o_dn_tail)
            room = self.down_size - ((head - tail) & 0xFFFFFFFF)
            if room <= 0:
                if time.time() - t0 > timeout:
                    raise RuntimeError('SWD console: the target does not read')
                time.sleep(0.005)
                continue
            k = min(room, len(data))
            start = head % self.down_size
            first = min(k, self.down_size - start)
            self.t.write_memory_block8(self.o_dn + start, list(data[:first]))
            if k > first:
                self.t.write_memory_block8(self.o_dn, list(data[first:k]))
            self.t.write32(self.o_dn_head, (head + k) & 0xFFFFFFFF)
            data = data[k:]

    def until(self, token, timeout, quiet=0.15, stream=None):
        """Collect output until it ends with @token and nothing more comes for @quiet seconds
        (the kernel log itself contains monitor prompts); returns (found, text). @stream
        gets the output as it arrives."""
        t0 = time.time()
        last = time.time()
        while True:
            if self.pending.endswith(token) and time.time() - last >= quiet:
                out = bytes(self.pending)
                self.pending.clear()
                return True, out
            if time.time() - t0 > timeout:
                out = bytes(self.pending)
                self.pending.clear()
                return False, out
            chunk = self.read()
            if chunk:
                self.pending.extend(chunk)
                if stream:
                    stream(chunk.decode('utf-8', 'replace'))
                last = time.time()
            else:
                time.sleep(0.01)

    def sync(self):
        self.read()  # drop what was printed while nobody listened
        self.pending.clear()
        self.write(b'\r')
        ok, _ = self.until(PROMPT, 3.0)
        return ok

    def command(self, cmd, timeout=30.0, echo=None, stream=None):
        """Run a monitor command. If it has not finished in @timeout seconds (e.g. "run -w"
        of a program that keeps running), Ctrl-C ends it so the console is free again."""
        self.write(cmd.encode() + b'\r')
        ok, out = self.until(PROMPT, timeout, stream=stream)
        text = out.decode('utf-8', 'replace')
        if not ok:
            self.write(b'\x03')
            _, rest = self.until(PROMPT, 5.0, stream=stream)
            text += rest.decode('utf-8', 'replace') + '\n[swd: timeout waiting for "%s"]\n' % cmd
        if echo:
            echo(text)
        return ok, text

    def upload(self, local, remote):
        blob = open(local, 'rb').read()
        crc = zlib.crc32(blob) & 0xFFFFFFFF
        t0 = time.time()
        ok, text = self.command('stage %d' % len(blob), 5.0)
        addr = None
        for line in text.splitlines():
            if line.startswith('STAGE '):
                addr = int(line.split()[1], 16)
        if not ok or addr is None:
            raise RuntimeError('stage: %s' % text.strip())
        for off in range(0, len(blob), 65536):
            self.t.write_memory_block8(addr + off, list(blob[off:off + 65536]))
        ok, text = self.command('savestage %s %d %08x' % (remote, len(blob), crc), 60.0)
        if not ok or '\nOK ' not in '\n' + text:
            raise RuntimeError('savestage %s: %s' % (remote, text.strip()))
        dt = time.time() - t0
        print('swd put %s: %d bytes, %.1f KB/s' % (remote, len(blob), len(blob) / 1024.0 / max(dt, 0.001)))
        return crc


def run_kmon(axf, cmds, timeout=60.0):
    con = SwdCon(axf)
    try:
        if not con.sync():
            print('[swd: no kmon prompt on the SWD console]')
            return False
        ok = True
        for c in cmds:
            done, _ = con.command(c, timeout, stream=lambda s: (sys.stdout.write(s), sys.stdout.flush()))
            ok = ok and done
        print()
        return ok
    finally:
        con.close()


LCDIF = 0x402B8000
LCDIF_TRANSFER_COUNT = LCDIF + 0x30
LCDIF_CUR_BUF = LCDIF + 0x40


def screenshot(axf, path):
    """Save the frame the display shows (RGB565 frame buffer read through the probe) as PNG"""
    from PIL import Image
    con = SwdCon(axf)
    try:
        t = con.t
        count = t.read32(LCDIF_TRANSFER_COUNT)
        w, h = count & 0xFFFF, count >> 16
        if not (0 < w <= 2048 and 0 < h <= 2048):
            raise RuntimeError('display not running (TRANSFER_COUNT %08x)' % count)
        t.halt()  # no composition into the buffer while we read it (~0.3 s)
        try:
            addr = t.read32(LCDIF_CUR_BUF)
            raw = bytes(t.read_memory_block8(addr, w * h * 2))
        finally:
            t.resume()
    finally:
        con.close()
    img = Image.frombytes('RGB', (w, h), raw, 'raw', 'BGR;16')
    img.save(path)
    print('screenshot: %dx%d from %08x -> %s' % (w, h, addr, path))
    return True


def upload_files(axf, pairs, on_done=None):
    con = SwdCon(axf)
    try:
        if not con.sync():
            print('[swd: no kmon prompt on the SWD console]')
            return False
        for local, remote in pairs:
            crc = con.upload(local, remote)
            if on_done:
                on_done(local, remote, crc)
        return True
    finally:
        con.close()


def reset(probe=None, method='hw'):
    """Restart the board. 'hw': the probe pulls the reset line, so everything restarts, also a
    system that hangs; 'sysresetreq': the debugger requests a system reset, as the kernel's own
    reboot does. (pyOCD's default for i.MX RT, a VECTRESET of the core, does nothing on the
    Cortex-M7 - the board keeps running.)"""
    logging.getLogger('pyocd').setLevel(logging.ERROR)
    from pyocd.core.helpers import ConnectHelper
    from pyocd.core.target import Target
    probe = probe or find_probe()
    session = ConnectHelper.session_with_chosen_probe(unique_id=probe, target_override=TARGET,
                                                      connect_mode='attach', blocking=False)
    if session is None:
        raise RuntimeError('debug probe %s not found' % probe)
    if method == 'hw':
        session.open(init_board=False)      # the probe alone: no need to find the core
        try:
            session.probe.connect()
            session.probe.reset()
            session.probe.disconnect()
        finally:
            session.close()
    else:
        session.open()
        try:
            session.target.reset(reset_type=Target.ResetType.SYSRESETREQ)
        finally:
            session.close()


def board_uptime(axf=None, probe=None):
    """Seconds since the running kernel started (kmon "uptime"), or None if it does not answer"""
    try:
        con = SwdCon(axf, probe)
    except Exception:
        return None
    try:
        if not con.sync():
            return None
        _, text = con.command('uptime', 5.0)
        m = re.search(r'up ([0-9.]+) s', text)
        return float(m.group(1)) if m else None
    except Exception:
        return None
    finally:
        con.close()
