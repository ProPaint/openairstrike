#!/usr/bin/env python3
"""Reader for AirStrike 3D .apk pak archives. See docs/spec/pak.md.

Usage:
  paktool.py list     <pak>...
  paktool.py extract  <outdir> <pak>...      later paks override earlier ones
  paktool.py manifest <out.json> <pak>...    name/pak/size/flag/sha1, after overrides
"""
import hashlib
import json
import os
import struct
import sys

MAGIC = bytes.fromhex("0000803f99990000")
KEY_OFFSET = 0x10
KEY_SIZE = 1024
ENTRY_SIZE = 76


def xor(data, key):
    return bytes(b ^ key[i % KEY_SIZE] for i, b in enumerate(data))


def read_pak(path):
    """Yields (name, flag, data). Names are lower-case with backslashes."""
    with open(path, "rb") as f:
        d = f.read()
    if d[:8] != MAGIC:
        raise ValueError(f"{path}: bad magic {d[:8].hex()}")
    table_offset, count = struct.unpack_from("<II", d, 8)
    key = d[KEY_OFFSET:KEY_OFFSET + KEY_SIZE]
    table = xor(d[table_offset:table_offset + count * ENTRY_SIZE], key)
    if len(table) != count * ENTRY_SIZE:
        raise ValueError(f"{path}: truncated file table")
    for i in range(count):
        entry = table[i * ENTRY_SIZE:(i + 1) * ENTRY_SIZE]
        name = entry[:64].split(b"\0")[0].decode("cp1251").lower()
        offset, size, flag = struct.unpack_from("<III", entry, 64)
        if offset + size > len(d):
            raise ValueError(f"{path}: entry {name} out of bounds")
        data = d[offset:offset + size]
        if flag:
            data = xor(data, key)
        yield name, flag, data


def merged(paks):
    files = {}
    for pak in paks:
        for name, flag, data in read_pak(pak):
            files[name] = (os.path.basename(pak), flag, data)
    return files


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "list":
        for pak in argv[2:]:
            for name, flag, data in read_pak(pak):
                print(f"{len(data):9d} f={flag} {os.path.basename(pak)} {name}")
    elif cmd == "extract":
        out = argv[2]
        files = merged(argv[3:])
        for name, (_, _, data) in files.items():
            dest = os.path.join(out, *name.split("\\"))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as f:
                f.write(data)
        print(f"extracted {len(files)} files to {out}")
    elif cmd == "manifest":
        files = merged(argv[3:])
        entries = [
            {"name": n, "pak": p, "size": len(d), "flag": fl,
             "sha1": hashlib.sha1(d).hexdigest()}
            for n, (p, fl, d) in sorted(files.items())
        ]
        with open(argv[2], "w") as f:
            json.dump(entries, f, indent=1)
        print(f"wrote {len(entries)} entries to {argv[2]}")
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
