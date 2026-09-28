// Helpers for tests that need the original game data, which is never committed.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

#include "as3d/core.h"

namespace testdata {

// Repository that holds the gitignored data. Worktrees do not contain it, so
// AS3D_DATA_ROOT can point at the main checkout.
inline std::string root() {
    const char* env = std::getenv("AS3D_DATA_ROOT");
    return env && *env ? env : AS3D_REPO_ROOT;
}

inline std::string extractedDir() { return root() + "/assets_extracted"; }
inline std::string originalDir() { return root() + "/third_party_local/original"; }
// Golden files are committed, so they always come from this checkout.
inline std::string goldenDir() { return std::string(AS3D_REPO_ROOT) + "/testdata/golden"; }

// Reads an extracted file by game path, e.g. "models\\apache\\apache.mdl".
inline bool readExtracted(const std::string& gamePath, as3d::Blob& out) {
    std::string p = as3d::normalizePath(gamePath);
    for (char& c : p) if (c == '\\') c = '/';
    std::FILE* f = std::fopen((extractedDir() + "/" + p).c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(static_cast<size_t>(n));
    size_t got = n ? std::fread(out.data(), 1, out.size(), f) : 0;
    std::fclose(f);
    return got == out.size();
}

inline bool available() {
    as3d::Blob b;
    return readExtracted("maps\\levels.txt", b);
}

} // namespace testdata

// Put at the top of a TEST_CASE that needs game data.
#define AS3D_REQUIRE_DATA()                                                     \
    if (!testdata::available()) {                                               \
        std::fprintf(stderr, "SKIPPED (no game data): %s\n", __FILE__);         \
        return;                                                                 \
    }
