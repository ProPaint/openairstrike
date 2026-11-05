// as3d/archive.h: the fixtures of testdata/archives/ (synthetic files only, made by
// tools/make_archive_fixtures.py, tools/make_archive_fixtures_docker.sh and
// tools/make_inno_fixture.py; docs/spec/archive.md): each container opens, lists and extracts
// the same fake game directory; the detector; nested containers; truncated and corrupt files
// refused cleanly. Built only with AS3D_WITH_ARCHIVES (not on the 3DS, which has no tests).
#include <doctest.h>

#if AS3D_WITH_ARCHIVES

#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "as3d/archive.h"

using namespace as3d;
namespace fs = std::filesystem;

namespace {

std::string fixture(const char* name) { return std::string(AS3D_REPO_ROOT) + "/testdata/archives/" + name; }

std::string readFile(const std::string& p) {
    std::string s;
    if (std::FILE* f = std::fopen(p.c_str(), "rb")) {
        char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
        std::fclose(f);
    }
    return s;
}

void writeFile(const std::string& p, const std::string& bytes) {
    if (std::FILE* f = std::fopen(p.c_str(), "wb")) {
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
               ("as3d_archive_" + std::to_string(::getpid()) + "_" + std::to_string(++counter) + "_" + std::to_string(rd()));
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string file(const char* name) const { return (path / name).string(); }
};

const ArchiveEntry* findEntry(const ArchiveReader& r, const std::string& suffix) {
    for (const ArchiveEntry& e : r.entries())
        if (!e.isDir && e.name.size() >= suffix.size() && e.name.compare(e.name.size() - suffix.size(), suffix.size(), suffix) == 0)
            return &e;
    return nullptr;
}

std::string extract(ArchiveReader& r, const std::string& suffix) {
    const ArchiveEntry* e = findEntry(r, suffix);
    REQUIRE_MESSAGE(e, r.name() << " has no entry ending in " << suffix);
    std::vector<std::uint8_t> bytes;
    std::string err;
    const bool ok = r.extractToMemory(*e, &bytes, 1u << 20, &err);
    REQUIRE_MESSAGE(ok, r.name() << ": " << suffix << ": " << err);
    return std::string(bytes.begin(), bytes.end());
}

// The fake game of every fixture (tools/make_archive_fixtures.py).
void checkGame(ArchiveReader& r, const std::string& exe) {
    CHECK(extract(r, "data/pak0.apk") == std::string(70000, 'A'));
    CHECK(extract(r, "data/pak1.apk") == std::string(3000, 'B'));
    CHECK(extract(r, "data/pak2.apk") == std::string(3000, 'C'));
    CHECK(extract(r, "data/Settings.xml").find("<settings>") != std::string::npos);
    CHECK(extract(r, "readme.txt").find("No game data") != std::string::npos);
    CHECK(extract(r, "AirStrike3D.exe") == exe);
    // Out of order: an entry before the last one read reopens the archive.
    CHECK(extract(r, "data/pak1.apk") == std::string(3000, 'B'));
    for (const ArchiveEntry& e : r.entries()) {
        CHECK(e.name.find('\\') == std::string::npos);
        CHECK(e.name.compare(0, 1, "/") != 0);
    }
}

// The synthetic exe as the plain zip holds it (zip is read by the long-tested path as well).
std::string referenceExe() {
    std::string err;
    std::unique_ptr<ArchiveReader> r = ArchiveReader::open(fixture("game.zip"), &err);
    REQUIRE_MESSAGE(r, err);
    const std::string exe = extract(*r, "AirStrike3D.exe");
    REQUIRE(exe.size() > 1000);
    REQUIRE(exe.compare(0, 2, "MZ") == 0);
    return exe;
}

} // namespace

TEST_CASE("archive: every fixture opens, lists and extracts the fake game") {
    const std::string exe = referenceExe();
    const struct {
        const char* file;
        ArchiveKind kind;
        const char* prefix; // where the game directory is inside
    } cases[] = {
        {"game.zip", ArchiveKind::Zip, "game/AirStrike 3D/"},
        {"game.7z", ArchiveKind::SevenZip, "game/AirStrike 3D/"},
        {"game.cab", ArchiveKind::Cab, "game/AirStrike 3D/"},
        {"game_rar4.rar", ArchiveKind::Rar, "game/AirStrike 3D/"},
        {"game_rar5.rar", ArchiveKind::Rar, "game/AirStrike 3D/"},
        {"game.tar.gz", ArchiveKind::Tar, "game/AirStrike 3D/"},
        {"cab_sfx.pe", ArchiveKind::Cab, "game/AirStrike 3D/"},
        {"inno5_setup.bin", ArchiveKind::Inno, "{app}/"},
        {"inno6_setup.bin", ArchiveKind::Inno, "{app}/"},
    };
    for (const auto& c : cases) {
        CAPTURE(c.file);
        std::string err;
        std::unique_ptr<ArchiveReader> r = ArchiveReader::open(fixture(c.file), &err);
        REQUIRE_MESSAGE(r, c.file << ": " << err);
        CHECK(r->kind() == c.kind);
        CHECK(r->name() == c.file);
        CHECK(findEntry(*r, std::string(c.prefix) + "data/pak0.apk"));
        const ArchiveEntry* pak0 = findEntry(*r, "data/pak0.apk");
        REQUIRE(pak0);
        CHECK(pak0->size == 70000);
        checkGame(*r, exe);
        // To a file, and the size cap of extractToMemory.
        TempDir tmp;
        std::string ferr;
        REQUIRE_MESSAGE(r->extractToFile(*pak0, tmp.file("pak0.apk"), &ferr), ferr);
        CHECK(readFile(tmp.file("pak0.apk")) == std::string(70000, 'A'));
        std::vector<std::uint8_t> small;
        CHECK_FALSE(r->extractToMemory(*pak0, &small, 1000, &ferr));
        CHECK(small.empty());
    }
}

TEST_CASE("archive: the detector") {
    const struct {
        const char* file;
        ArchiveKind kind;
        std::uint64_t offset;
    } cases[] = {
        {"game.zip", ArchiveKind::Zip, 0},         {"nested.zip", ArchiveKind::Zip, 0},
        {"game.7z", ArchiveKind::SevenZip, 0},     {"game.cab", ArchiveKind::Cab, 0},
        {"game_rar4.rar", ArchiveKind::Rar, 0},    {"game_rar5.rar", ArchiveKind::Rar, 0},
        {"game.tar.gz", ArchiveKind::Tar, 0},      {"cab_sfx.pe", ArchiveKind::Cab, 1024},
        {"inno5_setup.bin", ArchiveKind::Inno, 0}, {"inno6_setup.bin", ArchiveKind::Inno, 0},
        {"nsis_dummy.pe", ArchiveKind::Nsis, 0},   {"plain.pe", ArchiveKind::Pe, 0},
    };
    for (const auto& c : cases) {
        CAPTURE(c.file);
        std::uint64_t offset = 99;
        CHECK(archiveKind(fixture(c.file), &offset) == c.kind);
        CHECK(offset == c.offset);
        // From bytes in memory, whole.
        const std::string bytes = readFile(fixture(c.file));
        CHECK(archiveKind(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), true) == c.kind);
    }
    CHECK(std::string(archiveKindName(ArchiveKind::Nsis)) == "nsis");
    CHECK(std::string(archiveKindName(ArchiveKind::SevenZip)) == "7z");
    const std::uint8_t text[] = "just some text, not an archive";
    CHECK(archiveKind(text, sizeof text, true) == ArchiveKind::Unknown);
    CHECK(archiveKind(fixture("no_such_file.zip")) == ArchiveKind::Unknown);
}

