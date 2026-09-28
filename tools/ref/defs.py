#!/usr/bin/env python3
"""Reference typed loader for objects/*.obj, weapons/*.wpn, particles/*.ps
and maps/levels.txt. Builds on textblock.py's generic parser.

See docs/spec/obj.md, docs/spec/wpn.md, docs/spec/ps.md,
docs/spec/levels-txt.md for the grammar, enum values and canonical
serialization this mirrors. The engine implementation
(engine/src/game/defs.cpp) must produce byte-identical canonical
serializations (and therefore identical sha1 hashes) for every definition;
that agreement is what testdata/golden/defs_summary.json checks.

Stdlib only.
"""
from __future__ import annotations

import dataclasses
import glob
import hashlib
import os
import re
import struct
import sys
from typing import Dict, List, Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import textblock as tb

# ---------------------------------------------------------------------------
# Enum tables (VERIFIED-CODE unless noted; see docs/spec/obj.md &c.)
# ---------------------------------------------------------------------------

OBJECT_TYPES = {"TYPE_MODEL": 0, "TYPE_SPRITE": 1, "TYPE_MARK": 2, "TYPE_HSPRITE": 3, "TYPE_VSPRITE": 4}
OBJECT_KIND_PLAYER, OBJECT_KIND_ENEMY, OBJECT_KIND_ITEM = 1, 2, 3

BLEND_MODES = {"BLEND_ALPHA": 1, "BLEND_ADD": 2, "BLEND_FILTER": 3}
ENV_MODES = {"ENV_GLITTER": 1, "ENV_CHROME": 2, "ENV_QUAD": 3}

# SHADOW_PLANAR*/_PROJECTED* alias after the original's post-parse remap.
SHADOW_MODES = {
    "SHADOW_PROJECTED": 1,
    "SHADOW_PROJECTED_LOW": 2,
    "SHADOW_PROJECTED_HIGH": 3,
    "SHADOW_PLANAR": 7,
    "SHADOW_PLANAR_LOW": 8,
    "SHADOW_PLANAR_HIGH": 9,
    "SHADOW_PLANAR_PROJECTED": 7,
    "SHADOW_PLANAR_PROJECTED_LOW": 8,
    "SHADOW_PLANAR_PROJECTED_HIGH": 9,
}

SORT_MODES = {"SORT_OPAQUE": 0, "SORT_TRANS": 2, "SORT_EFFECT": 3}
TOUCH_MODES = {"TOUCH_ENEMIES": 1, "TOUCH_PLAYER": 2, "TOUCH_ALL": 3}

RF_NOLIGHTING, RF_NOCULLING, RF_NODEPTHTEST, RF_NODEPTHWRITE = 0x1, 0x2, 0x4, 0x8
# Auto-applied whenever "envmap" is set (VERIFIED-CODE bit value); it has no
# keyword of its own, so it never appears in OBJ_RFLAGS below.
RF_ENVMAP_IMPLIED = 0x20
RF_NODLIGHT, RF_BANNER = 0x200, 0x10000
OBJ_RFLAGS = {
    "RF_NOLIGHTING": RF_NOLIGHTING,
    "RF_NODLIGHT": RF_NODLIGHT,
    "RF_NOCULLING": RF_NOCULLING,
    "RF_NODEPTHTEST": RF_NODEPTHTEST,
    "RF_NODEPTHWRITE": RF_NODEPTHWRITE,
    "RF_BANNER": RF_BANNER,
}
PS_RFLAGS = {
    "RF_NOLIGHTING": RF_NOLIGHTING,
    "RF_NOCULLING": RF_NOCULLING,
    "RF_NODEPTHTEST": RF_NODEPTHTEST,
    "RF_NODEPTHWRITE": RF_NODEPTHWRITE,
}

FL_ONGROUND, FL_ONGROUND_NORMAL_EXTRA, FL_ONWATER = 0x1, 0x2, 0x4
FL_NODRAW, FL_TEMPORARY, FL_NONTARGET, FL_POINT_COLLISION = 0x10, 0x20, 0x100, 0x1000
OBJ_FLAGS = {
    "FL_ONGROUND": FL_ONGROUND,
    "FL_ONGROUND_NORMAL": FL_ONGROUND | FL_ONGROUND_NORMAL_EXTRA,
    "FL_ONWATER": FL_ONWATER,
    "FL_NODRAW": FL_NODRAW,
    "FL_TEMPORARY": FL_TEMPORARY,
    "FL_NONTARGET": FL_NONTARGET,
    "FL_POINT_COLLISION": FL_POINT_COLLISION,
}

