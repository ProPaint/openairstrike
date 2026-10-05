// as3d/game_import.h and its parts (as3d/sha256.h, as3d/zip_reader.h, as3d/exe_texts.h):
// importing the player's own game files into one directory per game, as the data-free Android
// build does on first start (docs/android.md). Synthetic files throughout; the last cases run
// against the real data when this machine has it (gitignored) and are skipped otherwise.
#include <doctest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "as3d/exe_texts.h"
#include "as3d/game_data.h"
#include "as3d/game_import.h"
#include "as3d/sha256.h"
#include "as3d/zip_reader.h"
#include "test_data.h"

using namespace as3d;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(const char* tag) {
        std::error_code ec;
        path = fs::temp_directory_path(ec) / (std::string("as3d_import_") + tag + "_" + std::to_string(std::rand()));
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string str() const { return path.string(); }
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

bool there(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

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

// Puts the built-in tables back at the end of a test.
struct TableGuard {
    ~TableGuard() {
        setPakSignaturesForTest(nullptr, 0);
        setKnownPakHashesForTest(nullptr, 0);
    }
};

// Synthetic pak0 contents with signatures for the three games, and no known pak hashes.
struct FakeGames {
    std::string pak0[3];
    PakSignature sigs[3];
    explicit FakeGames(const fs::path& scratch) {
        for (int i = 0; i < 3; ++i) {
            pak0[i] = std::string(70000 + i, static_cast<char>('A' + i));
            const fs::path p = scratch / ("sig" + std::to_string(i)) / "pak0.apk";
            writeBytes(p, pak0[i]);
            std::uint64_t size = 0, hash = 0;
            readPakSignature(p.string(), &size, &hash);
            sigs[i] = {static_cast<GameId>(i), size, hash};
        }
        setPakSignaturesForTest(sigs, 3);
        static const KnownPakHash none[1] = {{GameId::AirStrike3D, "", ""}};
        setKnownPakHashesForTest(none, 0);
    }
};

// ---- a zip writer for the tests (stored, or deflated from a precomputed stream)
void put16(std::string& s, unsigned v) {
    s += static_cast<char>(v & 0xFF);
    s += static_cast<char>((v >> 8) & 0xFF);
}
void put32(std::string& s, std::uint32_t v) {
    put16(s, v & 0xFFFF);
    put16(s, v >> 16);
}
struct ZipItem {
    std::string name, data, deflated; // deflated empty: stored
};
std::string buildZip(const std::vector<ZipItem>& items, bool corruptCrc = false) {
    std::string out, cd;
    for (const ZipItem& it : items) {
        const bool defl = !it.deflated.empty();
        const std::string& body = defl ? it.deflated : it.data;
        std::uint32_t crc = crc32Update(0, reinterpret_cast<const std::uint8_t*>(it.data.data()), it.data.size());
        if (corruptCrc) crc ^= 1;
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        put32(out, 0x04034b50);
        put16(out, 20);
        put16(out, 0);
        put16(out, defl ? 8 : 0);
        put32(out, 0);
        put32(out, crc);
        put32(out, static_cast<std::uint32_t>(body.size()));
        put32(out, static_cast<std::uint32_t>(it.data.size()));
        put16(out, static_cast<unsigned>(it.name.size()));
        put16(out, 0);
        out += it.name + body;
        put32(cd, 0x02014b50);
        put16(cd, 20);
        put16(cd, 20);
        put16(cd, 0);
        put16(cd, defl ? 8 : 0);
        put32(cd, 0);
        put32(cd, crc);
        put32(cd, static_cast<std::uint32_t>(body.size()));
        put32(cd, static_cast<std::uint32_t>(it.data.size()));
        put16(cd, static_cast<unsigned>(it.name.size()));
        put16(cd, 0);
        put16(cd, 0);
        put16(cd, 0);
        put16(cd, 0);
        put32(cd, 0);
        put32(cd, offset);
        cd += it.name;
    }
    const std::uint32_t cdOffset = static_cast<std::uint32_t>(out.size());
    out += cd;
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast<unsigned>(items.size()));
    put16(out, static_cast<unsigned>(items.size()));
    put32(out, static_cast<std::uint32_t>(cd.size()));
    put32(out, cdOffset);
    put16(out, 0);
    return out;
}

// Generated by Python's zlib (raw deflate, wbits -15) from the inputs described in the test.
const char kFixed[] =
    "\xcb\x48\xcd\xc9\xc9\x57\xc8\x40\x27\x75\x14\x52\x52\xd3\x72\x12\x4b\x52\x01";
const size_t kFixedSize = 19;
const char* const kFixedSha = "e084bac33041018228c46c55cc01df653f718800afd7533270bb63bb778a5967"; // of the 32 inflated bytes

const char kDynamic[] =
    "\x7d\xd9\x31\x72\x14\x41\x10\x44\x51\x9f\x53\xe8\x08\x93\xd9\xd3\xdd\xd3\x07\x12\x01\x11\x0a\x30\x58\x83\xe3\x03"
    "\x3e\x4f\x8e\x8c\xf2\x7e\x49\xda\x7d\x53\xf3\xf1\xfd\xc7\xfb\xdb\xf5\xf6\xf3\xeb\xdb\xeb\xdb\xfb\xdb\xeb\xfd\xd7"
    "\xeb\xef\x8f\xdf\xaf\x2f\x1f\xff\xe6\xc1\xbc\x98\x0f\xcc\x6f\xcc\x27\xe6\x0b\xf3\x8d\xf9\x83\xf9\x51\x17\x83\x55"
    "\x1c\x25\x47\xcd\x51\x74\x54\x1d\x65\x47\xdd\x51\x78\x54\x5e\x95\x97\xbf\x6b\x95\x57\xe5\x55\x79\x55\x5e\x95\x57"
    "\xe5\x55\x79\x55\x3e\x54\x3e\x54\x3e\xf8\x67\xae\xf2\xa1\xf2\xa1\xf2\xa1\xf2\xa1\xf2\xa1\xf2\xa1\xf2\x5b\xe5\xb7"
    "\xca\x6f\x95\xdf\xfc\x0f\x57\xf9\xad\xf2\x5b\xe5\xb7\xca\x6f\x95\xdf\x2a\x9f\x2a\x9f\x2a\x9f\x2a\x9f\x2a\x9f\xfc"
    "\x70\x53\xf9\x54\xf9\x54\xf9\x54\xf9\x54\xf9\x52\xf9\x52\xf9\x52\xf9\x52\xf9\x52\xf9\xe2\xe7\xba\xca\x97\xca\x97"
    "\xca\x97\xca\xb7\xca\xb7\xca\xb7\xca\xb7\xca\xb7\xca\xb7\xca\x37\xbf\xd2\x54\xbe\x55\xbe\x55\xfe\xa8\xfc\x51\xf9"
    "\xa3\xf2\x47\xe5\x8f\xca\x1f\x95\x3f\x2a\x7f\xf8\x6d\xae\xf2\x47\xe5\x47\xe5\x47\xe5\x47\xe5\x47\xe5\x47\xe5\x47"
    "\xe5\x47\xe5\x47\xe5\x87\x90\xb1\x64\x48\x99\x8b\x96\xb9\x88\x99\x8b\x9a\xb9\xc8\x99\x8b\x9e\xb9\x08\x9a\x8b\xa2"
    "\xb9\x48\x9a\x8b\x3b\xf8\x84\x73\xdc\x81\x41\x67\xd1\x99\x74\x36\x9d\x51\x67\xd5\x99\x75\x74\x5d\x08\xbb\xd4\xa6"
    "\xe5\x0e\x68\xbb\x10\x77\xa1\xee\x42\xde\x85\xbe\x0b\x81\x17\x0a\x2f\x24\x5e\x68\xbc\x0c\xc3\x9e\x3b\x20\xf3\x42"
    "\xe7\x85\xd0\x0b\xa5\x17\x52\x2f\xb4\x5e\x88\xbd\x50\x7b\x21\xf7\x72\xfb\xe9\x86\x3b\xa0\xf8\x42\xf2\x85\xe6\x0b"
    "\xd1\x17\xaa\x2f\x64\x5f\xe8\xbe\x10\x7e\xa1\xfc\x32\xfd\x88\xc7\x1d\x10\x7f\xa1\xfe\x42\xfe\x85\xfe\x0b\x01\x18"
    "\x0a\x30\x24\x60\x68\xc0\x10\x81\x59\x7e\xce\xe5\x0e\xe8\xc0\x10\x82\xa1\x04\x43\x0a\x86\x16\x0c\x31\x18\x6a\x30"
    "\xe4\x60\xe8\xc1\x6c\x3f\xec\x73\x07\x24\x61\x68\xc2\x10\x85\xa1\x0a\x43\x16\x86\x2e\x0c\x61\x18\xca\x30\xa4\x61"
    "\x1e\x5f\x3c\xb8\x03\xea\x30\xe4\x61\xe8\xc3\x10\x88\xa1\x10\x43\x22\x86\x46\x0c\x91\x18\x2a\x31\xc7\x67\x1f\xdf"
    "\x7d\x78\xf8\xa1\x13\x4b\x27\x96\x4e\x2c\x9d\x58\x3a\xb1\x74\x62\xe9\xc4\xd2\x89\xa5\x13\x1b\x1f\xbf\xb8\x03\x3a"
    "\xb1\x74\x62\xe9\xc4\xd2\x89\xa5\x13\x4b\x27\x96\x4e\xac\xef\x7f\x3e\x00\x7e\x72\x01\xe4\x0e\x7c\x03\xf4\x11\xd0"
    "\x57\x40\x9f\x01\x7d\x07\xf4\x21\x90\x4e\x2c\x9d\x58\x3a\xb1\xc3\x67\x50\xee\x80\x4e\x2c\x9d\x58\x3a\xb1\x74\x62"
    "\xe9\xc4\xd2\x89\xa5\x13\x4b\x27\x96\x4e\xec\xed\x5b\x30\x77\x40\x27\x96\x4e\x2c\x9d\x58\x3a\xb1\x74\x62\xe9\xc4"
    "\xd2\x89\xa5\x13\x4b\x27\x76\xfa\x20\xce\x1d\xd0\x89\xa5\x13\x4b\x27\x96\x4e\x2c\x9d\x58\x3a\xb1\x74\x62\xe9\xc4"
    "\xd2\x89\x5d\x7e\x2b\xc0\x1d\xd0\x89\xa5\x13\x4b\x27\x96\x4e\x2c\x9d\x58\x3a\xb1\x74\x62\xe9\xc4\xd2\x89\xdd\x7e"
    "\x35\xc2\x1d\xd0\x89\xa5\x13\x4b\x27\x96\x4e\x2c\x9d\x58\x3a\xb1\x74\x62\xe9\xc4\xd2\x89\x7d\xfc\x7e\x88\x3b\xa0"
    "\x13\x4b\x27\x96\x4e\x2c\x9d\x58\x3a\xb1\x74\x62\xe9\xc4\xd2\x89\xa5\x13\x7b\xfc\x92\xec\x3f\x3b\xf8\x03";
const size_t kDynamicSize = 698;
const char* const kDynamicSha = "8cd07a89bcd0dd362641cdf3e11ec648afdc7dda915bbc4d97620e8f701568a4"; // of the 7690 inflated bytes

const char kFar[] =
    "\xed\xdd\xbb\x96\x26\x21\x08\x04\xe0\x67\x15\x2f\xa0\x88\x10\xef\xd3\x6f\xf5\xe4\x93\x6d\xb4\xa7\xbe\x6c\xce\x3f"
    "\xdd\xad\x50\x98\x1a\x73\x79\xbd\xe7\xbe\xe2\x66\x8d\x76\xe2\xb5\x3a\xd5\xde\x8c\xf9\xfa\xd5\x3d\xbc\x57\xa5\xed"
    "\x23\x79\xda\xdd\x6b\xc5\x39\x52\xaa\x3d\x2b\xce\x7a\x77\x6e\x3f\x31\x47\x4f\xad\xe1\x0f\x0f\xe1\xff\xaa\xa9\x45"
    "\x4c\xeb\xfa\xa2\x1d\x9f\x47\xb5\xdd\xa1\xb3\x77\x3c\x30\x6c\xb9\xfb\xf6\xc4\xa3\x9a\x23\x87\xef\xf6\xf6\xdd\xe5"
    "\xf8\x3d\xb6\xdd\x30\x9f\xbb\xaf\x7b\x5f\x0b\xdb\x5a\xfd\x6d\x91\x93\x63\xdd\xbe\x2c\x5d\xa6\x6e\xc3\x27\x6f\x9e"
    "\x25\x7d\xca\xf4\xf0\xb3\xe7\xb2\xd7\x77\xaf\x75\xe3\x78\xdc\x95\xb5\xbd\x45\x68\xda\xf5\xfb\xd2\xc6\xe8\x51\xd5"
    "\xc3\xc6\xea\xf7\xd9\x6a\xb5\x8f\x97\xf9\xb7\x8b\xe9\x7b\xe5\x78\x3b\xde\xb3\x7b\xb0\xec\x31\xcb\x63\x4a\xd3\xbd"
    "\x43\x4a\x72\xe7\x49\x6f\xb7\xb9\xeb\xd1\x56\xcb\xf0\xeb\x1b\xf7\x6c\xd3\xa6\x77\x4b\x3b\xef\xf4\x7e\x67\xf6\xee"
    "\x58\x52\xb6\x36\xcd\xab\xc5\xcb\xec\xeb\x6c\xdd\x62\x67\x84\xe0\x1b\xfd\xdb\xe3\xb4\xc2\xfb\x53\xb1\x77\xbd\xd6"
    "\x0c\x5b\x91\x35\x75\xed\xec\x4d\xba\x49\x8d\x42\x57\x36\x6a\x5c\xbb\x8f\x53\xbd\x4c\x56\x5a\x9d\xe3\x63\xa3\xdc"
    "\xfb\xed\xd7\x9e\xee\xdb\xb1\x05\x09\xf5\x58\x4d\x0c\x45\xb9\x6f\xc8\x1b\x6f\x85\xc8\x8c\x36\x63\x39\x4a\x2d\x6b"
    "\xdc\x52\x2c\x6c\xb5\x37\x0c\xed\x5a\x6d\x0f\xb3\x86\x8f\x0d\x97\xab\x35\xb1\x90\x5b\xcf\x45\x75\x1c\xef\x7a\x9a"
    "\xaf\x33\x1a\x76\xb0\xb0\xbb\x40\x81\xc2\x0d\xe5\x44\x55\xb4\x79\x6f\xfd\x21\x0b\x35\xe7\x94\xa1\x21\xf3\x0c\x34"
    "\xb7\xbf\xe1\x03\x0b\x1f\xb7\x95\x56\xbe\x37\xd6\xb3\x9c\x72\x97\xcd\x3e\xe7\x9e\xb9\xe5\xcc\x1a\x08\x50\x64\x35"
    "\x41\xe5\xfd\xa1\x42\xb7\x02\x85\xed\xfe\xb5\x2d\x10\x07\xd1\x53\x82\x5c\x20\x7e\xfd\x39\x52\x68\xb7\xd9\x90\xf3"
    "\x1e\xca\xd8\x03\x19\x45\x44\xf5\xad\x27\xfd\x25\xf6\x9b\x8a\xbc\x2d\xb4\xe3\x2a\xb2\x86\x04\x9d\xab\xe8\xe2\xd8"
    "\xba\x96\x8f\x33\x5b\x77\x34\x76\x86\xbc\x78\x37\x76\x65\x22\xe9\x52\x17\xfb\x6a\x86\xb5\xcf\x75\x2a\xc6\x96\xbc"
    "\x03\x9b\x89\x65\xeb\xab\xd0\x1b\xb5\x0b\xf5\x46\xc9\xf0\xd1\xde\x5e\x97\x27\xa8\x9a\x3c\x55\xbf\x4d\xf6\xc0\xbe"
    "\x64\x2f\x6f\x99\x39\xce\x17\x9a\x68\x89\x54\x34\xcc\xcf\xc6\x07\x91\xb1\xd8\x37\x54\xe6\x98\x61\x89\x28\xd7\x1e"
    "\x61\x88\x8b\xa1\x1b\xba\x0a\x75\xeb\x0d\xdb\x8d\x7a\xa8\xd7\x1a\xfa\x9a\xd5\x74\x7d\x1a\x23\xe7\x71\x29\xb7\x86"
    "\x80\xc5\x69\x08\x0e\x9e\x92\x36\xb7\x05\x26\xeb\x88\x16\xe2\xf6\x46\x93\xd9\x50\x16\x15\x04\xaa\xe1\xaf\x56\x58"
    "\x54\xc6\x5a\xa7\x15\xb2\x78\x16\xe6\xe8\x18\x9e\x14\xb4\x1c\x2b\x46\x1c\xd0\x84\x9f\xce\x8f\x14\x9f\xc8\x74\x9b"
    "\x68\x08\xc6\xff\x20\x19\xd3\x0c\x7b\xe9\x77\xe7\x90\x1b\xa8\x63\xc7\xb7\xd4\x72\xad\x35\x0f\xc2\x85\x9a\x56\x8a"
    "\xe1\x8c\x40\xc4\xfb\xda\x8a\xa1\xc1\x6a\x6d\x3a\x4a\xd7\xbb\x1d\xf4\x7b\x21\xa0\xf3\x3d\x4d\xb4\x3a\x13\xfb\x7c"
    "\x9e\xd9\xf0\x87\x63\x41\x2d\x16\x66\x0a\xbb\xc6\xeb\xce\xb2\xab\xed\xec\xbe\xaf\x37\xe4\xc4\x7a\xb4\x9e\x81\xf9"
    "\x1a\x3e\xad\x2d\x6c\x72\x9c\xdc\x85\x83\x04\x8d\x46\x86\x0f\xd2\x6c\xc7\x46\xd5\xad\xc4\xd8\x8b\x0a\x66\x48\x30"
    "\x0b\x9e\x3d\xbd\x4a\x90\x99\x15\xba\x10\x27\xc4\x10\x39\xf1\x8e\xcd\xdf\x6b\xef\xb6\x79\x1d\xbf\xa0\x1a\x0d\x43"
    "\x81\xc1\xb7\x9e\x98\xc9\xad\xb6\x2f\x82\xda\x31\x85\x6e\xd6\x91\x92\x78\x3b\xe7\xf4\xa1\x18\xfd\xe1\x31\x50\x06"
    "\x41\xae\x47\x37\xc7\x19\xb4\x14\x2b\xeb\xa5\x82\xf1\xbb\x86\xa3\x46\x02\x69\x56\x04\x17\x2f\xca\x3c\xa3\x70\x9a"
    "\xec\x8d\x90\x63\x65\xf5\x04\xd1\x5d\x77\x62\x90\x34\xd7\x9c\xef\x58\x8b\xca\x83\x82\x19\x0a\xb2\x1f\x0e\x8e\x6c"
    "\x32\xd0\x6e\xaf\x7d\xf1\x9a\xd3\xd4\xbf\x51\x8e\x93\x68\xce\xd1\x23\x82\xa6\xa2\xc5\x85\xe3\x23\xd0\x61\xcd\x7d"
    "\x8f\x9f\x6e\x3a\x26\x8e\xd0\x13\xd9\xa5\xbb\xd6\x0b\x4c\xf1\x45\x3c\x4a\x90\x98\xbd\xee\xe8\x8a\x03\x68\xfc\x21"
    "\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22"
    "\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22"
    "\x22\xfa\x47\x82\x77\x61\xf3\x2e\x6c\xde\x85\xcd\xbb\xb0\x79\x17\x36\xef\xc2\x26\x22\x22\x22\x22\x22\x22\x22\x22"
    "\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22"
    "\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\x22\xfa\x05\xef\xc2\xe6\x5d\xd8"
    "\xbc\x0b\x9b\x77\x61\xf3\x2e\xec\xff\xef\x2e\xec\xbf";
const size_t kFarSize = 1049;
const char* const kFarSha = "a3b7c9ae1bbc1e02e4dad56bbdced7ad307303b1fb9990929c3cb4d64718bdb3"; // of the 64500 inflated bytes


std::string linesText() {
    std::string s;
    for (int i = 0; i < 300; ++i) s += "line " + std::to_string(i) + " of the test text\n";
    return s;
}

std::string farText() {
    std::uint32_t x = 12345;
    std::string a;
    for (int i = 0; i < 1500; ++i) {
        x = (x * 1103515245u + 12345u) & 0x7FFFFFFFu;
        a += static_cast<char>('a' + (x >> 16) % 16);
    }
    return a + std::string(30000, 'z') + a + std::string(30000, 'z') + a;
}

std::string inflateString(const char* data, size_t n, bool* ok) {
    std::vector<std::uint8_t> out;
    *ok = inflateRaw(reinterpret_cast<const std::uint8_t*>(data), n, &out);
    return std::string(out.begin(), out.end());
}

std::string jsonList(const std::string& key) {
    // tools/exe_texts/<key>.json: "key", "address", "kind" per entry, before "removed".
    const std::string json = readBytes(std::string(AS3D_REPO_ROOT) + "/tools/exe_texts/" + key + ".json");
    std::vector<std::string> out;
    const size_t ent = json.find("\"entries\"");
    const size_t rem = json.find("\"removed\"");
    std::string joined;
    for (size_t p = json.find("\"key\":", ent); p != std::string::npos && (rem == std::string::npos || p < rem);
         p = json.find("\"key\":", p + 1)) {
        auto field = [&](const char* name) {
            const size_t f = json.find(std::string("\"") + name + "\":", p);
            const size_t s = json.find('"', json.find(':', f) + 1);
            return json.substr(s + 1, json.find('"', s + 1) - s - 1);
        };
        const std::string kind = field("kind");
        const int k = kind == "text" ? 0 : kind == "text_ml" ? 1 : 2;
        joined += field("key") + "|" + std::to_string(std::stoul(field("address"), nullptr, 16)) + "|" +
                  std::to_string(k) + "\n";
    }
    return joined;
}

} // namespace

