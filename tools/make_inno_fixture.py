#!/usr/bin/env python3
"""Writes a minimal Inno Setup installer around a directory of synthetic files, for the tests of
the archive module (testdata/archives/, docs/spec/archive.md section 3). Not a working installer:
a tiny PE stub that holds the loader's offset table, the setup header data (a header with only
file entries, then the file location table) and the file data, laid out as the compiler of the
chosen version lays them out. Two layouts:

  5.1.10   ANSI, MD5 checksums, LZMA1 file data, one chunk per file
  6.4.3    unicode, SHA-256 checksums, LZMA2 file data, every file in one solid chunk

Files whose names end in .exe get the compiler's call instruction transform (the version 1
transform for 5.1.10, version 3 for 6.4.3) before compression, as the compiler does.

Usage: tools/make_inno_fixture.py --version 5.1.10|6.4.3 SRC_DIR OUT.exe
Written from Inno Setup's published Struct.pas, Compress.pas and SetupLdr sources (no code of
other extractors).
"""
import argparse, hashlib, lzma, os, struct, zlib

LDR_ID = b'rDlPtS\xcd\xe6\xd7\x7b\x0b\x2a'


def lzma1_stream(data):
    """Inno's LZMA1 stream: the 5 property bytes, then raw LZMA1 data (end marker included)."""
    lc, lp, pb, dict_size = 3, 0, 2, 1 << 16
    c = lzma.LZMACompressor(format=lzma.FORMAT_RAW, filters=[{'id': lzma.FILTER_LZMA1, 'lc': lc, 'lp': lp, 'pb': pb, 'dict_size': dict_size}])
    return bytes([(pb * 5 + lp) * 9 + lc]) + struct.pack('<I', dict_size) + c.compress(data) + c.flush()


