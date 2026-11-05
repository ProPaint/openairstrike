// as3d::importGameFiles (as3d/game_import.h).
#include "as3d/game_import.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <set>

#include "as3d/core.h"
#include "as3d/exe_texts.h"
#include "as3d/fs_util.h"
#include "as3d/game_data.h"
#include "as3d/pak_file.h"
#include "as3d/sha256.h"
#include "as3d/zip_reader.h"
#if AS3D_WITH_ARCHIVES
#include "as3d/archive.h"
#endif

namespace as3d {

namespace {

namespace fs = std::filesystem;

// tools/games.json, pak_sha256: every known build of each pak (apps/tests/game_import_test.cpp
// checks that both agree). The fast path; a pak with another hash is judged by its entry
// names (as3d/game_data.h, identifyPakFile).
const KnownPakHash kBuiltinPakHashes[] = {
    {GameId::AirStrike3D, "pak0.apk", "86745b285a6d31d7c1a9d1ee50c6afeb1b78163a75e5a52a39191095c7a6b3df"},
    {GameId::AirStrike3D, "pak1.apk", "548d8f1ad37cd3c2864ee1626dc4a592ba3640130ed8c0ec6155a99321a8d94a"},
    {GameId::AirStrike3D, "pak2.apk", "c992f0a28add309cedef544534c1a77d3cdf1415322ddffc9b3030766a6919d3"},
    {GameId::AirStrike3D, "pak3.apk", "9a8243b742997686cd49594e3bcf919bcbf2cc48e1c5337b63d8c75ef0111acf"},
    {GameId::AirStrike2, "pak0.apk", "9df601a5ebe1fec171a5089f054bdcc550268080b262043db30fb22c9f77250f"},
    {GameId::AirStrike2, "pak0.apk", "989b8324922a018c3d76a78583c419f94901393cac61409415df3623a06e1afa"},
    {GameId::AirStrike2, "pak1.apk", "858f176fd875f7c84439fb2398b1d37d1292afb708f0b63c0f2faa96efe46952"},
    {GameId::AirStrike2, "pak1.apk", "8189fbc64cf5051d9d7fba1dd1e9a1a4b8274abfc9233aaad6316f71dc539931"},
    {GameId::AirStrike2, "pak2.apk", "bc29cf7375e5557f3e9e19278e5318e77324d3f32d57f64116387712050c6175"},
    {GameId::GulfThunder, "pak0.apk", "0fde2db91134fdb2b97bed1797bc826f468fc9ad4f83198a2cb8fcbecc8d38f8"},
    {GameId::GulfThunder, "pak1.apk", "34dcddcbfb9b501f71a0a27c192431893345850843314eb2686e2b46d4f410c0"},
    {GameId::GulfThunder, "pak1.apk", "1cd68ae7e16cfef2979abf36ebde64c3467e523873b2d18ae5a76dee428265ff"},
    {GameId::GulfThunder, "pak2.apk", "4c7513ebcf969da5127e7db7953414bcfff36b3cafe628c9bba679100697f930"},
    {GameId::GulfThunder, "pak2.apk", "a4a688683374f4b6909d9c4a70eecc73546eaba7c3e8b92501fd23bf9219c941"},
    {GameId::GulfThunder, "pak4.apk", "836cf9c2d8e917ddc8cb442f2ff3e1d21b764b781bed4fe1cb95ece879552b6b"},
};
const KnownPakHash* g_pakHashes = kBuiltinPakHashes;
int g_pakHashCount = static_cast<int>(sizeof(kBuiltinPakHashes) / sizeof(kBuiltinPakHashes[0]));

constexpr size_t kMaxExeSize = 16u << 20;
constexpr int kMaxIgnoredNotes = 10; // loose files that are no game file: one note each up to this

enum class Kind { Pak, Settings, Logo, Exe };

// Where a file of the import comes from: loose under the incoming directory, or an entry of
// an opened container (zips today; the archive module of docs/spec/issues/170 section 4 is
// meant to plug in here as another container kind). `path` is the loose file, or the staged
// copy of a container entry once materialized.
struct Source {
    int container = -1; // -1: loose
    std::string path;
    ZipEntry entry;
    int archive = -1;        // an entry of an ArchiveReader (as3d/archive.h; AS3D_WITH_ARCHIVES)
    size_t archiveEntry = 0; // its index in the reader's entries()
    bool loose() const { return container < 0 && archive < 0; }
    // The candidates' group prefix: one name space per container.
    std::string prefix() const {
        if (archive >= 0) return "archive" + std::to_string(archive) + ":";
        return loose() ? "loose:" : "zip" + std::to_string(container) + ":";
    }
};

// One game file found in the import (by name: a pak, Settings.xml, the logo, an executable).
struct Candidate {
    Kind kind = Kind::Pak;
    std::string canon;   // the installed name: pakN.apk, Settings.xml, logo2s.tga (exe: its own)
    std::string group;   // its game directory (see the header)
    std::string display; // for the notes
    Source src;
    bool built = false;  // a pak0.apk made from loose files (in the staging directory)
    const GameProfile* game = nullptr; // decided
};

// A file that is not a game file by name: possibly an entry of a pak0.apk kept loose
// (docs/spec/issues/170, section 2 point 4).
struct OtherFile {
    std::string rel;     // relative to the incoming directory or inside the container
    std::string display;
    Source src;
    bool used = false;   // packed into a built pak0.apk
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
    return writeFileAtomic(path, text.data(), text.size());
}

// Moves (or, across file systems, copies) a file into place, replacing what is there.
bool moveInto(const std::string& from, const std::string& to, bool keepSource) {
    std::error_code ec;
    if (!keepSource) {
        if (replaceFile(from, to)) return true;
    }
    ec.clear();
    const std::string tmp = to + ".tmp";
    fs::copy_file(fs::path(from), fs::path(tmp), fs::copy_options::overwrite_existing, ec);
    if (ec) {
        fs::remove(fs::path(tmp), ec);
        return false;
    }
    if (!replaceFile(tmp, to)) {
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

bool profileHasPak(const GameProfile& g, const std::string& name) {
    for (const char* const* p = g.paks; *p; ++p)
        if (name == *p) return true;
    return false;
}

std::string percent(double share) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f %%", share * 100.0);
    return b;
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
        buildLoosePak0s();
        noteOtherFiles();
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

    // ---- 1: every file, loose or in a container. One entry point per file, whatever it
    // comes from, so that another container reader can feed it.
    void addFile(const Source& src, const std::string& rel, const std::string& display) {
        const std::string prefix = src.prefix();
        Candidate c;
        if (classify(baseName(rel), &c)) {
            c.group = prefix + gameRoot(dirName(rel));
            c.display = display;
            c.src = src;
            candidates_.push_back(c);
            return;
        }
        OtherFile o;
        o.rel = rel;
        o.display = display;
        o.src = src;
        others_.push_back(o);
    }

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
            Candidate probe;
#if AS3D_WITH_ARCHIVES
            if (collectArchiveFile(path, rel, probe)) continue;
#else
            if (!classify(baseName(rel), &probe)) {
                if ((lower(rel).size() > 4 && lower(rel).compare(lower(rel).size() - 4, 4, ".zip") == 0) || looksLikeZip(path)) {
                    collectZip(path, baseName(rel));
                    continue;
                }
            }
#endif
            Source src;
            src.path = path;
            addFile(src, rel, baseName(rel));
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
        const size_t before = candidates_.size();
        for (const ZipEntry& e : z->entries()) {
            if (e.isDir()) continue;
            Source src;
            src.container = index;
            src.entry = e;
            addFile(src, e.name, shown + ": " + e.name);
        }
        if (candidates_.size() == before) notes_.push_back(shown + ": no game files in this zip.");
        zips_.push_back(std::move(z));
    }

#if AS3D_WITH_ARCHIVES
    // A loose file that is an archive or an installer (docs/spec/issues/170 section 4): its
    // entries go to addFile like a zip's. A file with a game file's name is one, except an
    // executable that turns out to be an installer or a self-extractor. True when taken here.
    bool collectArchiveFile(const std::string& path, const std::string& rel, Candidate& probe) {
        const bool named = classify(baseName(rel), &probe);
        if (named && probe.kind != Kind::Exe) return false;
        const ArchiveKind k = archiveKind(path);
        if (k == ArchiveKind::Unknown || k == ArchiveKind::Pe) return false;
        std::string err;
        std::unique_ptr<ArchiveReader> r = ArchiveReader::open(path, &err);
        if (!r) {
            notes_.push_back(baseName(rel) + ": " + err + ".");
            return true;
        }
        collectArchive(std::move(r), baseName(rel), 0);
        return true;
    }

    // The entries of an opened container; an entry that is itself an archive or an installer
    // (a setup.exe in a zip, a zip in a RAR) is opened in turn, two levels deep.
    void collectArchive(std::unique_ptr<ArchiveReader> r, const std::string& shown, int depth) {
        const int index = static_cast<int>(archives_.size());
        archives_.push_back(std::move(r));
        ArchiveReader& a = *archives_.back();
        const size_t before = candidates_.size();
        for (const ArchiveEntry& e : a.entries()) {
            if (e.isDir) continue;
            const std::string n = lower(e.name);
            auto ends = [&](const char* x) {
                const size_t l = std::char_traits<char>::length(x);
                return n.size() > l && n.compare(n.size() - l, l, x) == 0;
            };
            // Game executables are below 1 MB; an executable above that is an installer or a
            // self-extractor, worth opening (the survey's are 12 to 45 MB).
            const bool container = ends(".zip") || ends(".rar") || ends(".7z") || ends(".cab") || (ends(".exe") && e.size > (1u << 20));
            if (container && depth < 2) {
                std::string err;
                std::unique_ptr<ArchiveReader> inner = a.openEntry(e, &err, staging_);
                if (inner) {
                    notes_.push_back(shown + ": " + e.name + ": " + archiveKindLabel(inner->kind()) + ", opened.");
                    collectArchive(std::move(inner), shown + ": " + e.name, depth + 1);
                } else {
                    notes_.push_back(shown + ": " + e.name + ": " + err + ".");
                }
                continue;
            }
            Source src;
            src.archive = index;
            src.archiveEntry = e.index;
            addFile(src, e.name, shown + ": " + e.name);
        }
        if (candidates_.size() == before) notes_.push_back(shown + ": no game files in this " + archiveKindLabel(a.kind()) + ".");
    }

    static std::string archiveKindLabel(ArchiveKind k) {
        switch (k) {
        case ArchiveKind::Inno: return "an Inno Setup installer";
        case ArchiveKind::Cab: return "a cabinet";
        case ArchiveKind::SevenZip: return "a 7z archive";
        case ArchiveKind::Rar: return "a RAR archive";
        default: return std::string("a ") + archiveKindName(k) + " archive";
        }
    }
#endif

    // A container entry copied out into the staging directory (once); a loose file as it is.
    bool materialize(Source& src, const std::string& name, const std::string& display) {
        if (!src.path.empty()) return true;
        const std::string dir = joinPath(staging_, std::to_string(stageCount_++));
        if (!makeDirs(dir)) return false;
        const std::string to = joinPath(dir, name);
        std::string err;
#if AS3D_WITH_ARCHIVES
        if (src.archive >= 0) {
            ArchiveReader& a = *archives_[static_cast<size_t>(src.archive)];
            if (!a.extractToFile(a.entries()[src.archiveEntry], to, &err)) {
                notes_.push_back(display + ": " + err + ".");
                return false;
            }
            src.path = to;
            return true;
        }
#endif
        if (!zips_[static_cast<size_t>(src.container)]->extractToFile(src.entry, to, &err)) {
            notes_.push_back(display + ": " + err + ".");
            return false;
        }
        src.path = to;
        return true;
    }

    bool stage(Candidate& c, const std::string& name) { return materialize(c.src, name, c.display); }

    // ---- 2: a game directory without pak0.apk whose loose files are pak0's entries (the
    // "Full RIP" and "Desert Hawk" copies): a pak0.apk is built from them.
    struct LooseGroup {
        std::string key;           // the candidates' group of the directory
        std::string shown;         // for the notes
        std::vector<size_t> files; // indexes into others_
        std::vector<LooseFile> entries;
    };

    // Splits a relative path into the game directory and the pak entry name, if its tail is a
    // pak0 entry of some game: "a/b/data/models/x.mdl" -> ("a/b", "models\x.mdl").
    bool splitLooseEntry(const std::string& rel, std::string* root, std::string* name) {
        std::vector<size_t> starts;
        starts.push_back(0);
        for (size_t i = 0; i < rel.size(); ++i)
            if (rel[i] == '/') starts.push_back(i + 1);
        for (size_t s : starts) {
            const std::string tail = normalizePakEntryName(rel.substr(s));
            if (tail.empty() || tail.size() > 64) continue;
            if (!pak0Names_.count(tail)) continue;
            *root = gameRoot(s == 0 ? std::string() : rel.substr(0, s - 1));
            *name = tail;
            return true;
        }
        return false;
    }

    void buildLoosePak0s() {
        if (others_.empty()) return;
        // Every pak0 entry name of every game whose list is known; nothing to do without.
        for (int i = 0; i < kGameCount; ++i)
            if (const std::vector<std::string>* names = pakEntryNamesOf(static_cast<GameId>(i), "pak0.apk"))
                pak0Names_.insert(names->begin(), names->end());
        if (pak0Names_.empty()) return;
        std::set<std::string> havePak0;
        for (const Candidate& c : candidates_)
            if (c.kind == Kind::Pak && c.canon == "pak0.apk") havePak0.insert(c.group);
        std::map<std::string, LooseGroup> groups;
        for (size_t i = 0; i < others_.size(); ++i) {
            const OtherFile& o = others_[i];
            std::string root, name;
            if (!splitLooseEntry(o.rel, &root, &name)) continue;
            const std::string prefix = o.src.prefix();
            LooseGroup& g = groups[prefix + root];
            if (g.key.empty()) {
                g.key = prefix + root;
                g.shown = o.src.loose() ? (root.empty() ? std::string("the chosen files") : root + "/")
                                        : o.display.substr(0, o.display.find(": ")) + (root.empty() ? "" : ": " + root + "/");
            }
            g.files.push_back(i);
            g.entries.push_back({name, std::string()});
        }
        for (auto& kv : groups) {
            LooseGroup& g = kv.second;
            if (havePak0.count(g.key)) continue; // the directory has its own pak0.apk
            double coverage = 0;
            const GameProfile* game = gameOfLooseFiles(g.entries, nullptr, &coverage);
            if (!game) {
                if (coverage >= 0.5)
                    notes_.push_back(g.shown + ": " + std::to_string(g.files.size()) + " loose files cover " + percent(coverage) +
                                     " of a pak0.apk's entries, " + percent(kLooseMinCoverage) + " are needed: no pak0.apk built.");
                continue;
            }
            // The files themselves: loose ones in place, container entries copied out.
            bool ok = true;
            for (size_t k = 0; k < g.files.size() && ok; ++k) {
                OtherFile& o = others_[g.files[k]];
                ok = materialize(o.src, baseName(o.rel), o.display);
                g.entries[k].path = o.src.path;
            }
            if (!ok) continue;
            const std::string dir = joinPath(staging_, "built" + std::to_string(stageCount_++));
            if (!makeDirs(dir)) continue;
            const std::string out = joinPath(dir, "pak0.apk");
            int count = 0;
            std::string err;
            if (!writePak0FromLooseFiles(g.entries, *game, out, &count, &err)) {
                notes_.push_back(g.shown + ": cannot build pak0.apk from its loose files (" + err + ").");
                continue;
            }
            for (size_t k : g.files) others_[k].used = true;
            Candidate c;
            c.kind = Kind::Pak;
            c.canon = "pak0.apk";
            c.group = g.key;
            c.display = "pak0.apk (built from " + std::to_string(count) + " loose files of " + g.shown + ")";
            c.src.path = out;
            c.built = true;
            candidates_.push_back(c);
            notes_.push_back(g.shown + ": no pak0.apk, but " + std::to_string(count) + " loose files are " + percent(coverage) +
                             " of " + game->title + "'s pak0.apk entries: built pak0.apk from them.");
        }
    }

    // Loose files that are not game files: a note each, up to a point (a copied game folder
    // has hundreds). Container entries are skipped silently, as before.
    void noteOtherFiles() {
        int shown = 0, more = 0;
        for (const OtherFile& o : others_) {
            if (o.used || !o.src.loose()) continue;
            if (shown < kMaxIgnoredNotes) {
                notes_.push_back(o.display + ": not a game file, ignored.");
                ++shown;
            } else {
                ++more;
            }
        }
        if (more) notes_.push_back(std::to_string(more) + " more files that are not game files ignored.");
    }

    // ---- 3: executables give the texts
    void readExecutables() {
        int unknown = 0;
        for (Candidate& c : candidates_) {
            if (c.kind != Kind::Exe) continue;
            std::vector<std::uint8_t> bytes;
            std::string err;
            bool ok;
#if AS3D_WITH_ARCHIVES
            if (c.src.archive >= 0) {
                ArchiveReader& a = *archives_[static_cast<size_t>(c.src.archive)];
                ok = a.extractToMemory(a.entries()[c.src.archiveEntry], &bytes, kMaxExeSize, &err);
            } else
#endif
            ok = c.src.loose() ? readWhole(c.src.path, &bytes, kMaxExeSize)
                               : zips_[static_cast<size_t>(c.src.container)]->extractToMemory(c.src.entry, &bytes, kMaxExeSize, &err);
            const GameProfile* g = nullptr;
            std::string text;
            if (!ok || !extractExeTexts(bytes, ok ? sha256Hex(bytes.data(), bytes.size()) : std::string(), &g, &text, &err)) {
                if (g) {
                    notes_.push_back(std::string(g->title) + ": cannot read the texts of " + c.display + " (" + err + ").");
                } else if (c.src.loose()) {
                    notes_.push_back(c.display + ": not the executable of a known game, ignored.");
                } else {
                    ++unknown;
                }
                continue;
            }
            texts_[g->id] = text;
            textsFrom_[g->id] = c.display;
        }
        if (unknown) notes_.push_back(std::to_string(unknown) + " other executable(s) in the zip ignored.");
    }

    // ---- 4: pak0.apk names the game of its directory
    void identifyPak0s() {
        for (Candidate& c : candidates_) {
            if (c.kind != Kind::Pak || c.canon != "pak0.apk") continue;
            if (groupGame_.count(c.group) && groupGame_[c.group]) {
                notes_.push_back(c.display + ": a second pak0.apk in the same place, ignored.");
                continue;
            }
            if (!stage(c, "pak0.apk")) continue;
            const PakIdentity id = identifyPakFile(c.src.path);
            const GameProfile* g = nullptr;
            if (id.game && id.pak == "pak0.apk" && !id.ambiguous && (id.exact || id.overlap >= kPak0MinOverlap)) g = id.game;
            if (!g) {
                if (!id.error.empty()) notes_.push_back(c.display + ": " + id.error + ".");
                else notes_.push_back(c.display + ": not the pak0.apk of a known game, ignored.");
                groupGame_[c.group] = nullptr;
                continue;
            }
            c.game = g;
            groupGame_[c.group] = g;
        }
    }

    // ---- 5: the other paks, by hash, by their entry names beside their pak0, or alone.
    // Two passes: a pak whose names are those of more than one game (the sequels' pak2) is
    // left for the second, when the other paks of its directory may have named the game.
    void identifyOtherPaks() {
        std::vector<size_t> later;
        for (size_t i = 0; i < candidates_.size(); ++i)
            if (identifyOtherPak(candidates_[i], false)) later.push_back(i);
        for (size_t i : later) identifyOtherPak(candidates_[i], true);
    }

    // True when the candidate is ambiguous on its own and `lastPass` is false: try it again.
    bool identifyOtherPak(Candidate& c, bool lastPass) {
        if (c.kind != Kind::Pak || c.canon == "pak0.apk" || c.game) return false;
        const GameProfile* beside = groupGame_.count(c.group) ? groupGame_[c.group] : nullptr;
        if (!beside && lastPass && groupHashGame_.count(c.group)) beside = groupHashGame_[c.group];
        // Inside a zip only the paks of an identified game directory are read (the
        // original download also holds games the engine does not run).
        if (!c.src.loose() && !beside) return false;
        if (!stage(c, c.canon)) return false;
        const std::string sha = sha256File(c.src.path);
        const GameProfile* byHash = pakGameByHash(c.canon, sha);
        if (byHash && (!beside || beside == byHash)) {
            c.game = byHash; // a known build: nothing to look at
        } else {
            const PakIdentity id = identifyPakFile(c.src.path);
            if (!id.error.empty()) {
                notes_.push_back(c.display + ": " + id.error + ".");
                return false;
            }
            const GameProfile* target = beside;
            if (!target) {
                if (id.ambiguous) {
                    if (!lastPass) return true;
                    notes_.push_back(c.display + ": the " + c.canon + " of more than one game (import it with the game's pak0.apk), ignored.");
                    return false;
                }
                if (id.game && id.pak == c.canon && (id.exact || id.overlap >= kPakMinOverlap)) target = id.game;
            }
            if (!target) {
                notes_.push_back(c.display + ": not a pak of a known game, ignored.");
                return false;
            }
            if (!profileHasPak(*target, c.canon)) {
                notes_.push_back(c.display + ": " + target->title + " has no " + c.canon + ", ignored.");
                return false;
            }
            if (pakHasReference(*target, c.canon)) {
                double overlap = 0;
                if (!pakMatches(id, *target, c.canon, kPakMinOverlap, &overlap)) {
                    notes_.push_back(c.display + ": not " + target->title + "'s " + c.canon + " (different content), ignored.");
                    return false;
                }
            } else {
                // No reference pak of that name (AirStrike 3D's pak3.apk, the override pak
                // of one patch): every entry must be a name of the game.
                const double share = shareOfGameNames(id.names, *target);
                if (share >= 0 && share < 1.0) {
                    const int foreign = static_cast<int>((1.0 - share) * static_cast<double>(id.names.size()) + 0.5);
                    notes_.push_back(c.display + ": not an override pak of " + target->title + " (" + std::to_string(foreign) +
                                     " of its " + std::to_string(id.names.size()) + " entries are not the game's), ignored.");
                    return false;
                }
            }
            c.game = target;
        }
        if (!profileHasPak(*c.game, c.canon)) {
            notes_.push_back(c.display + ": " + c.game->title + " has no " + c.canon + ", ignored.");
            c.game = nullptr;
            return false;
        }
        // A directory without pak0.apk takes the game of its paks.
        if (!beside) groupHashGame_[c.group] = c.game;
        return false;
    }

    // ---- 6: Settings.xml and the logo
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
            if (!g && c.src.loose() && only) g = only;
            if (!g) {
                if (c.src.loose())
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

    // ---- 7: into <gamesDir>/<key>/
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
                if (!moveInto(c->src.path, joinPath(dir, c->canon), false)) {
                    notes_.push_back(std::string(g.title) + ": cannot write " + c->canon + ".");
                    continue;
                }
                got.push_back(c->built ? c->canon + " (built)" : c->canon);
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
                if (!pakIsOptional(g, *p) && !isFile(joinPath(dir, *p))) missing.push_back(*p);
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
#if AS3D_WITH_ARCHIVES
    std::vector<std::unique_ptr<ArchiveReader>> archives_;
#endif
    std::vector<Candidate> candidates_;
    std::vector<OtherFile> others_;
    std::set<std::string> pak0Names_;
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
        if (!pakIsOptional(game, *p) && !isFile(joinPath(dir, *p))) return false;
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
