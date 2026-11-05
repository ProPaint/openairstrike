#!/usr/bin/env python3
"""Extracts the front-end texts compiled into the user's own game executable.

The original keeps the Information pages, the Game Complete congratulations and the rank
names in its executable, not in the data paks (docs/spec/frontend.md 3.9, 3.14, 5.11; issue
080, decided: an import step reads them from the user's copy). This tool reads the strings out
of the user's copy and writes them to a gitignored text file that ships with the extracted
game data. The texts are never committed.

Two ways to find them (docs/spec/issues/170, section 3):

1. By address, for the builds the specs were taken from, chosen by the SHA-256 of the
   executable (TABLES below). The first game's table (as3d, v1.70) is what the addresses in
   frontend.md describe. AirStrike 2's is the committed address list tools/exe_texts/as2.json
   (docs/spec/as2/frontend.md 7: every entry a key, an address and a kind, `text`, `text_ml`
   or `u32`); Gulf Thunder's is tools/exe_texts/gulf.json the same way (its addresses
   corrected, docs/spec/gulf/issues/402 and 410).
2. By hash dictionary, for any other build of a game (the survey of issue 170 found twelve):
   tools/exe_texts/<key>.hashes.json maps every text key to the SHA-256 of the string's cp1251
   bytes and its length (a `u32` entry keeps its value: a speaker number, not a text), made
   once from the owner's executable by `--write-hashes`; the strings themselves are not in
   the repository. The executable is scanned for NUL-terminated runs of printable cp1251 bytes
   (tab, LF, CR and 0x20..0xFF, 1 to 2000 bytes), each run's hash is looked up, and the keys
   found become the texts, in the dictionary's order. The game is the one with the best
   share of its string keys found, 60 % at least; below that the file is no game executable.
   A key whose string is not in the file gets no line (the front end's defaults apply). For
   the owner's builds both ways give the same file (apps/tests/game_import_test.cpp).

Usage:
    tools/extract_exe_texts.py [--game KEY] [--exe PATH] [--out PATH]
    tools/extract_exe_texts.py --write-hashes [--game KEY] [--exe PATH]
    tools/extract_exe_texts.py --write-tables
    tools/extract_exe_texts.py --by-hash [--game KEY] [--exe PATH] [--out PATH]

Defaults: the game is as3d (or KEY); the executable is that game's under $AS3D_DATA_ROOT (or
the repository root): third_party_local/original/AirStrike3D.exe for as3d,
third_party_local/games/<key>/<exe of tools/games.json> for the others; the output is the
game's `texts` file of tools/games.json in assets_extracted/ (as3d) or
assets_extracted_games/<key>/. With --game the executable must be that game's.
--write-hashes needs an executable the address tables know and writes its game's
tools/exe_texts/<key>.hashes.json. --write-tables regenerates the hash dictionaries compiled
into engine/src/game/exe_texts.cpp and apps/web/site/files.js (the blocks between the
"BEGIN <key> hash list" and "END <key> hash list" lines) from the three .hashes.json files.

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

Two ports read the same tables and dictionaries and must stay in step: apps/web/site/files.js
(the web page) and engine/src/game/exe_texts.cpp (the Android import, as3d/game_import.h).
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
    # ---- Gulf Thunder v2.71: the address list tools/exe_texts/gulf.json. Its addresses were
    # corrected against the executable's strings (docs/spec/gulf/issues/402 and 410: the
    # shifted credits, the controls rows, the helicopter names, the page values); nothing is
    # overridden or left out here any more.
    "86195a9653489064844c172ce43307c703a50e53be7e00d45fe346c45d5ae077": {"game": "gulf", "table": {
        "json": "gulf.json",
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


# ---- texts by hash dictionary (issue 170, section 3)

HASH_MIN_SHARE = 0.6      # of a game's string keys found: below it the file is no game executable
HASH_MAX_RUN = 2000       # bytes of one NUL-terminated run looked at
EXE_TEXTS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "exe_texts")


def hashes_path(key):
    return os.path.join(EXE_TEXTS_DIR, key + ".hashes.json")


def load_hashes(key):
    """The dictionary of a game, or None when it has none: {"game", "texts": {key: entry}}."""
    path = hashes_path(key)
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def hashes_from_entries(entries, kinds):
    """The dictionary entries of extracted (key, value) pairs: sha256 and length of the cp1251
    bytes of a string, the value of a u32 (kinds: key -> "u32" for those)."""
    texts = {}
    for key, value in entries:
        if kinds.get(key) == "u32":
            texts[key] = {"kind": "u32", "value": int(value)}
        else:
            raw = value.encode("cp1251")
            texts[key] = {"sha256": hashlib.sha256(raw).hexdigest(), "len": len(raw)}
    return texts


def scan_runs(data, lengths):
    """Every NUL-terminated run of printable cp1251 bytes (tab, LF, CR, 0x20..0xFF) of 1 to
    HASH_MAX_RUN bytes whose length is in `lengths`: (sha256, bytes), each hash once."""
    found = {}
    start = -1
    for i, b in enumerate(data):
        if b == 0x09 or b == 0x0A or b == 0x0D or b >= 0x20:
            if start < 0:
                start = i
            continue
        if b == 0 and start >= 0:
            n = i - start
            if 1 <= n <= HASH_MAX_RUN and n in lengths:
                raw = data[start:i]
                h = hashlib.sha256(raw).hexdigest()
                if h not in found:
                    found[h] = raw
        start = -1
    return found


def extract_by_hashes(data, games):
    """(key, entries) of the game whose dictionary the strings of `data` match best, or None.
    `games`: tools/games.json's games by key, for the order of the dictionaries."""
    dicts = [(key, load_hashes(key)) for key in games]
    dicts = [(k, d) for k, d in dicts if d]
    lengths = set()
    for _key, d in dicts:
        for e in d["texts"].values():
            if "len" in e:
                lengths.add(e["len"])
    found = scan_runs(data, lengths)
    best, best_share = None, 0.0
    for key, d in dicts:
        strings = [e for e in d["texts"].values() if "sha256" in e]
        hits = sum(1 for e in strings if e["sha256"] in found)
        share = hits / len(strings) if strings else 0.0
        if share > best_share:
            best, best_share = (key, d), share
    if best is None or best_share < HASH_MIN_SHARE:
        return None
    key, d = best
    entries = []
    for k, e in d["texts"].items():
        if "sha256" in e:
            if e["sha256"] in found:
                entries.append((k, found[e["sha256"]].decode("cp1251")))
        else:
            entries.append((k, str(e["value"])))
    return key, entries, best_share


