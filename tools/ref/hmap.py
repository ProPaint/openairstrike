#!/usr/bin/env python3
"""Reference parser for AirStrike 3D level files (maps/*.hsc, magic "HMAP").

See docs/spec/hmap.md for the format and for how the engine turns a map into
terrain and placed objects. Stdlib only for parsing; the `png` command needs
PIL (Pillow).

Usage:
  hmap.py info    <file.hsc>            header, tables and statistics
  hmap.py json    <file.hsc>            everything except the raw grids (summarised)
  hmap.py objects <file.hsc>            table of placements with resolved names
  hmap.py png     <file.hsc> <outdir>   pictures of every layer, a lit/textured
                                        preview and a placement map

Game data is located through AS3D_DATA_ROOT (default: the repository root),
then <root>/assets_extracted. A <file.hsc> argument that does not exist as a
path is also tried relative to <root>/assets_extracted and to its maps/ dir.
"""
from __future__ import annotations

import dataclasses
import hashlib
import json
import math
import os
import struct
import sys
from typing import Dict, List, Optional, Sequence, Tuple

MAGIC = b"HMAP"
VERSION = 2
HEADER_FMT = "<4s6I"
HEADER_SIZE = struct.calcsize(HEADER_FMT)  # 28
PLACEMENT_FIXED_FMT = "<HHHBBH"          # type, x, y, rotation, item, waypoint count
PLACEMENT_FIXED_SIZE = struct.calcsize(PLACEMENT_FIXED_FMT)  # 10
WAYPOINT_FMT = "<iii4ff"
WAYPOINT_SIZE = struct.calcsize(WAYPOINT_FMT)  # 32
CELL_BYTES = 4

# Engine limits (VERIFIED-CODE, loader 0x4091f0).
MAX_PLACEMENTS = 0x4000   # "Too many map objects."
MAX_TYPES = 1024          # size of the loader's local type table
MAX_ITEMS = 255           # item index is one byte, 0 = none

# Engine constants (VERIFIED-CODE, see docs/spec/hmap.md).
CELL_SIZE = 40.0          # world units per cell (0x416fd0, 0x4091f0, 0x407590)
CHUNK_ROWS = 8            # cell rows per render chunk (0x416fd0)
MAPTEX_ROWS = 32          # cell rows covered by one generated mapTexture<i> (0x4167f0)
MAPTEX_SIZE = 256         # generated map texture is 256x256 RGB (0x4167f0)
HEIGHT_BAND = 86.0        # terrain texture band width in raw height units (0x4167f0)
DETAIL_UV_PER_CELL = 0.25  # detail texture coordinate per cell (0x416fd0)
TILE_PIXELS = 64          # tile edge in tiles<N>.tga atlases (0x416d00)
ROTATION_STEP_DEG = 30.0  # placement rotation unit (0x407590)


class HmapError(ValueError):
    pass


@dataclasses.dataclass
class Waypoint:
    x: int                  # cell column of the curve point
    y: int                  # cell row of the curve point (0-based, unlike Placement.y)
    unknown8: int           # never read by the engine code examined (values 0, 1, 2)
    in_ctrl: Tuple[float, float]   # incoming Bezier control point, cell units
    out_ctrl: Tuple[float, float]  # outgoing Bezier control point, cell units
    delay: float            # returned by GetWaypointDelay

    def world(self) -> Tuple[float, float]:
        return (self.x * CELL_SIZE + CELL_SIZE / 2, self.y * CELL_SIZE + CELL_SIZE / 2)


@dataclasses.dataclass
class Placement:
    index: int              # position in the file
    offset: int             # file offset of the record
    type_index: int         # index into HmapFile.type_names
    x: int                  # cell column
    y: int                  # 1-based cell row: the object sits in row y-1
    rotation: int           # yaw in units of 30 degrees
    item_index: int         # 0 = none, else 1-based index into HmapFile.item_names
    script: Optional[str]   # per-placement script override, or None
    path_flag: int          # u16; only meaningful with waypoints: nonzero = closed loop
    waypoints: List[Waypoint]

    @property
    def row(self) -> int:
        return self.y - 1

    def world_position(self) -> Tuple[float, float, float]:
        """Spawn position as computed by the spawner (0x407590); z is 0 and is
        replaced by the terrain or water height for FL_ONGROUND / FL_ONWATER."""
        return (self.x * CELL_SIZE + CELL_SIZE / 2,
                self.y * CELL_SIZE + CELL_SIZE / 2 - CELL_SIZE,
                0.0)

    @property
    def yaw_degrees(self) -> float:
        return self.rotation * ROTATION_STEP_DEG

    @property
    def loops(self) -> bool:
        return bool(self.waypoints) and self.path_flag != 0


