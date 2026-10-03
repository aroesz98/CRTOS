#!/usr/bin/env python3
"""Minimal DeviceTree compiler for CRTOS: DTS -> DTB (FDT v17), plus a DTB dumper.

    dtc.py -I dts/include -o board.dtb dts/evkbimxrt1050.dts
    dtc.py --dump board.dtb

The source is run through the GCC preprocessor first (like Linux), so #include and
#define work. Supported DTS: /dts-v1/, nodes and node merging, labels, &label node
extension, properties with strings, <cells> (numbers, &phandle refs, (C expressions)),
[byte arrays], &label path references, /delete-node/ and /delete-property/.
"""
import argparse
import os
import re
import struct
import subprocess
import sys

def _find_cpp():
    """The C preprocessor: CRTOS_CPP, else the one of the cross compiler (CRTOS_GCC_BIN, which
    the build sets, or the PATH)"""
    if os.environ.get('CRTOS_CPP'):
        return os.environ['CRTOS_CPP']
    if os.environ.get('CRTOS_GCC_BIN'):
        return os.path.join(os.environ['CRTOS_GCC_BIN'], 'arm-none-eabi-cpp')
    import shutil
    return shutil.which('arm-none-eabi-cpp') or 'arm-none-eabi-cpp'


CPP = _find_cpp()

FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE, FDT_END_NODE, FDT_PROP, FDT_NOP, FDT_END = 1, 2, 3, 4, 9


class DtsError(Exception):
    pass


class Node:
    def __init__(self, name, parent=None):
        self.name = name
        self.parent = parent
        self.props = {}          # name -> list of value items (see Parser.values)
        self.children = []
        self.labels = []
        self.phandle = None

    def child(self, name):
        for c in self.children:
            if c.name == name:
                return c
        c = Node(name, self)
        self.children.append(c)
        return c

    def path(self):
        if self.parent is None:
            return '/'
        p = self.parent.path()
        return (p if p != '/' else '') + '/' + self.name


TOKEN_RE = re.compile(r'''
    (?P<ws>\s+|//[^\n]*|/\*.*?\*/)
  | (?P<string>"(?:\\.|[^"\\])*")
  | (?P<directive>/dts-v1/|/plugin/|/delete-node/|/delete-property/|/memreserve/|/bits/)
  | (?P<ref>&\{[^}]*\}|&[A-Za-z_][A-Za-z0-9_]*)
  | (?P<label>[A-Za-z_][A-Za-z0-9_]*:)
  | (?P<word>[A-Za-z0-9_.+\-#@?][A-Za-z0-9_,.+\-#@?]*)
  | (?P<punct>[{};=<>\[\],/()|&^~*%!])
''', re.S | re.X)


def tokenize(text):
    toks = []
    pos = 0
    while pos < len(text):
        m = TOKEN_RE.match(text, pos)
        if not m:
            raise DtsError('unexpected character %r at offset %d' % (text[pos], pos))
        kind = m.lastgroup
        if kind != 'ws':
            toks.append((kind, m.group(kind)))
        pos = m.end()
    return toks


