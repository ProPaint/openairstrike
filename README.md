# AirStrike 3D engine reimplementation

A new native engine that loads the original AirStrike 3D v1.70 (DivoGames, 2002) game data,
targeting Linux desktop and Android.

This repository contains **no game data**. You need your own copy of the game:

```
tools/setup_data.sh path/to/AirStrike.zip     # extracts into third_party_local/ (gitignored)
P=third_party_local/original/data
python3 tools/paktool.py extract assets_extracted $P/pak0.apk $P/pak1.apk $P/pak2.apk
tools/ci.sh                                   # build and test
```

In a git worktree, set `AS3D_DATA_ROOT` to the main checkout so tests can find the data.

An APK built with bundled game data contains copyrighted material and is for personal use only.

- Specifications: `docs/spec/`
- Layout: `engine/include/as3d/` public headers, `engine/src/<module>/`, `apps/`, `android/`, `tools/`
