#!/usr/bin/env python3
"""Extracts the front-end texts compiled into the user's own game executable.

The original keeps the Information pages, the Game Complete congratulations and the rank
names in its executable, not in the data paks (docs/spec/frontend.md 3.9, 3.14, 5.11; issue
080, decided: an import step reads them from the user's copy). This tool reads the strings at
the addresses listed in frontend.md and writes them to a gitignored text file that ships with
the extracted game data. The texts are never committed.

The table of addresses is chosen by the SHA-256 of the executable (TABLES below), so the
right one is used whichever game's executable is given. The first game's table (as3d, v1.70)
is what the addresses above describe. AirStrike 2's is the committed address list
tools/exe_texts/as2.json (docs/spec/as2/frontend.md 7: every entry a key, an address and a
kind, `text`, `text_ml` or `u32`); Gulf Thunder's is tools/exe_texts/gulf.json the same way,
with the addresses that miss their text corrected or left out (docs/spec/gulf/issues/402).

Usage:
    tools/extract_exe_texts.py [--game KEY] [--exe PATH] [--out PATH]

Defaults: the game is as3d (or KEY); the executable is that game's under $AS3D_DATA_ROOT (or
the repository root): third_party_local/original/AirStrike3D.exe for as3d,
third_party_local/games/<key>/<exe of tools/games.json> for the others; the output is the
game's `texts` file of tools/games.json in assets_extracted/ (as3d) or
assets_extracted_games/<key>/. With --game the executable must be that game's.

Output format (read by as3d::ui::Texts, engine/src/ui/frontend_texts.cpp): UTF-8 text, one
entry per line, `key = "value"`; inside the quotes a backslash escapes `"` and `\\`; lines
starting with `#` and blank lines are ignored. Keys:
    info.N.title       title of Information page N (1..10)
    info.N.L           body line L (0-based line slot) of page N; missing slots are blank lines
    congrats.L         Game Complete line L (0..4; line 1 is blank in the original)
    rank.I             rank name I (0..6)
    info.hint.prev, info.hint.next, info.page   the page hints and the spinner label
    info.pages.N       spinner value "N of 10"
AirStrike 2 (tools/exe_texts/as2.json): the keys listed there. A `text_ml` value keeps its
line breaks, written `\\n` inside the quotes; a `u32` value is the number in decimal.

The line slot of each body string is read from the code that fills the page: the page builder
stores each string pointer into a per-line array with `mov dword [reg+disp8], imm32`, so the
slot is (disp8 - 0x14) / 4 and gaps are blank lines. If that instruction is not found the
lines are numbered in order.

Standard library only. The PE section table maps virtual addresses to file offsets.
"""

import argparse
import hashlib
import json
import os
import struct
import sys

IMAGE_BASE_EXPECTED = 0x400000

# The front-end texts of each known executable, chosen by its SHA-256. `None` (a sequel whose
# addresses are not mapped yet) means "no table": the tool says so and writes nothing.
# Table: pages = (page, first body string, title string), the body strings of a page lie in
# memory from `body` up to its title, in line order; congrats = (line, address);
# rank_table = address of 7 pointers; hints = (key, address); page_values = address of
# "10 of 10" down to "1 of 10", 8-byte aligned slots.
TABLES = {
    "3b371bc2a72dcf18c17b5efa1b7e08b85fef73cfdd28dce00ec0aa2f2e93df1d": {
        "game": "as3d",
        "table": {
            # Information pages (frontend.md 3.14; strings 0x449ea4..0x44b07c)
            "pages": [
                (1, 0x449EA4, 0x44A1F8),
                (2, 0x44A210, 0x44A430),
                (3, 0x44A448, 0x44A688),
                (4, 0x44A694, 0x44A850),
                (5, 0x44A870, 0x44AA94),
                (6, 0x44AAB4, 0x44AB3C),
                (7, 0x44AB5C, 0x44AD90),
                (8, 0x44ADA8, 0x44AE28),
                (9, 0x44AE40, 0x44AFF0),
                (10, 0x44AFF8, 0x44B07C),
            ],
            "congrats": [(0, 0x449D6C), (2, 0x449D80), (3, 0x449DBC), (4, 0x449DF0)],  # frontend.md 3.9
            "rank_table": 0x45650C,  # 7 pointers (frontend.md 5.11)
            "hints": [("info.hint.prev", 0x44B084), ("info.hint.next", 0x44B09C), ("info.page", 0x44B0B0)],
            "page_values": 0x449E50,
        },
    },
    # AirStrike 2 v2.51: the address list of docs/spec/as2/frontend.md 7.
    # The list's ctl.row.* addresses read the row table as ten records; it has thirteen, three
    # of them "-" separators, so the ten action names are at these addresses instead
    # (docs/spec/as2/issues/300-frontend-implementation-findings.md).
    "b24b62b2c5b61cfa1cf0aad781788aa777a2e4f4a385c73ba53014b039e46f5b": {"game": "as2", "table": {
        "json": "as2.json",
        "override": [("ctl.row.%d" % i, a) for i, a in enumerate(
            [0x48D6EC, 0x48D6DC, 0x48D6C8, 0x48D6B8, 0x48D6AC, 0x48D6A0, 0x48D690, 0x48D680, 0x48D674, 0x48D668])],
    }},
    # ---- Gulf Thunder v2.71 (package F2): the address list of docs/spec/gulf/frontend.delta.md 7.
    # Eleven of its addresses are not the first byte of their text and a few point at a
    # neighbour (docs/spec/gulf/issues/402-exe-texts-addresses.md): the ones whose text is
    # certain are read from where the text is; three helicopter names of AirStrike 2 (Gulf
    # Thunder has three helicopters), a page value of an eighth page and two broken credits
    # lines are left out (the game's built-in text is used).
    "86195a9653489064844c172ce43307c703a50e53be7e00d45fe346c45d5ae077": {"game": "gulf", "table": {
        "json": "gulf.json",
        "override": [
            ("title.top_scores", 0x48CBD0), ("title.enter_name", 0x48CBF0), ("title.hint", 0x48CD70),
            ("difficulty.3", 0x48CD34), ("difficulty.4", 0x48CD28), ("mode.0", 0x48CD18), ("mode.1", 0x48CD0C),
            ("button.ok", 0x48CC00), ("heli.2", 0x48BDA4),
        ] + [("info.pages.%d" % n, 0x48BE80 - 8 * (n - 1)) for n in range(3, 8)],
        "leave_out": ["heli.3", "heli.4", "heli.5", "info.pages.8", "credits.4", "credits.19"],
    }},
    # ---- end of Gulf Thunder
}


