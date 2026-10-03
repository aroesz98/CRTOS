"""Deployment over the network: the client of deployd (system/services/deployd/deployd.c), also for
"crtos scp" (files both ways) and the password of "crtos desktop".

The board is found by a UDP broadcast ("CRTOS?" to port 5555), unless the host is given
(--host or CRTOS_HOST); the last address that worked is kept in ~/.crtos/board.host. Every
session starts with the token of ~/.crtos/deploy.token, which "crtos deploy" (through the
debug probe) or "crtos net setup" creates and installs on the board as
/sd/crtos/etc/deploy.token.
"""
import os
import secrets
import socket
import time
import zlib

PORT = 5555
PUT_PIECE = 64 * 1024   # a file is sent in pieces of this size, each within the timeout
PUT_SLOWEST = 100000    # bytes/s: the slowest the board writes (/flash0 with erases ~300 KB/s)


class DeployError(Exception):
    pass


def quote(path):
    """A board path for a command line of deployd: %XX for spaces, '%' and what is not ASCII"""
    return ''.join('%%%02X' % b if b <= 0x20 or b == 0x25 or b >= 0x7F else chr(b) for b in path.encode('utf-8'))


def unquote(text):
    raw, data, i = text.encode('utf-8'), bytearray(), 0
    while i < len(raw):
        if raw[i] == 0x25 and i + 3 <= len(raw):
            data.append(int(raw[i + 1:i + 3], 16))
            i += 3
        else:
            data.append(raw[i])
            i += 1
    return data.decode('utf-8', 'replace')