def extract_by_table(data, entry):
    """(game key, entries) through the address table `entry` (TABLES) of a known build."""
    game = entry["game"]
    if entry["table"] is None:
        raise NotMapped("texts of %s are not mapped yet" % game)
    if "json" in entry["table"]:
        pe = Pe(data)
        listing = load_listing(entry["table"]["json"])
        entries = extract_listed(pe, listing)
        fixed = {k: pe.cstr(a, 512) for k, a in entry["table"].get("override", [])}
        entries = [(k, fixed.get(k, v)) for k, v in entries]
        drop = set(entry["table"].get("leave_out", []))  # Gulf Thunder (issue gulf/402)
        entries = [(k, v) for k, v in entries if k not in drop]
        kinds = {e["key"]: e["kind"] for e in listing["entries"]}
    else:
        entries = extract(Pe(data), entry["table"])
        kinds = {}
    return game, entries, kinds


def write_hashes(key, entries, kinds, games):
    g = games[key]
    doc = {
        "comment": "The front-end texts of %s v%s as hashes: for every key of the address table, the SHA-256 of "
                   "the string's cp1251 bytes and its length (a u32 entry: its value, a speaker number). Made by "
                   "tools/extract_exe_texts.py --write-hashes from the owner's executable; lets the tool, "
                   "engine/src/game/exe_texts.cpp and apps/web/site/files.js read the texts of any other build "
                   "of the game (docs/spec/issues/170, section 3). No text is in here." % (g["title"], g["version"]),
        "game": key,
        "exe_sha256": next(h for h, e in TABLES.items() if e["game"] == key),
        "texts": hashes_from_entries(entries, kinds),
    }
    path = hashes_path(key)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")
    os.replace(tmp, path)
    n = len(doc["texts"])
    print("extract_exe_texts: wrote %d entries to %s" % (n, path))
    return path


