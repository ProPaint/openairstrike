#!/usr/bin/env python3
"""Reader for AirStrike 3D .mdl static model files. See docs/spec/mdl.md.

Usage:
  mdl.py info <file>              human-readable summary
  mdl.py json <file>               full parse dumped as JSON
  mdl.py obj  <file> <out.obj>     Wavefront OBJ export (v/vt/vn/f) for eyeballing

Stdlib only. Can also be imported as a module:

  import mdl
  model = mdl.load("apache.mdl")
  model.vertices, model.uvs, model.faces, model.normals, model.tags
"""
import dataclasses
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamesel  # noqa: E402

MAGIC = b"MDL!"
KNOWN_VERSIONS = (2, 3)  # 3 is accepted by the v1.70 loader but never seen in shipped data

HEADER_SIZE = 0x78          # size of the fixed header, including the bounding box
TEXTURE_PATH_OFFSET = 0x0C
TEXTURE_PATH_SIZE = 0x40    # 64 bytes, NUL-terminated when it fits
COUNTS_OFFSET = 0x4C        # 5x u32: vertices, uvs, faces, normals, tags
BBOX_OFFSET = 0x60          # 6x f32: min xyz, max xyz (immediately follows the counts)

FACE_SIZE = 12              # 3x u16 vertex index + 3x u16 uv index
VERTEX_SIZE = 12            # 3x f32
UV_SIZE = 8                 # 2x f32
NORMAL_SIZE = 12            # 3x f32
TAG_SIZE = 56                # 32-byte name + 3x f32 position + 3x f32 direction
TAG_NAME_SIZE = 32


class MdlError(ValueError):
    """The file is not a well-formed .mdl, or does not match this spec's layout."""


class MdlEmpty(MdlError):
    """The file is zero bytes. 4 shipped files are like this; see docs/spec/mdl.md."""


@dataclasses.dataclass
class Tag:
    name: str
    pos: tuple      # (x, y, z)
    direction: tuple  # (x, y, z); either the zero vector or ~unit length


@dataclasses.dataclass
class Model:
    version: int
    smooth_normals: bool     # header u32 @ 0x08: 1 = per-vertex normals, 0 = per-face
    texture_path: str        # original authoring-machine path, from the header
    bbox: tuple               # (minx, miny, minz, maxx, maxy, maxz)
    vertices: list            # [(x, y, z), ...]
    uvs: list                 # [(u, v), ...]
    faces: list                # [(v0, v1, v2, uv0, uv1, uv2), ...], CCW front-facing
    normals: list              # [(x, y, z), ...], len == len(vertices) or len(faces)
    tags: list                 # [Tag, ...]

    @property
    def normals_per_face(self):
        """True if `normals` has one entry per face (flat shading) rather than
        one per vertex (smooth shading). See docs/spec/mdl.md #normals."""
        return len(self.normals) == len(self.faces) and len(self.normals) != len(self.vertices)


def data_root():
    """Root that holds the game data, per AS3D_DATA_ROOT (see README.md)."""
    return gamesel.data_root()


def extracted_dir():
    """Extracted files of the selected game (--game, $AS3D_GAME, default as3d)."""
    return gamesel.extracted_dir()


def parse(data, source="<bytes>"):
    """Parse a .mdl file already read into memory. Raises MdlError/MdlEmpty."""
    if len(data) == 0:
        raise MdlEmpty(f"{source}: empty file")
    if len(data) < HEADER_SIZE:
        raise MdlError(f"{source}: file too small for header: {len(data)} bytes")
    if data[:4] != MAGIC:
        raise MdlError(f"{source}: bad magic {data[:4]!r}, expected {MAGIC!r}")

    version, smooth_flag = struct.unpack_from("<II", data, 4)
    if version not in KNOWN_VERSIONS:
        raise MdlError(f"{source}: illegal version {version}")

    raw_path = data[TEXTURE_PATH_OFFSET:TEXTURE_PATH_OFFSET + TEXTURE_PATH_SIZE]
    # Usually NUL-terminated; 11/442 shipped files fill the whole 64-byte field
    # with no room for a terminator (the original path was too long) -- in that
    # case split() harmlessly returns the whole (truncated) field.
    texture_path = raw_path.split(b"\0", 1)[0].decode("cp1251", "replace")

    nverts, nuvs, nfaces, nnormals, ntags = struct.unpack_from("<IIIII", data, COUNTS_OFFSET)
    bbox = struct.unpack_from("<6f", data, BBOX_OFFSET)

    def take(off, count, elem_size, fmt, what):
        end = off + elem_size * count
        if end > len(data):
            raise MdlError(
                f"{source}: {what} array truncated: need {count} x {elem_size} "
                f"bytes at offset {off}, file is only {len(data)} bytes"
            )
        return list(struct.iter_unpack(fmt, data[off:end])), end

    off = HEADER_SIZE
    vertices, off = take(off, nverts, VERTEX_SIZE, "<3f", "vertex")
    uvs, off = take(off, nuvs, UV_SIZE, "<2f", "uv")
    faces, off = take(off, nfaces, FACE_SIZE, "<6H", "face")
    normals, off = take(off, nnormals, NORMAL_SIZE, "<3f", "normal")

    tags = []
    for i in range(ntags):
        chunk = data[off:off + TAG_SIZE]
        if len(chunk) != TAG_SIZE:
            raise MdlError(f"{source}: truncated tag {i}")
        name = chunk[:TAG_NAME_SIZE].split(b"\0", 1)[0].decode("ascii", "replace")
        pos = struct.unpack_from("<3f", chunk, TAG_NAME_SIZE)
        direction = struct.unpack_from("<3f", chunk, TAG_NAME_SIZE + 12)
        tags.append(Tag(name=name, pos=pos, direction=direction))
        off += TAG_SIZE

    if off != len(data):
        raise MdlError(
            f"{source}: {len(data) - off} trailing byte(s) unaccounted for "
            f"(parsed {off}, file is {len(data)}); counts were "
            f"v={nverts} uv={nuvs} f={nfaces} n={nnormals} t={ntags}"
        )

    return Model(
        version=version,
        smooth_normals=bool(smooth_flag),
        texture_path=texture_path,
        bbox=bbox,
        vertices=vertices,
        uvs=uvs,
        faces=faces,
        normals=normals,
        tags=tags,
    )