class NotMapped(Exception):
    """The executable is a known game's, but its text addresses are not mapped yet."""


class Pe:
    def __init__(self, data):
        self.data = data
        if data[:2] != b"MZ":
            raise ValueError("not an MZ executable")
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if pe + 24 > len(data) or data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("no PE header")
        nsec = struct.unpack_from("<H", data, pe + 6)[0]
        opt = struct.unpack_from("<H", data, pe + 20)[0]
        if opt < 32 or nsec == 0 or nsec > 96:
            raise ValueError("unexpected PE layout")
        self.base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
        self.sections = []
        o = pe + 24 + opt
        for _ in range(nsec):
            if o + 40 > len(data):
                raise ValueError("truncated section table")
            name = data[o:o + 8].rstrip(b"\0").decode("latin-1")
            vsize, va, rsize, roff = struct.unpack_from("<IIII", data, o + 8)
            self.sections.append((name, va, vsize, roff, rsize))
            o += 40

    def offset(self, addr):
        rva = addr - self.base
        for _name, va, vsize, roff, rsize in self.sections:
            if va <= rva < va + min(vsize, rsize):
                off = roff + rva - va
                if off < len(self.data):
                    return off
        raise ValueError("address 0x%x is not in the file" % addr)

    def cstr(self, addr, limit=256, multiline=False):
        o = self.offset(addr)
        end = self.data.find(b"\0", o, o + limit + 1)
        if end < 0:
            raise ValueError("no string terminator at 0x%x" % addr)
        raw = self.data[o:end]
        if any((b < 0x20 and not (multiline and b == 0x0A)) or b > 0x7E for b in raw):
            raise ValueError("non-text bytes at 0x%x" % addr)
        return raw.decode("ascii")

    def u32(self, addr):
        return struct.unpack_from("<I", self.data, self.offset(addr))[0]

    def section(self, name):
        for s in self.sections:
            if s[0] == name:
                return s
        return None


def strings_between(pe, start, stop):
    """Consecutive NUL-terminated strings from start up to (not including) stop."""
    out = []
    a = start
    while a < stop:
        s = pe.cstr(a)
        out.append((a, s))
        a += len(s) + 1
        while a < stop and pe.data[pe.offset(a)] == 0:
            a += 1
    return out


def line_slot(pe, text, addr):
    """Line slot from `mov dword [reg+disp8], imm32` (C7 /0, mod 01) storing addr, or None."""
    _name, va, _vsize, roff, rsize = text
    needle = struct.pack("<I", addr)
    blob = pe.data[roff:roff + rsize]
    i = blob.find(needle)
    while i >= 0:
        # c7 44 24 d8 imm32 (esp base, SIB) or c7 4r d8 imm32 (other bases)
        if i >= 4 and blob[i - 4] == 0xC7 and blob[i - 3] == 0x44 and blob[i - 2] == 0x24:
            disp = blob[i - 1]
        elif i >= 3 and blob[i - 3] == 0xC7 and (blob[i - 2] & 0xF8) == 0x40 and (blob[i - 2] & 7) != 4:
            disp = blob[i - 1]
        else:
            disp = None
        if disp is not None and disp >= 0x14 and (disp - 0x14) % 4 == 0 and (disp - 0x14) // 4 < 32:
            return (disp - 0x14) // 4
        i = blob.find(needle, i + 1)
    return None


