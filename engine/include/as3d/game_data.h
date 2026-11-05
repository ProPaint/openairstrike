// Where the data of one game is on disk, and which game a directory of paks belongs to.
//
// Layout under a data root (docs/spec/README.md, "Games and which spec applies"):
//   as3d   root/third_party_local/original/{data/pak*.apk, data/Settings.xml}
//          root/assets_extracted/
//   others root/third_party_local/games/<key>/{data/pak*.apk, data/Settings.xml}
//          root/assets_extracted_games/<key>/
// Paks are identified by content, never by name or path (docs/spec/issues/170, section 2): by
// the names in their file table, compared with the reference lists of the owner's paks
// (tools/pak_entries/), as a fingerprint compiled in here (entry count and SHA-256 of the
// sorted names: an exact match) and, where the lists themselves are at hand, by overlap (a
// pak of another build with a few files more or less). The old head signature of pak0.apk
// (size and a hash of the first 64 KB) stays as a fast path. A directory without pak0.apk
// whose loose files cover the game's pak0 names gets a pak0.apk built from them. No SDL, no
// GL; desktop file system only (the Android and web builds read the paks from the APK / the
// page and pick the game by key).
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
    std::vector<std::string> missing; // what was looked for and is not there (optional paks left out)
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
// The same for one file, whatever its name: the game whose pak0.apk it is (its names equal a
// game's reference pak0, or overlap it by kPak0MinOverlap), or nullptr.
const GameProfile* identifyPak0File(const std::string& path);

// Games whose data is on this machine, in the order as3d, as2, gulf: paks that identify as
// the game (looked for in the game's own install directory), or extracted files.
std::vector<GameData> detectGames(const std::string& root);

// The data of a game for a desktop tool. `key` names the game ("" = $AS3D_GAME, then as3d if
// present, then the first detected); `paksDir` is the --paks option ("" = none): its paks are
// used, and without a key the game is identified from them. A paksDir without pak0.apk whose
// loose files cover a game's pak0 names (a "Desert Hawk" install) gets a pak0.apk built from
// them, kept under root/third_party_local/built/<key>/ (or in paksDir when the root cannot be
// written) and mounted first; the log says where. False and a message on `error` (unknown
// key, no data, unidentified paks) that lists the detected games.
bool chooseGameData(const std::string& root, const std::string& key, const std::string& paksDir, GameData* out,
                    std::string* error);

// One line per game: key, title, version, where found (--list-games).
std::string describeGames(const std::string& root);

// Whether the game runs end to end and is offered to players: AirStrike 3D and AirStrike 2;
// Gulf Thunder not yet. The one place this is decided (kPlayableGames in game_data.cpp, which
// tools/web_known_files.py and the tests read); the game selector lists only playable games,
// and the Android and web entry points refuse an unplayable one unless told otherwise.
bool gameIsPlayable(const GameProfile& g);
// The keys of the playable games, in GameId order.
std::vector<std::string> playableGameKeys();

// ---- Identification by content (docs/spec/issues/170, section 2)

// The head signature of a game's pak0.apk: the fast path. A fact about files, not game data.
struct PakSignature {
    GameId game;
    std::uint64_t size;
    std::uint64_t headHash; // FNV-1a 64 over the first 64 KB (all of it if shorter)
};
// Size and head hash of a pak file; false if it cannot be read.
bool readPakSignature(const std::string& path, std::uint64_t* size, std::uint64_t* headHash);
// Test hook: replaces the table of known signatures; (nullptr, 0) restores the built-in one.
void setPakSignaturesForTest(const PakSignature* sigs, int count);

// The reference file table of one pak of a game: how many entries, and the SHA-256 of their
// names (as3d::pakEntryNames: lower case, backslashes, sorted) joined by LF. Generated from
// tools/pak_entries/<key>[.pakN].txt by `tools/paktool.py fingerprint`; the test checks them.
struct PakFingerprint {
    GameId game;
    const char* pak;      // "pak0.apk", "pak1.apk", ...
    std::uint32_t count;
    const char* sha256;
};
// Test hook: replaces the fingerprint table; (nullptr, 0) restores the built-in one.
void setPakFingerprintsForTest(const PakFingerprint* table, int count);
// The built-in table, for the test that compares it with the lists.
const PakFingerprint* pakFingerprints(int* count);

