#!/usr/bin/env python3
"""Label Direct3D 8 device calls in the AS2 decompiled export, for reading only.

Reads `re/out/<game>/decompiled/<addr>_*.c` (gitignored export) and prints the text to
stdout with every indirect call `(**(code **)(*X + OFF))(X, a, b, ...)` whose offset is a
device method slot followed by a comment naming the method and, for render states,
texture-stage states and transforms, the symbolic state names and values. The output is a
reading aid and must stay out of the repository (it contains decompiled code).

    AS3D_DATA_ROOT=/path/to/airstrike3d \
        python3 re/tools/d3d8_annotate.py --game as2 0x0042f620 0x0042fb50

`--states` prints only the state settings found, one per line (no code), which is what the
render delta spec quotes.
"""
import argparse
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from d3d8_vtable import device_method  # noqa: E402

RS = {
    7: "ZENABLE", 8: "FILLMODE", 9: "SHADEMODE", 10: "LINEPATTERN", 14: "ZWRITEENABLE",
    15: "ALPHATESTENABLE", 16: "LASTPIXEL", 19: "SRCBLEND", 20: "DESTBLEND", 22: "CULLMODE",
    23: "ZFUNC", 24: "ALPHAREF", 25: "ALPHAFUNC", 26: "DITHERENABLE", 27: "ALPHABLENDENABLE",
    28: "FOGENABLE", 29: "SPECULARENABLE", 30: "ZVISIBLE", 34: "FOGCOLOR", 35: "FOGTABLEMODE",
    36: "FOGSTART", 37: "FOGEND", 38: "FOGDENSITY", 40: "EDGEANTIALIAS", 47: "ZBIAS",
    48: "RANGEFOGENABLE", 52: "STENCILENABLE", 53: "STENCILFAIL", 54: "STENCILZFAIL",
    55: "STENCILPASS", 56: "STENCILFUNC", 57: "STENCILREF", 58: "STENCILMASK",
    59: "STENCILWRITEMASK", 60: "TEXTUREFACTOR", 128: "WRAP0", 129: "WRAP1", 136: "CLIPPING",
    137: "LIGHTING", 139: "AMBIENT", 140: "FOGVERTEXMODE", 141: "COLORVERTEX",
    142: "LOCALVIEWER", 143: "NORMALIZENORMALS", 145: "DIFFUSEMATERIALSOURCE",
    146: "SPECULARMATERIALSOURCE", 147: "AMBIENTMATERIALSOURCE",
    148: "EMISSIVEMATERIALSOURCE", 151: "VERTEXBLEND", 152: "CLIPPLANEENABLE",
    153: "SOFTWAREVERTEXPROCESSING", 154: "POINTSIZE", 155: "POINTSIZE_MIN",
    156: "POINTSPRITEENABLE", 157: "POINTSCALEENABLE", 161: "MULTISAMPLEANTIALIAS",
    166: "POINTSIZE_MAX", 168: "COLORWRITEENABLE", 171: "BLENDOP",
}
TSS = {
    1: "COLOROP", 2: "COLORARG1", 3: "COLORARG2", 4: "ALPHAOP", 5: "ALPHAARG1",
    6: "ALPHAARG2", 11: "TEXCOORDINDEX", 13: "ADDRESSU", 14: "ADDRESSV", 15: "BORDERCOLOR",
    16: "MAGFILTER", 17: "MINFILTER", 18: "MIPFILTER", 19: "MIPMAPLODBIAS",
    20: "MAXMIPLEVEL", 21: "MAXANISOTROPY", 24: "TEXTURETRANSFORMFLAGS", 25: "ADDRESSW",
    26: "COLORARG0", 27: "ALPHAARG0", 28: "RESULTARG",
}
TOP = ["?", "DISABLE", "SELECTARG1", "SELECTARG2", "MODULATE", "MODULATE2X", "MODULATE4X",
       "ADD", "ADDSIGNED", "ADDSIGNED2X", "SUBTRACT", "ADDSMOOTH", "BLENDDIFFUSEALPHA",
       "BLENDTEXTUREALPHA", "BLENDFACTORALPHA", "BLENDTEXTUREALPHAPM", "BLENDCURRENTALPHA",
       "PREMODULATE", "MODULATEALPHA_ADDCOLOR", "MODULATECOLOR_ADDALPHA",
       "MODULATEINVALPHA_ADDCOLOR", "MODULATEINVCOLOR_ADDALPHA", "BUMPENVMAP",
       "BUMPENVMAPLUMINANCE", "DOTPRODUCT3", "MULTIPLYADD", "LERP"]
TA = ["DIFFUSE", "CURRENT", "TEXTURE", "TFACTOR", "SPECULAR", "TEMP"]
BLEND = ["?", "ZERO", "ONE", "SRCCOLOR", "INVSRCCOLOR", "SRCALPHA", "INVSRCALPHA",
         "DESTALPHA", "INVDESTALPHA", "DESTCOLOR", "INVDESTCOLOR", "SRCALPHASAT"]
CMP = ["?", "NEVER", "LESS", "EQUAL", "LESSEQUAL", "GREATER", "NOTEQUAL", "GREATEREQUAL",
       "ALWAYS"]
