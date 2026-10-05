// A small zip archive reader (PKWARE APPNOTE): the central directory, stored and deflated
// entries (RFC 1951, a compact streaming inflater), CRC-32 checked, zip64 sizes. Used to import
// the player's game files from a zip of the game directory or the original download
// (as3d/game_import.h). No encryption, no other compression methods.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace as3d {

struct ZipEntry {
    std::string name;            // as stored ('/' separated; '\\' turned into '/')
    std::uint16_t method = 0;    // 0 stored, 8 deflated
    std::uint16_t flags = 0;
    std::uint32_t crc = 0;
    std::uint64_t compSize = 0;
    std::uint64_t size = 0;
    std::uint64_t localOffset = 0; // of the local header
    bool isDir() const { return !name.empty() && name.back() == '/'; }
};

// Receives the uncompressed bytes in order; false stops the extraction.
using ZipSink = std::function<bool(const std::uint8_t* data, size_t n)>;

class ZipReader {
public:
    ZipReader() = default;
    ~ZipReader();
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    // Reads the central directory. False and a message if the file is not a zip we can read.
    bool open(const std::string& path, std::string* error);
    const std::vector<ZipEntry>& entries() const { return entries_; }

    // Streams one entry; checks its size and CRC-32.
    bool extract(const ZipEntry& e, const ZipSink& sink, std::string* error);
    bool extractToFile(const ZipEntry& e, const std::string& path, std::string* error);
    // Refuses entries larger than maxSize.
    bool extractToMemory(const ZipEntry& e, std::vector<std::uint8_t>* out, size_t maxSize, std::string* error);

private:
    std::FILE* f_ = nullptr;
    std::uint64_t fileSize_ = 0;
    std::vector<ZipEntry> entries_;
};

// Whether `path` starts with a zip signature (local header or an empty archive's end record).
bool looksLikeZip(const std::string& path);

// Raw deflate data (no zlib header) in memory; false on corrupt or truncated data.
bool inflateRaw(const std::uint8_t* data, size_t n, std::vector<std::uint8_t>* out);

std::uint32_t crc32Update(std::uint32_t crc, const std::uint8_t* data, size_t n); // crc starts at 0

} // namespace as3d
