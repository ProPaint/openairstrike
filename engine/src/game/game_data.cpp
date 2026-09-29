#include "as3d/game_data.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace as3d {

namespace {

namespace fs = std::filesystem;

constexpr size_t kHeadBytes = 64 * 1024;

// pak0.apk of each game: size and FNV-1a 64 of the first 64 KB (full SHA-256 of every pak in
// tools/games.json).
const PakSignature kBuiltinSignatures[] = {
    {GameId::AirStrike3D, 16717041ull, 0x4fe87f398b954b0aull},
    {GameId::AirStrike2, 39411244ull, 0x095dd5524bc31d6full},
    {GameId::GulfThunder, 35840140ull, 0xbf1903a53496c57eull},
};

// The games that run end to end (gameIsPlayable). The single source of that fact:
// tools/web_known_files.py reads this line (keep it one line of quoted keys), and the tests ask
// gameIsPlayable. Proposed to move into GameProfile as `bool playable` (docs/spec/issues/163).
const char* const kPlayableGames[] = {"as3d", "as2"};

const PakSignature* g_signatures = kBuiltinSignatures;
int g_signatureCount = static_cast<int>(sizeof(kBuiltinSignatures) / sizeof(kBuiltinSignatures[0]));

bool exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(fs::path(path), ec);
}

std::string join(const std::string& a, const std::string& b) { return a.empty() ? b : a + "/" + b; }

std::string dirOfFile(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// Fills the parts that depend on the data directory and the extracted directory.
void fillFiles(GameData& d) {
    d.hasExtracted = exists(join(d.extractedDir, "maps/levels.txt"));
    for (const char* const* p = d.game->paks; *p; ++p) {
        std::string path = join(d.dataDir, *p);
        if (exists(path)) d.paks.push_back(path);
        else d.missing.push_back(path);
    }
    if (!d.hasExtracted) d.missing.push_back(join(d.extractedDir, "maps/levels.txt"));
    std::string s = join(d.dataDir, "Settings.xml");
    if (exists(s)) d.settingsXml = s;
    std::string t = join(d.extractedDir, d.game->textsFile);
    if (exists(t)) d.textsFile = t;
    std::string l = join(d.dataDir, "gfx/logo2s.tga");
    if (exists(l)) d.logoFile = l;
}

std::string knownKeys() {
    std::string s;
    for (int i = 0; i < kGameCount; ++i) {
        if (i) s += ", ";
        s += gameProfile(static_cast<GameId>(i)).key;
    }
    return s;
}

std::string detectedKeys(const std::vector<GameData>& found) {
    std::string s;
    for (const GameData& g : found) {
        if (!s.empty()) s += ", ";
        s += g.game->key;
    }
    return s.empty() ? "none" : s;
}

std::string whereFound(const GameData& d) {
    std::string s;
    if (!d.paks.empty()) s = d.dataDir + " (" + std::to_string(d.paks.size()) + " paks)";
    if (d.hasExtracted) s += (s.empty() ? "" : ", ") + d.extractedDir + " (extracted)";
    return s;
}

} // namespace

bool gameIsPlayable(const GameProfile& g) {
    for (const char* k : kPlayableGames)
        if (std::strcmp(k, g.key) == 0) return true;
    return false;
}

std::vector<std::string> playableGameKeys() {
    std::vector<std::string> out;
    for (int i = 0; i < kGameCount; ++i) {
        const GameProfile& g = gameProfile(static_cast<GameId>(i));
        if (gameIsPlayable(g)) out.push_back(g.key);
    }
    return out;
}

bool readPakSignature(const std::string& path, std::uint64_t* size, std::uint64_t* headHash) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char buf[4096];
    std::uint64_t h = 0xcbf29ce484222325ull, total = 0;
    size_t head = 0;
    for (;;) {
        size_t n = std::fread(buf, 1, sizeof buf, f);
        if (n == 0) break;
        total += n;
        for (size_t i = 0; i < n && head < kHeadBytes; ++i, ++head) h = (h ^ buf[i]) * 0x100000001b3ull;
        // Past the head only the length is needed.
    }
    bool ok = !std::ferror(f);
    std::fclose(f);
    if (!ok) return false;
    if (size) *size = total;
    if (headHash) *headHash = h;
    return true;
}

void setPakSignaturesForTest(const PakSignature* sigs, int count) {
    if (!sigs) {
        g_signatures = kBuiltinSignatures;
        g_signatureCount = static_cast<int>(sizeof(kBuiltinSignatures) / sizeof(kBuiltinSignatures[0]));
    } else {
        g_signatures = sigs;
        g_signatureCount = count;
    }
}

