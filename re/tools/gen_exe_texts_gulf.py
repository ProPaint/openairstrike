#!/usr/bin/env python3
"""Writes tools/exe_texts/gulf.json: the addresses (never the texts) of the front-end texts
compiled into Gulf Thunder's executable, under the keys of tools/exe_texts/as2.json
(docs/spec/as2/frontend.md section 7; differences in docs/spec/gulf/frontend.delta.md section 7).

    AS3D_DATA_ROOT=<main checkout> python3 re/tools/gen_exe_texts_gulf.py DATAMAP.json [out.json]

DATAMAP.json is `match_data.py --from as2 --to gulf --dump`. Each AirStrike 2 entry is placed
in Gulf Thunder by, in order: (1) the data map (an instruction of a matched function uses the
address); (2) the pointer that holds it: a pointer to the AirStrike 2 text stored at a data
address that the data map (or a table start plus the same offset) places in Gulf Thunder, whose
Gulf Thunder pointer is followed; (3) a unique identical string in Gulf Thunder's data.
Entries found by none are listed under "removed" (the text does not exist in Gulf Thunder).
Dialogue pages are regenerated from Gulf Thunder's own dialogue table (gulf@0x0049b378).
Every result is checked: a text entry must be a non-empty printable string, a u32 entry 0 or 1.
Standard library only; prints counts, never texts.
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import re_export as R  # noqa: E402

DIALOGS = 0x0049B378


def printable(s):
    return s and all(32 <= ord(c) < 127 or c == "\n" for c in s)


def main():
    dm = {int(k, 16): int(v[0], 16) for k, v in json.load(open(sys.argv[1])).items()}
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(REPO, "tools", "exe_texts", "gulf.json")
    A = R.PE(os.path.join(R.DATA_ROOT, R.EXES["as2"]))
    G = R.PE(os.path.join(R.DATA_ROOT, R.EXES["gulf"]))
    src = json.load(open(os.path.join(REPO, "tools", "exe_texts", "as2.json")))

    def words(pe, sec):
        for n, va, vs, ro, rs in pe.sections:
            if n == sec:
                size = min(vs, rs) // 4 * 4
                return va, struct.unpack("<%dI" % (size // 4), pe.data[ro:ro + size])
        return 0, ()
    # where AirStrike 2 stores pointers to each address
    holders = {}
    for sec in (".data", ".rdata"):
        va, ws = words(A, sec)
        for i, w in enumerate(ws):
            holders.setdefault(w, []).append(va + 4 * i)
    gstr = {}
    for sec in (".rdata", ".data"):
        for n, va, vs, ro, rs in G.sections:
            if n != sec:
                continue
            blob = G.data[ro:ro + rs]
            i = 0
            while i < len(blob):
                j = blob.find(b"\0", i)
                if j < 0:
                    break
                if j - i >= 3:
                    gstr.setdefault(blob[i:j], []).append(va + i)
                i = j + 1

    def mapped(a):
        if a in dm:
            return dm[a]
        for d in range(4, 0x40, 4):  # inside a table whose start is mapped
            if a - d in dm:
                return dm[a - d] + d
        return None
    entries, removed, how = [], [], {"map": 0, "pointer": 0, "string": 0}
    for e in src["entries"]:
        if e["key"].startswith("dialog."):
            continue
        a = int(e["address"], 16)
        g = None
        if e["kind"] == "u32":
            g = mapped(a)
            ok = g is not None and G.u32(g) in (0, 1)
            if ok:
                how["map"] += 1
        else:
            s_as2 = A.cstr(a, 4096)
            g = mapped(a)
            if g is not None and printable(G.cstr(g, 4096) or ""):
                how["map"] += 1
            else:
                g = None
                for h in holders.get(a, []):
                    gh = mapped(h)
                    if gh is not None:
                        p = G.u32(gh)
                        if p and printable(G.cstr(p, 4096) or ""):
                            g = p
                            how["pointer"] += 1
                            break
                if g is None and s_as2:
                    c = gstr.get(s_as2.encode("latin1"), [])
                    if len(c) == 1:
                        g = c[0]
                        how["string"] += 1
            ok = g is not None
        if ok:
            ne = dict(e, address="0x%08x" % g)
            entries.append(ne)
        else:
            removed.append(e["key"])
    # dialogues: Gulf Thunder's own table, mission * 2 + end
    nd = 0
    for i in range(48):
        rec = G.u32(DIALOGS + 4 * i)
        if not rec:
            continue
        m, which = i // 2 + 1, "end" if i % 2 else "start"
        k = 0
        while True:
            speaker, text = G.u32(rec + 8 * k), G.u32(rec + 8 * k + 4)
            if not text:
                break
            entries.append({"key": "dialog.%d.%s.%d" % (m, which, k), "address": "0x%08x" % text,
                            "kind": "text_ml"})
            entries.append({"key": "dialog.%d.%s.%d.speaker" % (m, which, k),
                            "address": "0x%08x" % (rec + 8 * k), "kind": "u32"})
            nd += 1
            k += 1
    for e in entries:
        a = int(e["address"], 16)
        if e["kind"] == "u32":
            assert G.u32(a) in (0, 1), e["key"]
        else:
            assert printable(G.cstr(a, 4096)), e["key"]
    import hashlib
    doc = {
        "game": "gulf",
        "exe": "AirStrike3D II - Gulf.exe",
        "exe_sha256": hashlib.sha256(G.data).hexdigest(),
        "image_base": "0x00400000",
        "address_to_file_offset": "file offset = address - image_base - section virtual address + section raw offset; .rdata at virtual 0x47e000, raw 0x7e000 (offset = address - 0x400000); .data at virtual 0x496000, raw 0x96000",
        "kinds": src["kinds"],
        "spec": "docs/spec/as2/frontend.md section 7 (keys); docs/spec/gulf/frontend.delta.md section 7 (differences)",
        "entries": entries,
        "removed": removed,
    }
    with open(out, "w") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")
    print("wrote %s: %d entries (%s), %d dialogue pages, %d AirStrike 2 keys without a Gulf Thunder text"
          % (out, len(entries), ", ".join("%s %d" % kv for kv in how.items()), nd, len(removed)))


if __name__ == "__main__":
    main()