@dataclasses.dataclass
class HmapFile:
    version: int
    width: int
    height: int
    type_names: List[str]
    item_names: List[str]
    cells: bytes            # width*height*4 bytes, row-major, row 0 first
    placements: List[Placement]
    size: int
    type_table_offset: int = HEADER_SIZE
    item_table_offset: int = 0
    grid_offset: int = 0
    placements_offset: int = 0

    def layer(self, k: int) -> bytes:
        """k = 0 height, 1 tile set, 2 tile index, 3 tile rotation."""
        return self.cells[k::CELL_BYTES]

    @property
    def heights(self) -> bytes:
        return self.layer(0)

    def cell(self, col: int, row: int) -> Tuple[int, int, int, int]:
        i = (row * self.width + col) * CELL_BYTES
        return tuple(self.cells[i:i + CELL_BYTES])  # type: ignore[return-value]

    def type_name(self, p: Placement) -> str:
        return self.type_names[p.type_index]

    def item_name(self, p: Placement) -> Optional[str]:
        return self.item_names[p.item_index - 1] if p.item_index else None

    def spawn_order(self) -> List[Placement]:
        """Order in which the engine walks the placements: a stable sort by y
        (the loader bubble-sorts, swapping only on strictly greater y)."""
        return sorted(self.placements, key=lambda p: p.y)


# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------

def _read_names(data: bytes, off: int, count: int, what: str) -> Tuple[List[str], int]:
    names = []
    for i in range(count):
        if off >= len(data):
            raise HmapError(f"{what} {i}: truncated at offset {off}")
        n = data[off]
        if n == 0 or off + 1 + n > len(data):
            raise HmapError(f"{what} {i}: bad length {n} at offset {off}")
        raw = data[off + 1:off + 1 + n]
        if raw[-1] != 0 or 0 in raw[:-1]:
            raise HmapError(f"{what} {i}: string at offset {off} is not exactly NUL-terminated")
        names.append(raw[:-1].decode("latin-1"))
        off += 1 + n
    return names, off


def parse(data: bytes, source: str = "<bytes>") -> HmapFile:
    if len(data) < HEADER_SIZE:
        raise HmapError(f"{source}: file shorter than the header")
    magic, version, width, height, count, ntypes, nitems = struct.unpack_from(HEADER_FMT, data, 0)
    if magic != MAGIC:
        raise HmapError(f"{source}: bad magic {magic!r}")
    if version != VERSION:
        raise HmapError(f"{source}: unsupported version {version}")
    if count > MAX_PLACEMENTS:
        raise HmapError(f"{source}: {count} placements exceeds engine limit {MAX_PLACEMENTS}")
    if ntypes > MAX_TYPES or nitems > MAX_ITEMS:
        raise HmapError(f"{source}: table sizes {ntypes}/{nitems} exceed engine limits")
    if width < 2 or height < 2:
        raise HmapError(f"{source}: degenerate grid {width}x{height}")
    off = HEADER_SIZE
    types, off = _read_names(data, off, ntypes, "type name")
    item_off = off
    items, off = _read_names(data, off, nitems, "item name")
    grid_off = off
    grid_len = width * height * CELL_BYTES
    if off + grid_len > len(data):
        raise HmapError(f"{source}: grid truncated")
    cells = data[off:off + grid_len]
    off += grid_len
    pl_off = off
    placements = []
    for i in range(count):
        rec_off = off
        if off + PLACEMENT_FIXED_SIZE + 1 > len(data):
            raise HmapError(f"{source}: placement {i} truncated at {off}")
        t, x, y, rot, item, nwp = struct.unpack_from(PLACEMENT_FIXED_FMT, data, off)
        off += PLACEMENT_FIXED_SIZE
        n = data[off]
        off += 1
        script = None
        if n:
            raw = data[off:off + n]
            if len(raw) != n or raw[-1] != 0 or 0 in raw[:-1]:
                raise HmapError(f"{source}: placement {i}: bad script string at {off}")
            script = raw[:-1].decode("latin-1")
            off += n
        if off + 2 + WAYPOINT_SIZE * nwp > len(data):
            raise HmapError(f"{source}: placement {i} path truncated")
        (flag,) = struct.unpack_from("<H", data, off)
        off += 2
        wps = []
        for _ in range(nwp):
            wx, wy, u8, ix, iy, ox, oy, delay = struct.unpack_from(WAYPOINT_FMT, data, off)
            off += WAYPOINT_SIZE
            wps.append(Waypoint(wx, wy, u8, (ix, iy), (ox, oy), delay))
        placements.append(Placement(i, rec_off, t, x, y, rot, item, script, flag, wps))
    if off != len(data):
        raise HmapError(f"{source}: parsed {off} bytes but file has {len(data)}")
    return HmapFile(version, width, height, types, items, cells, placements, len(data),
                    HEADER_SIZE, item_off, grid_off, pl_off)