COORD_MODES = {"COORD_DECART": 0, "COORD_CILINDER": 1, "COORD_SPHERE": 2}
DRAW_MODES = {"DRAW_VERT": 1, "DRAW_HORIZ": 2}
EMIT_MODES = {"EMIT_ONCE": 1, "EMIT_DURATION": 2}
ANIM_MODES = {"ANIM_LINEAR": 1, "ANIM_NORMAL": 2, "ANIM_LOOP": 3}
FADE_MODES = {"FADE_LINEAR": 1, "FADE_EXP": 2}


def f2bits(v: float) -> int:
    return struct.unpack("<I", struct.pack("<f", v))[0]


def fhex(v: float) -> str:
    return "0x%08x" % f2bits(v)


def uhex(v: int) -> str:
    return "0x%08x" % (v & 0xFFFFFFFF)


# ---------------------------------------------------------------------------
# Typed records
# ---------------------------------------------------------------------------

@dataclasses.dataclass
class AttachDef:
    target: str = ""
    tag: str = ""
    id: str = ""
    absolute: bool = False
    night: bool = False
    line: int = 0


@dataclasses.dataclass
class ObjectDef:
    name: str = ""
    line: int = 0
    type: int = 0
    kind: int = 0
    model: str = ""
    skin: str = ""
    envmap: str = ""
    blend: int = 0
    envmode: int = 0
    rflag: int = 0
    shadow: int = 0
    sort: int = 0
    health: int = 0
    score: int = 0
    damage: int = 0
    flags: int = 0
    touch: int = 0
    scale: float = 0.0
    bbox_scale: List[float] = dataclasses.field(default_factory=lambda: [0.7, 0.7, 0.7])
    has_bbox: bool = False
    bbox_min: List[float] = dataclasses.field(default_factory=lambda: [0.0] * 4)
    bbox_max: List[float] = dataclasses.field(default_factory=lambda: [0.0] * 4)
    has_frames: bool = False
    frame_start: int = 0
    frame_end: int = 0
    has_light: bool = False
    light_radius: int = 0
    light_color: List[float] = dataclasses.field(default_factory=lambda: [0.0] * 3)
    has_light_dir: bool = False
    light_dir: List[float] = dataclasses.field(default_factory=lambda: [0.0] * 3)
    light_cone_angle: float = 0.0
    script: str = ""
    attachments: List[AttachDef] = dataclasses.field(default_factory=list)
    block: object = None  # the source TextBlock


@dataclasses.dataclass
class WeaponDef:
    name: str = ""
    line: int = 0
    missile: str = ""
    flash: str = ""
    speed: float = 0.0
    block: object = None


@dataclasses.dataclass
class ParticleSystemDef:
    name: str = ""
    line: int = 0
    texture: str = ""
    texture_frame_w: int = 0
    texture_frame_h: int = 0
    blend_mode: int = 0
    rflag: int = 0
    coords: int = 0
    draw_mode: int = 0
    emit_mode: int = 0
    emit_rate: float = 0.0
    life_time: float = 0.0
    init_offset: List[float] = dataclasses.field(default_factory=lambda: [0.0] * 6)
    init_velocity: List[float] = dataclasses.field(default_factory=lambda: [0.0] * 6)
    init_size: List[float] = dataclasses.field(default_factory=lambda: [0.0] * 2)
    has_init_frame: bool = False
    init_frame: List[int] = dataclasses.field(default_factory=lambda: [0, 0])
    init_color: List[float] = dataclasses.field(default_factory=lambda: [0.0, 0.0, 0.0, 1.0])
    accel: List[float] = dataclasses.field(default_factory=lambda: [0.0] * 3)
    fade_mode: int = 0
    fade_factor: float = 1.0
    size: float = 0.0
    anim_mode: int = 0
    has_damage: bool = False
    damage_touch: int = 0
    damage_amount: float = 0.0
    damage_param2: float = 0.0
    damage_param3: float = 0.0
    block: object = None


