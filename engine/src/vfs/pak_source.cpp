// PAK archive source. See docs/spec/pak.md. Reference: tools/paktool.py.
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "as3d/vfs.h"

namespace as3d {
namespace {

constexpr u8 kMagic[8] = {0x00, 0x00, 0x80, 0x3F, 0x99, 0x99, 0x00, 0x00};
constexpr size_t kKeyOffset = 0x10;
constexpr size_t kKeySize = 1024;
constexpr size_t kEntrySize = 76;
constexpr size_t kNameSize = 64;

u32 readLE32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

struct PakEntry {
    u32 offset;
    u32 size;
    u32 flag;
};

class PakSource : public IFileSource {
public:
    PakSource(std::unique_ptr<IStream> stream, std::array<u8, kKeySize> key,
              std::unordered_map<std::string, PakEntry> entries)
        : stream_(std::move(stream)), key_(key), entries_(std::move(entries)) {}

    bool exists(const std::string& path) override {
        return entries_.find(path) != entries_.end();
    }

    bool read(const std::string& path, Blob& out) override {
        auto it = entries_.find(path);
        if (it == entries_.end()) return false;
        const PakEntry& e = it->second;
        out.resize(e.size);
        if (e.size != 0 && !stream_->readAt(e.offset, out.data(), e.size)) {
            out.clear();
            return false;
        }
        if (e.flag) {
            for (size_t j = 0; j < out.size(); ++j) out[j] ^= key_[j % kKeySize];
        }
        return true;
    }

    void list(std::vector<std::string>& out) override {
        out.reserve(out.size() + entries_.size());
        for (const auto& kv : entries_) out.push_back(kv.first);
    }

private:
    std::unique_ptr<IStream> stream_;
    std::array<u8, kKeySize> key_;
    std::unordered_map<std::string, PakEntry> entries_;
};

} // namespace

std::unique_ptr<IFileSource> makePakSource(std::unique_ptr<IStream> stream) {
    if (!stream) return nullptr;

    u8 magic[8];
    if (!stream->readAt(0, magic, sizeof magic)) return nullptr;
    if (std::memcmp(magic, kMagic, sizeof kMagic) != 0) return nullptr;

    u8 header[8];
    if (!stream->readAt(8, header, sizeof header)) return nullptr;
    u32 tableOffset = readLE32(header);
    u32 count = readLE32(header + 4);

    std::array<u8, kKeySize> key{};
    if (!stream->readAt(kKeyOffset, key.data(), kKeySize)) return nullptr;

    const size_t streamSize = stream->size();
    const std::uint64_t tableBytes = static_cast<std::uint64_t>(count) * kEntrySize;
    const std::uint64_t tableEnd = static_cast<std::uint64_t>(tableOffset) + tableBytes;
    if (tableEnd > streamSize) return nullptr;

    std::vector<u8> table(static_cast<size_t>(tableBytes));
    if (!table.empty() && !stream->readAt(tableOffset, table.data(), table.size())) {
        return nullptr;
    }
    for (size_t i = 0; i < table.size(); ++i) table[i] ^= key[i % kKeySize];

    std::unordered_map<std::string, PakEntry> entries;
    entries.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        const u8* e = table.data() + static_cast<size_t>(i) * kEntrySize;
        size_t nameLen = 0;
        while (nameLen < kNameSize && e[nameLen] != 0) ++nameLen;
        std::string name(reinterpret_cast<const char*>(e), nameLen);

        u32 offset = readLE32(e + 64);
        u32 size = readLE32(e + 68);
        u32 flag = readLE32(e + 72);

        const std::uint64_t bodyEnd = static_cast<std::uint64_t>(offset) + size;
        if (bodyEnd > streamSize) {
            AS3D_WARN("pak: entry '%s' out of bounds, skipped", name.c_str());
            continue;
        }

        entries[normalizePath(name)] = PakEntry{offset, size, flag};
    }

    return std::unique_ptr<IFileSource>(
        new PakSource(std::move(stream), key, std::move(entries)));
}

} // namespace as3d