def load(path):
    """Read and parse a .mdl file from disk."""
    with open(path, "rb") as f:
        data = f.read()
    return parse(data, source=path)


def iter_models(root=None):
    """Yield (relpath, path, size) for every models/**/*.mdl under assets_extracted."""
    root = root or extracted_dir()
    models_dir = os.path.join(root, "models")
    for dirpath, _dirnames, filenames in os.walk(models_dir):
        for fn in filenames:
            if fn.lower().endswith(".mdl"):
                p = os.path.join(dirpath, fn)
                yield os.path.relpath(p, root), p, os.path.getsize(p)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def cmd_info(path):
    m = load(path)
    print(f"file:           {path}")
    print(f"version:        {m.version}")
    print(f"smooth_normals: {m.smooth_normals} ({'per-vertex' if m.smooth_normals else 'per-face'})")
    print(f"texture_path:   {m.texture_path!r}")
    print(f"bbox min:       {m.bbox[0:3]}")
    print(f"bbox max:       {m.bbox[3:6]}")
    print(f"vertices:       {len(m.vertices)}")
    print(f"uvs:            {len(m.uvs)}")
    print(f"faces:          {len(m.faces)}")
    print(f"normals:        {len(m.normals)} "
          f"({'per-vertex' if len(m.normals) == len(m.vertices) else 'per-face' if m.normals_per_face else 'other'})")
    print(f"tags:           {len(m.tags)}")
    for t in m.tags:
        print(f"  {t.name!r:32s} pos={t.pos} dir={t.direction}")


def cmd_json(path):
    m = load(path)
    d = dataclasses.asdict(m)
    print(json.dumps(d, indent=1))


def cmd_obj(path, out_path):
    m = load(path)
    per_vertex = len(m.normals) == len(m.vertices)
    per_face = (not per_vertex) and len(m.normals) == len(m.faces)
    lines = [f"# exported by tools/ref/mdl.py from {path}", f"# texture: {m.texture_path}"]
    for x, y, z in m.vertices:
        lines.append(f"v {x!r} {y!r} {z!r}")
    for u, v in m.uvs:
        lines.append(f"vt {u!r} {v!r}")
    for x, y, z in m.normals:
        lines.append(f"vn {x!r} {y!r} {z!r}")
    if not per_vertex and not per_face and m.normals:
        print(f"warning: {path}: normals are neither per-vertex nor per-face; "
              f"omitting vn indices", file=sys.stderr)
    for i, (a, b, c, ua, ub, uc) in enumerate(m.faces):
        if per_vertex:
            na, nb, nc = a, b, c
        elif per_face:
            na = nb = nc = i
        else:
            na = nb = nc = None
        if na is None:
            face = f"f {a+1}/{ua+1} {b+1}/{ub+1} {c+1}/{uc+1}"
        else:
            face = f"f {a+1}/{ua+1}/{na+1} {b+1}/{ub+1}/{nb+1} {c+1}/{uc+1}/{nc+1}"
        lines.append(face)
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {out_path}: {len(m.vertices)} v, {len(m.uvs)} vt, {len(m.normals)} vn, {len(m.faces)} f")


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd = argv[1]
    try:
        if cmd == "info":
            cmd_info(argv[2])
        elif cmd == "json":
            cmd_json(argv[2])
        elif cmd == "obj":
            if len(argv) < 4:
                print(__doc__)
                return 2
            cmd_obj(argv[2], argv[3])
        else:
            print(__doc__)
            return 2
    except MdlError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