class Parser:
    def __init__(self, toks):
        self.toks = toks
        self.i = 0
        self.root = Node('')
        self.labels = {}
        self.deferred = []       # (label, body tokens start) for &label { } blocks

    # -- token helpers -------------------------------------------------------
    def peek(self, k=0):
        j = self.i + k
        return self.toks[j] if j < len(self.toks) else (None, None)

    def take(self, kind=None, value=None):
        tok = self.peek()
        if tok[0] is None:
            raise DtsError('unexpected end of input')
        if (kind and tok[0] != kind) or (value is not None and tok[1] != value):
            raise DtsError('expected %s %s, got %s %r' % (kind or '', value or '', tok[0], tok[1]))
        self.i += 1
        return tok[1]

    def accept(self, kind, value=None):
        tok = self.peek()
        if tok[0] == kind and (value is None or tok[1] == value):
            self.i += 1
            return True
        return False

    # -- grammar -------------------------------------------------------------
    def parse(self):
        while self.peek()[0] is not None:
            kind, val = self.peek()
            if kind == 'directive' and val == '/dts-v1/':
                self.take(); self.take('punct', ';')
            elif kind == 'directive' and val == '/memreserve/':
                self.take(); self.take('word'); self.take('word'); self.take('punct', ';')
            elif kind == 'directive' and val == '/delete-node/':
                self.take(); ref = self.take('ref'); self.take('punct', ';')
                node = self.resolve_ref(ref)
                node.parent.children.remove(node)
            elif kind == 'punct' and val == '/':
                self.take(); self.take('punct', '{')
                self.body(self.root)
                self.take('punct', '}'); self.take('punct', ';')
            elif kind == 'label' or kind == 'ref':
                labels = []
                while self.peek()[0] == 'label':
                    labels.append(self.take()[:-1])
                ref = self.take('ref')
                node = self.resolve_ref(ref)
                for l in labels:
                    self.add_label(l, node)
                self.take('punct', '{')
                self.body(node)
                self.take('punct', '}'); self.take('punct', ';')
            else:
                raise DtsError('unexpected top-level token %s %r' % (kind, val))
        return self.root

    def add_label(self, label, node):
        if label in self.labels and self.labels[label] is not node:
            raise DtsError('duplicate label %s' % label)
        self.labels[label] = node
        if label not in node.labels:
            node.labels.append(label)

    def resolve_ref(self, ref):
        if ref.startswith('&{'):
            path = ref[2:-1]
            node = self.root
            for part in [p for p in path.split('/') if p]:
                found = [c for c in node.children if c.name == part or c.name.split('@')[0] == part]
                if not found:
                    raise DtsError('unknown path %s' % path)
                node = found[0]
            return node
        label = ref[1:]
        if label not in self.labels:
            raise DtsError('unknown label %s' % label)
        return self.labels[label]

    def body(self, node):
        while True:
            kind, val = self.peek()
            if kind == 'punct' and val == '}':
                return
            if kind == 'directive' and val == '/delete-node/':
                self.take(); name = self.take('word'); self.take('punct', ';')
                node.children = [c for c in node.children if c.name != name]
                continue
            if kind == 'directive' and val == '/delete-property/':
                self.take(); name = self.take('word'); self.take('punct', ';')
                node.props.pop(name, None)
                continue
            labels = []
            while self.peek()[0] == 'label':
                labels.append(self.take()[:-1])
            name = self.take('word')
            if self.accept('punct', '{'):
                child = node.child(name)
                for l in labels:
                    self.add_label(l, child)
                self.body(child)
                self.take('punct', '}'); self.take('punct', ';')
            elif self.accept('punct', '='):
                node.props[name] = self.values()
                self.take('punct', ';')
            else:
                self.take('punct', ';')
                node.props[name] = []

    def values(self):
        items = []
        while True:
            kind, val = self.peek()
            if kind == 'string':
                items.append(('str', bytes(val[1:-1], 'utf-8').decode('unicode_escape')))
                self.take()
            elif kind == 'ref':
                items.append(('path', self.take()))
            elif kind == 'punct' and val == '<':
                self.take()
                items.append(('cells', self.cells()))
            elif kind == 'punct' and val == '[':
                self.take()
                data = ''
                while not self.accept('punct', ']'):
                    data += self.take('word')
                items.append(('bytes', bytes.fromhex(data)))
            elif kind == 'directive' and val == '/bits/':
                raise DtsError('/bits/ is not supported')
            else:
                raise DtsError('bad property value %s %r' % (kind, val))
            if not self.accept('punct', ','):
                return items

    def cells(self):
        cells = []
        while True:
            kind, val = self.peek()
            if kind == 'punct' and val == '>':
                self.take()
                return cells
            if kind == 'ref':
                cells.append(('ref', self.take()))
            elif kind == 'punct' and val == '(':
                cells.append(('num', self.expr()))
            elif kind == 'word':
                cells.append(('num', parse_int(self.take())))
            elif kind == 'punct' and val == '-':
                self.take()
                cells.append(('num', -parse_int(self.take('word'))))
            else:
                raise DtsError('bad cell %s %r' % (kind, val))

    def expr(self):
        # Collect a balanced parenthesised C integer expression and evaluate it safely.
        depth = 0
        parts = []
        while True:
            kind, val = self.peek()
            if kind is None:
                raise DtsError('unterminated expression')
            self.i += 1
            if kind == 'punct' and val == '(':
                depth += 1
            elif kind == 'punct' and val == ')':
                depth -= 1
            if kind == 'word':
                val = str(parse_int(val))
            elif kind != 'punct':
                raise DtsError('bad token in expression: %r' % val)
            parts.append(val)
            if depth == 0:
                break
        # Operators like << arrive as two '<' tokens, so join without separators
        src = ''.join(parts).replace('/', '//').replace('!', ' not ')
        return int(eval(src, {'__builtins__': {}}, {}))


def parse_int(text):
    t = text.rstrip('uUlL')
    try:
        return int(t, 0)
    except ValueError:
        raise DtsError('bad number %r' % text)


# -- phandles and encoding ------------------------------------------------------

def assign_phandles(root, labels):
    nodes = []

    def walk(n):
        nodes.append(n)
        for c in n.children:
            walk(c)
    walk(root)
    used = set()
    for n in nodes:
        if 'phandle' in n.props:
            n.phandle = n.props['phandle'][0][1][0][1]
            used.add(n.phandle)
    next_ph = 1

    def need(node):
        nonlocal next_ph
        if node.phandle is None:
            while next_ph in used:
                next_ph += 1
            node.phandle = next_ph
            used.add(next_ph)
            node.props['phandle'] = [('cells', [('num', next_ph)])]
    for n in nodes:
        for items in n.props.values():
            for kind, v in items:
                if kind == 'cells':
                    for ck, cv in v:
                        if ck == 'ref':
                            need(resolve(root, labels, cv))


def resolve(root, labels, ref):
    p = Parser([])
    p.root, p.labels = root, labels
    return p.resolve_ref(ref)


