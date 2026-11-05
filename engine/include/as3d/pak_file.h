// A pak archive's file table without its bodies, and a pak writer (docs/spec/pak.md): what
// identifying a player's pak by its entry names and building a pak0.apk from loose files need
// (docs/spec/issues/170). The reader beside it (makePakSource, as3d/vfs.h) mounts paks; this
// file looks at them and makes them. No game data in here.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/vfs.h"

namespace as3d {

struct PakTableEntry {
    std::string name; // as stored: cp1251 bytes, backslashes, up to 64 bytes
    u32 offset = 0;
    u32 size = 0;
    u32 flag = 0;     // 1: the body is XORed with the key
};

struct PakTable {
    std::vector<PakTableEntry> entries;
    std::uint64_t fileSize = 0;
    // > 0: the file is truncated by this many bytes (its table, or the body of an entry, ends
    // past the end of the file; a pak that came out of a split CAB). The entries are those
    // the table holds within the file.
    std::uint64_t bytesShort = 0;
};

// Reads the header and the file table. False with a message when the stream is not a pak
// (magic, header) or cannot be read; a truncated pak reads with bytesShort > 0.
bool readPakTable(IStream& stream, PakTable* out, std::string* error);
bool readPakTableFile(const std::string& path, PakTable* out, std::string* error);

// The entry names as the identification compares them: ASCII lower case, '/' turned into '\',
// sorted, without duplicates.
std::vector<std::string> pakEntryNames(const PakTable& table);
std::string normalizePakEntryName(const std::string& name);

// One entry to write: the body comes from a file (sourcePath) or from memory (data).
struct PakWriteEntry {
    std::string name;       // backslash path, cp1251 bytes, 1 to 64 bytes
    std::string sourcePath; // read when not empty, else `data`
    Blob data;
};

// Writes a pak: stored bodies, the encrypted flag clear, a zero XOR key (so the table and the
// bodies are plain), the entries in the given order. Written as <path>.tmp then renamed
// (as3d/fs_util.h, writeFileAtomic's pattern), so `path` is either absent, the old file or
// the complete new one. False with a message naming the entry or the step that failed.
bool writePak(const std::string& path, const std::vector<PakWriteEntry>& entries, std::string* error);

} // namespace as3d
