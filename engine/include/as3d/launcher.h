// The game selector's logic (docs/spec/issues/163): which games it offers, whether it is shown
// at all, the remembered last choice (<user data dir>/launcher.bin) and the one-line summary of
// each game's save. No GL, no SDL, no platform paths: the callers pass the paths; the screen
// that draws it is ui::GameSelector (as3d/frontend.h), the window's use of both is in
// apps/game/game_loop.cpp.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "as3d/game_profile.h"

namespace as3d {

// What the selector decides at start.
struct LaunchPlan {
    bool showSelector = false;
    // Without the selector: the game to start ("" when none of the present games may be started;
    // the caller reports it).
    std::string startKey;
    // With the selector: the games it lists, in GameId order, and the one focused first.
    std::vector<const GameProfile*> offered;
    int preselected = 0;
};

// `present`: the games whose data is here, in any order. `forcedKey`: --game, $AS3D_GAME, ?game=,
// the Android extra `game` ("" = none): that game starts, no selector. `lastChoice`: the key in
// launcher.bin ("" = none). `allowUnfinished`: the development override, the games that are not
// playable yet are listed too. The selector is shown only when more than one game can be
// offered; with exactly one, that game starts; the last choice only preselects, it never starts
// a game by itself.
LaunchPlan planLaunch(const std::vector<const GameProfile*>& present, const std::string& forcedKey,
                      const std::string& lastChoice, bool allowUnfinished);

// launcher.bin (ours, little-endian): "AS3DLNCH" | u32 version (1) | u8 key length (1..15) |
// key (a-z 0-9 _) | u32 CRC-32 of the key. Anything else (short, long, bad magic, version, CRC,
// characters, a key that names no game) is ignored: `key` stays empty and false is returned.
bool readLauncherChoice(const std::string& path, std::string* key);
// Writes `<path>.tmp` and renames it over `path`. False (nothing changed) on any failure.
bool writeLauncherChoice(const std::string& path, const std::string& key);
// The bytes of the file, for tests.
std::vector<std::uint8_t> serializeLauncherChoice(const std::string& key);
bool parseLauncherChoice(const std::uint8_t* data, size_t size, std::string* key);

// A game's save as the selector card shows it, read through the profile code (loadProfileFile),
// never written, moved or migrated.
struct SaveSummary {
    bool found = false;       // a save file exists
    bool readable = false;    // and it could be read as this game's
    int missionsUnlocked = 0; // of missionCount
    int missionCount = 0;
    bool hasScore = false;    // a high-score entry that is not one of the fresh table's
    std::int64_t bestScore = 0;
};
// `path`: <user data>/<key>/profile.bin. `legacyPath`: the first game's file from before the
// saves moved (<user data>/profile.bin), read (not migrated) when `path` does not exist and the
// game is the first one; "" = none.
SaveSummary readSaveSummary(const std::string& path, const std::string& legacyPath, const GameProfile& game);
// "3 of 20 missions open, best score 12 345", "No save yet", ...
std::string describeSave(const SaveSummary& s);

} // namespace as3d
