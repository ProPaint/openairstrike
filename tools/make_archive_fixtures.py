#!/usr/bin/env python3
"""Makes the synthetic fixtures of testdata/archives/ (apps/tests/archive_test.cpp): a fake game
directory (no game data: pak0.apk is 70000 'A's, the FakeGames pak0 of the import tests) and the
containers that need no external tool: game.zip, nested.zip (game.zip inside a zip),
truncated.zip, game.tar.gz, plain.pe (a tiny PE stub), nsis_dummy.pe (the stub with the NSIS
marker after it). Then, from the same tree, tools/make_archive_fixtures_docker.sh makes game.7z,
game.cab, game_rar4.rar and game_rar5.rar in a Debian container, "sfx" appends game.cab to the
stub (cab_sfx.pe), and tools/make_inno_fixture.py writes inno5_setup.bin and inno6_setup.bin.

  python3 tools/make_archive_fixtures.py OUT
  docker run --rm -e HOST_UID=$(id -u) -e HOST_GID=$(id -g) -v OUT:/fx \
      -v $PWD/tools/make_archive_fixtures_docker.sh:/s.sh:ro debian:bookworm-slim bash /s.sh
  python3 tools/make_archive_fixtures.py OUT/out sfx
  python3 tools/make_inno_fixture.py --version 5.1.10 "OUT/tree/game/AirStrike 3D" inno5_setup.bin
  python3 tools/make_inno_fixture.py --version 6.4.3 "OUT/tree/game/AirStrike 3D" inno6_setup.bin
"""
import io, os, struct, sys, tarfile, zipfile

OUT = sys.argv[1]
GAME = os.path.join(OUT, 'tree', 'game', 'AirStrike 3D')

def put(rel, data):
    p = os.path.join(GAME, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'wb').write(data)

def tree():
    # Not a program: text and byte patterns that look like x86 CALL/JMP instructions, so that
    # the Inno Setup fixtures exercise the call instruction transform their compilers apply to .exe files.
    calls = b''.join(bytes([0xE8 if i % 2 else 0xE9]) + (i * 77).to_bytes(3, 'little') + bytes([0x00 if i % 3 else 0xFF]) + b'pad' for i in range(300))
    put('AirStrike3D.exe', b'MZ synthetic stand-in for the tests, not a program\n' + calls)
    put('data/pak0.apk', b'A' * 70000)            # the FakeGames pak0 of apps/tests/game_import_test.cpp, game 0
    put('data/pak1.apk', b'B' * 3000)
    put('data/pak2.apk', b'C' * 3000)
    put('data/Settings.xml', b'<?xml version="1.0"?>\n<settings>\n  <synthetic game="AirStrike 3D" version="1.70"/>\n</settings>\n')
    put('data/gfx/logo2s.tga', bytes(18) + b'\x00' * 64)
    put('readme.txt', b'Synthetic game files for the archive tests of OpenAirStrike. No game data.\n')

def files():
    out = []
    for d, _, fs in os.walk(os.path.join(OUT, 'tree')):
        for f in fs:
            p = os.path.join(d, f)
            out.append((os.path.relpath(p, os.path.join(OUT, 'tree')), open(p, 'rb').read()))
    return sorted(out)

def zip_bytes(items, method=zipfile.ZIP_DEFLATED):
    b = io.BytesIO()
    with zipfile.ZipFile(b, 'w', method) as z:
        for name, data in items:
            z.writestr(name.replace(os.sep, '/'), data)
    return b.getvalue()

def pe_stub():
    # A minimal PE32 image: DOS header, PE header, one 512-byte section; nothing runs it.
    dos = bytearray(64)
    dos[0:2] = b'MZ'
    struct.pack_into('<I', dos, 0x3C, 0x40)
    coff = struct.pack('<4sHHIIIHH', b'PE\0\0', 0x14C, 1, 0, 0, 0, 224, 0x0102)
    opt = struct.pack('<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII', 0x10B, 14, 0, 0x200, 0, 0, 0x1000, 0x1000, 0x2000, 0x400000, 0x1000, 0x200,
                      4, 0, 0, 0, 4, 0, 0, 0x2000, 0x200, 0, 2, 0, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
    opt += bytes(16 * 8)
    sec = struct.pack('<8sIIIIIIHHI', b'.text\0\0\0', 0x200, 0x1000, 0x200, 0x200, 0, 0, 0, 0, 0x60000020)
    head = bytes(dos) + coff + opt + sec
    head += bytes(0x200 - len(head))
    body = b'\xC3' + b'synthetic PE stub for the archive tests\n'
    body += bytes(0x200 - len(body))
    return head + body

def main():
    os.makedirs(OUT, exist_ok=True)
    if sys.argv[2:] == ['sfx']:
        cab = open(os.path.join(OUT, 'game.cab'), 'rb').read()
        open(os.path.join(OUT, 'cab_sfx.pe'), 'wb').write(pe_stub() + cab)
        return
    tree()
    fs = files()
    game_zip = zip_bytes(fs)
    open(os.path.join(OUT, 'game.zip'), 'wb').write(game_zip)
    open(os.path.join(OUT, 'nested.zip'), 'wb').write(zip_bytes([('downloads/game.zip', game_zip), ('downloads/notes.txt', b'a zip inside a zip\n')]))
    open(os.path.join(OUT, 'truncated.zip'), 'wb').write(game_zip[:len(game_zip) // 2])
    b = io.BytesIO()
    with tarfile.open(fileobj=b, mode='w:gz') as t:
        for name, data in fs:
            ti = tarfile.TarInfo(name.replace(os.sep, '/'))
            ti.size = len(data)
            t.addfile(ti, io.BytesIO(data))
    open(os.path.join(OUT, 'game.tar.gz'), 'wb').write(b.getvalue())
    stub = pe_stub()
    open(os.path.join(OUT, 'plain.pe'), 'wb').write(stub)
    nsis = stub + bytes(0x200) + b'\x00\x00\x00\x80' + b'\xEF\xBE\xAD\xDE' + b'NullsoftInst' + bytes(100)
    open(os.path.join(OUT, 'nsis_dummy.pe'), 'wb').write(nsis)
    print('tree and python-made fixtures in', OUT)

main()