def render_hash_tables(key, d):
    """The generated lines for C++ and JS: one entry per text, in the dictionary's order."""
    cpp, js = [], []
    for k, e in d["texts"].items():
        if "sha256" in e:
            cpp.append('    {"%s", "%s", %d, 0},' % (k, e["sha256"], e["len"]))
            js.append("      ['%s', '%s', %d]," % (k, e["sha256"], e["len"]))
        else:
            cpp.append('    {"%s", "", 0, %du},' % (k, e["value"]))
            js.append("      ['%s', '', %d, 'u']," % (k, e["value"]))
    return cpp, js


def replace_block(text, begin, end, lines, path):
    b = text.find(begin)
    e = text.find(end)
    if b < 0 or e < 0 or e < b:
        sys.exit("extract_exe_texts: no block %r .. %r in %s" % (begin, end, path))
    b = text.index("\n", b) + 1
    e = text.rfind("\n", 0, e) + 1
    return text[:b] + "\n".join(lines) + "\n" + text[e:]


def write_tables(games):
    """Regenerates the hash dictionaries of exe_texts.cpp and files.js from the .hashes.json files."""
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    targets = {
        os.path.join(repo, "engine", "src", "game", "exe_texts.cpp"): 0,
        os.path.join(repo, "apps", "web", "site", "files.js"): 1,
    }
    for path, which in targets.items():
        with open(path, encoding="utf-8") as f:
            text = f.read()
        for key in games:
            d = load_hashes(key)
            if not d:
                continue
            lines = render_hash_tables(key, d)[which]
            text = replace_block(text, "BEGIN %s hash list" % key, "END %s hash list" % key, lines, path)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("extract_exe_texts: updated %s" % path)



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
    ap.add_argument("--write-hashes", action="store_true",
                    help="write tools/exe_texts/<key>.hashes.json from the executable (a known build) instead of the texts")
    ap.add_argument("--write-tables", action="store_true",
                    help="regenerate the hash dictionaries of exe_texts.cpp and files.js from the .hashes.json files")
    ap.add_argument("--by-hash", action="store_true",
                    help="use the hash dictionaries even for a known build (to compare the two ways)")
    args = ap.parse_args()
    if args.write_tables:
        write_tables(games)
        return 0
    game = games[args.game or "as3d"]
    exe, out = default_paths(root, game)
    exe = args.exe or exe
    out = args.out or out
    try:
        with open(exe, "rb") as f:
            data = f.read()
        digest = hashlib.sha256(data).hexdigest()
        entry = None if args.by_hash and not args.write_hashes else TABLES.get(digest)
        how = "by address"
        if entry is not None:
            key, entries, kinds = extract_by_table(data, entry)
            if args.write_hashes:
                if args.game and key != args.game:
                    raise ValueError("this is the executable of %s, not %s" % (key, args.game))
                write_hashes(key, entries, kinds, games)
                return 0
        else:
            if args.write_hashes:
                raise ValueError("--write-hashes needs the executable of a known build (sha256 %s)" % digest)
            found = extract_by_hashes(data, games)
            if found is None:
                raise ValueError("an executable of no known game (sha256 %s): fewer than %d %% of any game's texts in it"
                                 % (digest, int(HASH_MIN_SHARE * 100)))
            key, entries, share = found
            strings = sum(1 for e in load_hashes(key)["texts"].values() if "sha256" in e)
            how = "by hash, %d of %d texts (%.1f %%), another build than v%s" % (
                sum(1 for k, _ in entries if "sha256" in load_hashes(key)["texts"][k]), strings, share * 100, games[key]["version"])
        if args.game and key != args.game:
            raise ValueError("this is the executable of %s, not %s" % (key, args.game))
        game = games[key]
        if args.exe is None or args.out is None:
            # A different game than the default was found by content: its own output name.
            _, gout = default_paths(root, game)
            out = args.out or gout
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
        f.write(header_lines(game))
        for k, v in entries:
            f.write("%s = %s\n" % (k, quote(v)))
    os.replace(tmp, out)
    print("extract_exe_texts: wrote %d entries to %s (%s)" % (len(entries), out, how))
    return 0


def header_lines(game):
    """The two comment lines every texts file starts with (the ports write the same)."""
    return ("# %s v%s front-end texts, read from the user's executable by\n" % (game["title"], game["version"]) +
            "# tools/extract_exe_texts.py. Do not commit. Format: key = \"value\".\n")


if __name__ == "__main__":
    sys.exit(main())