@dataclasses.dataclass
class LevelDef:
    line: int = 0
    id: str = ""
    name: str = ""
    map: str = ""
    music: str = ""
    textures: str = ""
    hmin: float = 0.0
    hmax: float = 0.0
    has_fog: bool = False
    fog_color: List[float] = dataclasses.field(default_factory=lambda: [0.0] * 3)
    fog_near: float = 0.0
    fog_far: float = 0.0
    sun: List[float] = dataclasses.field(default_factory=lambda: [0.0] * 9)
    has_water: bool = False
    water_texture: str = ""
    water_level: float = 0.0
    water_alpha: float = 0.0
    night: bool = False
    enable_helic: int = -1
    has_intermission: bool = False
    intermission: List[float] = dataclasses.field(default_factory=lambda: [0.0] * 6)
    block: object = None


class DefDatabase:
    def __init__(self):
        self.objects: List[ObjectDef] = []
        self.weapons: List[WeaponDef] = []
        self.particle_systems: List[ParticleSystemDef] = []
        self.levels: List[LevelDef] = []
        self.warnings: List[str] = []
        self._object_by_name: Dict[str, ObjectDef] = {}
        self._ps_by_name: Dict[str, ParticleSystemDef] = {}
        self._weapon_by_name: Dict[str, WeaponDef] = {}

    # -- loading -----------------------------------------------------------

    def load(self, root: str) -> bool:
        obj_files = sorted(glob.glob(os.path.join(root, "objects", "*.obj")))
        wpn_files = sorted(glob.glob(os.path.join(root, "weapons", "*.wpn")))
        ps_files = sorted(glob.glob(os.path.join(root, "particles", "*.ps")))
        levels_file = os.path.join(root, "maps", "levels.txt")

        if not obj_files:
            self.warnings.append("no objects/*.obj files found")
            return False

        for path in obj_files:
            self._load_objects(path)
        for path in wpn_files:
            self._load_weapons(path)
        for path in ps_files:
            self._load_particle_systems(path)
        if os.path.isfile(levels_file):
            self._load_levels(levels_file)
        else:
            self.warnings.append("maps/levels.txt not found")

        # First-defined-wins name lookup (VERIFIED-CODE: FUN_00409860 scans
        # in ascending load order and returns on the first match).
        for o in self.objects:
            key = o.name.lower()
            if key and key not in self._object_by_name:
                self._object_by_name[key] = o
        for p in self.particle_systems:
            key = p.name.lower()
            if key and key not in self._ps_by_name:
                self._ps_by_name[key] = p
        for w in self.weapons:
            key = w.name.lower()
            if key and key not in self._weapon_by_name:
                self._weapon_by_name[key] = w
        return True

    def _parse(self, path: str) -> tb.TextFile:
        with open(path, "rb") as f:
            return tb.parse_text_blocks(f.read())

    def _num(self, tok) -> float:
        return tok.as_float() if tok is not None else 0.0

    def _load_objects(self, path: str) -> None:
        tf = self._parse(path)
        base = os.path.basename(path)
        for b in tf.blocks:
            o = ObjectDef(name=b.name, line=b.line, block=b)
            attach_count_capped = False
            for s in b.statements:
                key = s.key.lower()
                a = s.args
                if key == "type":
                    o.type = OBJECT_TYPES.get(a[0].text, 0) if a else 0
                elif key == "model":
                    o.model = a[0].text if a else ""
                elif key == "skin":
                    o.skin = a[0].text if a else ""
                elif key == "envmap":
                    o.envmap = a[0].text if a else ""
                    o.rflag |= RF_ENVMAP_IMPLIED  # see docs/spec/obj.md
                elif key == "blend":
                    o.blend = BLEND_MODES.get(a[0].text, 0) if a else 0
                elif key == "envmode":
                    o.envmode = ENV_MODES.get(a[0].text, 0) if a else 0
                elif key == "rflag":
                    if a:
                        o.rflag |= OBJ_RFLAGS.get(a[0].text, 0)
                elif key == "shadow":
                    o.shadow = SHADOW_MODES.get(a[0].text, 0) if a else 0
                elif key == "sort":
                    o.sort = SORT_MODES.get(a[0].text, 0) if a else 0
                elif key == "health":
                    o.health = a[0].as_int() if a else 0
                elif key == "damage":
                    o.damage = a[0].as_int() if a else 0
                elif key == "score":
                    o.score = a[0].as_int() if a else 0
                elif key == "flag":
                    if a:
                        o.flags |= OBJ_FLAGS.get(a[0].text, 0)
                elif key == "touch":
                    o.touch = TOUCH_MODES.get(a[0].text, 0) if a else 0
                elif key == "player":
                    o.kind = OBJECT_KIND_PLAYER
                elif key == "enemy":
                    o.kind = OBJECT_KIND_ENEMY
                elif key == "item":
                    o.kind = OBJECT_KIND_ITEM
                elif key == "scale":
                    o.scale = self._num(a[0] if a else None)
                elif key == "bbox_scale":
                    o.bbox_scale = [self._num(a[i] if i < len(a) else None) for i in range(3)]
                elif key == "min":
                    o.has_bbox = True
                    o.bbox_min = [self._num(a[i] if i < len(a) else None) for i in range(4)]
                elif key == "max":
                    o.has_bbox = True
                    o.bbox_max = [self._num(a[i] if i < len(a) else None) for i in range(4)]
                elif key == "frames":
                    o.has_frames = True
                    o.frame_start = a[0].as_int() if len(a) > 0 else 0
                    o.frame_end = a[1].as_int() if len(a) > 1 else 0
                elif key == "light":
                    o.has_light = True
                    o.light_radius = a[0].as_int() if len(a) > 0 else 0
                    o.light_color = [self._num(a[i] if i < len(a) else None) for i in (1, 2, 3)]
                elif key == "light_dir":
                    o.has_light = True
                    o.has_light_dir = True
                    o.light_radius = a[0].as_int() if len(a) > 0 else 0
                    o.light_color = [self._num(a[i] if i < len(a) else None) for i in (1, 2, 3)]
                    o.light_dir = [self._num(a[i] if i < len(a) else None) for i in (4, 5, 6)]
                    o.light_cone_angle = float(a[7].as_int()) if len(a) > 7 else 0.0
                elif key == "script":
                    o.script = a[0].text if a else ""
                elif key == "attach":
                    if len(o.attachments) < 64:
                        o.attachments.append(self._parse_attach(a, s.line))
                    else:
                        attach_count_capped = True
                # "{"/"}" already excluded by the block parser; any other
                # key is genuinely unrecognized (never observed).
                elif key not in ("scale", "frames"):
                    pass
            if attach_count_capped:
                self.warnings.append(f"{base}: object '{b.name}' at line {b.line}: too many attach statements (>64), extra ones dropped")
            self.objects.append(o)

    def _parse_attach(self, args, line: int) -> AttachDef:
        i = 0
        n = len(args)
        ad = AttachDef(line=line)
        while i < n:
            t = args[i].text
            if not args[i].quoted and t == "abs":
                ad.absolute = True
                i += 1
            elif not args[i].quoted and t == "night":
                ad.night = True
                i += 1
            elif not args[i].quoted and t == "id" and i + 1 < n:
                ad.id = args[i + 1].text
                i += 2
            else:
                break
        if i < n:
            ad.target = args[i].text
            i += 1
        if i < n:
            ad.tag = args[i].text
            i += 1
        return ad

    def _load_weapons(self, path: str) -> None:
        tf = self._parse(path)
        for b in tf.blocks:
            w = WeaponDef(name=b.name, line=b.line, block=b)
            for s in b.statements:
                key = s.key.lower()
                a = s.args
                if key == "missile":
                    w.missile = a[0].text if a else ""
                elif key == "flash":
                    w.flash = a[0].text if a else ""
                elif key == "speed":
                    w.speed = self._num(a[0] if a else None)
            self.weapons.append(w)

    def _load_particle_systems(self, path: str) -> None:
        tf = self._parse(path)
        for b in tf.blocks:
            p = ParticleSystemDef(name=b.name, line=b.line, block=b)
            for s in b.statements:
                key = s.key.lower()
                a = s.args
                if key == "texture":
                    p.texture = a[0].text if a else ""
                    p.texture_frame_w = a[1].as_int() if len(a) > 1 else 0
                    p.texture_frame_h = a[2].as_int() if len(a) > 2 else 0
                elif key == "blend_mode":
                    p.blend_mode = BLEND_MODES.get(a[0].text, 0) if a else 0
                elif key == "rflag":
                    if a:
                        p.rflag |= PS_RFLAGS.get(a[0].text, 0)
                elif key == "coords":
                    p.coords = COORD_MODES.get(a[0].text, 0) if a else 0
                elif key == "draw_mode":
                    p.draw_mode = DRAW_MODES.get(a[0].text, 0) if a else 0
                elif key == "emit_mode":
                    p.emit_mode = EMIT_MODES.get(a[0].text, 0) if a else 0
                elif key == "emit_rate":
                    p.emit_rate = self._num(a[0] if a else None)
                elif key == "life_time":
                    p.life_time = self._num(a[0] if a else None)
                elif key == "init_offset":
                    p.init_offset = [self._num(a[i] if i < len(a) else None) for i in range(6)]
                elif key == "init_velocity":
                    p.init_velocity = [self._num(a[i] if i < len(a) else None) for i in range(6)]
                elif key == "init_size":
                    p.init_size = [self._num(a[i] if i < len(a) else None) for i in range(2)]
                elif key == "init_frame":
                    p.has_init_frame = True
                    p.init_frame = [a[i].as_int() if i < len(a) else 0 for i in range(2)]
                elif key == "init_color":
                    p.init_color = [self._num(a[i] if i < len(a) else None) for i in range(4)]
                elif key == "accel":
                    p.accel = [self._num(a[i] if i < len(a) else None) for i in range(3)]
                elif key == "fade_mode":
                    p.fade_mode = FADE_MODES.get(a[0].text, 0) if a else 0
                elif key == "fade_factor":
                    p.fade_factor = self._num(a[0] if a else None)
                elif key == "size":
                    p.size = self._num(a[0] if a else None)
                elif key == "anim_mode":
                    p.anim_mode = ANIM_MODES.get(a[0].text, 0) if a else 0
                elif key == "damage":
                    p.has_damage = True
                    p.damage_touch = TOUCH_MODES.get(a[0].text, 0) if a else 0
                    p.damage_amount = self._num(a[1] if len(a) > 1 else None)
                    p.damage_param2 = self._num(a[2] if len(a) > 2 else None)
                    p.damage_param3 = self._num(a[3] if len(a) > 3 else None)
            self.particle_systems.append(p)

    def _load_levels(self, path: str) -> None:
        tf = self._parse(path)
        for b in tf.blocks:
            lv = LevelDef(line=b.line, block=b)
            for s in b.statements:
                key = s.key.lower()
                a = s.args
                if key == "id":
                    lv.id = a[0].text if a else ""
                elif key == "name":
                    lv.name = a[0].text if a else ""
                elif key == "map":
                    lv.map = a[0].text if a else ""
                elif key == "music":
                    lv.music = a[0].text if a else ""
                elif key == "textures":
                    lv.textures = a[0].text if a else ""
                elif key == "hmin":
                    lv.hmin = self._num(a[0] if a else None)
                elif key == "hmax":
                    lv.hmax = self._num(a[0] if a else None)
                elif key == "fog":
                    lv.has_fog = True
                    lv.fog_color = [self._num(a[i] if i < len(a) else None) for i in range(3)]
                    lv.fog_near = self._num(a[3] if len(a) > 3 else None)
                    lv.fog_far = self._num(a[4] if len(a) > 4 else None)
                elif key == "sun":
                    lv.sun = [self._num(a[i] if i < len(a) else None) for i in range(9)]
                elif key == "water":
                    lv.has_water = True
                    lv.water_texture = a[0].text if a else ""
                    lv.water_level = self._num(a[1] if len(a) > 1 else None)
                    lv.water_alpha = self._num(a[2] if len(a) > 2 else None)
                elif key == "night":
                    lv.night = True
                elif key == "enablehelic":
                    lv.enable_helic = a[0].as_int() if a else -1
                elif key == "intermission":
                    lv.has_intermission = True
                    lv.intermission = [self._num(a[i] if i < len(a) else None) for i in range(6)]
            self.levels.append(lv)

    # -- lookup --------------------------------------------------------

    def find_object(self, name: str) -> Optional[ObjectDef]:
        return self._object_by_name.get(name.lower())

    def find_weapon(self, name: str) -> Optional[WeaponDef]:
        return self._weapon_by_name.get(name.lower())

    def find_particle_system(self, name: str) -> Optional[ParticleSystemDef]:
        return self._ps_by_name.get(name.lower())

    # -- validation ------------------------------------------------------

    def validate(self, root: str) -> List[str]:
        """Returns the list of unresolved-reference warnings (also appended
        to self.warnings). `root` is assets_extracted (stands in for the
        Vfs the C++ side validates against)."""
        problems: List[str] = []

        # Case-insensitive, like the real Vfs (as3d::normalizePath lower-cases
        # and unifies separators; assets_extracted itself is all lower-case,
        # per tools/paktool.py, but references in the text files are not).
        def file_exists(rel: str) -> bool:
            if not rel:
                return True
            p = rel.replace("\\", "/").lower()
            return os.path.isfile(os.path.join(root, p))

        def dir_has_any(rel: str) -> bool:
            if not rel:
                return True
            p = rel.replace("\\", "/").lower()
            d = os.path.join(root, p)
            return os.path.isdir(d) and len(os.listdir(d)) > 0

        for o in self.objects:
            if o.model and not file_exists(o.model):
                problems.append(f"object '{o.name}': model '{o.model}' not found")
            if o.skin and not file_exists(o.skin):
                problems.append(f"object '{o.name}': skin '{o.skin}' not found")
            if o.envmap and not file_exists(o.envmap):
                problems.append(f"object '{o.name}': envmap '{o.envmap}' not found")
            if o.script and not file_exists(o.script):
                problems.append(f"object '{o.name}': script '{o.script}' not found")
            for at in o.attachments:
                if not at.target:
                    continue
                # VERIFIED-CODE resolution order: particle system first, then object.
                if self.find_particle_system(at.target) is None and self.find_object(at.target) is None:
                    problems.append(
                        f"object '{o.name}': attach target '{at.target}' at line {at.line} "
                        "does not resolve to any object or particle system"
                    )

        for w in self.weapons:
            if w.missile and self.find_object(w.missile) is None:
                problems.append(f"weapon '{w.name}': missile '{w.missile}' does not resolve to an object")
            if w.flash and self.find_object(w.flash) is None:
                problems.append(f"weapon '{w.name}': flash '{w.flash}' does not resolve to an object")

        for p in self.particle_systems:
            if p.texture and not file_exists(p.texture):
                problems.append(f"particle system '{p.name}': texture '{p.texture}' not found")

        for lv in self.levels:
            label = lv.id or f"(line {lv.line})"
            if lv.map and not file_exists(lv.map):
                problems.append(f"level '{label}': map '{lv.map}' not found")
            if lv.music and not file_exists(lv.music):
                problems.append(f"level '{label}': music '{lv.music}' not found")
            if lv.textures and not dir_has_any(lv.textures):
                problems.append(f"level '{label}': textures '{lv.textures}' has no files")
            if lv.has_water and lv.water_texture and not file_exists(lv.water_texture):
                problems.append(f"level '{label}': water texture '{lv.water_texture}' not found")

        self.warnings.extend(problems)
        return problems