def parse_file(path: str) -> HmapFile:
    with open(path, "rb") as f:
        return parse(f.read(), source=path)


# ---------------------------------------------------------------------------
# Data location
# ---------------------------------------------------------------------------

def data_root() -> str:
    env = os.environ.get("AS3D_DATA_ROOT")
    if env:
        return env
    return os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def extracted_dir() -> str:
    return os.path.join(data_root(), "assets_extracted")


def asset_path(game_path: str) -> str:
    """Game-style relative path ("maps\\level1.hsc") to a local path."""
    return os.path.join(extracted_dir(), *game_path.replace("\\", "/").lower().split("/"))


def list_maps(root: Optional[str] = None) -> List[str]:
    d = os.path.join(root or extracted_dir(), "maps")
    if not os.path.isdir(d):
        return []
    return sorted(os.path.join(d, f) for f in os.listdir(d) if f.lower().endswith(".hsc"))


def resolve_arg(path: str) -> str:
    if os.path.isfile(path):
        return path
    for cand in (os.path.join(extracted_dir(), path), os.path.join(extracted_dir(), "maps", path)):
        if os.path.isfile(cand):
            return cand
    raise SystemExit(f"hmap: no such file: {path}")


def _textblock():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import textblock  # noqa: E402
    return textblock


def load_object_definitions(root: Optional[str] = None) -> Dict[str, dict]:
    """Block name -> {file, flags} for every objects/*.obj block. On a
    duplicate name the first definition wins, like the engine's lookup
    (0x409860 returns the first match)."""
    tb = _textblock()
    base = root or extracted_dir()
    d = os.path.join(base, "objects")
    defs: Dict[str, dict] = {}
    if not os.path.isdir(d):
        return defs
    for fn in sorted(os.listdir(d)):
        if not fn.lower().endswith(".obj"):
            continue
        tf = tb.parse_file(os.path.join(d, fn))
        for b in tf.blocks:
            if b.name in defs:
                continue
            flags = [s.args[0].text for s in b.statements if s.key == "flag" and s.args]
            script = b.find("script")
            defs[b.name] = {"file": f"objects/{fn}", "flags": flags,
                            "script": script.args[0].text if script and script.args else None}
    return defs


def load_level_list(root: Optional[str] = None) -> List[dict]:
    """maps/levels.txt blocks as dicts (only the keys the terrain needs)."""
    tb = _textblock()
    path = os.path.join(root or extracted_dir(), "maps", "levels.txt")
    if not os.path.isfile(path):
        return []
    out = []
    for b in tb.parse_file(path).blocks:
        rec: dict = {}
        for s in b.statements:
            k = s.key.lower()
            a = s.args
            if k in ("id", "name", "map", "music", "textures") and a:
                rec[k] = a[0].text
            elif k in ("hmin", "hmax", "enablehelic") and a:
                rec[k] = a[0].as_float()
            elif k == "fog" and len(a) >= 5:
                rec["fog"] = [t.as_float() for t in a[:5]]
            elif k == "sun" and len(a) >= 9:
                rec["sun"] = [t.as_float() for t in a[:9]]
            elif k == "water" and len(a) >= 3:
                rec["water"] = [a[0].text, a[1].as_float(), a[2].as_float()]
            elif k == "night":
                rec["night"] = True
            elif k == "intermission" and len(a) >= 6:
                rec["intermission"] = [t.as_float() for t in a[:6]]
        out.append(rec)
    return out


