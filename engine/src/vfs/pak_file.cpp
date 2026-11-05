// as3d/pak_file.h: the file table of a pak, and a pak writer. Layout: docs/spec/pak.md.
#include "as3d/pak_file.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "as3d/fs_util.h"

namespace as3d {

namespace {

constexpr u8 kMagic[8] = {0x00, 0x00, 0x80, 0x3F, 0x99, 0x99, 0x00, 0x00};
constexpr size_t kKeyOffset = 0x10;
constexpr size_t kKeySize = 1024;
constexpr size_t kBodiesOffset = kKeyOffset + kKeySize; // 0x410
constexpr size_t kEntrySize = 76;
constexpr size_t kNameSize = 64;

u32 readLE32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

void writeLE32(u8* p, u32 v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<u8>((v >> (8 * i)) & 0xFF);
}

void fail(std::string* error, const std::string& msg) {
    if (error) *error = msg;
}

} // namespace

bool readPakTable(IStream& stream, PakTable* out, std::string* error) {
    out->entries.clear();
    out->fileSize = stream.size();
    out->bytesShort = 0;
    u8 head[16];
    if (out->fileSize < kBodiesOffset || !stream.readAt(0, head, sizeof head)) return fail(error, "not a pak archive (too short)"), false;
    if (std::memcmp(head, kMagic, sizeof kMagic) != 0) return fail(error, "not a pak archive (bad header)"), false;
    const u32 tableOffset = readLE32(head + 8);
    const u32 count = readLE32(head + 12);
    std::vector<u8> key(kKeySize);
    if (!stream.readAt(kKeyOffset, key.data(), kKeySize)) return fail(error, "cannot read the pak's key"), false;
    if (tableOffset < kBodiesOffset) return fail(error, "not a pak archive (table inside the header)"), false;
    const std::uint64_t tableEnd = static_cast<std::uint64_t>(tableOffset) + static_cast<std::uint64_t>(count) * kEntrySize;
    std::uint64_t needed = tableEnd;
    // The entries within the file, even of a truncated table.
    const std::uint64_t within =
        tableOffset >= out->fileSize ? 0 : std::min<std::uint64_t>(count, (out->fileSize - tableOffset) / kEntrySize);
    std::vector<u8> table(static_cast<size_t>(within * kEntrySize));
    if (!table.empty() && !stream.readAt(tableOffset, table.data(), table.size())) return fail(error, "cannot read the file table"), false;
    for (size_t i = 0; i < table.size(); ++i) table[i] ^= key[i % kKeySize];
    out->entries.reserve(static_cast<size_t>(within));
    for (std::uint64_t i = 0; i < within; ++i) {
        const u8* e = table.data() + static_cast<size_t>(i) * kEntrySize;
        size_t nameLen = 0;
        while (nameLen < kNameSize && e[nameLen] != 0) ++nameLen;
        PakTableEntry entry;
        entry.name.assign(reinterpret_cast<const char*>(e), nameLen);
        entry.offset = readLE32(e + 64);
        entry.size = readLE32(e + 68);
        entry.flag = readLE32(e + 72);
        needed = std::max(needed, static_cast<std::uint64_t>(entry.offset) + entry.size);
        out->entries.push_back(std::move(entry));
    }
    if (needed > out->fileSize) out->bytesShort = needed - out->fileSize;
    return true;
}

bool readPakTableFile(const std::string& path, PakTable* out, std::string* error) {
    std::unique_ptr<IStream> s = openFileStream(path);
    if (!s) return fail(error, "cannot open " + path), false;
    return readPakTable(*s, out, error);
}

std::string normalizePakEntryName(const std::string& name) {
    std::string n = name;
    for (char& c : n) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        else if (c == '/') c = '\\';
    }
    return n;
}

std::vector<std::string> pakEntryNames(const PakTable& table) {
    std::vector<std::string> names;
    names.reserve(table.entries.size());
    for (const PakTableEntry& e : table.entries) names.push_back(normalizePakEntryName(e.name));
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

bool writePak(const std::string& path, const std::vector<PakWriteEntry>& entries, std::string* error) {
    if (entries.size() > 0xFFFFFFFFull / kEntrySize) return fail(error, "too many entries"), false;
    for (const PakWriteEntry& e : entries)
        if (e.name.empty() || e.name.size() > kNameSize) return fail(error, "entry name '" + e.name + "' is not 1 to 64 bytes"), false;
    const std::string tmp = path + ".tmp";
    FileWriteScope scope;
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return fail(error, "cannot create " + tmp), false;
    auto abort = [&](const std::string& msg) {
        std::fclose(f);
        std::remove(tmp.c_str());
        fail(error, msg);
        return false;
    };
    std::vector<u8> header(kBodiesOffset, 0); // magic, table offset and count (patched below), zero key
    std::memcpy(header.data(), kMagic, sizeof kMagic);
    if (std::fwrite(header.data(), 1, header.size(), f) != header.size()) return abort("cannot write " + tmp);
    std::vector<u8> table(entries.size() * kEntrySize, 0);
    std::uint64_t at = kBodiesOffset;
    std::vector<u8> buf(1 << 16);
    for (size_t i = 0; i < entries.size(); ++i) {
        const PakWriteEntry& e = entries[i];
        std::uint64_t size = 0;
        if (!e.sourcePath.empty()) {
            std::FILE* in = std::fopen(e.sourcePath.c_str(), "rb");
            if (!in) return abort("cannot read " + e.sourcePath);
            for (;;) {
                const size_t n = std::fread(buf.data(), 1, buf.size(), in);
                if (n == 0) break;
                if (std::fwrite(buf.data(), 1, n, f) != n) {
                    std::fclose(in);
                    return abort("cannot write " + tmp);
                }
                size += n;
            }
            const bool readError = std::ferror(in) != 0;
            std::fclose(in);
            if (readError) return abort("cannot read " + e.sourcePath);
        } else if (!e.data.empty()) {
            if (std::fwrite(e.data.data(), 1, e.data.size(), f) != e.data.size()) return abort("cannot write " + tmp);
            size = e.data.size();
        }
        if (at + size > 0xFFFFFFFFull) return abort("the pak would exceed 4 GB");
        u8* t = table.data() + i * kEntrySize;
        std::memcpy(t, e.name.data(), e.name.size());
        writeLE32(t + 64, static_cast<u32>(at));
        writeLE32(t + 68, static_cast<u32>(size));
        writeLE32(t + 72, 0);
        at += size;
    }
    if (!table.empty() && std::fwrite(table.data(), 1, table.size(), f) != table.size()) return abort("cannot write " + tmp);
    // The table offset and the count, now that the bodies are in place.
    u8 counts[8];
    writeLE32(counts, static_cast<u32>(at));
    writeLE32(counts + 4, static_cast<u32>(entries.size()));
    if (std::fseek(f, 8, SEEK_SET) != 0 || std::fwrite(counts, 1, 8, f) != 8) return abort("cannot write " + tmp);
    if (!syncStream(f)) return abort("cannot flush " + tmp);
    if (std::fclose(f) != 0) {
        std::remove(tmp.c_str());
        return fail(error, "cannot close " + tmp), false;
    }
    if (!replaceFile(tmp, path)) {
        std::remove(tmp.c_str());
        return fail(error, "cannot rename " + tmp + " to " + path), false;
    }
    return true;
}

} // namespace as3d