def encode_value(root, labels, items):
    out = b''
    for kind, v in items:
        if kind == 'str':
            out += v.encode('utf-8') + b'\0'
        elif kind == 'path':
            out += resolve(root, labels, v).path().encode() + b'\0'
        elif kind == 'bytes':
            out += v
        elif kind == 'cells':
            for ck, cv in v:
                val = resolve(root, labels, cv).phandle if ck == 'ref' else cv
                out += struct.pack('>I', val & 0xFFFFFFFF)
    return out


def to_dtb(root, labels):
    assign_phandles(root, labels)
    strings = bytearray()
    str_off = {}
    struct_blk = bytearray()

    def s_off(name):
        if name not in str_off:
            str_off[name] = len(strings)
            strings.extend(name.encode() + b'\0')
        return str_off[name]

    def pad4():
        while len(struct_blk) % 4:
            struct_blk.append(0)

    def emit(node):
        struct_blk.extend(struct.pack('>I', FDT_BEGIN_NODE))
        struct_blk.extend(node.name.encode() + b'\0')
        pad4()
        for name, items in node.props.items():
            data = encode_value(root, labels, items)
            struct_blk.extend(struct.pack('>III', FDT_PROP, len(data), s_off(name)))
            struct_blk.extend(data)
            pad4()
        for c in node.children:
            emit(c)
        struct_blk.extend(struct.pack('>I', FDT_END_NODE))

    emit(root)
    struct_blk.extend(struct.pack('>I', FDT_END))
    hdr_size = 40
    off_rsv = hdr_size
    rsv = struct.pack('>QQ', 0, 0)
    off_struct = off_rsv + len(rsv)
    off_strings = off_struct + len(struct_blk)
    total = off_strings + len(strings)
    total = (total + 7) & ~7
    hdr = struct.pack('>10I', FDT_MAGIC, total, off_struct, off_strings, off_rsv, 17, 16, 0,
                      len(strings), len(struct_blk))
    blob = hdr + rsv + bytes(struct_blk) + bytes(strings)
    return blob + b'\0' * (total - len(blob))


# -- dumper -----------------------------------------------------------------------

def dump(blob):
    magic, total, off_struct, off_strings = struct.unpack_from('>4I', blob, 0)
    if magic != FDT_MAGIC:
        raise DtsError('not a DTB')
    p = off_struct
    depth = 0
    out = []
    while True:
        (tok,) = struct.unpack_from('>I', blob, p); p += 4
        if tok == FDT_BEGIN_NODE:
            end = blob.index(b'\0', p)
            name = blob[p:end].decode() or '/'
            p = (end + 4) & ~3
            out.append('  ' * depth + name + ' {')
            depth += 1
        elif tok == FDT_END_NODE:
            depth -= 1
            out.append('  ' * depth + '};')
        elif tok == FDT_PROP:
            ln, noff = struct.unpack_from('>II', blob, p); p += 8
            data = blob[p:p + ln]; p = (p + ln + 3) & ~3
            name = blob[off_strings + noff:blob.index(b'\0', off_strings + noff)].decode()
            out.append('  ' * depth + '%s = %s;' % (name, fmt_value(data)) if ln else '  ' * depth + name + ';')
        elif tok == FDT_NOP:
            continue
        elif tok == FDT_END:
            break
        else:
            raise DtsError('bad token %d' % tok)
    return '\n'.join(out)


def fmt_value(data):
    if data and data[-1] == 0 and all(32 <= b < 127 or b == 0 for b in data) and data[0] != 0:
        return ', '.join('"%s"' % s for s in data[:-1].decode().split('\0'))
    if len(data) % 4 == 0:
        return '<' + ' '.join('0x%x' % v for v in struct.unpack('>%dI' % (len(data) // 4), data)) + '>'
    return '[' + data.hex() + ']'


def compile_dts(path, includes):
    cmd = [CPP, '-nostdinc', '-undef', '-D__DTS__', '-x', 'assembler-with-cpp', '-P']
    for inc in includes:
        cmd += ['-I', inc]
    cmd.append(path)
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise DtsError('preprocessor failed:\n' + proc.stderr)
    parser = Parser(tokenize(proc.stdout))
    root = parser.parse()
    return to_dtb(root, parser.labels)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('source')
    ap.add_argument('-o', '--output')
    ap.add_argument('-I', '--include', action='append', default=[])
    ap.add_argument('--dump', action='store_true', help='decode a DTB and print it')
    a = ap.parse_args()
    try:
        if a.dump:
            print(dump(open(a.source, 'rb').read()))
            return
        blob = compile_dts(a.source, a.include + [os.path.dirname(os.path.abspath(a.source))])
        out = a.output or os.path.splitext(a.source)[0] + '.dtb'
        with open(out, 'wb') as f:
            f.write(blob)
        print('%s: %d bytes' % (out, len(blob)))
    except DtsError as e:
        sys.exit('dtc: error: %s' % e)


if __name__ == '__main__':
    main()