def lzma2_prop(dict_size):
    for p in range(41):
        size = (2 | (p & 1)) << (p // 2 + 11)
        if size >= dict_size:
            return p
    return 40


def lzma2_stream(data):
    """Inno's LZMA2 stream: the dictionary size property byte, then raw LZMA2 data."""
    dict_size = 1 << 16
    c = lzma.LZMACompressor(format=lzma.FORMAT_RAW, filters=[{'id': lzma.FILTER_LZMA2, 'dict_size': dict_size}])
    return bytes([lzma2_prop(dict_size)]) + c.compress(data) + c.flush()


def block(payload):
    """A setup data block: CRC-checked header (stored size, compressed flag), then 4 KB chunks
    of the LZMA1-compressed payload, each preceded by its CRC-32."""
    comp = lzma1_stream(payload)
    out = b''
    for i in range(0, len(comp), 4096):
        part = comp[i:i + 4096]
        out += struct.pack('<I', zlib.crc32(part)) + part
    hdr = struct.pack('<IB', len(out), 1)
    return struct.pack('<I', zlib.crc32(hdr)) + hdr + out


def call_v1(data):
    """Encodes as the 5.1 compiler does: a running offset, the 4 bytes after E8/E9 get +offset."""
    b = bytearray(data)
    offset, addr, left = 5, 0, 0
    for i in range(len(b)):
        if left == 0:
            if b[i] in (0xE8, 0xE9):
                addr, left = offset, 4
        else:
            addr += b[i]
            b[i] = addr & 0xFF
            addr >>= 8
            left -= 1
        offset = (offset + 1) & 0xFFFFFFFF
    return bytes(b)


def call_v3(data):
    """Encodes as the 5.3.9 and later compilers do, per 64 KB block."""
    b = bytearray(data)
    for start in range(0, len(b), 65536):
        size = min(65536, len(b) - start)
        if size < 5:
            continue
        i = 0
        while i < size - 4:
            p = start + i
            if b[p] in (0xE8, 0xE9):
                i += 1
                p += 1
                if b[p + 3] in (0x00, 0xFF):
                    a = (start + i + 4) & 0xFFFFFF  # offsets count from the start of the file
                    rel = b[p] | (b[p + 1] << 8) | (b[p + 2] << 16)
                    if rel & 0x800000:
                        b[p + 3] ^= 0xFF
                    rel = (rel + a) & 0xFFFFFF
                    b[p], b[p + 1], b[p + 2] = rel & 0xFF, (rel >> 8) & 0xFF, (rel >> 16) & 0xFF
                i += 4
            else:
                i += 1
    return bytes(b)


def pe_stub(table):
    """A minimal PE32 image with the loader table in its only section (a resource in a real one)."""
    dos = bytearray(64)
    dos[0:2] = b'MZ'
    struct.pack_into('<I', dos, 0x3C, 0x40)
    coff = struct.pack('<4sHHIIIHH', b'PE\0\0', 0x14C, 1, 0, 0, 0, 224, 0x0102)
    opt = struct.pack('<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII', 0x10B, 14, 0, 0x200, 0, 0, 0x1000, 0x1000, 0x2000, 0x400000, 0x1000, 0x200,
                      4, 0, 0, 0, 4, 0, 0, 0x2000, 0x200, 0, 2, 0, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16) + bytes(128)
    sec = struct.pack('<8sIIIIIIHHI', b'.rsrc\0\0\0', 0x200, 0x1000, 0x200, 0x200, 0, 0, 0, 0, 0x40000040)
    head = bytes(dos) + coff + opt + sec
    head += bytes(0x200 - len(head))
    body = b'synthetic Inno Setup stub for the archive tests\0' + table
    return head + body + bytes(0x200 - len(body))


def build(version, src, out):
    files = []
    for d, _, fs in os.walk(src):
        for f in sorted(fs):
            p = os.path.join(d, f)
            files.append((os.path.relpath(p, src).replace(os.sep, '\\'), open(p, 'rb').read()))
    files.sort()
    unicode = version == '6.4.3'
    enc = (lambda s: s.encode('utf-16-le')) if unicode else (lambda s: s.encode('latin-1'))
    sstr = lambda s: struct.pack('<I', len(enc(s))) + enc(s)

    # File data: 5.1.10 one LZMA1 chunk per file; 6.4.3 one solid LZMA2 chunk.
    data = b''
    locs = []
    if version == '5.1.10':
        for name, body in files:
            plain = call_v1(body) if name.lower().endswith('.exe') else body
            comp = lzma1_stream(plain)
            start = len(data)
            data += b'zlb\x1a' + comp
            flags = (1 << 7) | ((1 << 4) if name.lower().endswith('.exe') else 0)
            loc = struct.pack('<iiIQQQ', 0, 0, start, 0, len(body), len(comp)) + hashlib.md5(body).digest() + bytes(8) + bytes(8) + bytes([flags])
            assert len(loc) == 69
            locs.append(loc)
    else:
        solid = b''
        subs = []
        for name, body in files:
            plain = call_v3(body) if name.lower().endswith('.exe') else body
            subs.append((len(solid), name, body))
            solid += plain
        comp = lzma2_stream(solid)
        data = b'zlb\x1a' + comp
        for sub, name, body in subs:
            flags = (1 << 4) | ((1 << 2) if name.lower().endswith('.exe') else 0)
            loc = struct.pack('<iiIQQQ', 0, 0, 0, sub, len(body), len(comp)) + hashlib.sha256(body).digest() + bytes(8) + bytes(8) + bytes([flags])
            assert len(loc) == 85
            locs.append(loc)

    # The header and the file entries.
    n = len(files)
    if version == '5.1.10':
        hdr = b''.join(sstr('') for _ in range(27))
        fixed = bytearray(178)
        struct.pack_into('<16i', fixed, 32, 0, 0, 0, 0, 0, 0, 0, n, n, 0, 0, 0, 0, 0, 0, 0)
        fixed[169] = 3  # CompressMethod: LZMA
        file_strings = 9
    else:
        hdr = b''.join(sstr('') for _ in range(37))
        fixed = bytearray(177)
        struct.pack_into('<16i', fixed, 0, 0, 0, 0, 0, 0, 0, 0, n, n, 0, 0, 0, 0, 0, 0, 0)
        fixed[160] = 4  # CompressMethod: LZMA2
        file_strings = 10
    payload = hdr + bytes(fixed)
    for i, (name, _) in enumerate(files):
        strs = ['{app}\\' + name, '{app}\\' + name] + [''] * (file_strings - 2)
        fe = bytearray(43)
        struct.pack_into('<i', fe, 20, i)  # LocationEntry
        payload += b''.join(sstr(s) for s in strs) + bytes(fe)
    setup0 = (b'Inno Setup Setup Data (' + version.encode() + b')').ljust(64, b'\0') + block(payload) + block(b''.join(locs))

    # Layout: stub (with the table), setup-0 data, file data.
    stub_len = 0x400
    offset0 = stub_len
    offset1 = offset0 + len(setup0)
    total = offset1 + len(data)
    t = LDR_ID + struct.pack('<IIIIiII', 1, total, 0, 0, 0, offset0, offset1)
    t += struct.pack('<I', zlib.crc32(t))
    stub = pe_stub(t)
    assert len(stub) == stub_len
    open(out, 'wb').write(stub + setup0 + data)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--version', choices=['5.1.10', '6.4.3'], required=True)
    ap.add_argument('src')
    ap.add_argument('out')
    a = ap.parse_args()
    build(a.version, a.src, a.out)