// The reference names themselves (tools/pak_entries/), needed for the near matches, the
// override paks and building pak0.apk from loose files. Loaded on first use from
// $AS3D_PAK_ENTRIES or the source tree's tools/pak_entries (desktop builds); a host without
// a source tree gives them with setPakEntryNames (the Android import reads them out of the
// APK's assets). Without them only exact matches work.
// One name per line; replaces what was known for (game, pak). Lines are normalized.
void setPakEntryNames(GameId game, const std::string& pak, const std::string& text);
// Reads every <key>.txt (pak0.apk) and <key>.<pakN>.txt of `dir`; false with a message when
// the directory has no list at all.
bool loadPakEntryNames(const std::string& dir, std::string* error);
// The sorted names of a reference pak, or nullptr when they are not known.
const std::vector<std::string>* pakEntryNamesOf(GameId game, const std::string& pak);
// Test hook: forgets every list (the built-in location is read again on the next use).
void resetPakEntryNamesForTest();

constexpr double kPak0MinOverlap = 0.98;    // Jaccard index for a pak0.apk of another build
constexpr double kPakMinOverlap = 0.9;      // ... for the other paks beside an identified pak0
constexpr double kLooseMinCoverage = 0.99;  // share of pak0's names loose files must cover

// What a pak file is, from its file table.
struct PakIdentity {
    const GameProfile* game = nullptr; // the game of the reference pak it matches best (null: none)
    std::string pak;                   // that reference pak's name
    bool exact = false;                // its names are the reference's (or the head signature matched)
    double overlap = 0;                // Jaccard index with that reference pak
    bool ambiguous = false;            // an exact match of more than one game (the sequels' pak2 share their names)
    std::uint32_t entries = 0;
    std::uint64_t bytesShort = 0;      // > 0: truncated by this many bytes; refused
    std::string error;                 // why it is refused ("truncated, N bytes short: unpack it again", "not a pak archive (...)")
    std::vector<std::string> names;    // its entry names, normalized and sorted (empty after the fast path or an error)
};
// Reads the file's table (or, for a pak0 with a known head signature, nothing more) and
// compares its names with every reference pak: an exact fingerprint first, else the best
// overlap among the references whose names are known. No threshold is applied here.
PakIdentity identifyPakFile(const std::string& path);
// Whether the file identifyPakFile looked at is the given reference pak of the game: an
// equal fingerprint, or (names known) an overlap of minOverlap or more, which `overlap`
// (optional) receives. Decides for one reference, where `id` only names the best one.
bool pakMatches(const PakIdentity& id, const GameProfile& game, const std::string& pak, double minOverlap, double* overlap);
// Whether a reference fingerprint exists for the game's pak (AirStrike 3D's pak3.apk has none).
bool pakHasReference(const GameProfile& game, const std::string& pak);
// The share of `names` (normalized) that are names of the game, in any of its reference paks
// whose names are known: 1 for an override pak such as AirStrike 3D's pak3.apk. -1 when none
// of the game's lists is known.
double shareOfGameNames(const std::vector<std::string>& names, const GameProfile& game);

// ---- A pak0.apk from loose files (section 2, point 4)

// One loose file: its entry name (relative path, normalized) and where it is.
struct LooseFile {
    std::string name;
    std::string path;
};
// The game whose pak0 names the loose files cover by kLooseMinCoverage or more (`only`:
// that game or nothing), and the share covered. nullptr when none, or no list is known.
const GameProfile* gameOfLooseFiles(const std::vector<LooseFile>& files, const GameProfile* only, double* coverage);
// The regular files under `dir` (a data directory: gfx\, models\, ... below it), recursively,
// as entry names relative to it. Paks, Settings.xml and other files not under a subdirectory
// are included too; gameOfLooseFiles only counts the names pak0 has.
std::vector<LooseFile> looseFilesUnder(const std::string& dir);
// Writes `out` (a pak0.apk, as3d/pak_file.h) from the files of `files` that are names of the
// game's pak0, in the reference order. `count` gets how many were packed.
bool writePak0FromLooseFiles(const std::vector<LooseFile>& files, const GameProfile& game, const std::string& out,
                             int* count, std::string* error);

} // namespace as3d