FILTER = ["NONE", "POINT", "LINEAR", "ANISOTROPIC", "FLATCUBIC", "GAUSSIANCUBIC"]
ADDR = ["?", "WRAP", "MIRROR", "CLAMP", "BORDER", "MIRRORONCE"]
FOG = ["NONE", "EXP", "EXP2", "LINEAR"]
CULL = ["?", "NONE", "CW", "CCW"]
TRANSFORM = {2: "VIEW", 3: "PROJECTION", 16: "TEXTURE0", 17: "TEXTURE1", 256: "WORLD"}


def ival(s):
    s = s.strip()
    try:
        return int(s, 0)
    except ValueError:
        m = re.fullmatch(r"(0x[0-9a-fA-F]+|\d+)U?", s)
        return int(m.group(1), 0) if m else None


def name_value(state, v):
    if v is None:
        return None
    if state in ("SRCBLEND", "DESTBLEND") and 0 < v < len(BLEND):
        return BLEND[v]
    if state in ("ZFUNC", "ALPHAFUNC") and 0 < v < len(CMP):
        return CMP[v]
    if state in ("FOGTABLEMODE", "FOGVERTEXMODE") and v < len(FOG):
        return FOG[v]
    if state == "CULLMODE" and 0 < v < len(CULL):
        return CULL[v]
    if state in ("COLOROP", "ALPHAOP") and 0 < v < len(TOP):
        return TOP[v]
    if state.startswith(("COLORARG", "ALPHAARG")) or state == "RESULTARG":
        base = TA[v & 0xF] if (v & 0xF) < len(TA) else "?"
        if v & 0x10:
            base += "|COMPLEMENT"
        if v & 0x20:
            base += "|ALPHAREPLICATE"
        return base
    if state in ("MAGFILTER", "MINFILTER", "MIPFILTER") and v < len(FILTER):
        return FILTER[v]
    if state.startswith("ADDRESS") and 0 < v < len(ADDR):
        return ADDR[v]
    if state == "TEXCOORDINDEX":
        gen = {0: "", 0x10000: "CAMERASPACENORMAL", 0x20000: "CAMERASPACEPOSITION",
               0x30000: "CAMERASPACEREFLECTIONVECTOR"}.get(v & 0xFFFF0000, "?")
        return f"index {v & 0xFFFF}" + (f"|{gen}" if gen else "")
    if state == "TEXTURETRANSFORMFLAGS":
        return {0: "DISABLE", 1: "COUNT1", 2: "COUNT2", 3: "COUNT3", 4: "COUNT4"}.get(
            v & 0xFF, "?") + ("|PROJECTED" if v & 0x100 else "")
    return str(v)


def split_args(s):
    depth, cur, out = 0, "", []
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            if depth == 0:
                out.append(cur)
                return out
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    out.append(cur)
    return out


CALL = re.compile(r"\(\*\*\(code \*\*\)\(\*(\w+) \+ (0x[0-9a-f]+|\d+)\)\)\(")


def label(method, args):
    a = [x.strip() for x in args]
    if method == "SetRenderState" and len(a) >= 3:
        st = RS.get(ival(a[1]), a[1])
        return f"{st} = {name_value(st, ival(a[2])) or a[2]}"
    if method == "SetTextureStageState" and len(a) >= 4:
        st = TSS.get(ival(a[2]), a[2])
        return f"stage {a[1]} {st} = {name_value(st, ival(a[3])) or a[3]}"
    if method in ("SetTransform", "GetTransform") and len(a) >= 2:
        return f"{TRANSFORM.get(ival(a[1]), a[1])}"
    if method == "SetTexture" and len(a) >= 3:
        return f"stage {a[1]} texture {a[2]}"
    return ""


def annotate(text, states_only=False):
    out_lines = []
    for line in text.splitlines():
        notes = []
        for m in CALL.finditer(line):
            off = int(m.group(2), 0)
            if off < 0x40:
                continue
            method = device_method(off)
            if not method:
                continue
            args = split_args(line[m.end():])
            lab = label(method, args)
            notes.append(f"{method}" + (f"({lab})" if lab else ""))
        if states_only:
            out_lines.extend(notes)
        else:
            out_lines.append(line + ("   /* " + "; ".join(notes) + " */" if notes else ""))
    return "\n".join(out_lines)


def load_names(game):
    """AS2 function names from re/symbols_<game>.csv and re/symbols_<game>_render.csv."""
    import csv
    names = {}
    here = os.path.join(os.path.dirname(__file__), "..")
    for fn in (f"symbols_{game}.csv", f"symbols_{game}_render.csv"):
        path = os.path.join(here, fn)
        if os.path.exists(path):
            with open(path, newline="") as f:
                for row in csv.DictReader(f):
                    names[int(row["address"], 16)] = row["name"]
    return names


def rename(text, names):
    return re.sub(r"FUN_([0-9a-f]{8})",
                  lambda m: names.get(int(m.group(1), 16), m.group(0)), text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="as2")
    ap.add_argument("--states", action="store_true")
    ap.add_argument("addresses", nargs="+")
    ap.add_argument("--no-names", action="store_true")
    ns = ap.parse_args()
    names = {} if ns.no_names else load_names(ns.game)
    root = os.environ.get("AS3D_DATA_ROOT", ".")
    for a in ns.addresses:
        a = int(a, 16)
        files = glob.glob(os.path.join(root, "re/out", ns.game, "decompiled", f"{a:08x}_*.c"))
        if not files:
            print(f"// no export for {a:#010x}")
            continue
        with open(files[0], encoding="utf-8", errors="replace") as f:
            print(f"==== {os.path.basename(files[0])}")
            print(rename(annotate(f.read(), ns.states), names))


if __name__ == "__main__":
    main()
