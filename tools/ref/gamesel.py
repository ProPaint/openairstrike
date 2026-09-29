#!/usr/bin/env python3
"""Which game a reference tool or test works on, and where its data and goldens are.

The key comes from `--game <key>` (taken out of sys.argv by parse_game_arg), then
$AS3D_GAME, then `as3d`. Keys, titles and paks are in tools/games.json. Layout (same as
engine/include/as3d/game_data.h, docs/spec/README.md "Games and which spec applies"):

  as3d     <root>/assets_extracted/            <root>/third_party_local/original/
  others   <root>/assets_extracted_games/<key>/  <root>/third_party_local/games/<key>/

Goldens are committed: testdata/golden/<key>/*.json. `expected.json` there holds the counts
and the lists of known exceptions each test compares against (documented in
testdata/golden/README.md). Stdlib only.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DEFAULT_GAME = "as3d"


def data_root():
    env = os.environ.get("AS3D_DATA_ROOT")
    return env if env else REPO


def games():
    """The games of tools/games.json, in file order."""
    with open(os.path.join(REPO, "tools", "games.json")) as f:
        return json.load(f)["games"]


def known_keys():
    return [g["key"] for g in games()]


def parse_game_arg(argv=None):
    """Removes `--game K` / `--game=K` from argv (default sys.argv), stores K in
    $AS3D_GAME so that every module of the process agrees, and returns the key."""
    argv = sys.argv if argv is None else argv
    key = None
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--game" and i + 1 < len(argv):
            key = argv[i + 1]
            del argv[i:i + 2]
            continue
        if a.startswith("--game="):
            key = a[len("--game="):]
            del argv[i]
            continue
        i += 1
    if key:
        os.environ["AS3D_GAME"] = key
    return game_key()


def game_key():
    key = os.environ.get("AS3D_GAME") or DEFAULT_GAME
    if key not in known_keys():
        sys.exit(f"unknown game '{key}' (known: {', '.join(known_keys())})")
    return key


def game(key=None):
    key = key or game_key()
    return next(g for g in games() if g["key"] == key)


def extracted_dir(key=None):
    key = key or game_key()
    if key == DEFAULT_GAME:
        return os.path.join(data_root(), "assets_extracted")
    return os.path.join(data_root(), "assets_extracted_games", key)


def install_dir(key=None):
    key = key or game_key()
    if key == DEFAULT_GAME:
        return os.path.join(data_root(), "third_party_local", "original")
    return os.path.join(data_root(), "third_party_local", "games", key)


def has_data(key=None):
    """Extracted files of the game are there."""
    return os.path.isfile(os.path.join(extracted_dir(key), "maps", "levels.txt"))


def present_games():
    """Keys of the games with extracted data on this machine, in games.json order."""
    return [k for k in known_keys() if has_data(k)]


def golden_dir(key=None):
    return os.path.join(REPO, "testdata", "golden", key or game_key())


def golden_path(name, key=None):
    return os.path.join(golden_dir(key), name)


# The sequels have the same builtins in the same order: gulf reads the as2 table.
BUILTINS_FALLBACK = {"gulf": "as2"}


def builtins_path(key=None):
    """testdata/golden/<key>/rcsl_builtins.json, falling back to the table of the game the
    key builds on when it has none of its own."""
    key = key or game_key()
    p = golden_path("rcsl_builtins.json", key)
    if not os.path.exists(p) and key in BUILTINS_FALLBACK:
        return builtins_path(BUILTINS_FALLBACK[key])
    return p


_EXPECTED = {}


def expected(key=None):
    """testdata/golden/<key>/expected.json as a dict."""
    key = key or game_key()
    if key not in _EXPECTED:
        with open(golden_path("expected.json", key)) as f:
            _EXPECTED[key] = json.load(f)
    return _EXPECTED[key]


def skip_no_data(tool, key=None):
    """Prints the loud SKIPPED message and returns True when the game's data is absent."""
    key = key or game_key()
    if has_data(key):
        return False
    bar = "=" * 70
    print(bar, file=sys.stderr)
    print(f"SKIPPED {tool}: no data for game '{key}' (looked in {extracted_dir(key)})", file=sys.stderr)
    print("Set AS3D_DATA_ROOT to a checkout with the game data extracted, see README.md.",
          file=sys.stderr)
    print(bar, file=sys.stderr)
    return True


if __name__ == "__main__":
    # `gamesel.py present` prints the keys with data, one per line (for tools/ci.sh).
    if len(sys.argv) > 1 and sys.argv[1] == "present":
        for k in present_games():
            print(k)
    else:
        print(game_key())
