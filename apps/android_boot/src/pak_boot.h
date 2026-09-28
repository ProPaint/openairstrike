// TEMPORARY pak reader for the WP-18 Android/desktop bring-up milestone.
//
// This duplicates a small slice of docs/spec/pak.md's logic (header parsing,
// XOR-decrypted file table, per-file XOR decryption). It exists only because
// the real as3d_vfs module (engine/src/vfs), with makePakSource() building on
// an SDL_RWops-backed as3d::IStream, is being implemented in parallel in
// another worktree and is not available here.
//
// DO NOT extend this. Once as3d_vfs lands, apps/android_boot should mount
// pak0/pak1/pak2 through as3d::Vfs + as3d::makePakSource() instead, and this
// file (pak_boot.h/.cpp) should be deleted.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "as3d/core.h"

namespace as3d_boot {

struct PakEntry {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint32_t flag = 0; // 1 => body is XOR-encrypted with the pak's key
};

// One opened, fully-loaded pak archive (data\pak*.apk, custom format despite
// the extension; see docs/spec/pak.md). Loads the whole archive into memory,
// which is fine at this size (a few MB to ~17 MB per pak).
class PakArchive {
public:
    // `rwPath` is passed to SDL_RWFromFile: a relative path resolves to the
    // Android asset manager (APK assets/) on Android, and a real filesystem
    // path otherwise. Returns false (and logs) on any failure.
    bool open(const std::string& rwPath);

    std::size_t fileCount() const { return entries_.size(); }

    // `name` must already be normalized (as3d::normalizePath: lower-case,
    // backslash-separated), matching how docs/spec/pak.md stores names.
    bool read(const std::string& name, as3d::Blob& out) const;

private:
    as3d::Blob data_;
    std::unordered_map<std::string, PakEntry> entries_;
    std::uint8_t key_[1024] = {};
};

// Returns the value of the first `name "..."` field in a decrypted
// maps\levels.txt blob (as read via PakArchive::read), or "" if none is
// found. `levels.txt` has no committed spec yet (only pak.md exists), so this
// is a minimal, deliberately tolerant text scan: the first line whose
// trimmed content starts with the token "name" followed by whitespace and a
// double-quoted string.
std::string firstMissionName(const as3d::Blob& levelsTxt);

} // namespace as3d_boot