TEST_CASE("sha256: FIPS 180-4 examples, chunked updates, files") {
    CHECK(sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(sha256Hex(two.data(), two.size()) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // A million 'a', fed in uneven pieces.
    Sha256 s;
    const std::string piece(997, 'a');
    size_t left = 1000000;
    while (left) {
        size_t n = left < piece.size() ? left : piece.size();
        s.update(piece.data(), n);
        left -= n;
    }
    CHECK(s.hexDigest() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // Every length around the padding boundary agrees between one call and byte by byte.
    for (size_t n = 50; n < 70; ++n) {
        const std::string m(n, 'x');
        Sha256 b;
        for (char c : m) b.update(&c, 1);
        CHECK(b.hexDigest() == sha256Hex(m.data(), m.size()));
    }
    TempDir tmp("sha");
    writeBytes(tmp.path / "f", two);
    CHECK(sha256File((tmp.path / "f").string()) == sha256Hex(two.data(), two.size()));
    CHECK(sha256File((tmp.path / "nothing").string()).empty());
}

TEST_CASE("inflate: stored, fixed and dynamic blocks, far matches across the window, bad data") {
    bool ok = false;
    // A stored block by hand: final, type 0, LEN, NLEN, bytes.
    const char stored[] = "\x01\x05\x00\xfa\xffhello";
    CHECK(inflateString(stored, 10, &ok) == "hello");
    CHECK(ok);
    CHECK((static_cast<unsigned char>(kFixed[0]) >> 1 & 3) == 1);
    std::string out = inflateString(kFixed, kFixedSize, &ok);
    CHECK(ok);
    CHECK(sha256Hex(out.data(), out.size()) == kFixedSha);
    CHECK(out == "hello hello hello hello, deflate");
    CHECK((static_cast<unsigned char>(kDynamic[0]) >> 1 & 3) == 2);
    out = inflateString(kDynamic, kDynamicSize, &ok);
    CHECK(ok);
    CHECK(out == linesText());
    CHECK(sha256Hex(out.data(), out.size()) == kDynamicSha);
    // 93 KB out of 1 KB: matches 31 500 bytes back, the ring buffer wrapping.
    out = inflateString(kFar, kFarSize, &ok);
    CHECK(ok);
    CHECK(out == farText());
    CHECK(sha256Hex(out.data(), out.size()) == kFarSha);

    // Truncated, a reserved block type, a stored block whose NLEN is wrong.
    inflateString(kDynamic, kDynamicSize / 2, &ok);
    CHECK_FALSE(ok);
    inflateString("\x07", 1, &ok);
    CHECK_FALSE(ok);
    inflateString("\x01\x05\x00\xfa\xfehello", 10, &ok);
    CHECK_FALSE(ok);
}

TEST_CASE("zip reader: stored and deflated entries, CRC check, not a zip") {
    TempDir tmp("zip");
    const std::string lines = linesText();
    const std::string zip = buildZip({{"dir/", "", ""},
                                      {"dir\\a.txt", "first entry", ""},
                                      {"dir/sub/lines.txt", lines, std::string(kDynamic, kDynamicSize)}});
    writeBytes(tmp.path / "t.zip", zip);
    CHECK(looksLikeZip((tmp.path / "t.zip").string()));
    ZipReader z;
    std::string err;
    REQUIRE(z.open((tmp.path / "t.zip").string(), &err));
    REQUIRE(z.entries().size() == 3);
    CHECK(z.entries()[0].isDir());
    CHECK(z.entries()[1].name == "dir/a.txt"); // backslashes turned into slashes
    CHECK(z.entries()[2].method == 8);
    CHECK(z.entries()[2].size == lines.size());
    std::vector<std::uint8_t> mem;
    REQUIRE(z.extractToMemory(z.entries()[1], &mem, 1000, &err));
    CHECK(std::string(mem.begin(), mem.end()) == "first entry");
    CHECK_FALSE(z.extractToMemory(z.entries()[2], &mem, 100, &err)); // too large for the limit
    REQUIRE(z.extractToFile(z.entries()[2], (tmp.path / "lines.txt").string(), &err));
    CHECK(readBytes(tmp.path / "lines.txt") == lines);

    writeBytes(tmp.path / "bad.zip", buildZip({{"x.txt", lines, std::string(kDynamic, kDynamicSize)}}, true));
    ZipReader b;
    REQUIRE(b.open((tmp.path / "bad.zip").string(), &err));
    CHECK_FALSE(b.extractToFile(b.entries()[0], (tmp.path / "x.txt").string(), &err));
    CHECK(err.find("CRC") != std::string::npos);
    CHECK_FALSE(there(tmp.path / "x.txt")); // a failed extraction leaves nothing behind

    writeBytes(tmp.path / "no.zip", "this is not a zip archive at all");
    ZipReader n;
    CHECK_FALSE(looksLikeZip((tmp.path / "no.zip").string()));
    CHECK_FALSE(n.open((tmp.path / "no.zip").string(), &err));
}

TEST_CASE("exe texts: the sequels' address lists are tools/exe_texts/<key>.json's; unknown executables") {
    for (const char* key : {"as2", "gulf"}) {
        std::string mine;
        for (const std::string& s : exeTextListForTest(key)) mine += s + "\n";
        const std::string json = jsonList(key);
        REQUIRE_FALSE(json.empty());
        CHECK(mine == json);
    }
    CHECK(exeTextListForTest("as3d").empty());
    CHECK(gameOfExecutable("3b371bc2a72dcf18c17b5efa1b7e08b85fef73cfdd28dce00ec0aa2f2e93df1d") == findGameProfile("as3d"));
    CHECK(gameOfExecutable("b24b62b2c5b61cfa1cf0aad781788aa777a2e4f4a385c73ba53014b039e46f5b") == findGameProfile("as2"));
    CHECK(gameOfExecutable("86195a9653489064844c172ce43307c703a50e53be7e00d45fe346c45d5ae077") == findGameProfile("gulf"));
    CHECK(gameOfExecutable(std::string(64, '0')) == nullptr);
    std::vector<std::uint8_t> junk(100, 0);
    std::string text, err;
    CHECK_FALSE(extractExeTexts(junk, std::string(64, '0'), nullptr, &text, &err));
    // A known hash on bytes that are not a PE file: an error, no crash.
    CHECK_FALSE(extractExeTexts(junk, "3b371bc2a72dcf18c17b5efa1b7e08b85fef73cfdd28dce00ec0aa2f2e93df1d", nullptr, &text, &err));
    CHECK(err.find("MZ") != std::string::npos);
}

TEST_CASE("exe texts: the built-in pak hashes are tools/games.json's") {
    const std::string json = readBytes(std::string(AS3D_REPO_ROOT) + "/tools/games.json");
    REQUIRE_FALSE(json.empty());
    // Every pak of every profile has a hash in games.json, and the importer knows the same
    // one: an unrelated pak1.apk is refused, so the table must be right. Checked through an
    // import that offers each game's real name with a fake content: refused by hash.
    int hashes = 0;
    for (size_t p = json.find("\"pak_sha256\""); p != std::string::npos; p = json.find("\"pak_sha256\"", p + 1)) {
        const size_t end = json.find('}', p);
        for (size_t q = json.find(".apk\": \"", p); q != std::string::npos && q < end; q = json.find(".apk\": \"", q + 1))
            ++hashes;
    }
    CHECK(hashes == 10);
    const std::string src = readBytes(std::string(AS3D_REPO_ROOT) + "/engine/src/game/game_import.cpp");
    for (size_t p = json.find("\"pak_sha256\""); p != std::string::npos; p = json.find("\"pak_sha256\"", p + 1)) {
        const size_t end = json.find('}', p);
        for (size_t q = json.find(".apk\": \"", p); q != std::string::npos && q < end; q = json.find(".apk\": \"", q + 1)) {
            const std::string sha = json.substr(q + 8, 64);
            CHECK(src.find(sha) != std::string::npos);
        }
    }
}

TEST_CASE("import: loose files of one game, then what is missing, then the rest") {
    TableGuard guard;
    TempDir tmp("loose");
    FakeGames fake(tmp.path);
    const fs::path in = tmp.path / "incoming", games = tmp.path / "games";

    // AirStrike 2: three paks, Settings.xml, the logo, and things that are not game files.
    writeBytes(in / "pak0.apk", fake.pak0[1]);
    writeBytes(in / "PAK1.APK", "second pak");
    writeBytes(in / "pak2.apk", "third pak");
    writeBytes(in / "Settings.xml", "<settings/>");
    writeBytes(in / "logo2s.tga", "logo");
    writeBytes(in / "readme.txt", "hello");
    writeBytes(in / "pak7.apk", "not one of this game's paks");
    ImportResult r = importGameFiles(in.string(), games.string());
    INFO(allNotes(r));
    REQUIRE(r.imported == std::vector<std::string>{"as2"});
    CHECK(readBytes(games / "as2/pak0.apk") == fake.pak0[1]);
    CHECK(readBytes(games / "as2/pak1.apk") == "second pak"); // the installed name is lower case
    CHECK(readBytes(games / "as2/pak2.apk") == "third pak");
    CHECK(readBytes(games / "as2/Settings.xml") == "<settings/>");
    CHECK(readBytes(games / "as2/logo2s.tga") == "logo");
    CHECK_FALSE(there(in / "pak0.apk")); // used files are moved
    CHECK(there(in / "readme.txt"));     // the rest stays for the caller to remove
    CHECK(hasNote(r, "readme.txt: not a game file, ignored."));
    CHECK(hasNote(r, "pak7.apk: AirStrike 2 has no pak7.apk, ignored."));
    CHECK(hasNote(r, "AirStrike 2: ready to play."));
    CHECK(hasNote(r, "the executable (AirStrike3D II.exe) for the menu texts"));
    CHECK_FALSE(there(games / ".import-staging"));
    CHECK(importedGameComplete(games.string(), gameProfile(GameId::AirStrike2)));

    // Gulf Thunder with two of its four paks: imported but not complete.
    fs::remove_all(in);
    writeBytes(in / "pak0.apk", fake.pak0[2]);
    writeBytes(in / "pak1.apk", "gulf 1");
    r = importGameFiles(in.string(), games.string());
    CHECK(r.imported.empty());
    CHECK(hasNote(r, "AirStrike II: Gulf Thunder: pak2.apk missing."));
    CHECK(hasNote(r, "AirStrike II: Gulf Thunder: pak4.apk missing."));
    CHECK(importedGames(games.string()).size() == 1);

    // The other two later, on their own: known by their hash.
    static std::string h2, h4;
    h2 = sha256Hex("gulf 2", 6);
    h4 = sha256Hex("gulf 4", 6);
    const KnownPakHash table[2] = {{GameId::GulfThunder, "pak2.apk", h2.c_str()}, {GameId::GulfThunder, "pak4.apk", h4.c_str()}};
    setKnownPakHashesForTest(table, 2);
    fs::remove_all(in);
    writeBytes(in / "pak2.apk", "gulf 2");
    writeBytes(in / "pak4.apk", "gulf 4");
    writeBytes(in / "Settings.xml", "<gulf/>"); // beside Gulf Thunder's paks: Gulf Thunder's
    r = importGameFiles(in.string(), games.string());
    INFO(allNotes(r));
    CHECK(r.imported == std::vector<std::string>{"gulf"});
    CHECK(hasNote(r, "AirStrike II: Gulf Thunder: imported pak2.apk, pak4.apk, Settings.xml."));
    CHECK(readBytes(games / "gulf/Settings.xml") == "<gulf/>");
    // On its own with two games there: nowhere to put it.
    fs::remove_all(in);
    writeBytes(in / "logo2s.tga", "which game?");
    r = importGameFiles(in.string(), games.string());
    CHECK(r.imported.empty());
    CHECK(hasNote(r, "logo2s.tga: no game to attach it to"));
    CHECK_FALSE(there(games / "gulf/logo2s.tga"));
    CHECK(there(in / "logo2s.tga"));
    REQUIRE(importedGames(games.string()).size() == 2);
    CHECK(importedGames(games.string())[0]->id == GameId::AirStrike2);
    CHECK(importedGames(games.string())[1]->id == GameId::GulfThunder);

    // A pak with a known hash for its name but other content is refused beside its pak0.
    fs::remove_all(in);
    writeBytes(in / "pak0.apk", fake.pak0[2]);
    writeBytes(in / "pak2.apk", "not gulf 2");
    r = importGameFiles(in.string(), games.string());
    CHECK(hasNote(r, "pak2.apk: not AirStrike II: Gulf Thunder's pak2.apk (different content), ignored."));
    CHECK(readBytes(games / "gulf/pak2.apk") == "gulf 2");
}

TEST_CASE("import: Settings.xml and the logo alone go to the one game there is; unknown paks; nothing at all") {
    TableGuard guard;
    TempDir tmp("attach");
    FakeGames fake(tmp.path);
    const fs::path in = tmp.path / "incoming", games = tmp.path / "games";
    for (const char* p : {"pak0.apk", "pak1.apk", "pak2.apk"}) writeBytes(games / "as3d" / p, p);
    writeBytes(in / "Settings.xml", "<as3d/>");
    writeBytes(in / "logo2s.tga", "logo");
    ImportResult r = importGameFiles(in.string(), games.string());
    INFO(allNotes(r));
    CHECK(r.imported == std::vector<std::string>{"as3d"});
    CHECK(readBytes(games / "as3d/Settings.xml") == "<as3d/>");
    CHECK(readBytes(games / "as3d/logo2s.tga") == "logo");

    fs::remove_all(in);
    writeBytes(in / "pak0.apk", std::string(5000, 'q'));
    writeBytes(in / "pak1.apk", "x");
    r = importGameFiles(in.string(), games.string());
    CHECK(r.imported.empty());
    CHECK(hasNote(r, "pak0.apk: not the pak0.apk of a known game, ignored."));
    CHECK(hasNote(r, "pak1.apk: not a pak of a known game, ignored."));
    CHECK(there(in / "pak0.apk"));

    fs::remove_all(in);
    fs::create_directories(in);
    r = importGameFiles(in.string(), games.string());
    CHECK(r.imported.empty());
    CHECK(hasNote(r, "No game files recognised."));
}

TEST_CASE("import: a zip with two game directories and a third without pak0, nested data/ and data/gfx/") {
    TableGuard guard;
    TempDir tmp("zipimport");
    FakeGames fake(tmp.path);
    const fs::path in = tmp.path / "incoming", games = tmp.path / "games";
    const std::string lines = linesText();
    const std::string zip = buildZip({
        {"AirStrike/AirStrike_3D/", "", ""},
        {"AirStrike/AirStrike_3D/data/pak0.apk", fake.pak0[0], ""},
        {"AirStrike/AirStrike_3D/data/pak1.apk", "as3d 1", ""},
        {"AirStrike/AirStrike_3D/data/pak2.apk", "as3d 2", ""},
        {"AirStrike/AirStrike_3D/data/Settings.xml", lines, std::string(kDynamic, kDynamicSize)},
        {"AirStrike/AirStrike_3D/data/gfx/logo2s.tga", "as3d logo", ""},
        {"AirStrike/AirStrike_3D/AirStrike3D.exe", "not the real executable", ""},
        {"AirStrike/AirStrike_3D/data/textures/x.tga", "ignored", ""},
        {"AirStrike/AirStrike 2/data/pak0.apk", fake.pak0[1], ""},
        {"AirStrike/AirStrike 2/data/pak1.apk", "as2 1", ""},
        {"AirStrike/AirStrike 2/data/Settings.xml", "<as2/>", ""},
        {"AirStrike/Galaxy Strike/data/pak1.apk", "other game 1", ""},
        {"AirStrike/Galaxy Strike/data/Settings.xml", "<other/>", ""},
    });
    writeBytes(in / "AirStrike.zip", zip);
    ImportResult r = importGameFiles(in.string(), games.string());
    INFO(allNotes(r));
    CHECK(r.imported == std::vector<std::string>{"as3d"});
    CHECK(readBytes(games / "as3d/pak0.apk") == fake.pak0[0]);
    CHECK(readBytes(games / "as3d/pak2.apk") == "as3d 2");
    CHECK(readBytes(games / "as3d/Settings.xml") == lines);
    CHECK(readBytes(games / "as3d/logo2s.tga") == "as3d logo");
    CHECK(readBytes(games / "as2/Settings.xml") == "<as2/>");
    CHECK(readBytes(games / "as2/pak1.apk") == "as2 1");
    CHECK(hasNote(r, "AirStrike 2: pak2.apk missing."));
    CHECK(hasNote(r, "1 other executable(s) in the zip ignored."));
    CHECK_FALSE(there(games / "gulf"));
    CHECK(there(in / "AirStrike.zip")); // zips stay for the caller
    CHECK_FALSE(there(games / ".import-staging"));
}

// ---- the real files, when this machine has them

namespace {

std::string dataRoot() { return testdata::root(); }

struct RealGame {
    const char* key;
    std::string install, data, texts;
};

std::vector<RealGame> realGames() {
    const std::string r = dataRoot();
    return {
        {"as3d", r + "/third_party_local/original", r + "/third_party_local/original/data", r + "/assets_extracted/texts_v170.txt"},
        {"as2", r + "/third_party_local/games/as2", r + "/third_party_local/games/as2/data",
         r + "/assets_extracted_games/as2/texts_as2.txt"},
        {"gulf", r + "/third_party_local/games/gulf", r + "/third_party_local/games/gulf/data",
         r + "/assets_extracted_games/gulf/texts_gulf.txt"},
    };
}

} // namespace

TEST_CASE("import (real data): each game's own files, loose; texts as tools/extract_exe_texts.py writes them") {
    int ran = 0;
    for (const RealGame& rg : realGames()) {
        const GameProfile& g = *findGameProfile(rg.key);
        const std::string exe = rg.install + "/" + g.exeName;
        if (!there(rg.data + "/pak0.apk") || !there(exe)) continue;
        ++ran;
        TempDir tmp(rg.key);
        const fs::path in = tmp.path / "incoming", games = tmp.path / "games";
        std::error_code ec;
        fs::create_directories(in, ec);
        // As the Android picker leaves them: flat, under their own names.
        for (const char* const* p = g.paks; *p; ++p) fs::copy_file(rg.data + "/" + *p, in / *p, ec);
        fs::copy_file(exe, in / g.exeName, ec);
        if (there(rg.data + "/Settings.xml")) fs::copy_file(rg.data + "/Settings.xml", in / "Settings.xml", ec);
        if (there(rg.data + "/gfx/logo2s.tga")) fs::copy_file(rg.data + "/gfx/logo2s.tga", in / "logo2s.tga", ec);
        ImportResult r = importGameFiles(in.string(), games.string());
        INFO(rg.key << "\n" << allNotes(r));
        CHECK(r.imported == std::vector<std::string>{rg.key});
        const fs::path dir = games / rg.key;
        for (const char* const* p = g.paks; *p; ++p)
            CHECK(sha256File((dir / *p).string()) == sha256File(rg.data + "/" + *p));
        if (there(rg.texts)) CHECK(readBytes(dir / g.textsFile) == readBytes(rg.texts));
        CHECK(there(dir / g.textsFile));
        CHECK(there(dir / "Settings.xml"));
    }
    if (!ran) std::fprintf(stderr, "SKIPPED (no game data under %s): %s\n", dataRoot().c_str(), __FILE__);
}

TEST_CASE("import (real data): the original download's zip, all three games at once") {
    const std::string zip = dataRoot() + "/binaries/AirStrike.zip";
    if (!there(zip)) {
        std::fprintf(stderr, "SKIPPED (no %s): %s\n", zip.c_str(), __FILE__);
        return;
    }
    TempDir tmp("download");
    const fs::path in = tmp.path / "incoming", games = tmp.path / "games";
    std::error_code ec;
    fs::create_directories(in, ec);
    fs::create_symlink(zip, in / "AirStrike.zip", ec);
    ImportResult r = importGameFiles(in.string(), games.string());
    INFO(allNotes(r));
    CHECK(r.imported == std::vector<std::string>{"as3d", "as2", "gulf"});
    for (const RealGame& rg : realGames()) {
        const GameProfile& g = *findGameProfile(rg.key);
        for (const char* const* p = g.paks; *p; ++p)
            if (there(rg.data + "/" + *p))
                CHECK(sha256File((games / rg.key / *p).string()) == sha256File(rg.data + "/" + *p));
        if (there(rg.texts)) CHECK(readBytes(games / rg.key / g.textsFile) == readBytes(rg.texts));
        CHECK(there(games / rg.key / "Settings.xml"));
    }
    CHECK(there(games / "as3d/logo2s.tga"));
}
