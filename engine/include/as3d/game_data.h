// Where the data of one game is on disk, and which game a directory of paks belongs to.
//
// Layout under a data root (docs/spec/README.md, "Games and which spec applies"):
//   as3d   root/third_party_local/original/{data/pak*.apk, data/Settings.xml}
//          root/assets_extracted/
//   others root/third_party_local/games/<key>/{data/pak*.apk, data/Settings.xml}
//          root/assets_extracted_games/<key>/
// Paks are identified by content (pak0.apk's size and a hash of its first 64 KB), never by
// name or path. No SDL, no GL; desktop file system only (the Android and web builds read the
// paks from the APK / the page and pick the game by key).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/game_profile.h"

namespace as3d {

// Where one game's data is. Empty strings and empty lists mean "not there".
struct GameData {
    const GameProfile* game = nullptr;
    std::string root;
    std::string installDir;   // holds the executable and data/
    std::string dataDir;      // holds the paks and Settings.xml
    std::string extractedDir; // the extracted files (may not exist)
    bool hasExtracted = false; // extractedDir holds maps/levels.txt
    std::vector<std::string> paks;    // the profile's paks that exist, in mount order
    std::vector<std::string> missing; // what was looked for and is not there
    std::string settingsXml;  // dataDir/Settings.xml if it exists
    std::string textsFile;    // extractedDir/<profile textsFile> if it exists
    std::string logoFile;     // dataDir/gfx/logo2s.tga if it exists

    // Something to run the game from: extracted files, or the game's base paks.
    bool present() const { return hasExtracted || !paks.empty(); }
};

// Applies the layout above. Never fails: what is absent is listed in `missing`.
GameData locateGameData(const std::string& root, const GameProfile& game);

// The game whose paks sit in `dir`, from the content of dir/pak0.apk; nullptr if the file is
// missing or matches no known game.
const GameProfile* identifyPaks(const std::string& dir);

// Games whose data is on this machine, in the order as3d, as2, gulf: paks that identify as
// the game (looked for in the game's own install directory), or extracted files.
std::vector<GameData> detectGames(const std::string& root);

// The data of a game for a desktop tool. `key` names the game ("" = $AS3D_GAME, then as3d if
// present, then the first detected); `paksDir` is the --paks option ("" = none): its paks are
// used, and without a key the game is identified from them. False and a message on `error`
// (unknown key, no data, unidentified paks) that lists the detected games.
bool chooseGameData(const std::string& root, const std::string& key, const std::string& paksDir, GameData* out,
                    std::string* error);

// One line per game: key, title, version, where found (--list-games).
std::string describeGames(const std::string& root);

// Whether the game runs end to end. False for the sequels until their packages land; the
// Android and web entry points refuse an unplayable game unless told otherwise.
inline bool gameIsPlayable(const GameProfile& g) { return g.id == GameId::AirStrike3D; }

// What identifies a game's pak0.apk. The table is a fact about files, not game data.
struct PakSignature {
    GameId game;
    std::uint64_t size;
    std::uint64_t headHash; // FNV-1a 64 over the first 64 KB (all of it if shorter)
};
// Size and head hash of a pak file; false if it cannot be read.
bool readPakSignature(const std::string& path, std::uint64_t* size, std::uint64_t* headHash);
// Test hook: replaces the table of known signatures; (nullptr, 0) restores the built-in one.
void setPakSignaturesForTest(const PakSignature* sigs, int count);

} // namespace as3d
