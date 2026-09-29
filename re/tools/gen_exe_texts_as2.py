#!/usr/bin/env python3
"""Writes tools/exe_texts/as2.json: the addresses (never the texts) of the front-end texts compiled
into AirStrike 2's executable, with their kind, and checks every address against the user's copy
of the executable (each text entry must be a non-empty printable ASCII string; each u32 entry 0 or
1). The key scheme and the meaning of each key are in docs/spec/as2/frontend.md section 7.

Usage:
    AS3D_DATA_ROOT=<main checkout> python3 re/tools/gen_exe_texts_as2.py [out.json]

Default output: tools/exe_texts/as2.json next to this repository. Tables in the executable
(difficulty, rank, camera and helicopter names, controls rows, page labels) are followed through
their pointers; the portrait dialogues are found by walking the dialogue table (as2@0x49d530).
Standard library only.
"""
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA_ROOT = os.environ.get("AS3D_DATA_ROOT", ROOT)
EXE = os.path.join(DATA_ROOT, "third_party_local", "games", "as2", "AirStrike3D II.exe")


class Pe:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        d = self.data
        pe = struct.unpack_from('<I', d, 0x3C)[0]
        nsec = struct.unpack_from('<H', d, pe + 6)[0]
        opt = struct.unpack_from('<H', d, pe + 20)[0]
        self.base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
        self.sections = []
        o = pe + 24 + opt
        for _ in range(nsec):
            name = d[o:o + 8].rstrip(b'\0').decode('latin-1')
            vsize, va, rsize, roff = struct.unpack_from('<IIII', d, o + 8)
            self.sections.append((name, va, vsize, roff, rsize))
            o += 40

    def off(self, addr):
        rva = addr - self.base
        for name, va, vs, ro, rs in self.sections:
            if va <= rva < va + max(vs, rs):
                if rva - va >= rs:
                    return None
                return ro + rva - va
        return None

    def u32(self, a):
        o = self.off(a)
        return None if o is None else struct.unpack_from('<I', self.data, o)[0]

    def i32(self, a):
        o = self.off(a)
        return None if o is None else struct.unpack_from('<i', self.data, o)[0]

    def f32(self, a):
        o = self.off(a)
        return None if o is None else struct.unpack_from('<f', self.data, o)[0]

    def f64(self, a):
        o = self.off(a)
        return None if o is None else struct.unpack_from('<d', self.data, o)[0]

    def cstr(self, a, lim=512):
        o = self.off(a)
        if o is None:
            return None
        e = self.data.find(b'\0', o, o + lim)
        return self.data[o:e].decode('latin-1')



p = Pe(EXE)

E = []  # (key, address, kind, note)


def t(key, addr, note=''):
    E.append((key, addr, None, note))


def u32(key, addr, note=''):
    E.append((key, addr, 'u32', note))


# --- titles and labels
t('title.start_game', 0x48ee84)
t('title.options', 0x48ebc4, 'Options panel title; same string as button.options')
t('title.controls', 0x48d728)
t('title.heli', 0x48de98)
t('title.mission_complete', 0x48d934)
t('title.top_scores', 0x48eedc)
t('title.enter_name', 0x48ed24)
t('title.exit', 0x48d9a0)
t('title.hint', 0x48eeb0)
t('title.game_over', 0x48dd34)
t('label.exit', 0x48d980)
t('label.difficulty', 0x48ee90)
t('label.game_mode', 0x48ee9c)
t('label.player', 0x48deac)
for i in range(5):
    t('difficulty.%d' % i, p.u32(0x49e6d8 + 4 * i), 'pointer table 0x49e6d8')
t('mode.0', 0x48ee44, 'first value of the game mode spinner')
t('mode.1', 0x48ee38, 'second value (two players)')
# --- buttons (captions keep their padding spaces: they set the button width)
t('button.start_game', 0x48ece8)
t('button.top_scores', 0x48ecf8)
t('button.options', 0x48ebc4)
t('button.information', 0x48ed08)
t('button.credits', 0x48ed18)
t('button.quit', 0x48ebd0, 'main menu and in-game menu')
t('button.quit_wide', 0x48d954, 'mission complete and game over')
t('button.yes', 0x48d9b0)
t('button.no', 0x48d9b8)
t('button.back', 0x48d794)
t('button.back_wide', 0x48d8b0, 'Options, Credits and Information screens')
t('button.next', 0x48eea8, 'Start Game screen')
t('button.next_wide', 0x48d974, 'mission complete')
t('button.start', 0x48dec0)
t('button.continue', 0x48dd28)
t('button.accept', 0x48deb4)
t('button.restart', 0x48d948)
t('button.resume', 0x48ebb8)
t('button.choose_heli', 0x48d960)
t('button.configure_controls', 0x48ee18)
t('button.apply', 0x48ee30)
t('button.ok', 0x48ed34)
# --- statistics, rank, messages
t('stat.enemies', 0x48d8c8)
t('stat.stars', 0x48d8e4)
t('stat.rank', 0x48d900)
t('msg.new_heli', 0x48d914)
for i in range(7):
    t('rank.%d' % i, p.u32(0x49cba4 + 4 * i), 'pointer table 0x49cba4')