def find(timeout=1.5):
    """{ip: mac} of the boards that answer the discovery broadcast"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.settimeout(0.25)
    found = {}
    t0 = time.time()
    sent = 0.0
    try:
        while time.time() - t0 < timeout:
            if time.time() - sent > 0.5:
                s.sendto(b'CRTOS?', ('255.255.255.255', PORT))
                sent = time.time()
            try:
                data, (ip, _) = s.recvfrom(128)
            except (socket.timeout, ConnectionResetError):
                if found:
                    break
                continue
            if data.startswith(b'CRTOS'):
                found[ip] = data[6:].decode('ascii', 'replace')
    finally:
        s.close()
    return found


class Session:
    def __init__(self, host, token, timeout=15.0):
        self.s = socket.create_connection((host, PORT), timeout=timeout)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.f = self.s.makefile('rb')
        greeting = self.line()
        if not greeting.startswith('CRTOS deployd'):
            raise DeployError('not deployd: %r' % greeting)
        self.command('AUTH ' + token)

    def line(self):
        text = self.f.readline()
        if not text:
            raise DeployError('connection closed')
        return text.decode('utf-8', 'replace').strip()

    def command(self, text):
        self.s.sendall(text.encode() + b'\n')
        answer = self.line()
        if not answer.startswith('OK'):
            raise DeployError(answer)
        return answer[2:].strip()

    def put(self, data, remote):
        crc = zlib.crc32(data) & 0xFFFFFFFF
        self.s.sendall(('PUT %s %d %08x\n' % (quote(remote), len(data), crc)).encode())
        # in pieces: the timeout of one sendall() covers all of it (Python 3.5+), and a big
        # file going into the flash (/flash0: ~300 KB/s with the erases) takes longer than that
        view = memoryview(data)
        for i in range(0, len(data), PUT_PIECE):
            self.s.sendall(view[i:i + PUT_PIECE])
        # the answer comes when the board has written it all, which from the flash may be long
        # after the last byte left here (this computer's send buffer holds megabytes)
        timeout = self.s.gettimeout()
        self.s.settimeout(timeout + len(data) / PUT_SLOWEST)
        try:
            answer = self.line()
        finally:
            self.s.settimeout(timeout)
        if not answer.startswith('OK'):
            raise DeployError('%s: %s' % (remote, answer))
        self.last = answer[2:].strip()
        return crc

    def crc(self, remote):
        size, crc = self.command('CRC ' + quote(remote)).split()
        return int(size), int(crc, 16)

    def get(self, remote, progress=None):
        """A file of the board, checked with its CRC"""
        size = int(self.command('GET ' + quote(remote)))
        chunks, got, crc = [], 0, 0
        while got < size:
            data = self.f.read(min(PUT_PIECE, size - got))
            if not data:
                raise DeployError('connection closed')
            crc = zlib.crc32(data, crc)
            chunks.append(data)
            got += len(data)
            if progress:
                progress(got, size)
        answer = self.line()
        if not answer.startswith('OK'):
            raise DeployError('%s: %s' % (remote, answer))
        if int(answer[2:].strip(), 16) != crc & 0xFFFFFFFF:
            raise DeployError('%s: the CRC does not match' % remote)
        return b''.join(chunks)

    def stat(self, remote):
        """('d' or 'f', size, mtime), or None when there is no such file"""
        self.s.sendall(('STAT %s\n' % quote(remote)).encode())
        answer = self.line()
        if not answer.startswith('OK'):
            return None
        kind, size, mtime = answer[2:].split()
        return kind, int(size), int(mtime)

    def list(self, remote):
        """[(kind, size, mtime, name)] of a directory of the board"""
        n = int(self.command('LIST ' + quote(remote)))
        out = []
        for _ in range(n):
            kind, size, mtime, name = self.line().split(' ', 3)
            out.append((kind, int(size), int(mtime), unquote(name)))
        return out

    def mkdir(self, remote):
        self.command('MKDIR ' + quote(remote))

    def close(self):
        try:
            self.s.sendall(b'QUIT\n')
        except OSError:
            pass
        self.s.close()


STATE = os.environ.get('CRTOS_STATE') or os.path.join(os.path.expanduser('~'), '.crtos')


class Net:
    def __init__(self, host=None):
        self.state = STATE
        self.token_path = os.path.join(self.state, 'deploy.token')
        self.host_path = os.path.join(self.state, 'board.host')
        self.host = host or os.environ.get('CRTOS_HOST')
        self.verbose = bool(os.environ.get('CRTOS_NET_VERBOSE'))

    def token(self, create=False):
        if not os.path.exists(self.token_path):
            if not create:
                raise DeployError('no %s: "crtos deploy" through the debug probe (or "crtos net setup") '
                                  'installs one' % self.token_path)
            os.makedirs(self.state, exist_ok=True)
            with open(self.token_path, 'w') as f:
                f.write(secrets.token_hex(16) + '\n')
        return open(self.token_path).read().strip()

    def resolve(self):
        """The board's address: given, remembered (if it answers) or found"""
        if self.host:
            return self.host
        if os.path.exists(self.host_path):
            host = open(self.host_path).read().strip()
            try:
                socket.create_connection((host, PORT), timeout=1.0).close()
                return host
            except OSError:
                pass
        boards = find()
        if not boards:
            raise DeployError('no board answered on UDP port %d (is deployd running? --host IP)' % PORT)
        if len(boards) > 1:
            raise DeployError('several boards: %s (choose one with --host)' % ', '.join(sorted(boards)))
        host = next(iter(boards))
        return host

    def session(self):
        host = self.resolve()
        s = Session(host, self.token())
        os.makedirs(self.state, exist_ok=True)
        with open(self.host_path, 'w') as f:
            f.write(host + '\n')
        return host, s

    def upload(self, pairs, on_done=None):
        """[(local, remote)]: send each file; on_done(local, remote, crc) after each"""
        host, s = self.session()
        total = 0
        t0 = time.time()
        try:
            for local, remote in pairs:
                data = open(local, 'rb').read()
                t = time.time()
                crc = s.put(data, remote)
                dt = time.time() - t
                total += len(data)
                print('net put %s: %d bytes, %.0f KB/s%s' % (remote, len(data), len(data) / 1024.0 / max(dt, 1e-3),
                                                             ' (%s)' % s.last if self.verbose else ''))
                if on_done:
                    on_done(local, remote, crc)
        finally:
            s.close()
        dt = time.time() - t0
        print('net: %d file(s), %d bytes to %s in %.2f s (%.0f KB/s)' % (len(pairs), total, host, dt,
                                                                           total / 1024.0 / max(dt, 1e-3)))
        return True

    def reboot(self, wait=True):
        host, s = self.session()
        s.command('REBOOT')
        s.s.close()
        print('net: %s is rebooting' % host)
        if not wait:
            return True
        time.sleep(1.0)
        t0 = time.time()
        while time.time() - t0 < 30:
            try:
                socket.create_connection((host, PORT), timeout=1.0).close()
                print('net: %s is back after %.1f s' % (host, time.time() - t0 + 1.0))
                return True
            except OSError:
                time.sleep(0.5)
        print('net: %s did not come back within 30 s' % host)
        return False

    def flash(self, image, remote='/sd/crtos/boot/crtos.bin'):
        """A kernel image into the board's boot flash: sent to the SD card first, then deployd
        writes it (/dev/mtd0) and the board restarts. True once deployd answers again."""
        data = open(image, 'rb').read()
        host, s = self.session()
        try:
            s.put(data, remote)
            print('net: %s -> %s (%d bytes), writing the flash...' % (os.path.basename(image), remote, len(data)))
            s.s.sendall(('FLASH %s\n' % remote).encode())
            answer = s.line()
            if not answer.startswith('OK'):
                raise DeployError(answer)
            t0 = time.time()
            # a refusal comes at once: the kernel checks the image before it stops the system
            s.s.settimeout(3.0)
            try:
                refused = s.line()
            except (DeployError, OSError):
                refused = None              # writing the flash, then the board restarts
            if refused:
                raise DeployError(refused)
        finally:
            s.s.close()
        # the old deployd never answers again (it waits in the update until the restart), so
        # a greeting comes from the new system; a bare connection could be the old one's backlog
        while time.time() - t0 < 30:
            if greets(host):
                print('net: %s runs the new kernel (deployd answers after %.1f s)' % (host, time.time() - t0))
                return True
            time.sleep(0.5)
        print('net: %s did not come back within 30 s' % host)
        return False


def greets(host, timeout=1.5):
    """True when deployd on host sends its greeting"""
    try:
        with socket.create_connection((host, PORT), timeout=timeout) as c:
            c.settimeout(timeout)
            return c.recv(64).startswith(b'CRTOS deployd')
    except OSError:
        return False
