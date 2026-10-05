// SHA-256 (FIPS 180-4), small and dependency-free: identifies the user's game files by content
// (the executable whose front-end texts are read, the paks of tools/games.json) when they are
// imported (as3d/game_import.h).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace as3d {

class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t n);
    // The digest as 64 lower-case hex digits. The object is spent afterwards.
    std::string hexDigest();

private:
    void block(const std::uint8_t* p);
    std::uint32_t h_[8];
    std::uint8_t buf_[64];
    size_t bufLen_ = 0;
    std::uint64_t total_ = 0;
};

std::string sha256Hex(const void* data, size_t n);
// The digest of a whole file, read in chunks; empty if it cannot be read.
std::string sha256File(const std::string& path);

} // namespace as3d