# --- helicopter selection
for i in range(6):
    t('heli.%d' % i, p.u32(0x49cbec + 4 * i), 'pointer table 0x49cbec, helicopter table order')
t('heli.speed', 0x48de88)
t('heli.armor', 0x48de90)
t('heli.na', 0x48de78)
# --- top scores
t('scores.number', 0x48eec0)
t('scores.name', 0x48eec4)
t('scores.score', 0x48eecc)
t('scores.rank', 0x48eed4)
# --- options
for k, a in [('resolution', 0x48ed90), ('refresh', 0x48ed9c), ('depth', 0x48edac), ('fullscreen', 0x48edbc),
             ('brightness', 0x48edc8), ('sfx', 0x48edd4), ('music', 0x48ede4), ('sound3d', 0x48edf4),
             ('camera', 0x48ee00), ('mouse', 0x48ee08)]:
    t('opt.' + k, a)
t('opt.refresh.default', 0x48ed80)
t('opt.off', 0x48ed7c)
t('opt.on', 0x48ed78)
t('opt.depth.0', 0x48ed64)
t('opt.depth.16', 0x48ed44)
t('opt.depth.32', 0x48ed3c)
for i in range(4):
    t('camera.%d' % i, p.u32(0x49e6b4 + 4 * i), 'pointer table 0x49e6b4')
# --- controls
t('ctl.set', 0x48d784)
t('ctl.player.1', 0x48d52c)
t('ctl.player.2', 0x48d520)
for i in range(10):
    t('ctl.row.%d' % i, p.u32(0x49d080 + 20 * i), 'row table 0x49d080, stride 20')
# --- information
t('info.page', 0x48ebb0)
t('info.hint.prev', 0x48eb84)
t('info.hint.next', 0x48eb9c)
for i in range(8):
    t('info.pages.%d' % (i + 1), p.u32(0x49e684 + 4 * i), 'pointer table 0x49e684')
INFO = {  # page: (title, [(slot, address)])  slots = (y - 184) / 18 from the emulator; blank slots omitted
    1: (0x48e150, [(0, 0x48df18), (1, 0x48df60), (2, 0x48dfa0), (3, 0x48dfe0), (5, 0x48dff0), (6, 0x48e020),
                   (8, 0x48e068), (9, 0x48e0b0), (10, 0x48e0f8), (11, 0x48e13c)]),
    2: (0x48e340, [(0, 0x48e15c), (1, 0x48e16c), (2, 0x48e1a8), (3, 0x48e1e4), (5, 0x48e200), (6, 0x48e210),
                   (7, 0x48e24c), (9, 0x48e260), (10, 0x48e270), (11, 0x48e2a4), (13, 0x48e2e0), (14, 0x48e2f0),
                   (15, 0x48e328)]),
    3: (0x48e50c, [(0, 0x48e360), (1, 0x48e36c), (2, 0x48e3ac), (3, 0x48e3e8), (4, 0x48e420), (6, 0x48e440),
                   (7, 0x48e450), (8, 0x48e48c), (10, 0x48e4bc), (11, 0x48e4c8), (12, 0x48e4f8)]),
    4: (0x48e5d0, [(0, 0x48e52c), (1, 0x48e53c), (2, 0x48e578), (4, 0x48e5ac), (5, 0x48e5bc)]),
    5: (0x48e828, [(0, 0x48e5f0), (1, 0x48e604), (2, 0x48e640), (4, 0x48e680), (5, 0x48e690), (7, 0x48e6c8),
                   (8, 0x48e6e8), (9, 0x48e728), (10, 0x48e764), (12, 0x48e76c), (13, 0x48e788), (14, 0x48e7c4),
                   (15, 0x48e7fc)]),
    6: (0x48e8c0, [(0, 0x48e840), (1, 0x48e854), (2, 0x48e88c)]),
    7: (0x48ea84, [(0, 0x48e8d8), (1, 0x48e8e8), (2, 0x48e924), (4, 0x48e950), (5, 0x48e960), (7, 0x48e98c),
                   (8, 0x48e99c), (10, 0x48e9d8), (11, 0x48e9ec), (12, 0x48ea24), (13, 0x48ea5c)]),
    8: (0x48eb70, [(0, 0x48ea98), (1, 0x48eaac), (2, 0x48eae0), (4, 0x48eb10), (5, 0x48eb20), (6, 0x48eb4c)]),
}
for page, (title, lines) in INFO.items():
    t('info.%d.title' % page, title)
    for slot, a in lines:
        t('info.%d.%d' % (page, slot), a)
