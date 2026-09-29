// Level map (.hsc) loader. See docs/spec/hmap.md for the format and the reverse-
// engineering evidence behind every check below. Reference implementation:
// tools/ref/hmap.py, which every shipped .hsc is cross-checked against (see
// apps/tests/hmap_test.cpp).
//
// No exceptions, no RTTI (this module is built with -fno-exceptions -fno-rtti):
// malformed input is reported by returning false / setting *error, never by throwing or
// asserting, and every buffer access is bounds-checked before it happens.
#include "as3d/level.h"

#include <cstdint>
#include <cstring>

#include "as3d/core.h"

namespace as3d {

namespace {

// Engine limits (VERIFIED-CODE, loader 0x4091f0; mirrored by tools/ref/hmap.py).
constexpr u32 kMaxPlacements = 0x4000; // "Too many map objects."
constexpr u32 kMaxTypes = 1024;        // size of the loader's local type table
constexpr u32 kMaxItems = 255;         // item index is one byte, 0 = none

constexpr u32 kMagic = 0x50414d48; // "HMAP" as a little-endian u32, per 0x409280
constexpr u32 kVersion = 2;

void setError(std::string* error, const char* msg) {
    if (error) *error = msg;
}

// Reads `count` length-prefixed strings (u8 n, then n bytes, the last a NUL) starting at
// `reader`'s current position. Returns false (reader position undefined) on any
// malformed string.
bool readNames(ByteReader& reader, u32 count, std::vector<std::string>& out, std::string* error) {
    out.clear();
    out.reserve(count);
    for (u32 i = 0; i < count; i++) {
        if (reader.remaining() < 1) {
            setError(error, "name table: truncated");
            return false;
        }
        u8 n = reader.readU8();
        if (n == 0 || reader.remaining() < n) {
            setError(error, "name table: bad string length");
            return false;
        }
        std::string raw(n, '\0');
        if (!reader.readBytes(raw.data(), n)) {
            setError(error, "name table: truncated string");
            return false;
        }
        // Exactly one NUL, at the end (VERIFIED-DATA).
        if (raw[static_cast<size_t>(n) - 1] != '\0') {
            setError(error, "name table: string not NUL-terminated");
            return false;
        }
        for (size_t k = 0; k + 1 < raw.size(); k++) {
            if (raw[k] == '\0') {
                setError(error, "name table: embedded NUL");
                return false;
            }
        }
        out.emplace_back(raw.data(), static_cast<size_t>(n) - 1);
    }
    return true;
}

bool readWaypoint(ByteReader& reader, Waypoint& wp) {
    wp.x = reader.readI32();
    wp.y = reader.readI32();
    wp.unknown8 = reader.readI32();
    wp.inCtrl.x = reader.readF32();
    wp.inCtrl.y = reader.readF32();
    wp.outCtrl.x = reader.readF32();
    wp.outCtrl.y = reader.readF32();
    wp.delay = reader.readF32();
    return !reader.failed();
}

// Parses into `out` in place; returns false on any malformed input. The public
// loadLevel() wraps this so a failure partway through (e.g. a truncated placement list,
// discovered only after the grid has already been filled in) can never leave the
// caller's LevelData half-populated -- see the wrapper below.
bool loadLevelInto(const u8* data, size_t size, LevelData& out, std::string* error) {

    if (!data || size < 28) {
        setError(error, "file shorter than the header");
        return false;
    }

    ByteReader reader(data, size);
    u32 magic = reader.readU32();
    u32 version = reader.readU32();
    u32 width = reader.readU32();
    u32 height = reader.readU32();
    u32 count = reader.readU32();
    u32 ntypes = reader.readU32();
    u32 nitems = reader.readU32();
    if (reader.failed()) {
        setError(error, "file shorter than the header");
        return false;
    }
    if (magic != kMagic) {
        setError(error, "File header corrupted.");
        return false;
    }
    if (version != kVersion) {
        setError(error, "Illegal file version.");
        return false;
    }
    if (count > kMaxPlacements) {
        setError(error, "Too many map objects.");
        return false;
    }
    if (ntypes > kMaxTypes || nitems > kMaxItems) {
        setError(error, "table sizes exceed engine limits");
        return false;
    }
    if (width < 2 || height < 2) {
        setError(error, "degenerate grid");
        return false;
    }
    // Guard the width*height*4 multiplication against overflow for a maliciously large
    // header (the shipped data never exceeds 32x256); reject rather than let the
    // multiplication wrap.
    constexpr std::uint64_t kMaxCells = static_cast<std::uint64_t>(1) << 24; // generous; 32*256 = 8192 in the data
    if (static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > kMaxCells) {
        setError(error, "grid too large");
        return false;
    }

    if (!readNames(reader, ntypes, out.typeNames, error)) return false;
    if (!readNames(reader, nitems, out.itemNames, error)) return false;

    std::uint64_t gridLen64 = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ull;
    if (reader.remaining() < gridLen64) {
        setError(error, "grid truncated");
        return false;
    }
    out.cells.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (size_t i = 0; i < out.cells.size(); i++) {
        LevelCell c;
        c.height = reader.readU8();
        c.tileSet = reader.readU8();
        c.tileIndex = reader.readU8();
        c.tileRotation = reader.readU8();
        out.cells[i] = c;
    }
    if (reader.failed()) {
        setError(error, "grid truncated");
        return false;
    }

    out.width = width;
    out.height = height;
    out.placements.reserve(count);
    for (u32 i = 0; i < count; i++) {
        if (reader.remaining() < 10 + 1) {
            setError(error, "placement truncated");
            return false;
        }
        Placement p;
        p.typeIndex = reader.readU16();
        p.x = reader.readU16();
        p.y = reader.readU16();
        p.rotationSteps = reader.readU8();
        p.dropItem = reader.readU8();
        u16 nwp = reader.readU16();
        u8 scriptLen = reader.readU8();
        if (reader.failed()) {
            setError(error, "placement truncated");
            return false;
        }
        if (scriptLen) {
            if (reader.remaining() < scriptLen) {
                setError(error, "placement script truncated");
                return false;
            }
            std::string raw(scriptLen, '\0');
            reader.readBytes(raw.data(), scriptLen);
            if (raw[static_cast<size_t>(scriptLen) - 1] != '\0') {
                setError(error, "placement script not NUL-terminated");
                return false;
            }
            for (size_t k = 0; k + 1 < raw.size(); k++) {
                if (raw[k] == '\0') {
                    setError(error, "placement script has embedded NUL");
                    return false;
                }
            }
            p.scriptOverride.assign(raw.data(), static_cast<size_t>(scriptLen) - 1);
        }
        if (reader.remaining() < 2) {
            setError(error, "placement path flag truncated");
            return false;
        }
        p.pathFlag = reader.readU16();
        std::uint64_t wpBytes = static_cast<std::uint64_t>(nwp) * 32ull;
        if (reader.remaining() < wpBytes) {
            setError(error, "placement waypoints truncated");
            return false;
        }
        p.waypoints.reserve(nwp);
        for (u16 k = 0; k < nwp; k++) {
            Waypoint wp;
            if (!readWaypoint(reader, wp)) {
                setError(error, "waypoint truncated");
                return false;
            }
            p.waypoints.push_back(wp);
        }
        out.placements.push_back(std::move(p));
    }

    if (reader.pos() != reader.size()) {
        setError(error, "trailing data after the last placement");
        return false;
    }
    return true;
}

} // namespace

bool loadLevel(const u8* data, size_t size, LevelData& out, std::string* error) {
    if (error) error->clear();
    LevelData result;
    if (!loadLevelInto(data, size, result, error)) {
        out = LevelData{};
        return false;
    }
    out = std::move(result);
    return true;
}

} // namespace as3d
