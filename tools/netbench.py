"""Network throughput between this computer and the board: the client of "crtos netbench".

The board runs the "nettest" program (tests/nettest), which serves on TCP port 5001:
    tcp_to_board(host, secs)        TCP, this computer sends, the board counts
    tcp_from_board(host, secs)      TCP, the board sends, we count
    udp_to_board(host, secs, mbit)  UDP datagrams of 1472 bytes at @mbit Mbit/s; the board
                                    counts what arrives (the rest was lost on the way)
    udp_from_board(host, secs)      UDP, the board sends as fast as it can, we count
Each returns a dict with "mbit" (as measured at the receiving end) and the details.
"""
import socket
import struct
import time

PORT = 5001
DGRAM = 1472


class BenchError(Exception):
    pass


def _request(host, test, secs=0, uport=0, timeout=10.0):
    s = socket.create_connection((host, PORT), timeout=timeout)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s.sendall(b'NTST' + struct.pack('>BBHHH', ord(test), secs, 0, uport, 0))
    return s


def _answer(s, timeout=20.0):
    """One line from the board, split into words"""
    s.settimeout(timeout)
    data = b''
    while not data.endswith(b'\n'):
        part = s.recv(256)
        if not part:
            break
        data += part
    words = data.decode('ascii', 'replace').split()
    if not words or words[0] == 'error':
        raise BenchError('the board could not run the test (%s)' % ' '.join(words) or 'no answer')
    return words


def _mbit(nbytes, seconds):
    return nbytes * 8 / seconds / 1e6 if seconds > 0 else 0.0


def tcp_to_board(host, secs):
    s = _request(host, 'R')
    s.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1 << 20)
    buf = bytes(65536)
    sent = 0
    end = time.perf_counter() + secs
    try:
        while time.perf_counter() < end:
            sent += s.send(buf)
        s.shutdown(socket.SHUT_WR)
        w = _answer(s)
    finally:
        s.close()
    nbytes, us = int(w[2]), int(w[3])
    return {'mbit': _mbit(nbytes, us / 1e6), 'bytes': nbytes, 'seconds': us / 1e6}


def tcp_from_board(host, secs):
    s = _request(host, 'S', secs)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    s.settimeout(secs + 15)
    buf = bytearray(1 << 16)
    view = memoryview(buf)
    total, t0, t1 = 0, None, None
    try:
        while True:
            n = s.recv_into(view)
            if not n:
                break
            t1 = time.perf_counter()
            if t0 is None:
                t0 = t1
            total += n
    finally:
        s.close()
    if not total:
        raise BenchError('nothing came from the board')
    return {'mbit': _mbit(total, t1 - t0), 'bytes': total, 'seconds': t1 - t0}


def udp_to_board(host, secs, mbit=95.0):
    s = _request(host, 'U')
    try:
        if _answer(s)[0] != 'ready':
            raise BenchError('the board did not start the UDP test')
        u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        u.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1 << 20)
        payload = bytes(DGRAM)
        gap = DGRAM * 8 / (mbit * 1e6)     # paced: faster would only be lost at the switch
        sent = 0
        t0 = time.perf_counter()
        while True:
            now = time.perf_counter()
            if now - t0 >= secs:
                break
            if now < t0 + sent * gap:
                continue
            try:
                u.sendto(payload, (host, PORT))
                sent += 1
            except OSError:
                pass
        u.close()
        time.sleep(0.3)
        s.sendall(b'end\n')
        w = _answer(s)
    finally:
        s.close()
    packets, nbytes, us = int(w[1]), int(w[2]), int(w[3])
    return {'mbit': _mbit(nbytes, us / 1e6), 'sent': sent, 'packets': packets,
            'lost': max(0, sent - packets), 'seconds': us / 1e6}


def udp_from_board(host, secs):
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    u.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 << 20)
    u.bind(('', 0))
    u.sendto(b'hello', (host, PORT))       # (a firewall here then lets the board's datagrams in)
    s = _request(host, 'V', secs, u.getsockname()[1])
    buf = bytearray(2048)
    packets, nbytes, t0, t1 = 0, 0, None, None
    try:
        u.settimeout(3.0)
        while True:
            try:
                n = u.recv_into(buf)
            except socket.timeout:
                break
            t1 = time.perf_counter()
            if t0 is None:
                t0 = t1
                u.settimeout(1.0)
            packets += 1
            nbytes += n
        w = _answer(s)
    finally:
        s.close()
        u.close()
    if not packets:
        raise BenchError('no UDP datagram came from the board (a firewall?)')
    sent = int(w[1])
    return {'mbit': _mbit(nbytes, t1 - t0), 'sent': sent, 'packets': packets,
            'lost': max(0, sent - packets), 'seconds': t1 - t0}


def wait_server(host, timeout=10.0):
    """True once the board's nettest takes connections (it is checked with an empty request)"""
    end = time.time() + timeout
    while time.time() < end:
        try:
            socket.create_connection((host, PORT), timeout=1.0).close()
            return True
        except OSError:
            time.sleep(0.3)
    return False


TESTS = [
    ('tcp-rx', 'TCP to the board', tcp_to_board),
    ('tcp-tx', 'TCP from the board', tcp_from_board),
    ('udp-rx', 'UDP to the board', udp_to_board),
    ('udp-tx', 'UDP from the board', udp_from_board),
]