const GameProfile* identifyPaks(const std::string& dir) {
    std::uint64_t size = 0, hash = 0;
    if (!readPakSignature(join(dir, "pak0.apk"), &size, &hash)) return nullptr;
    for (int i = 0; i < g_signatureCount; ++i)
        if (g_signatures[i].size == size && g_signatures[i].headHash == hash) return &gameProfile(g_signatures[i].game);
    return nullptr;
}

GameData locateGameData(const std::string& root, const GameProfile& game) {
    GameData d;
    d.game = &game;
    d.root = root;
    if (game.id == GameId::AirStrike3D) {
        d.installDir = join(root, "third_party_local/original");
        d.extractedDir = join(root, "assets_extracted");
    } else {
        d.installDir = join(root, std::string("third_party_local/games/") + game.key);
        d.extractedDir = join(root, std::string("assets_extracted_games/") + game.key);
    }
    d.dataDir = join(d.installDir, "data");
    fillFiles(d);
    return d;
}

std::vector<GameData> detectGames(const std::string& root) {
    std::vector<GameData> found;
    for (int i = 0; i < kGameCount; ++i) {
        const GameProfile& g = gameProfile(static_cast<GameId>(i));
        GameData d = locateGameData(root, g);
        // Paks count only if they are this game's; a directory named after a game may hold
        // another one's files.
        if (!d.paks.empty()) {
            const GameProfile* id = identifyPaks(d.dataDir);
            if (id != &g) {
                d.paks.clear();
                d.missing.push_back(d.dataDir + ": pak0.apk is not " + g.title);
            }
        }
        if (d.present()) found.push_back(d);
    }
    return found;
}

bool chooseGameData(const std::string& root, const std::string& keyArg, const std::string& paksDir, GameData* out,
                    std::string* error) {
    std::string key = keyArg;
    if (key.empty()) {
        const char* env = std::getenv("AS3D_GAME");
        if (env && *env) key = env;
    }
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (!paksDir.empty()) {
        const GameProfile* g = nullptr;
        if (key.empty()) {
            g = identifyPaks(paksDir);
            if (!g)
                return fail("cannot tell which game the paks in " + paksDir +
                            " belong to (pak0.apk missing or unknown); name it with --game (" + knownKeys() + ")");
        } else {
            g = findGameProfile(key);
            if (!g) return fail("unknown game '" + key + "' (known: " + knownKeys() + ")");
        }
        GameData d = locateGameData(root, *g);
        d.dataDir = paksDir;
        d.installDir = dirOfFile(paksDir);
        d.paks.clear();
        d.missing.clear();
        d.settingsXml.clear();
        d.logoFile.clear();
        fillFiles(d);
        if (d.paks.empty()) return fail("no " + std::string(g->title) + " paks in " + paksDir);
        *out = d;
        return true;
    }
    std::vector<GameData> found = detectGames(root);
    if (key.empty()) {
        if (found.empty())
            return fail("no game data found under " + root + " (set AS3D_DATA_ROOT or use --data); detected: none");
        for (const GameData& d : found)
            if (d.game->id == GameId::AirStrike3D) {
                *out = d;
                return true;
            }
        *out = found.front();
        return true;
    }
    const GameProfile* g = findGameProfile(key);
    if (!g) return fail("unknown game '" + key + "' (known: " + knownKeys() + "); detected: " + detectedKeys(found));
    for (const GameData& d : found)
        if (d.game == g) {
            *out = d;
            return true;
        }
    GameData d = locateGameData(root, *g);
    std::string msg = std::string("no data for game '") + g->key + "' (" + g->title + ") under " + root;
    if (!d.missing.empty()) msg += "; looked for " + d.missing.front();
    return fail(msg + "; detected: " + detectedKeys(found));
}

std::string describeGames(const std::string& root) {
    std::vector<GameData> found = detectGames(root);
    std::string s;
    for (int i = 0; i < kGameCount; ++i) {
        const GameProfile& g = gameProfile(static_cast<GameId>(i));
        const GameData* d = nullptr;
        for (const GameData& f : found)
            if (f.game == &g) d = &f;
        s += std::string(g.key) + "\t" + g.title + "\t" + g.version + "\t" + (d ? whereFound(*d) : "not found") + "\n";
    }
    return s;
}

} // namespace as3d