def level_for_map(path: str, levels: Sequence[dict]) -> Optional[dict]:
    base = os.path.basename(path).lower()
    for rec in levels:
        m = rec.get("map", "").replace("\\", "/").lower()
        if os.path.basename(m) == base:
            return rec
    return None


# ---------------------------------------------------------------------------
# Engine derivations (terrain), bit-exact where the spec says so
# ---------------------------------------------------------------------------

def resample(src: bytes, sw: int, sh: int, dw: int, dh: int, src_offset: int = 0) -> bytearray:
    """The engine's 8-bit bilinear resampler (0x4185b0): maps dw x dh output
    samples onto source positions 0..sw-1 / 0..sh-1 with 16.16 fixed-point
    steps, clamps the right/bottom neighbours at the edge, and truncates
    after each of the three interpolations."""
    out = bytearray(dw * dh)
    stepx = ((sw - 1) << 32) // ((dw - 1) << 16)
    stepy = ((sh - 1) << 32) // ((dh - 1) << 16)
    fy_acc = 0
    o = 0
    for _ in range(dh):
        iy = fy_acc >> 16
        fy = (fy_acc & 0xFFFF) / 65536.0
        fx_acc = 0
        rowbase = src_offset + iy * sw
        for _ in range(dw):
            ix = fx_acc >> 16
            fx = (fx_acc & 0xFFFF) / 65536.0
            a = src[rowbase + ix]
            if ix < sw - 1:
                b = src[rowbase + ix + 1]
                if iy < sh - 1:
                    c = src[rowbase + sw + ix]
                    d = src[rowbase + sw + ix + 1]
                else:
                    c, d = a, b
            else:
                b = a
                if iy < sh - 1:
                    c = src[rowbase + sw + ix]
                else:
                    c = a
                d = c
            top = int(a * (1.0 - fx) + b * fx) & 0xFF
            bot = int(c * (1.0 - fx) + d * fx) & 0xFF
            out[o] = int(top * (1.0 - fy) + bot * fy) & 0xFF
            o += 1
            fx_acc += stepx
        fy_acc += stepy
    return out


def vertex_heights(m: HmapFile) -> bytearray:
    """(width+1) x (height+1) raw vertex heights, row-major (0x416fd0 -> 0x4185b0)."""
    return resample(m.heights, m.width, m.height, m.width + 1, m.height + 1)


def raw_to_world_z(raw: int, hmin: float, hmax: float) -> float:
    return (hmax - hmin) * (raw / 255.0) + hmin