def quote(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def extract_listed(pe, listing):
    """The entries of an address list (tools/exe_texts/<game>.json), (key, value) in its order."""
    if pe.base != int(listing["image_base"], 16):
        raise ValueError("unexpected image base 0x%x" % pe.base)
    entries = []
    for e in listing["entries"]:
        addr = int(e["address"], 16)
        if e["kind"] == "text":
            entries.append((e["key"], pe.cstr(addr, 512)))
        elif e["kind"] == "text_ml":
            entries.append((e["key"], pe.cstr(addr, 512, multiline=True)))
        elif e["kind"] == "u32":
            entries.append((e["key"], str(pe.u32(addr))))
        else:
            raise ValueError("unknown kind %r of %s" % (e["kind"], e["key"]))
    return entries


def load_listing(name):
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "exe_texts", name), encoding="utf-8") as f:
        return json.load(f)


def extract(pe, table):
    if pe.base != IMAGE_BASE_EXPECTED:
        raise ValueError("unexpected image base 0x%x" % pe.base)
    text = pe.section(".text")
    if text is None:
        raise ValueError("no .text section")
    entries = []
    for page, body, title in table["pages"]:
        entries.append(("info.%d.title" % page, pe.cstr(title)))
        lines = strings_between(pe, body, title)
        if not lines:
            raise ValueError("page %d has no text" % page)
        next_slot = 0
        used = set()
        for addr, s in lines:
            slot = line_slot(pe, text, addr)
            if slot is None or slot in used:
                slot = next_slot
            used.add(slot)
            next_slot = slot + 1
            entries.append(("info.%d.%d" % (page, slot), s))
    for line, addr in table["congrats"]:
        entries.append(("congrats.%d" % line, pe.cstr(addr)))
    for i in range(7):
        entries.append(("rank.%d" % i, pe.cstr(pe.u32(table["rank_table"] + 4 * i), 32)))
    for key, addr in table["hints"]:
        entries.append((key, pe.cstr(addr, 64)))
    for k in range(10):
        entries.append(("info.pages.%d" % (10 - k), pe.cstr(table["page_values"] + 8 * k + (0 if k == 0 else 4), 16)))
    return entries


def games_json():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "games.json"), encoding="utf-8") as f:
        return {g["key"]: g for g in json.load(f)["games"]}


def default_paths(root, game):
    """(exe, out) of a game under a data root (docs/spec/README.md, data layout)."""
    if game["key"] == "as3d":
        return (os.path.join(root, "third_party_local", "original", game["exe"]),
                os.path.join(root, "assets_extracted", game["texts"]))
    return (os.path.join(root, "third_party_local", "games", game["key"], game["exe"]),
            os.path.join(root, "assets_extracted_games", game["key"], game["texts"]))


def main():
    root = os.environ.get("AS3D_DATA_ROOT") or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    games = games_json()
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game", choices=sorted(games), help="the game (default as3d); the executable must be its own")
    ap.add_argument("--exe", help="the executable (default: the game's, under the data root)")
    ap.add_argument("--out", help="the output file (default: the game's `texts` file beside its extracted data)")
    args = ap.parse_args()
    game = games[args.game or "as3d"]
    exe, out = default_paths(root, game)
    exe = args.exe or exe
    out = args.out or out
    try:
        with open(exe, "rb") as f:
            data = f.read()
        digest = hashlib.sha256(data).hexdigest()
        entry = TABLES.get(digest)
        if entry is None:
            raise ValueError("an executable of no known game (sha256 %s)" % digest)
        if args.game and entry["game"] != args.game:
            raise ValueError("this is the executable of %s, not %s" % (entry["game"], args.game))
        game = games[entry["game"]]
        if args.exe is None or args.out is None:
            # A different game than the default was found by content: its own output name.
            _, gout = default_paths(root, game)
            out = args.out or gout
        if entry["table"] is None:
            raise NotMapped("texts of %s are not mapped yet" % game["title"])
        if "json" in entry["table"]:
            pe = Pe(data)
            entries = extract_listed(pe, load_listing(entry["table"]["json"]))
            fixed = {k: pe.cstr(a, 512) for k, a in entry["table"].get("override", [])}
            entries = [(k, fixed.get(k, v)) for k, v in entries]
            drop = set(entry["table"].get("leave_out", []))  # Gulf Thunder (issue gulf/402)
            entries = [(k, v) for k, v in entries if k not in drop]
        else:
            entries = extract(Pe(data), entry["table"])
    except NotMapped as e:
        print("extract_exe_texts: %s: %s" % (exe, e), file=sys.stderr)
        return 1
    except (OSError, ValueError, struct.error) as e:
        print("extract_exe_texts: %s: %s" % (exe, e), file=sys.stderr)
        print("extract_exe_texts: this tool needs the executable of one of: %s"
              % ", ".join("%s (%s)" % (g["title"], g["exe"]) for g in games.values()), file=sys.stderr)
        return 1
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    tmp = out + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write("# %s v%s front-end texts, read from the user's executable by\n" % (game["title"], game["version"]))
        f.write("# tools/extract_exe_texts.py. Do not commit. Format: key = \"value\".\n")
        for k, v in entries:
            f.write("%s = %s\n" % (k, quote(v)))
    os.replace(tmp, out)
    print("extract_exe_texts: wrote %d entries to %s" % (len(entries), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