# ---------------------------------------------------------------------------
# Canonical serialization for the golden hash. See docs/spec/obj.md /
# wpn.md / ps.md / levels-txt.md ("Canonical serialization") -- the C++
# loader (engine/src/game/defs.cpp) must produce byte-identical text.
# ---------------------------------------------------------------------------

def _farr(vals) -> str:
    return ",".join(fhex(v) for v in vals)


def _iarr(vals) -> str:
    return ",".join(str(int(v)) for v in vals)


def canonical_object(o: ObjectDef) -> str:
    lines = [
        f"name={o.name}",
        f"type={o.type}",
        f"kind={o.kind}",
        f"model={o.model}",
        f"skin={o.skin}",
        f"envmap={o.envmap}",
        f"blend={o.blend}",
        f"envmode={o.envmode}",
        f"rflag={uhex(o.rflag)}",
        f"shadow={o.shadow}",
        f"sort={o.sort}",
        f"health={o.health}",
        f"score={o.score}",
        f"damage={o.damage}",
        f"flags={uhex(o.flags)}",
        f"touch={o.touch}",
        f"scale={fhex(o.scale)}",
        f"bboxScale={_farr(o.bbox_scale)}",
        f"hasBbox={int(o.has_bbox)}",
        f"bboxMin={_farr(o.bbox_min)}",
        f"bboxMax={_farr(o.bbox_max)}",
        f"hasFrames={int(o.has_frames)}",
        f"frameStart={o.frame_start}",
        f"frameEnd={o.frame_end}",
        f"hasLight={int(o.has_light)}",
        f"lightRadius={o.light_radius}",
        f"lightColor={_farr(o.light_color)}",
        f"hasLightDir={int(o.has_light_dir)}",
        f"lightDir={_farr(o.light_dir)}",
        f"lightConeAngle={fhex(o.light_cone_angle)}",
        f"script={o.script}",
        f"attachCount={len(o.attachments)}",
    ]
    for i, a in enumerate(o.attachments):
        lines.append(f"attach{i}.target={a.target}")
        lines.append(f"attach{i}.tag={a.tag}")
        lines.append(f"attach{i}.id={a.id}")
        lines.append(f"attach{i}.abs={int(a.absolute)}")
        lines.append(f"attach{i}.night={int(a.night)}")
    return "\n".join(lines)