def vertex_positions(m: HmapFile, hmin: float, hmax: float) -> List[Tuple[float, float, float]]:
    vh = vertex_heights(m)
    w1 = m.width + 1
    return [((i % w1) * CELL_SIZE, (i // w1) * CELL_SIZE, raw_to_world_z(h, hmin, hmax))
            for i, h in enumerate(vh)]


def _norm(v):
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if length == 0:
        return v
    return (v[0] / length, v[1] / length, v[2] / length)


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def vertex_normals(m: HmapFile, pos) -> List[Tuple[float, float, float]]:
    """Face normals of the two triangles per cell, combined per vertex by the
    engine's component-wise acos / mean / cos rule, then normalised (0x416fd0)."""
    w, h, w1 = m.width, m.height, m.width + 1
    face = {}
    for r in range(h):
        for c in range(w):
            p = pos[r * w1 + c]
            p10 = pos[r * w1 + c + 1]
            p11 = pos[(r + 1) * w1 + c + 1]
            p01 = pos[(r + 1) * w1 + c]
            e1 = (p10[0] - p[0], p10[1] - p[1], p10[2] - p[2])
            e2 = (p11[0] - p[0], p11[1] - p[1], p11[2] - p[2])
            face[(c, r, 0)] = _norm(_cross(e1, e2))
            e1 = e2
            e2 = (p01[0] - p[0], p01[1] - p[1], p01[2] - p[2])
            face[(c, r, 1)] = _norm(_cross(e1, e2))
    out = []
    for r in range(h + 1):
        for c in range(w + 1):
            adj = []
            if c < w and r < h:
                adj += [face[(c, r, 0)], face[(c, r, 1)]]
            if c > 0:
                if r < h:
                    adj.append(face[(c - 1, r, 0)])
                if r > 0:
                    adj += [face[(c - 1, r - 1, 0)], face[(c - 1, r - 1, 1)]]
            if c < w and r > 0:
                adj.append(face[(c, r - 1, 1)])
            if not adj:
                out.append((0.0, 0.0, 0.0))
                continue
            n = tuple(math.cos(sum(math.acos(max(-1.0, min(1.0, f[k]))) for f in adj) / len(adj))
                      for k in range(3))
            out.append(_norm(n))
    return out


def vertex_colours(m: HmapFile, level: dict) -> List[Tuple[int, int, int]]:
    """Static per-vertex lighting (0x416fd0). `level` is a load_level_list() record."""
    hmin, hmax = level.get("hmin", -120.0), level.get("hmax", 130.0)
    sun = level.get("sun", [1, 1, 1, 0, 0, 1, 0, 0, 0])
    col, amb = sun[0:3], sun[6:9]
    ldir = _norm(tuple(sun[3:6]))
    water = level.get("water")
    pos = vertex_positions(m, hmin, hmax)
    nrm = vertex_normals(m, pos)
    out = []
    for p, n in zip(pos, nrm):
        f = 1.0
        if water and water[0]:
            wl = water[1]
            if p[2] < wl:
                f = (p[2] - hmin) / (wl - hmin)
        d = n[0] * ldir[0] + n[1] * ldir[1] + n[2] * ldir[2]
        if d > 1.0:
            d = 1.0
        if d > 0.0:
            rgb = tuple(min(255, int((col[k] * d + amb[k]) * 255.0 * f)) for k in range(3))
        else:
            rgb = tuple(int(amb[k]) for k in range(3))  # engine quirk, see spec
        out.append(rgb)
    return out


# ---------------------------------------------------------------------------
# Summaries
# ---------------------------------------------------------------------------

def layer_stats(m: HmapFile) -> List[dict]:
    out = []
    names = ("height", "tile_set", "tile_index", "tile_rotation")
    for k in range(CELL_BYTES):
        v = m.layer(k)
        out.append({"layer": k, "name": names[k], "min": min(v), "max": max(v),
                    "nonzero": sum(1 for b in v if b), "sha1": hashlib.sha1(v).hexdigest()})
    return out


def _f(x: float) -> str:
    return float(x).hex()


def placement_canonical(p: Placement) -> dict:
    return {"type": p.type_index, "x": p.x, "y": p.y, "rot": p.rotation, "item": p.item_index,
            "script": p.script, "flag": p.path_flag,
            "wp": [[w.x, w.y, w.unknown8, _f(w.in_ctrl[0]), _f(w.in_ctrl[1]),
                    _f(w.out_ctrl[0]), _f(w.out_ctrl[1]), _f(w.delay)] for w in p.waypoints]}


def placements_sha1(m: HmapFile) -> str:
    blob = json.dumps([placement_canonical(p) for p in m.placements], sort_keys=True,
                      separators=(",", ":")).encode()
    return hashlib.sha1(blob).hexdigest()


def to_json(m: HmapFile, defs: Optional[Dict[str, dict]] = None) -> dict:
    pls = []
    for p in m.placements:
        name = m.type_name(p)
        wx, wy, _ = p.world_position()
        d = {"index": p.index, "offset": p.offset, "type_index": p.type_index, "type": name,
             "x": p.x, "y": p.y, "row": p.row, "world": [wx, wy],
             "rotation": p.rotation, "yaw_deg": p.yaw_degrees,
             "item_index": p.item_index, "item": m.item_name(p), "script": p.script,
             "path_flag": p.path_flag, "loops": p.loops,
             "waypoints": [{"x": w.x, "y": w.y, "unknown8": w.unknown8, "in_ctrl": list(w.in_ctrl),
                            "out_ctrl": list(w.out_ctrl), "delay": w.delay} for w in p.waypoints]}
        if defs is not None:
            d["flags"] = defs.get(name, {}).get("flags")
        pls.append(d)
    return {"version": m.version, "width": m.width, "height": m.height, "size": m.size,
            "offsets": {"type_table": m.type_table_offset, "item_table": m.item_table_offset,
                        "grid": m.grid_offset, "placements": m.placements_offset},
            "type_names": m.type_names, "item_names": m.item_names,
            "layers": layer_stats(m), "placements": pls}


# ---------------------------------------------------------------------------
# Pictures
# ---------------------------------------------------------------------------

_SET_COLOURS = {1: (230, 60, 60), 2: (60, 200, 60), 3: (60, 120, 255), 4: (240, 200, 40), 5: (220, 60, 220)}


def _category(name: str, flags: List[str], has_path: bool) -> Tuple[int, int, int]:
    if name.startswith("item_"):
        return (255, 255, 0)
    if has_path:
        return (255, 60, 60)
    if "FL_ONWATER" in flags:
        return (0, 200, 255)
    if name.startswith(("palm", "cactus", "bush", "tree", "stone", "ruins", "grass", "elka", "snow")):
        return (40, 160, 40)
    return (255, 140, 0)


def _load_rgb(path: str):
    from PIL import Image
    return Image.open(path).convert("RGBA")


def write_pngs(m: HmapFile, path: str, outdir: str, level: Optional[dict] = None,
               defs: Optional[Dict[str, dict]] = None) -> List[str]:
    from PIL import Image, ImageDraw, ImageFont
    os.makedirs(outdir, exist_ok=True)
    base = os.path.splitext(os.path.basename(path))[0]
    W, H = m.width, m.height
    written = []

    def flip(img):  # world +y (scroll direction) points up on screen
        return img.transpose(Image.FLIP_TOP_BOTTOM)

    # 1. Height layer, greyscale, 4 px per cell.
    img = Image.frombytes("L", (W, H), bytes(m.heights))
    p = os.path.join(outdir, f"{base}_height.png")
    flip(img).resize((W * 4, H * 4), Image.NEAREST).save(p)
    written.append(p)

    # 2. Tile layers in false colour on a dim height background.
    img = Image.new("RGB", (W, H))
    px = img.load()
    hts = m.heights
    for r in range(H):
        for c in range(W):
            s, idx, rot = m.cell(c, r)[1:]
            g = hts[r * W + c] // 3
            if s:
                base_col = _SET_COLOURS.get(s, (255, 255, 255))
                k = 0.55 + 0.45 * ((idx % 8) / 7.0)
                px[c, r] = tuple(int(v * k) for v in base_col)
            else:
                px[c, r] = (g, g, g)
    p = os.path.join(outdir, f"{base}_tiles.png")
    flip(img).resize((W * 8, H * 8), Image.NEAREST).save(p)
    written.append(p)

    # 3. Textured + lit preview, 8 px per cell (the resolution of the engine's
    #    generated map textures), with tile overlays and water.
    scale = 8
    level = level or {}
    tex_dir = asset_path(level.get("textures", "textures\\desert"))
    hmin, hmax = level.get("hmin", -120.0), level.get("hmax", 130.0)
    preview = Image.new("RGB", (W * scale, H * scale))
    try:
        texs = [_load_rgb(os.path.join(tex_dir, f"texture{i}.tga")).convert("RGB").transpose(
            Image.FLIP_TOP_BOTTOM) for i in range(1, 5)]
        tex_px = [t.load() for t in texs]
        tsize = texs[0].size
    except (OSError, FileNotFoundError):
        texs = None
    if texs and W * scale == MAPTEX_SIZE:
        ppx = preview.load()
        for blk in range(H // MAPTEX_ROWS):
            hs = resample(m.heights, W, MAPTEX_ROWS, MAPTEX_SIZE, MAPTEX_SIZE,
                          src_offset=blk * MAPTEX_ROWS * W)
            for r in range(MAPTEX_SIZE):
                for c in range(MAPTEX_SIZE):
                    t = hs[r * MAPTEX_SIZE + c] / HEIGHT_BAND
                    i0 = int(math.floor(t))
                    i1 = int(math.ceil(t))
                    fr = t - i0
                    a = tex_px[i0][c % tsize[0], r % tsize[1]]
                    b = tex_px[i1][c % tsize[0], r % tsize[1]]
                    ppx[c, blk * MAPTEX_SIZE + r] = tuple(int(a[k] * (1 - fr) + b[k] * fr)
                                                          for k in range(3))
    else:
        g = Image.frombytes("L", (W, H), bytes(m.heights)).resize((W * scale, H * scale))
        preview = g.convert("RGB")
    # Tile overlays (alpha blended, as the engine draws them after the terrain).
    atlases: Dict[int, object] = {}
    for r in range(H):
        for c in range(W):
            s, idx, rot = m.cell(c, r)[1:]
            if not s:
                continue
            if s not in atlases:
                try:
                    atlases[s] = _load_rgb(os.path.join(extracted_dir(), "tiles", f"tiles{s}.tga"))
                except (OSError, FileNotFoundError):
                    atlases[s] = None
            at = atlases[s]
            if at is None:
                continue
            per_row = at.size[0] // TILE_PIXELS
            rows = at.size[1] // TILE_PIXELS
            tc, tr = idx % per_row, (idx // per_row) % rows
            tile = at.crop((tc * TILE_PIXELS, tr * TILE_PIXELS,
                            (tc + 1) * TILE_PIXELS, (tr + 1) * TILE_PIXELS))
            # rotation byte 3/6/9: 90/180/270 degrees counter-clockwise on screen
            tile = tile.rotate({3: 90, 6: 180, 9: 270}.get(rot, 0))
            tile = tile.resize((scale, scale), Image.BILINEAR)
            # preview image is built with row 0 at the bottom; flip the tile to match
            tile = tile.transpose(Image.FLIP_TOP_BOTTOM)
            preview.paste(tile, (c * scale, r * scale), tile)
    # Lighting: modulate by bilinearly interpolated vertex colours.
    try:
        vcol = vertex_colours(m, level)
        lit = Image.new("RGB", (W + 1, H + 1))
        lit.putdata(vcol)
        lit = lit.resize((W * scale, H * scale), Image.BILINEAR)
        from PIL import ImageChops
        preview = ImageChops.multiply(preview, lit)
    except Exception:  # pragma: no cover - lighting is a nicety for the picture
        pass
    # Water: plane at the level's water height, drawn with its alpha.
    water = level.get("water")
    if water and water[0]:
        vh = vertex_heights(m)
        mask = Image.new("L", (W + 1, H + 1))
        mask.putdata([255 if raw_to_world_z(v, hmin, hmax) < water[1] else 0 for v in vh])
        mask = mask.resize((W * scale, H * scale), Image.BILINEAR).point(
            lambda v: int(v * max(0.0, min(1.0, water[2]))) if v > 127 else 0)
        try:
            wt = _load_rgb(asset_path(water[0])).convert("RGB").resize((64, 64))
            wimg = Image.new("RGB", preview.size)
            for yy in range(0, preview.size[1], 64):
                for xx in range(0, preview.size[0], 64):
                    wimg.paste(wt, (xx, yy))
        except (OSError, FileNotFoundError):
            wimg = Image.new("RGB", preview.size, (40, 80, 160))
        preview = Image.composite(wimg, preview, mask)
    p = os.path.join(outdir, f"{base}_preview.png")
    flip(preview).save(p)
    written.append(p)

    # 4. Placement map: preview (dimmed) + dots, waypoint curves and labels.
    scale2 = 16
    pm = flip(preview).resize((W * scale2, H * scale2), Image.BILINEAR)
    pm = Image.blend(pm, Image.new("RGB", pm.size, (0, 0, 0)), 0.35)
    dr = ImageDraw.Draw(pm)
    font = ImageFont.load_default()
    Ht = H * scale2

    def scr(wx, wy):
        return (wx / CELL_SIZE * scale2, Ht - wy / CELL_SIZE * scale2)

    defs = defs or {}
    for pl in m.placements:
        name = m.type_name(pl)
        flags = defs.get(name, {}).get("flags", [])
        colr = _category(name, flags, bool(pl.waypoints))
        if pl.waypoints:
            pts = path_polyline(pl)
            dr.line([scr(*q) for q in pts], fill=(255, 90, 90), width=2)
        wx, wy, _ = pl.world_position()
        sx, sy = scr(wx, wy)
        dr.ellipse((sx - 4, sy - 4, sx + 4, sy + 4), fill=colr, outline=(0, 0, 0))
        ang = math.radians(pl.yaw_degrees)
        dr.line((sx, sy, sx - 8 * math.sin(ang), sy - 8 * math.cos(ang)), fill=(0, 0, 0))
        label = name + (f" +{m.item_name(pl)}" if pl.item_index else "")
        dr.text((sx + 6, sy - 6), label, fill=(255, 255, 255), font=font)
    p = os.path.join(outdir, f"{base}_placements.png")
    pm.save(p)
    written.append(p)
    return written


def path_polyline(p: Placement, steps: int = 16) -> List[Tuple[float, float]]:
    """World-space points along the placement's Bezier path (0x405dd0 shape)."""
    wps = p.waypoints
    n = len(wps)
    if n < 2:
        return [w.world() for w in wps]
    segs = n if p.loops else n - 1
    pts = []

    def s(v):
        return v * CELL_SIZE + CELL_SIZE / 2

    for i in range(segs):
        a, b = wps[i], wps[(i + 1) % n]
        P0 = (s(a.x), s(a.y))
        P1 = (s(a.out_ctrl[0]), s(a.out_ctrl[1]))
        P2 = (s(b.in_ctrl[0]), s(b.in_ctrl[1]))
        P3 = (s(b.x), s(b.y))
        for k in range(steps + 1):
            t = k / steps
            u = 1 - t
            pts.append(tuple(P0[j] * u * u * u + 3 * P1[j] * u * u * t + 3 * P2[j] * u * t * t
                             + P3[j] * t * t * t for j in range(2)))
    return pts


# ---------------------------------------------------------------------------
# Command line
# ---------------------------------------------------------------------------

def _cmd_info(m: HmapFile, path: str) -> None:
    print(f"{path}: HMAP v{m.version}, {m.size} bytes")
    print(f"  grid {m.width} x {m.height} cells ({m.width * CELL_SIZE:g} x {m.height * CELL_SIZE:g} world units)")
    print(f"  offsets: types {m.type_table_offset}, items {m.item_table_offset}, "
          f"grid {m.grid_offset}, placements {m.placements_offset}")
    print(f"  {len(m.type_names)} object types, {len(m.item_names)} item types, "
          f"{len(m.placements)} placements")
    for s in layer_stats(m):
        print(f"  layer {s['layer']} {s['name']:<14} min {s['min']:3d} max {s['max']:3d} "
              f"nonzero {s['nonzero']:5d}")
    npath = sum(1 for p in m.placements if p.waypoints)
    nloop = sum(1 for p in m.placements if p.loops)
    nscript = sum(1 for p in m.placements if p.script)
    nitem = sum(1 for p in m.placements if p.item_index)
    print(f"  placements with path {npath} (looping {nloop}), with script {nscript}, "
          f"carrying an item {nitem}")
    print("  types: " + " ".join(m.type_names))
    print("  items: " + " ".join(m.item_names))


def _cmd_objects(m: HmapFile, defs: Dict[str, dict]) -> None:
    print(f"{'#':>4} {'type':<28} {'x':>3} {'y':>4} {'rot':>4} {'item':<22} {'wp':>3} "
          f"{'loop':>4} flags / script")
    for p in m.spawn_order():
        name = m.type_name(p)
        d = defs.get(name)
        fl = ",".join(d["flags"]) if d else "UNRESOLVED"
        extra = f" script={p.script}" if p.script else ""
        print(f"{p.index:4d} {name:<28} {p.x:3d} {p.y:4d} {p.yaw_degrees:4.0f} "
              f"{(m.item_name(p) or '-'):<22} {len(p.waypoints):3d} "
              f"{('yes' if p.loops else '-'):>4} {fl}{extra}")


def main(argv: Sequence[str]) -> int:
    if len(argv) < 3 or argv[1] not in ("info", "json", "png", "objects"):
        print(__doc__, file=sys.stderr)
        return 2
    cmd = argv[1]
    path = resolve_arg(argv[2])
    try:
        m = parse_file(path)
    except HmapError as e:
        print(f"hmap: {e}", file=sys.stderr)
        return 1
    if cmd == "info":
        _cmd_info(m, path)
    elif cmd == "json":
        json.dump(to_json(m, load_object_definitions()), sys.stdout, indent=1)
        sys.stdout.write("\n")
    elif cmd == "objects":
        _cmd_objects(m, load_object_definitions())
    elif cmd == "png":
        if len(argv) < 4:
            print("hmap: png needs an output directory", file=sys.stderr)
            return 2
        level = level_for_map(path, load_level_list())
        for p in write_pngs(m, path, argv[3], level, load_object_definitions()):
            print(p)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except BrokenPipeError:  # e.g. piped into head
        sys.stderr.close()
        sys.exit(0)