TEST_CASE("archive: installers and programs the module cannot open are refused with a note") {
    std::string err;
    CHECK_FALSE(ArchiveReader::open(fixture("nsis_dummy.pe"), &err));
    CHECK(err.find("NSIS installer") != std::string::npos);
    CHECK(err.find("install it on a PC") != std::string::npos);
    CHECK_FALSE(ArchiveReader::open(fixture("plain.pe"), &err));
    CHECK(err.find("Windows program") != std::string::npos);
    CHECK_FALSE(ArchiveReader::open(fixture("no_such_file.zip"), &err));
    CHECK_FALSE(err.empty());
    TempDir tmp;
    writeFile(tmp.file("text.bin"), "just some text, not an archive");
    CHECK_FALSE(ArchiveReader::open(tmp.file("text.bin"), &err));
    CHECK(err.find("not an archive") != std::string::npos);
}

TEST_CASE("archive: a container inside a container, in memory and through a temporary file") {
    const std::string exe = referenceExe();
    std::string err;
    std::unique_ptr<ArchiveReader> outer = ArchiveReader::open(fixture("nested.zip"), &err);
    REQUIRE_MESSAGE(outer, err);
    const ArchiveEntry* inner = findEntry(*outer, "downloads/game.zip");
    REQUIRE(inner);
    CHECK(outer->entryKind(*inner) == ArchiveKind::Zip);
    const ArchiveEntry* notes = findEntry(*outer, "downloads/notes.txt");
    REQUIRE(notes);
    CHECK(outer->entryKind(*notes) == ArchiveKind::Unknown);

    std::unique_ptr<ArchiveReader> mem = outer->openEntry(*inner, &err);
    REQUIRE_MESSAGE(mem, err);
    CHECK(mem->tempFile().empty());
    CHECK(mem->name() == "nested.zip: downloads/game.zip");
    checkGame(*mem, exe);

    TempDir tmp;
    std::string tempPath;
    {
        std::unique_ptr<ArchiveReader> onDisk = outer->openEntry(*inner, &err, tmp.path.string(), 0);
        REQUIRE_MESSAGE(onDisk, err);
        tempPath = onDisk->tempFile();
        CHECK_FALSE(tempPath.empty());
        CHECK(fs::exists(tempPath));
        checkGame(*onDisk, exe);
    }
    CHECK_FALSE(fs::exists(tempPath)); // removed with its reader

    // A nested entry that is not a container.
    CHECK_FALSE(outer->openEntry(*notes, &err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("archive: truncated and corrupt archives are refused cleanly") {
    std::string err;
    CHECK_FALSE(ArchiveReader::open(fixture("truncated.zip"), &err));
    CHECK_FALSE(err.empty());

    TempDir tmp;
    for (const char* name : {"game.7z", "game_rar5.rar", "game_rar4.rar", "game.cab", "inno5_setup.bin", "inno6_setup.bin", "game.tar.gz"}) {
        CAPTURE(name);
        const std::string bytes = readFile(fixture(name));
        REQUIRE(bytes.size() > 400);
        // Cut at several places: either the open fails, or extracting the fake pak0 does.
        for (size_t cut : {bytes.size() / 4, bytes.size() / 2, bytes.size() - 40}) {
            CAPTURE(cut);
            const std::string path = tmp.file("cut.bin");
            writeFile(path, bytes.substr(0, cut));
            std::string e1;
            std::unique_ptr<ArchiveReader> r = ArchiveReader::open(path, &e1);
            if (!r) {
                CHECK_FALSE(e1.empty());
                continue;
            }
            bool allOk = true;
            for (const ArchiveEntry& e : r->entries()) {
                if (e.isDir) continue;
                std::vector<std::uint8_t> out;
                std::string e2;
                if (!r->extractToMemory(e, &out, 1u << 20, &e2)) {
                    CHECK_FALSE(e2.empty());
                    allOk = false;
                }
            }
            CHECK_FALSE(allOk);
        }
    }

    // A flipped byte in an Inno Setup installer's file data: its checksum (or the decoder) fails.
    for (const char* name : {"inno5_setup.bin", "inno6_setup.bin"}) {
        CAPTURE(name);
        std::string bytes = readFile(fixture(name));
        bytes[bytes.size() - 60] = static_cast<char>(bytes[bytes.size() - 60] ^ 0x55);
        const std::string path = tmp.file("flipped.bin");
        writeFile(path, bytes);
        std::unique_ptr<ArchiveReader> r = ArchiveReader::open(path, &err);
        REQUIRE_MESSAGE(r, err); // the header data is intact
        bool anyFailed = false;
        for (const ArchiveEntry& e : r->entries()) {
            std::vector<std::uint8_t> out;
            std::string e2;
            if (!r->extractToMemory(e, &out, 1u << 20, &e2)) anyFailed = true;
        }
        CHECK(anyFailed);
    }
}

#endif // AS3D_WITH_ARCHIVES
