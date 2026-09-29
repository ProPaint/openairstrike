// Helpers for tests that need the original game data, which is never committed.
//
// The game the data tests run against comes from $AS3D_GAME (default as3d, see
// tools/ci.sh, which runs the suite once per game present). Where its data is comes from
// as3d::locateGameData; its goldens are testdata/golden/<key>/ and its expected counts
// testdata/golden/<key>/expected.json (apps/tests/expected.h).
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/game_data.h"
#include "as3d/game_profile.h"
#include "as3d/vfs.h"

namespace testdata {

// Repository that holds the gitignored data. Worktrees do not contain it, so
// AS3D_DATA_ROOT can point at the main checkout.
inline std::string root() {
    const char* env = std::getenv("AS3D_DATA_ROOT");
    return env && *env ? env : AS3D_REPO_ROOT;
}

// Key of the game under test: $AS3D_GAME, else as3d. Read once at start: game_data_test.cpp
// changes the variable while it runs.
inline const std::string kGameKey = [] {
    const char* env = std::getenv("AS3D_GAME");
    return std::string(env && *env ? env : "as3d");
}();

inline const std::string& gameKey() { return kGameKey; }

inline const as3d::GameProfile& game() {
    static const as3d::GameProfile* g = [] {
        const as3d::GameProfile* p = as3d::findGameProfile(gameKey());
        if (!p) {
            std::fprintf(stderr, "AS3D_GAME='%s' names no game (as3d, as2, gulf)\n", gameKey().c_str());
            std::exit(2);
        }
        return p;
    }();
    return *g;
}

inline const as3d::GameData& gameData() {
    static const as3d::GameData d = as3d::locateGameData(root(), game());
    return d;
}

inline std::string extractedDir() { return gameData().extractedDir; }
// The game's install directory: the executable and data/ with the paks.
inline std::string installDir() { return gameData().installDir; }
// Golden files are committed, so they always come from this checkout.
inline std::string goldenDir() { return std::string(AS3D_REPO_ROOT) + "/testdata/golden/" + gameKey(); }

// Reads an extracted file by game path, e.g. "models\\apache\\apache.mdl".
inline bool readExtracted(const std::string& gamePath, as3d::Blob& out) {
    std::string p = as3d::normalizePath(gamePath);
    for (char& c : p) if (c == '\\') c = '/';
    std::FILE* f = std::fopen((extractedDir() + "/" + p).c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(static_cast<size_t>(n));
    size_t got = n ? std::fread(out.data(), 1, out.size(), f) : 0;
    std::fclose(f);
    return got == out.size();
}

// Mounts the paks of the game under test, in mount order (later ones override earlier ones).
// False if the game has none on this machine or one does not open.
inline bool mountGamePaks(as3d::Vfs& vfs) {
    const std::vector<std::string>& paks = gameData().paks;
    if (paks.empty()) return false;
    for (const std::string& path : paks) {
        auto src = as3d::makePakSource(as3d::openFileStream(path));
        if (!src) return false;
        vfs.mount(std::move(src));
    }
    return true;
}

inline bool available() {
    as3d::Blob b;
    return readExtracted("maps\\levels.txt", b);
}

// Whether the game under test plays (as3d::gameIsPlayable); defined in expected.h.
inline bool playable();

} // namespace testdata

#include "expected.h"

// Put at the top of a TEST_CASE that needs the data of the game under test: a format or
// definition test that runs for every game.
#define AS3D_REQUIRE_DATA()                                                                       \
    if (!testdata::available()) {                                                                 \
        std::fprintf(stderr, "SKIPPED (no data for game '%s'): %s\n", testdata::gameKey().c_str(), \
                     __FILE__);                                                                   \
        return;                                                                                   \
    }

// Put at the top of a TEST_CASE that plays the game (simulation, front end, integration): it
// is skipped, loudly, for a game that is not playable (as3d::gameIsPlayable). Combine with
// AS3D_REQUIRE_DATA() when the test also needs the game's files.
#define AS3D_REQUIRE_PLAYABLE()                                                                   \
    if (!testdata::playable()) {                                                                  \
        std::fprintf(stderr, "SKIPPED (game '%s' is not playable yet): %s\n",                     \
                     testdata::gameKey().c_str(), __FILE__);                                      \
        return;                                                                                   \
    }

// Put at the top of a TEST_CASE that assumes the first game's menus or content (its mission
// scripts, objects, front-end pictures, recorded numbers): skipped, loudly, for any other game,
// with the reason. Such a test runs in the as3d pass of tools/ci.sh.
#define AS3D_REQUIRE_FIRST_GAME(why)                                                              \
    if (testdata::game().id != as3d::GameId::AirStrike3D) {                                       \
        std::fprintf(stderr, "SKIPPED (game '%s': the test assumes the first game's %s): %s\n",   \
                     testdata::gameKey().c_str(), why, __FILE__);                                 \
        return;                                                                                   \
    }