# --- credits (line slot L at y = 120 + 18 L; blank slots omitted)
CREDITS = [(0, 0x48d79c), (1, 0x48d7ac), (3, 0x48d7c0), (4, 0x48d7cc), (6, 0x48d7dc), (7, 0x48d7f8),
           (8, 0x48d808), (10, 0x48d81c), (11, 0x48d830), (13, 0x48d840), (14, 0x48d850), (16, 0x48d864),
           (17, 0x48d86c), (19, 0x48d888), (20, 0x48d898)]
for slot, a in CREDITS:
    t('credits.%d' % slot, a)
# --- game complete (line L at y = 160 + 18 L; lines 1, 5, 9 are a one-space string, omitted)
for slot, a in [(0, 0x48db10), (2, 0x48db24), (3, 0x48db60), (4, 0x48db9c), (6, 0x48dbd0), (7, 0x48dc14),
                (8, 0x48dc50), (10, 0x48dc68)]:
    t('congrats.%d' % slot, a)
# --- loading, cheats
t('loading.label', 0x48a424)
for k, a in [('god_on', 0x48a018), ('god_off', 0x48a02c), ('lives', 0x48a054), ('weapons', 0x48a07c),
             ('missiles', 0x48a0a4), ('powerups', 0x48a0d0)]:
    t('cheat.' + k, a)
# --- portrait dialogues: walk the table (mission * 2 + end), pairs {u32 speaker, char* text}
for slot in range(36):
    ptr = p.u32(0x49d530 + 4 * slot)
    if not ptr:
        continue
    m, end = slot // 2 + 1, slot & 1
    i = 0
    while True:
        spk, txt = p.u32(ptr + 8 * i), p.u32(ptr + 8 * i + 4)
        if not txt:
            break
        base = 'dialog.%d.%s.%d' % (m, 'end' if end else 'start', i)
        t(base, txt, 'record at 0x%08x' % (ptr + 8 * i))
        u32(base + '.speaker', ptr + 8 * i, '0 officer, 1 pilot')
        i += 1

# --- validate and classify
out, seen = [], set()
counts = {}
for key, a, kind, note in E:
    assert key not in seen, key
    seen.add(key)
    if kind is None:
        s = p.cstr(a, 600)
        assert s is not None and len(s) > 0, (key, hex(a))
        bad = [c for c in s if not (0x20 <= ord(c) < 0x7f or c == '\n')]
        assert not bad, (key, hex(a))
        kind = 'text_ml' if '\n' in s else 'text'
    else:
        v = p.u32(a)
        assert v in (0, 1), (key, hex(a), v)
    counts[kind] = counts.get(kind, 0) + 1
    e = {'key': key, 'address': '0x%08x' % a, 'kind': kind}
    if note:
        e['note'] = note
    out.append(e)
doc = {
    'game': 'as2',
    'exe': 'AirStrike3D II.exe',
    'exe_sha256': 'b24b62b2c5b61cfa1cf0aad781788aa777a2e4f4a385c73ba53014b039e46f5b',
    'image_base': '0x00400000',
    'address_to_file_offset': 'file offset = address - image_base - section virtual address + section raw offset; '
                              'all texts are in .rdata (virtual address 0x47f000, raw offset 0x7f000, so offset = '
                              'address - 0x400000); the dialogue records are in .data (virtual 0x498000, raw 0x98000)',
    'kinds': {
        'text': 'NUL-terminated ASCII string at the address, one line; leading and trailing spaces are part of '
                'button captions (they set the button width)',
        'text_ml': 'NUL-terminated ASCII string whose lines are separated by a single LF byte (0x0A); an empty '
                   'line is LF LF',
        'u32': 'little-endian 32-bit integer at the address (not a text)',
    },
    'spec': 'docs/spec/as2/frontend.md section 7',
    'entries': out,
}
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'tools', 'exe_texts', 'as2.json')
with open(path, 'w') as f:
    json.dump(doc, f, indent=1)
    f.write('\n')
print(len(out), counts)
