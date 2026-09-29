# 161: choosing the game and finding its data (A4)

One binary runs three games (`as3d`, `as2`, `gulf`); this is how it finds out which one and
where its files are. Code: `engine/include/as3d/game_data.h`, `engine/src/game/game_data.cpp`.

## Layout under the data root

| Game | Install (exe, `data/pak*.apk`, `data/Settings.xml`) | Extracted files |
|---|---|---|
| `as3d` | `third_party_local/original/` | `assets_extracted/` |
| `as2`, `gulf` | `third_party_local/games/<key>/` | `assets_extracted_games/<key>/` |

`locateGameData(root, game)` applies it: paks in the profile's mount order (only those that
exist; the rest are listed in `missing`), `Settings.xml`, the logo `data/gfx/logo2s.tga`, the
profile's texts file in the extracted directory. `detectGames(root)` lists the games that have
data, in the order as3d, as2, gulf.

## Identifying paks

`identifyPaks(dir)` reads `dir/pak0.apk` only and compares its size and the FNV-1a 64 hash of
its first 64 KB with a table of three signatures. Names and paths are not trusted (a directory
called `gulf` holding the first game's paks is the first game). A pak whose head is identical
and whose size is equal but whose tail differs is not told apart; the full SHA-256 of every pak
is in `tools/games.json` (`pak_sha256`) for the tools.

| Game | pak0.apk size | FNV-1a 64 of the first 64 KB |
|---|---|---|
| as3d | 16717041 | `4fe87f398b954b0a` |
| as2 | 39411244 | `095dd5524bc31d6f` |
| gulf | 35840140 | `bf1903a53496c57e` |

`detectGames` only accepts paks under a game's own install directory when they identify as that
game.

## Choosing

Desktop tools (`as3d_game`, `as3d_sim`, `as3d_viewer`, `rcsl_tool`): `--game KEY`, else
`$AS3D_GAME`, else `as3d` if present, else the first detected game. `--paks DIR` without a key
identifies the game from the paks. An unknown key, a game without data or paks that match no
game is an error that lists the detected games (`chooseGameData`). `--list-games` prints key,
title, version and where each game was found. `--data ROOT` and `$AS3D_DATA_ROOT` name the root.

Mounting (`GameSession`): the game's extracted directory, or, with `GameOptions::paks`, the
paks in the profile's order (Gulf Thunder's `pak4.apk` last, so it overrides); the extra files
(Settings.xml logo) go underneath as before. The desktop tools mount the extracted directory
unless `--paks` is given or the game has none.

Android and web: the game is `--game KEY` (intent extra `game`, URL parameter `?game=`),
default `as3d`. A game that is not `gameIsPlayable` is refused with a log line ("not playable
yet") unless `--allow-unfinished` (extra `allow_unfinished`) / `--unfinished` (`?unfinished=1`)
is given. Turning the intent extras into arguments is in `GameActivity.getArguments()`.
