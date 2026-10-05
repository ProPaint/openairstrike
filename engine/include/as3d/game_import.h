// Importing the player's own game files (the data-free Android build, docs/android.md, "Your
// own game files"): the files picked by the player, loose or in zip archives, are recognised
// by content and installed as one directory per game,
//   <gamesDir>/<key>/{pak0.apk, pak1.apk, ..., Settings.xml, logo2s.tga, <texts file>}
// with the names the APK assets use (tools/android_build.sh), so the game reads them the same
// way. No SDL, no GL: unit-tested on the desktop (apps/tests/game_import_test.cpp).
//
// Recognition:
//   pak0.apk     by its signature (as3d/game_data.h, readPakSignature); it names the game of
//                the files around it.
//   pakN.apk     by its SHA-256 (tools/games.json); a pak whose game has no known hash for that
//                name is taken by name beside an identified pak0.apk.
//   *.exe        by its SHA-256: the front-end texts are read out of it (as3d/exe_texts.h)
//                into the game's texts file; the executable itself is not kept.
//   Settings.xml, logo2s.tga   belong to the game identified beside them. Loose ones imported
//                on their own go to the one game already imported, when there is exactly one.
// "Beside" means in the same game directory: the files' directory with a trailing data/ or
// data/gfx/ left out, so a zip of the game directory (exe at the top, paks in data/, the logo
// in data/gfx/) or of the original download (several games in one archive) groups correctly.
#pragma once

#include <string>
#include <vector>

#include "as3d/game_profile.h"

namespace as3d {

struct ImportResult {
    // The games that received files in this import and now have every pak, in GameId order.
    std::vector<std::string> imported;
    // What was recognised, what was ignored, what is missing; one line each, for the player.
    std::vector<std::string> notes;
};

// Imports every file under `incomingDir` (recursively; zips are opened) into `gamesDir`.
// Loose files that are used are moved (renamed when on the same file system); the caller
// removes what is left in incomingDir. Never fails as a whole: problems become notes.
ImportResult importGameFiles(const std::string& incomingDir, const std::string& gamesDir);

// Whether <gamesDir>/<key>/ holds every pak of the game.
bool importedGameComplete(const std::string& gamesDir, const GameProfile& game);
// The games with a complete directory under gamesDir, in GameId order.
std::vector<const GameProfile*> importedGames(const std::string& gamesDir);

// The SHA-256 of a known pak (tools/games.json, pak_sha256): a fact about files, not data.
struct KnownPakHash {
    GameId game;
    const char* name;
    const char* sha256;
};
// Test hook: replaces the table of known pak hashes; (nullptr, 0) restores the built-in one.
void setKnownPakHashesForTest(const KnownPakHash* hashes, int count);

} // namespace as3d
