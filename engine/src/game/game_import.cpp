// as3d::importGameFiles (as3d/game_import.h).
#include "as3d/game_import.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>

#include "as3d/core.h"
#include "as3d/exe_texts.h"
#include "as3d/game_data.h"
#include "as3d/sha256.h"
#include "as3d/zip_reader.h"

namespace as3d {

namespace {

namespace fs = std::filesystem;

// tools/games.json, pak_sha256 (apps/tests/game_import_test.cpp checks that both agree).
const KnownPakHash kBuiltinPakHashes[] = {
    {GameId::AirStrike3D, "pak0.apk", "86745b285a6d31d7c1a9d1ee50c6afeb1b78163a75e5a52a39191095c7a6b3df"},
    {GameId::AirStrike3D, "pak1.apk", "548d8f1ad37cd3c2864ee1626dc4a592ba3640130ed8c0ec6155a99321a8d94a"},
    {GameId::AirStrike3D, "pak2.apk", "c992f0a28add309cedef544534c1a77d3cdf1415322ddffc9b3030766a6919d3"},
    {GameId::AirStrike2, "pak0.apk", "9df601a5ebe1fec171a5089f054bdcc550268080b262043db30fb22c9f77250f"},
    {GameId::AirStrike2, "pak1.apk", "858f176fd875f7c84439fb2398b1d37d1292afb708f0b63c0f2faa96efe46952"},
    {GameId::AirStrike2, "pak2.apk", "bc29cf7375e5557f3e9e19278e5318e77324d3f32d57f64116387712050c6175"},
    {GameId::GulfThunder, "pak0.apk", "0fde2db91134fdb2b97bed1797bc826f468fc9ad4f83198a2cb8fcbecc8d38f8"},
    {GameId::GulfThunder, "pak1.apk", "34dcddcbfb9b501f71a0a27c192431893345850843314eb2686e2b46d4f410c0"},
    {GameId::GulfThunder, "pak2.apk", "4c7513ebcf969da5127e7db7953414bcfff36b3cafe628c9bba679100697f930"},
    {GameId::GulfThunder, "pak4.apk", "836cf9c2d8e917ddc8cb442f2ff3e1d21b764b781bed4fe1cb95ece879552b6b"},
};
const KnownPakHash* g_pakHashes = kBuiltinPakHashes;
int g_pakHashCount = static_cast<int>(sizeof(kBuiltinPakHashes) / sizeof(kBuiltinPakHashes[0]));

constexpr size_t kMaxExeSize = 16u << 20;

enum class Kind { Pak, Settings, Logo, Exe };

// One file found in the import, loose or in a zip.
struct Candidate {
    Kind kind = Kind::Pak;
    std::string canon;   // the installed name: pakN.apk, Settings.xml, logo2s.tga (exe: its own)
    std::string group;   // its game directory (see the header)
    std::string display; // for the notes
    bool fromZip = false;
    bool loose = false;
    std::string path;    // a loose file, or the staged copy of a zip entry
    int zip = -1;        // index into the open zips
    ZipEntry entry;
    const GameProfile* game = nullptr; // decided
};

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string baseName(const std::string& p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

std::string dirName(const std::string& p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? std::string() : p.substr(0, s);
}

// The game directory of a file's directory: a trailing gfx/ and data/ left out.
std::string gameRoot(std::string dir) {
    auto strip = [&](const char* name) {
        const std::string last = lower(baseName(dir));
        if (last == name) dir = dirName(dir);
    };
    strip("gfx");
    strip("data");
    return dir;
}

bool classify(const std::string& name, Candidate* c) {
    const std::string n = lower(name);
    if (n.size() == 8 && n.compare(0, 3, "pak") == 0 && std::isdigit(static_cast<unsigned char>(n[3])) &&
        n.compare(4, 4, ".apk") == 0) {
        c->kind = Kind::Pak;
        c->canon = n;
    } else if (n == "settings.xml") {
        c->kind = Kind::Settings;
        c->canon = "Settings.xml";
    } else if (n == "logo2s.tga") {
        c->kind = Kind::Logo;
        c->canon = "logo2s.tga";
    } else if (n.size() > 4 && n.compare(n.size() - 4, 4, ".exe") == 0) {
        c->kind = Kind::Exe;
        c->canon = name;
    } else {
        return false;
    }
    return true;
}

bool isFile(const std::string& p) {
    std::error_code ec;
    return fs::is_regular_file(fs::path(p), ec);
}

std::uint64_t fileSize(const std::string& p) {
    std::error_code ec;
    auto s = fs::file_size(fs::path(p), ec);
    return ec ? 0 : static_cast<std::uint64_t>(s);
}

bool readWhole(const std::string& path, std::vector<std::uint8_t>* out, size_t maxSize) {
    if (fileSize(path) > maxSize) return false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out->resize(static_cast<size_t>(fileSize(path)));
    const bool ok = out->empty() || std::fread(out->data(), 1, out->size(), f) == out->size();
    std::fclose(f);
    return ok;
}

bool writeWhole(const std::string& path, const std::string& text) {
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = std::fclose(f) == 0 && ok;
    std::error_code ec;
    if (ok) fs::rename(fs::path(tmp), fs::path(path), ec);
    if (!ok || ec) {
        fs::remove(fs::path(tmp), ec);
        return false;
    }
    return true;
}

// Moves (or, across file systems, copies) a file into place, replacing what is there.
bool moveInto(const std::string& from, const std::string& to, bool keepSource) {
    std::error_code ec;
    if (!keepSource) {
        fs::rename(fs::path(from), fs::path(to), ec);
        if (!ec) return true;
    }
    ec.clear();
    const std::string tmp = to + ".tmp";
    fs::copy_file(fs::path(from), fs::path(tmp), fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    fs::rename(fs::path(tmp), fs::path(to), ec);
    if (ec) {
        fs::remove(fs::path(tmp), ec);
        return false;
    }
    if (!keepSource) fs::remove(fs::path(from), ec);
    return true;
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    return a.back() == '/' ? a + b : a + "/" + b;
}

const GameProfile* pakGameByHash(const std::string& name, const std::string& sha) {
    for (int i = 0; i < g_pakHashCount; ++i)
        if (name == g_pakHashes[i].name && sha == g_pakHashes[i].sha256) return &gameProfile(g_pakHashes[i].game);
    return nullptr;
}

bool gameHasPakHash(const GameProfile& g, const std::string& name) {
    for (int i = 0; i < g_pakHashCount; ++i)
        if (g_pakHashes[i].game == g.id && name == g_pakHashes[i].name) return true;
    return false;
}

bool profileHasPak(const GameProfile& g, const std::string& name) {
    for (const char* const* p = g.paks; *p; ++p)
        if (name == *p) return true;
    return false;
}

class Importer {
public:
    Importer(const std::string& incoming, const std::string& games) : incoming_(incoming), games_(games) {}

    ImportResult run() {
        std::error_code ec;
        staging_ = joinPath(games_, ".import-staging");
        fs::remove_all(fs::path(staging_), ec);
        if (!makeDirs(staging_)) {
            notes_.push_back("Cannot write to " + games_ + ".");
            return finish();
        }
        collect();
        readExecutables();
        identifyPak0s();
        identifyOtherPaks();
        attachLooseFiles();
        install();
        fs::remove_all(fs::path(staging_), ec);
        return finish();
    }

private:
    static bool makeDirs(const std::string& d) {
        std::error_code ec;
        fs::create_directories(fs::path(d), ec);
        return fs::is_directory(fs::path(d), ec);
    }

    ImportResult finish() {
        ImportResult r;
        for (int i = 0; i < kGameCount; ++i)
            if (std::find(imported_.begin(), imported_.end(), static_cast<GameId>(i)) != imported_.end())
                r.imported.push_back(gameProfile(static_cast<GameId>(i)).key);
        r.notes = notes_;
        return r;
    }

    // ---- 1: every file, loose or in a zip
    void collect() {
        std::error_code ec;
        std::vector<std::string> files;
        fs::recursive_directory_iterator it(fs::path(incoming_), ec), end;
        if (ec) {
            notes_.push_back("Cannot read " + incoming_ + ".");
            return;
        }
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            if (it->is_regular_file(ec)) files.push_back(it->path().string());
        }
        std::sort(files.begin(), files.end());
        const std::string prefix = joinPath(incoming_, "");
        for (const std::string& path : files) {
            std::string rel = path.compare(0, prefix.size(), prefix) == 0 ? path.substr(prefix.size()) : baseName(path);
            Candidate c;
            if (classify(baseName(rel), &c)) {
                c.group = "loose:" + gameRoot(dirName(rel));
                c.display = baseName(rel);
                c.loose = true;
                c.path = path;
                candidates_.push_back(c);
            } else if (lower(rel).size() > 4 && lower(rel).compare(lower(rel).size() - 4, 4, ".zip") == 0) {
                collectZip(path, baseName(rel));
            } else if (looksLikeZip(path)) {
                collectZip(path, baseName(rel));
            } else {
                notes_.push_back(baseName(rel) + ": not a game file, ignored.");
            }
        }
    }

    void collectZip(const std::string& path, const std::string& shown) {
        std::unique_ptr<ZipReader> z(new ZipReader());
        std::string err;
        if (!z->open(path, &err)) {
            notes_.push_back(shown + ": cannot read this zip (" + err + ").");
            return;
        }
        const int index = static_cast<int>(zips_.size());
        size_t used = 0;
        for (const ZipEntry& e : z->entries()) {
            if (e.isDir()) continue;
            Candidate c;
            if (!classify(baseName(e.name), &c)) continue;
            c.group = "zip" + std::to_string(index) + ":" + gameRoot(dirName(e.name));
            c.display = shown + ": " + e.name;
            c.fromZip = true;
            c.zip = index;
            c.entry = e;
            candidates_.push_back(c);
            ++used;
        }
        if (!used) notes_.push_back(shown + ": no game files in this zip.");
        zips_.push_back(std::move(z));
    }

    // A zip entry copied out into the staging directory (once).
    bool stage(Candidate& c, const std::string& name) {
        if (!c.path.empty()) return true;
        const std::string dir = joinPath(staging_, std::to_string(stageCount_++));
        if (!makeDirs(dir)) return false;
        const std::string to = joinPath(dir, name);
        std::string err;
        if (!zips_[static_cast<size_t>(c.zip)]->extractToFile(c.entry, to, &err)) {
            notes_.push_back(c.display + ": " + err + ".");
            return false;
        }
        c.path = to;
        return true;
    }

    // ---- 2: executables give the texts
    void readExecutables() {
        int unknown = 0;
        for (Candidate& c : candidates_) {
            if (c.kind != Kind::Exe) continue;
            std::vector<std::uint8_t> bytes;
            std::string err;
            bool ok = c.fromZip ? zips_[static_cast<size_t>(c.zip)]->extractToMemory(c.entry, &bytes, kMaxExeSize, &err)
                                : readWhole(c.path, &bytes, kMaxExeSize);
            if (!ok) {
                if (c.loose) notes_.push_back(c.display + ": not the executable of a known game, ignored.");
                else ++unknown;
                continue;
            }
            const std::string sha = sha256Hex(bytes.data(), bytes.size());
            const GameProfile* g = gameOfExecutable(sha);
            if (!g) {
                if (c.loose) notes_.push_back(c.display + ": not the executable of a known game, ignored.");
                else ++unknown;
                continue;
            }
            std::string text;
            if (!extractExeTexts(bytes, sha, nullptr, &text, &err)) {
                notes_.push_back(std::string(g->title) + ": cannot read the texts of " + c.display + " (" + err + ").");
                continue;
            }
            texts_[g->id] = text;
            textsFrom_[g->id] = c.display;
        }
        if (unknown) notes_.push_back(std::to_string(unknown) + " other executable(s) in the zip ignored.");
    }

    // ---- 3: pak0.apk names the game of its directory
    void identifyPak0s() {
        for (Candidate& c : candidates_) {
            if (c.kind != Kind::Pak || c.canon != "pak0.apk") continue;
            if (groupGame_.count(c.group) && groupGame_[c.group]) {
                notes_.push_back(c.display + ": a second pak0.apk in the same place, ignored.");
                continue;
            }
            if (!stage(c, "pak0.apk")) continue;
            const GameProfile* g = identifyPak0File(c.path);
            if (!g) {
                notes_.push_back(c.display + ": not the pak0.apk of a known game, ignored.");
                groupGame_[c.group] = nullptr;
                continue;
            }
            c.game = g;
            groupGame_[c.group] = g;
        }
    }

    // ---- 4: the other paks, by hash or beside their pak0
    void identifyOtherPaks() {
        for (Candidate& c : candidates_) {
            if (c.kind != Kind::Pak || c.canon == "pak0.apk") continue;
            const GameProfile* beside = groupGame_.count(c.group) ? groupGame_[c.group] : nullptr;
            // Inside a zip only the paks of an identified game directory are read (the
            // original download also holds games the engine does not run).
            if (c.fromZip && !beside) continue;
            if (!stage(c, c.canon)) continue;
            const std::string sha = sha256File(c.path);
            const GameProfile* byHash = pakGameByHash(c.canon, sha);
            if (byHash && (!beside || beside == byHash)) {
                c.game = byHash;
            } else if (beside) {
                if (!profileHasPak(*beside, c.canon)) {
                    notes_.push_back(c.display + ": " + beside->title + " has no " + c.canon + ", ignored.");
                    continue;
                }
                if (gameHasPakHash(*beside, c.canon)) {
                    notes_.push_back(c.display + ": not " + beside->title + "'s " + c.canon + " (different content), ignored.");
                    continue;
                }
                c.game = beside; // no hash to compare with: taken by name
            } else {
                notes_.push_back(c.display + ": not a pak of a known game, ignored.");
                continue;
            }
            if (!profileHasPak(*c.game, c.canon)) {
                notes_.push_back(c.display + ": " + c.game->title + " has no " + c.canon + ", ignored.");
                c.game = nullptr;
                continue;
            }
            // A directory without pak0.apk takes the game of its paks.
            if (!beside) groupHashGame_[c.group] = c.game;
        }
    }

    // ---- 5: Settings.xml and the logo
    void attachLooseFiles() {
        bool anyGame = false;
        for (const Candidate& c : candidates_)
            if (c.game && c.kind == Kind::Pak) anyGame = true;
        const GameProfile* only = nullptr;
        if (!anyGame) {
            const std::vector<const GameProfile*> have = presentGames();
            if (have.size() == 1) only = have.front();
        }
        for (Candidate& c : candidates_) {
            if (c.kind != Kind::Settings && c.kind != Kind::Logo) continue;
            const GameProfile* g = groupGame_.count(c.group) ? groupGame_[c.group] : nullptr;
            if (!g && groupHashGame_.count(c.group)) g = groupHashGame_[c.group];
            if (!g && c.loose && only) g = only;
            if (!g) {
                if (c.loose)
                    notes_.push_back(c.display + ": no game to attach it to (import it with the game's pak0.apk), ignored.");
                continue;
            }
            c.game = g;
        }
        // One of each per game: the first.
        std::map<std::pair<int, std::string>, bool> seen;
        for (Candidate& c : candidates_) {
            if (!c.game || c.kind == Kind::Exe) continue;
            auto key = std::make_pair(static_cast<int>(c.game->id), c.canon);
            if (seen[key]) {
                notes_.push_back(c.display + ": " + c.game->title + " already has a " + c.canon + " in this import, ignored.");
                c.game = nullptr;
            }
            seen[key] = true;
        }
    }

    // Games that already have a pak0.apk under gamesDir (complete or not).
    std::vector<const GameProfile*> presentGames() const {
        std::vector<const GameProfile*> out;
        for (int i = 0; i < kGameCount; ++i) {
            const GameProfile& g = gameProfile(static_cast<GameId>(i));
            if (isFile(joinPath(joinPath(games_, g.key), "pak0.apk"))) out.push_back(&g);
        }
        return out;
    }

    // ---- 6: into <gamesDir>/<key>/
    void install() {
        for (int i = 0; i < kGameCount; ++i) {
            const GameProfile& g = gameProfile(static_cast<GameId>(i));
            std::vector<Candidate*> files;
            for (Candidate& c : candidates_)
                if (c.game == &g && c.kind != Kind::Exe) files.push_back(&c);
            // Paks in mount order, then Settings.xml and the logo.
            std::stable_sort(files.begin(), files.end(), [](const Candidate* a, const Candidate* b) {
                return std::make_pair(static_cast<int>(a->kind), a->canon) < std::make_pair(static_cast<int>(b->kind), b->canon);
            });
            const bool hasTexts = texts_.count(g.id) != 0;
            if (files.empty() && !hasTexts) continue;
            const std::string dir = joinPath(games_, g.key);
            if (!makeDirs(dir)) {
                notes_.push_back(std::string(g.title) + ": cannot create " + dir + ".");
                continue;
            }
            std::vector<std::string> got;
            for (Candidate* c : files) {
                if (!stage(*c, c->canon)) continue;
                if (!moveInto(c->path, joinPath(dir, c->canon), false)) {
                    notes_.push_back(std::string(g.title) + ": cannot write " + c->canon + ".");
                    continue;
                }
                got.push_back(c->canon);
            }
            if (hasTexts) {
                if (writeWhole(joinPath(dir, g.textsFile), texts_[g.id]))
                    got.push_back(std::string("texts (from ") + baseName(textsFrom_[g.id]) + ")");
                else
                    notes_.push_back(std::string(g.title) + ": cannot write " + g.textsFile + ".");
            }
            std::string line = std::string(g.title) + ": imported ";
            for (size_t k = 0; k < got.size(); ++k) line += (k ? ", " : "") + got[k];
            notes_.push_back(line + ".");
            std::vector<std::string> missing;
            for (const char* const* p = g.paks; *p; ++p)
                if (!isFile(joinPath(dir, *p))) missing.push_back(*p);
            for (const std::string& m : missing) notes_.push_back(std::string(g.title) + ": " + m + " missing.");
            if (missing.empty()) {
                imported_.push_back(g.id);
                std::vector<std::string> optional;
                if (!isFile(joinPath(dir, g.textsFile)))
                    optional.push_back(std::string("the executable (") + g.exeName + ") for the menu texts");
                if (!isFile(joinPath(dir, "Settings.xml"))) optional.push_back("Settings.xml");
                std::string ready = std::string(g.title) + ": ready to play.";
                if (!optional.empty()) {
                    ready += " Optional, not imported yet: ";
                    for (size_t k = 0; k < optional.size(); ++k) ready += (k ? ", " : "") + optional[k];
                    ready += ".";
                }
                notes_.push_back(ready);
            }
        }
        if (imported_.empty() && texts_.empty()) {
            bool any = false;
            for (const Candidate& c : candidates_)
                if (c.game) any = true;
            if (!any)
                notes_.push_back("No game files recognised. Pick the files of the game's data folder (pak0.apk, "
                                 "pak1.apk, ...) and its executable, or a zip of the game folder.");
        }
    }

    std::string incoming_, games_, staging_;
    int stageCount_ = 0;
    std::vector<std::unique_ptr<ZipReader>> zips_;
    std::vector<Candidate> candidates_;
    std::map<std::string, const GameProfile*> groupGame_, groupHashGame_;
    std::map<GameId, std::string> texts_, textsFrom_;
    std::vector<GameId> imported_;
    std::vector<std::string> notes_;
};

} // namespace

ImportResult importGameFiles(const std::string& incomingDir, const std::string& gamesDir) {
    Importer imp(incomingDir, gamesDir);
    ImportResult r = imp.run();
    for (const std::string& n : r.notes) AS3D_INFO("import: %s", n.c_str());
    return r;
}

bool importedGameComplete(const std::string& gamesDir, const GameProfile& game) {
    const std::string dir = joinPath(gamesDir, game.key);
    for (const char* const* p = game.paks; *p; ++p)
        if (!isFile(joinPath(dir, *p))) return false;
    return true;
}

std::vector<const GameProfile*> importedGames(const std::string& gamesDir) {
    std::vector<const GameProfile*> out;
    for (int i = 0; i < kGameCount; ++i) {
        const GameProfile& g = gameProfile(static_cast<GameId>(i));
        if (importedGameComplete(gamesDir, g)) out.push_back(&g);
    }
    return out;
}

void setKnownPakHashesForTest(const KnownPakHash* hashes, int count) {
    if (!hashes) {
        g_pakHashes = kBuiltinPakHashes;
        g_pakHashCount = static_cast<int>(sizeof(kBuiltinPakHashes) / sizeof(kBuiltinPakHashes[0]));
    } else {
        g_pakHashes = hashes;
        g_pakHashCount = count;
    }
}

} // namespace as3d
