// as3d::importGameFiles over the archive module (docs/spec/issues/170 section 4): a synthetic
// game imported from every container kind of testdata/archives/ (zip, a zip in a zip, 7z, CAB,
// RAR 4 and 5, tar.gz, a PE with a CAB appended, Inno Setup 5 and 6 layouts), and the notes
// for installers it cannot open. The fixtures' fake game (tools/make_archive_fixtures.py) is
// recognised through the test hooks: pak0.apk (70000 'A') by its head signature, pak1.apk and
// pak2.apk (3000 'B', 3000 'C') by their SHA-256. No game data.
#include <doctest.h>

#if AS3D_WITH_ARCHIVES

#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "as3d/game_data.h"
#include "as3d/game_import.h"
#include "as3d/sha256.h"

using namespace as3d;
namespace fs = std::filesystem;

namespace {

std::string fixture(const char* name) { return std::string(AS3D_REPO_ROOT) + "/testdata/archives/" + name; }

std::string readBytes(const fs::path& p) {
    std::string s;
    if (std::FILE* f = std::fopen(p.string().c_str(), "rb")) {
        char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
        std::fclose(f);
    }
    return s;
}

void writeBytes(const fs::path& p, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (std::FILE* f = std::fopen(p.string().c_str(), "wb")) {
        std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
    }
}

struct TempDir {
    fs::path path;
    TempDir() {
        std::error_code ec;
        static int counter = 0;
        std::random_device rd;
        path = fs::temp_directory_path(ec) /
               ("as3d_import_ar_" + std::to_string(::getpid()) + "_" + std::to_string(++counter) + "_" + std::to_string(rd()));
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

bool hasNote(const ImportResult& r, const std::string& part) {
    for (const std::string& n : r.notes)
        if (n.find(part) != std::string::npos) return true;
    return false;
}

std::string allNotes(const ImportResult& r) {
    std::string s;
    for (const std::string& n : r.notes) s += n + "\n";
    return s;
}

// The fixtures' fake AirStrike 3D, recognised through the hooks; restores the built-in tables.
struct FixtureGame {
    PakSignature sig[1];
    std::string sha1 = sha256Hex(std::string(3000, 'B').data(), 3000), sha2 = sha256Hex(std::string(3000, 'C').data(), 3000);
    KnownPakHash hashes[2];
    explicit FixtureGame(const fs::path& scratch) {
        const fs::path p = scratch / "sig" / "pak0.apk";
        writeBytes(p, std::string(70000, 'A'));
        std::uint64_t size = 0, hash = 0;
        REQUIRE(readPakSignature(p.string(), &size, &hash));
        sig[0] = {GameId::AirStrike3D, size, hash};
        setPakSignaturesForTest(sig, 1);
        hashes[0] = {GameId::AirStrike3D, "pak1.apk", sha1.c_str()};
        hashes[1] = {GameId::AirStrike3D, "pak2.apk", sha2.c_str()};
        setKnownPakHashesForTest(hashes, 2);
    }
    ~FixtureGame() {
        setPakSignaturesForTest(nullptr, 0);
        setKnownPakHashesForTest(nullptr, 0);
    }
};

ImportResult importFixture(const TempDir& tmp, const char* name, const std::string& as) {
    const fs::path incoming = tmp.path / "incoming";
    std::error_code ec;
    fs::remove_all(incoming, ec);
    fs::create_directories(incoming, ec);
    fs::copy_file(fixture(name), incoming / as, ec);
    REQUIRE_FALSE(ec);
    return importGameFiles(incoming.string(), (tmp.path / "games").string());
}

} // namespace

TEST_CASE("import: the fake game from every container kind") {
    const struct {
        const char* file;
        const char* as; // the name the player's download would have
        const char* opened; // a note naming the nested container, or ""
    } cases[] = {
        {"game.zip", "game.zip", ""},
        {"nested.zip", "download.zip", "a zip archive, opened"},
        {"game.7z", "game.7z", ""},
        {"game.cab", "game.cab", ""},
        {"game_rar4.rar", "game_rar4.rar", ""},
        {"game_rar5.rar", "AirStrike 3D.rar", ""},
        {"game.tar.gz", "game.tar.gz", ""},
        {"cab_sfx.pe", "Latest Patch.exe", ""},
        {"inno5_setup.bin", "setup.exe", ""},
        {"inno6_setup.bin", "Desert-Hawk.exe", ""},
    };
    for (const auto& c : cases) {
        CAPTURE(c.file);
        TempDir tmp;
        FixtureGame fake(tmp.path);
        const ImportResult r = importFixture(tmp, c.file, c.as);
        INFO(allNotes(r));
        REQUIRE(r.imported == std::vector<std::string>{"as3d"});
        const fs::path dir = tmp.path / "games" / "as3d";
        CHECK(readBytes(dir / "pak0.apk") == std::string(70000, 'A'));
        CHECK(readBytes(dir / "pak1.apk") == std::string(3000, 'B'));
        CHECK(readBytes(dir / "pak2.apk") == std::string(3000, 'C'));
        CHECK(readBytes(dir / "Settings.xml").find("<settings>") != std::string::npos);
        CHECK(fs::exists(dir / "logo2s.tga"));
        CHECK(hasNote(r, "AirStrike 3D: ready to play."));
        if (*c.opened) CHECK(hasNote(r, c.opened));
        CHECK_FALSE(fs::exists(tmp.path / "games" / ".import-staging"));
    }
}

TEST_CASE("import: installers and programs the archive module cannot open become notes") {
    TempDir tmp;
    FixtureGame fake(tmp.path);
    ImportResult r = importFixture(tmp, "nsis_dummy.pe", "Setup.exe");
    CHECK(r.imported.empty());
    CHECK(hasNote(r, "Setup.exe: an NSIS installer, which this app cannot open; install it on a PC and pick the game's data folder."));

    r = importFixture(tmp, "truncated.zip", "game.zip");
    CHECK(r.imported.empty());
    CHECK(hasNote(r, "game.zip: "));

    // A small plain executable stays an executable (not a known game's: ignored as before).
    r = importFixture(tmp, "plain.pe", "AirStrike3D.exe");
    CHECK(r.imported.empty());
    CHECK(hasNote(r, "AirStrike3D.exe: not the executable of a known game"));
}

#endif // AS3D_WITH_ARCHIVES