def canonical_weapon(w: WeaponDef) -> str:
    return "\n".join([
        f"name={w.name}",
        f"missile={w.missile}",
        f"flash={w.flash}",
        f"speed={fhex(w.speed)}",
    ])


def canonical_particle_system(p: ParticleSystemDef) -> str:
    return "\n".join([
        f"name={p.name}",
        f"texture={p.texture}",
        f"textureFrameW={p.texture_frame_w}",
        f"textureFrameH={p.texture_frame_h}",
        f"blendMode={p.blend_mode}",
        f"rflag={uhex(p.rflag)}",
        f"coords={p.coords}",
        f"drawMode={p.draw_mode}",
        f"emitMode={p.emit_mode}",
        f"emitRate={fhex(p.emit_rate)}",
        f"lifeTime={fhex(p.life_time)}",
        f"initOffset={_farr(p.init_offset)}",
        f"initVelocity={_farr(p.init_velocity)}",
        f"initSize={_farr(p.init_size)}",
        f"hasInitFrame={int(p.has_init_frame)}",
        f"initFrame={_iarr(p.init_frame)}",
        f"initColor={_farr(p.init_color)}",
        f"accel={_farr(p.accel)}",
        f"fadeMode={p.fade_mode}",
        f"fadeFactor={fhex(p.fade_factor)}",
        f"size={fhex(p.size)}",
        f"animMode={p.anim_mode}",
        f"hasDamage={int(p.has_damage)}",
        f"damageTouch={p.damage_touch}",
        f"damageAmount={fhex(p.damage_amount)}",
        f"damageParam2={fhex(p.damage_param2)}",
        f"damageParam3={fhex(p.damage_param3)}",
    ])


