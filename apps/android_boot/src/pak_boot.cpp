// See pak_boot.h: temporary, to be replaced by engine/src/vfs (as3d_vfs).
#include "pak_boot.h"

#include <SDL.h>

#include <cstring>

namespace as3d_boot {

namespace {

constexpr std::uint8_t kMagic[8] = {0x00, 0x00, 0x80, 0x3F, 0x99, 0x99, 0x00, 0x00};
constexpr std::size_t kKeyOffset = 0x10;
constexpr std::size_t kKeySize = 1024;
constexpr std::size_t kEntrySize = 76;

bool readWholeFile(const std::string& path, as3d::Blob& out) {
    SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw) {
        AS3D_ERROR("pak_boot: SDL_RWFromFile('%s') failed: %s", path.c_str(), SDL_GetError());
        return false;
    }
    Sint64 size = SDL_RWsize(rw);
    if (size < 0) {
        AS3D_ERROR("pak_boot: SDL_RWsize('%s') failed: %s", path.c_str(), SDL_GetError());
        SDL_RWclose(rw);
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    std::size_t got = 0;
    while (got < out.size()) {
        std::size_t chunk = SDL_RWread(rw, out.data() + got, 1, out.size() - got);
        if (chunk == 0) break;
        got += chunk;
    }
    SDL_RWclose(rw);
    if (got != out.size()) {
        AS3D_ERROR("pak_boot: short read on '%s' (%zu/%zu bytes)", path.c_str(), got, out.size());
        return false;
    }
    return true;
}

} // namespace

bool PakArchive::open(const std::string& rwPath) {
    entries_.clear();
    if (!readWholeFile(rwPath, data_)) return false;

    as3d::ByteReader header(data_);
    std::uint8_t magic[8];
    header.readBytes(magic, sizeof(magic));
    if (header.failed() || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        AS3D_ERROR("pak_boot: '%s' bad magic", rwPath.c_str());
        return false;
    }
    std::uint32_t tableOffset = header.readU32();
    std::uint32_t count = header.readU32();
    if (header.failed() || kKeyOffset + kKeySize > data_.size() ||
        static_cast<std::uint64_t>(tableOffset) + static_cast<std::uint64_t>(count) * kEntrySize >
            data_.size()) {
        AS3D_ERROR("pak_boot: '%s' truncated header/table", rwPath.c_str());
        return false;
    }
    std::memcpy(key_, data_.data() + kKeyOffset, kKeySize);

    entries_.reserve(count);
    const std::uint8_t* tableEnc = data_.data() + tableOffset;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint8_t entry[kEntrySize];
        for (std::size_t b = 0; b < kEntrySize; ++b) {
            std::size_t tableByteIndex = static_cast<std::size_t>(i) * kEntrySize + b;
            entry[b] = tableEnc[tableByteIndex] ^ key_[tableByteIndex % kKeySize];
        }
        as3d::ByteReader er(entry, kEntrySize);
        std::string name = er.readFixedString(64);
        PakEntry pe;
        pe.offset = er.readU32();
        pe.size = er.readU32();
        pe.flag = er.readU32();
        if (static_cast<std::uint64_t>(pe.offset) + pe.size > data_.size()) {
            AS3D_WARN("pak_boot: '%s' entry '%s' out of bounds, skipping", rwPath.c_str(), name.c_str());
            continue;
        }
        entries_[as3d::normalizePath(name)] = pe;
    }
    AS3D_INFO("pak_boot: opened '%s': %zu entries", rwPath.c_str(), entries_.size());
    return true;
}

bool PakArchive::read(const std::string& name, as3d::Blob& out) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) return false;
    const PakEntry& e = it->second;
    out.resize(e.size);
    std::memcpy(out.data(), data_.data() + e.offset, e.size);
    if (e.flag) {
        for (std::size_t j = 0; j < out.size(); ++j) {
            out[j] ^= key_[j % kKeySize];
        }
    }
    return true;
}

std::string firstMissionName(const as3d::Blob& levelsTxt) {
    std::string text(reinterpret_cast<const char*>(levelsTxt.data()), levelsTxt.size());
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::size_t start = pos;
        while (start < eol && (text[start] == ' ' || text[start] == '\t' || text[start] == '\r')) {
            ++start;
        }
        static const std::string kToken = "name";
        if (eol - start > kToken.size() && text.compare(start, kToken.size(), kToken) == 0 &&
            (text[start + kToken.size()] == ' ' || text[start + kToken.size()] == '\t')) {
            std::size_t q1 = text.find('"', start + kToken.size());
            if (q1 != std::string::npos && q1 < eol) {
                std::size_t q2 = text.find('"', q1 + 1);
                if (q2 != std::string::npos && q2 <= eol) {
                    return text.substr(q1 + 1, q2 - q1 - 1);
                }
            }
        }
        pos = eol + 1;
    }
    return "";
}

} // namespace as3d_boot
