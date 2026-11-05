// as3d/pak_file.h: the file table of a pak without its bodies, and the pak writer, round-tripped
// through the pak reader (makePakSource). Synthetic paks only; the owner's pak0.apk when it is
// on this machine.
#include <doctest.h>

#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "as3d/pak_file.h"
#include "as3d/vfs.h"
#include "test_data.h"

using namespace as3d;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(const char* tag) {
        std::error_code ec;
        static int counter = 0;
        std::random_device rd;
        path = fs::temp_directory_path(ec) / (std::string("as3d_pak_file_") + tag + "_" + std::to_string(::getpid()) + "_" +
                                              std::to_string(++counter) + "_" + std::to_string(rd()));
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void writeBytes(const fs::path& p, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (std::FILE* f = std::fopen(p.string().c_str(), "wb")) {
        std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
    }
}

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

std::string bytesOf(const Blob& b) { return std::string(b.begin(), b.end()); }

} // namespace

TEST_CASE("pak writer: entries from files and memory, read back through makePakSource and readPakTable") {
    TempDir tmp("write");
    const std::string big(200000, 'q'); // several read buffers long
    writeBytes(tmp.path / "src/one.bin", "first body");
    writeBytes(tmp.path / "src/big.bin", big);
    std::vector<PakWriteEntry> entries(4);
    entries[0].name = "models\\one.bin";
    entries[0].sourcePath = (tmp.path / "src/one.bin").string();
    entries[1].name = "Textures/Big.bin"; // stored as given; the reader normalizes lookups
    entries[1].sourcePath = (tmp.path / "src/big.bin").string();
    entries[2].name = "maps\\empty.txt"; // an empty body is fine
    entries[3].name = "sixtyfour.........................................................";
    entries[3].name.resize(64, 'x'); // the longest name: no terminator in the 64-byte field
    entries[3].data = Blob{'a', 'b', 'c'};
    std::string err;
    const std::string out = (tmp.path / "out.apk").string();
    REQUIRE(writePak(out, entries, &err));
    CHECK_FALSE(fs::exists(tmp.path / "out.apk.tmp"));

    PakTable table;
    REQUIRE(readPakTableFile(out, &table, &err));
    REQUIRE(table.entries.size() == 4);
    CHECK(table.bytesShort == 0);
    CHECK(table.fileSize == fs::file_size(tmp.path / "out.apk"));
    CHECK(table.entries[0].name == "models\\one.bin");
    CHECK(table.entries[0].offset == 0x410);
    CHECK(table.entries[0].size == 10);
    CHECK(table.entries[0].flag == 0);
    CHECK(table.entries[1].size == big.size());
    CHECK(table.entries[2].size == 0);
    CHECK(table.entries[3].name.size() == 64);
    CHECK(table.entries[3].size == 3);
    // Bodies are plain (zero key, flag 0): the first body is right after the header.
    CHECK(readBytes(tmp.path / "out.apk").substr(0x410, 10) == "first body");

    const std::vector<std::string> names = pakEntryNames(table);
    REQUIRE(names.size() == 4);
    CHECK(names[0] == "maps\\empty.txt");
    CHECK(names[1] == "models\\one.bin");
    CHECK(names[2] == std::string(table.entries[3].name));
    CHECK(names[3] == "textures\\big.bin");

    std::unique_ptr<IFileSource> src = makePakSource(openFileStream(out));
    REQUIRE(src);
    Blob b;
    REQUIRE(src->read("models\\one.bin", b));
    CHECK(bytesOf(b) == "first body");
    REQUIRE(src->read("textures\\big.bin", b));
    CHECK(bytesOf(b) == big);
    REQUIRE(src->read("maps\\empty.txt", b));
    CHECK(b.empty());
    REQUIRE(src->read(table.entries[3].name, b));
    CHECK(bytesOf(b) == "abc");

    // Bad entries: an empty name, a name over 64 bytes, a missing source file: nothing written.
    std::vector<PakWriteEntry> bad(1);
    CHECK_FALSE(writePak((tmp.path / "bad.apk").string(), bad, &err));
    CHECK(err.find("1 to 64") != std::string::npos);
    bad[0].name = std::string(65, 'n');
    CHECK_FALSE(writePak((tmp.path / "bad.apk").string(), bad, &err));
    bad[0].name = "x";
    bad[0].sourcePath = (tmp.path / "nothing").string();
    CHECK_FALSE(writePak((tmp.path / "bad.apk").string(), bad, &err));
    CHECK(err.find("cannot read") != std::string::npos);
    CHECK_FALSE(fs::exists(tmp.path / "bad.apk"));
    CHECK_FALSE(fs::exists(tmp.path / "bad.apk.tmp"));
    // No entries at all is a valid, empty pak.
    REQUIRE(writePak((tmp.path / "empty.apk").string(), {}, &err));
    REQUIRE(readPakTableFile((tmp.path / "empty.apk").string(), &table, &err));
    CHECK(table.entries.empty());
}