def canonical_level(lv: LevelDef) -> str:
    return "\n".join([
        f"id={lv.id}",
        f"name={lv.name}",
        f"map={lv.map}",
        f"music={lv.music}",
        f"textures={lv.textures}",
        f"hmin={fhex(lv.hmin)}",
        f"hmax={fhex(lv.hmax)}",
        f"hasFog={int(lv.has_fog)}",
        f"fogColor={_farr(lv.fog_color)}",
        f"fogNear={fhex(lv.fog_near)}",
        f"fogFar={fhex(lv.fog_far)}",
        f"sun={_farr(lv.sun)}",
        f"hasWater={int(lv.has_water)}",
        f"waterTexture={lv.water_texture}",
        f"waterLevel={fhex(lv.water_level)}",
        f"waterAlpha={fhex(lv.water_alpha)}",
        f"night={int(lv.night)}",
        f"enableHelic={lv.enable_helic}",
        f"hasIntermission={int(lv.has_intermission)}",
        f"intermission={_farr(lv.intermission)}",
    ])


def sha1_of(text: str) -> str:
    return hashlib.sha1(text.encode("utf-8")).hexdigest()


def data_root() -> str:
    env = os.environ.get("AS3D_DATA_ROOT")
    if env:
        return env
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def extracted_dir() -> str:
    return os.path.join(data_root(), "assets_extracted")
