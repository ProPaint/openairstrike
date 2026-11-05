// Importing the player's own game files (the data-free Android build, docs/android.md, "Your
// own game files"): the files picked by the player, loose or in zip archives, are recognised
// by content and installed as one directory per game,
//   <gamesDir>/<key>/{pak0.apk, pak1.apk, ..., Settings.xml, logo2s.tga, <texts file>}
// with the names the APK assets use (tools/android_build.sh), so the game reads them the same
// way. No SDL, no GL: unit-tested on the desktop (apps/tests/game_import_test.cpp).
//
// Recognition (docs/spec/issues/170, section 2: by content, any build of a game):
//   pak0.apk     by its entry names (as3d/game_data.h, identifyPakFile: the head signature as
//                a fast path, then the fingerprint of its file table, then the overlap with
//                the reference lists); it names the game of the files around it. A truncated
//                pak or a file that is no pak is refused with a note saying so.
//   pakN.apk     by a known SHA-256 first (tools/games.json, every build of the survey), else
//                by its names: beside an identified pak0.apk it must overlap the game's
//                reference pak of that name by 0.9 (another build with a few files changed
//                passes; "different content" otherwise); a pak the profile lists without a
//                reference (AirStrike 3D's optional pak3.apk, one patch's override pak) is kept
//                when every name it holds is the game's. Alone, a pak whose names match one
//                game's pak of that name is taken; one whose names two games share (the
//                sequels' pak2) waits for the paks beside it.
//   no pak0.apk  a game directory whose other files are pak0's entries (the "Full RIP" and
//                "Desert Hawk" copies keep them loose under data/): when they cover 99 % of a
//                game's pak0 names a pak0.apk is built from them (as3d/pak_file.h, writePak)
//                and takes its place.
//   *.exe        by its SHA-256 (the address tables) or, for any other build, by the hash
//                dictionary (as3d/exe_texts.h): the front-end texts are read out of it into
//                the game's texts file; the executable itself is not kept.
//   Settings.xml, logo2s.tga   belong to the game identified beside them. Loose ones imported
//                on their own go to the one game already imported, when there is exactly one.
// "Beside" means in the same game directory: the files' directory with a trailing data/ or
// data/gfx/ left out, so a zip of the game directory (exe at the top, paks in data/, the logo
// in data/gfx/) or of the original download (several games in one archive) groups correctly.
// The reference name lists (tools/pak_entries/) come with the desktop build from the source
// tree and with the Android import from the APK's assets; without them only exact matches
// work and no pak0.apk can be built. A game is complete with its required paks; an optional
// one is mounted when there.
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