TEST_CASE("readPakTable: a truncated pak says how many bytes are missing; not a pak") {
    TempDir tmp("trunc");
    std::vector<PakWriteEntry> entries(2);
    entries[0].name = "a.txt";
    entries[0].data = Blob(1000, 'a');
    entries[1].name = "b.txt";
    entries[1].data = Blob(1000, 'b');
    std::string err;
    const std::string out = (tmp.path / "full.apk").string();
    REQUIRE(writePak(out, entries, &err));
    const std::string whole = readBytes(tmp.path / "full.apk");
    // The table cut short: both entries' bodies are there but the table ends past the file.
    writeBytes(tmp.path / "table.apk", whole.substr(0, whole.size() - 40));
    PakTable t;
    REQUIRE(readPakTableFile((tmp.path / "table.apk").string(), &t, &err));
    CHECK(t.bytesShort == 40);
    CHECK(t.entries.size() == 1); // the entries within the file are still listed
    // A body past the end: the table offset points past the file (what a split CAB leaves).
    std::string moved = whole;
    const std::uint32_t off = 0x410 + 2000 + 500;
    for (int i = 0; i < 4; ++i) moved[8 + i] = static_cast<char>((off >> (8 * i)) & 0xFF);
    writeBytes(tmp.path / "body.apk", moved);
    REQUIRE(readPakTableFile((tmp.path / "body.apk").string(), &t, &err));
    CHECK(t.bytesShort == 500); // the table now starts 500 bytes past the old one, so it ends 500 bytes past the file
    CHECK(makePakSource(openFileStream((tmp.path / "body.apk").string())) == nullptr);

    writeBytes(tmp.path / "no.apk", "this is not a pak archive at all, but a long enough file to read a header from, 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 ");
    CHECK_FALSE(readPakTableFile((tmp.path / "no.apk").string(), &t, &err));
    CHECK(err.find("bad header") != std::string::npos);
    writeBytes(tmp.path / "short.apk", "tiny");
    CHECK_FALSE(readPakTableFile((tmp.path / "short.apk").string(), &t, &err));
    CHECK(err.find("too short") != std::string::npos);
    CHECK_FALSE(readPakTableFile((tmp.path / "missing.apk").string(), &t, &err));
}

TEST_CASE("normalizePakEntryName: ASCII lower case, slashes to backslashes, nothing else") {
    CHECK(normalizePakEntryName("Models/Tanks\\T80.MDL") == "models\\tanks\\t80.mdl");
    CHECK(normalizePakEntryName("maps\\level1.hsc") == "maps\\level1.hsc");
    CHECK(normalizePakEntryName("") == "");
    const std::string cyr = "\xC0\xE0"; // cp1251 bytes are left alone
    CHECK(normalizePakEntryName(cyr) == cyr);
}

TEST_CASE("readPakTable (real data): the owner's pak0.apk has the profile's entry count") {
    AS3D_REQUIRE_DATA();
    const std::string pak0 = testdata::gameData().dataDir + "/pak0.apk";
    std::error_code ec;
    if (!fs::exists(pak0, ec)) {
        std::fprintf(stderr, "SKIPPED (no %s): %s\n", pak0.c_str(), __FILE__);
        return;
    }
    PakTable t;
    std::string err;
    REQUIRE(readPakTableFile(pak0, &t, &err));
    CHECK(t.bytesShort == 0);
    const std::vector<std::string> names = pakEntryNames(t);
    CHECK(names.size() == t.entries.size());
    const std::string key = testdata::gameKey();
    const size_t expected = key == "as3d" ? 1201 : key == "as2" ? 1609 : 1142;
    CHECK(names.size() == expected);
}
