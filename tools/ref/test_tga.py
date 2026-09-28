#!/usr/bin/env python3
"""Decodes every shipped .tga with tga.py, cross-checks against PIL, and maintains the
golden hash file used by apps/tests/tga_test.cpp. See docs/spec/tga.md.

For every .tga found under <AS3D_DATA_ROOT>/assets_extracted (AS3D_DATA_ROOT defaults to
the repository root, two levels above this file):
  - decode with tga.py's independent pure-Python decoder;
  - decode with PIL (`Image.open(path).convert("RGBA")`) and require the two to agree
    pixel-for-pixel, after PIL's own top-down normalisation;
  - fold the result into testdata/golden/tga_hashes.json: on first run this writes the
    file, on later runs it validates every entry still matches (so a change in decoded
    output for a file already in the golden set is treated as a regression, not silently
    accepted).

PIL cross-check rationale (see docs/spec/tga.md "Alpha bits and hasAlpha" and
"Orientation"): AirStrike 3D's shipped textures are simple enough (uncompressed types 1
and 2, standard colour-map/pixel depths, bottom-left origin, TGA 2.0 footer) that PIL's
TGA plugin and this reference decoder agree on every one of the 464 shipped files, byte
for byte, once both are normalised to top-down RGBA8. There is no known case in the real
data where they'd disagree; if `Image.open(...).convert("RGBA")` ever mismatches our
decoder here, that is treated as a real bug (in either decoder) worth investigating, not
brushed aside -- see the comment below where the mismatch is reported.

Exits 0 (with a loud SKIPPED message) if assets_extracted/ isn't present, e.g. when run
in a worktree without AS3D_DATA_ROOT pointed at a checkout that has the game data
extracted. Exits non-zero if any file fails to decode, disagrees with PIL, or disagrees
with the existing golden file.
"""
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tga  # noqa: E402


def repo_root():
    # tools/ref/test_tga.py -> tools/ref -> tools -> repo root.
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def data_root():
    env = os.environ.get("AS3D_DATA_ROOT")
    return env if env else repo_root()


def find_tga_files(extracted_dir):
    files = []
    for dirpath, _dirnames, filenames in os.walk(extracted_dir):
        for fn in filenames:
            if fn.lower().endswith(".tga"):
                files.append(os.path.join(dirpath, fn))
    files.sort()
    return files


def main():
    extracted = os.path.join(data_root(), "assets_extracted")
    if not os.path.isdir(extracted):
        print("=" * 70, file=sys.stderr)
        print("SKIPPED: tga: assets_extracted/ not found under AS3D_DATA_ROOT", file=sys.stderr)
        print(f"         (looked in {extracted})", file=sys.stderr)
        print("         Set AS3D_DATA_ROOT to a checkout with the game data extracted,", file=sys.stderr)
        print("         see README.md.", file=sys.stderr)
        print("=" * 70, file=sys.stderr)
        return 0

    try:
        from PIL import Image
    except ImportError:
        print("SKIPPED: tga: PIL (Pillow) is not installed", file=sys.stderr)
        return 0

    files = find_tga_files(extracted)
    if not files:
        print(f"SKIPPED: tga: no .tga files found under {extracted}", file=sys.stderr)
        return 0

    failures = []
    golden = []

    for path in files:
        rel = os.path.relpath(path, extracted)
        game_path = rel.replace(os.sep, "\\")

        with open(path, "rb") as f:
            data = f.read()

        try:
            img = tga.decode_tga(data)
        except tga.TgaError as e:
            failures.append(f"{game_path}: reference decoder failed: {e}")
            continue

        try:
            pil_img = Image.open(path).convert("RGBA")
        except Exception as e:  # noqa: BLE001 - report any PIL failure as a mismatch
            failures.append(f"{game_path}: PIL failed to open/convert: {e}")
            continue

        if pil_img.size != (img.width, img.height):
            failures.append(
                f"{game_path}: size mismatch: ours={img.width}x{img.height} "
                f"pil={pil_img.size[0]}x{pil_img.size[1]}"
            )
            continue

        pil_bytes = pil_img.tobytes()
        if pil_bytes != img.rgba:
            first_diff = None
            for i in range(0, len(pil_bytes), 4):
                if pil_bytes[i:i + 4] != img.rgba[i:i + 4]:
                    first_diff = i // 4
                    break
            px = (first_diff % img.width, first_diff // img.width) if first_diff is not None else "?"
            failures.append(
                f"{game_path}: pixel data mismatch vs PIL, first differing pixel {px} "
                f"ours={img.rgba[first_diff*4:first_diff*4+4].hex()} "
                f"pil={pil_bytes[first_diff*4:first_diff*4+4].hex()} "
                "-- investigate: see the module docstring for how this is expected to "
                "never happen on the shipped data."
            )
            continue

        golden.append({
            "name": game_path,
            "width": img.width,
            "height": img.height,
            "hasAlpha": img.has_alpha,
            "sha1": hashlib.sha1(img.rgba).hexdigest(),
        })

    golden.sort(key=lambda e: e["name"])

    golden_path = os.path.join(repo_root(), "testdata", "golden", "tga_hashes.json")
    if os.path.exists(golden_path):
        with open(golden_path) as f:
            existing = json.load(f)
        existing_by_name = {e["name"]: e for e in existing}
        new_by_name = {e["name"]: e for e in golden}
        for name, entry in new_by_name.items():
            old = existing_by_name.get(name)
            if old is None:
                failures.append(f"{name}: decoded but missing from golden {golden_path} (regenerate it)")
            elif old != entry:
                failures.append(f"{name}: golden mismatch: was {old}, now {entry}")
        for name in existing_by_name:
            if name not in new_by_name:
                failures.append(f"{name}: in golden {golden_path} but no longer found/decodable")
    else:
        os.makedirs(os.path.dirname(golden_path), exist_ok=True)
        with open(golden_path, "w") as f:
            json.dump(golden, f, indent=1)
            f.write("\n")
        print(f"tga: wrote {len(golden)} entries to {golden_path}")

    print(f"tga: decoded and cross-checked {len(files)} files against PIL, {len(failures)} failures")
    if failures:
        print("tga: FAILURES:", file=sys.stderr)
        for msg in failures[:100]:
            print("  " + msg, file=sys.stderr)
        if len(failures) > 100:
            print(f"  ... and {len(failures) - 100} more", file=sys.stderr)
        return 1

    print("tga: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
