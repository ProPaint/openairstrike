// as3d::openPlatformStream and readPlatformFile for the 3DS: plain stdio on sdmc:/ (the SDL_RWops
// of the desktop backend is not needed; openFileStream already works there).
#include <3ds.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "as3d/core.h"
#include "as3d/platform.h"
#include "ctr_context.h"

namespace as3d {

namespace ctr {
namespace {
StreamReadStats g_reads;
size_t g_cacheBytes = 0;
} // namespace
const StreamReadStats& streamReadStats() { return g_reads; }
void setStreamBlockCache(size_t bytes) { g_cacheBytes = bytes; }
} // namespace ctr

namespace {

// A pak read through the FS service directly (FSFILE_Read at an offset): no stdio buffer, no
// seek per read (the game's paks are read entry by entry, docs/3ds-port.md "Hardware tuning").
// Counts its reads (ctr_context.h streamReadStats: the load profile's read time).
//
// Block cache (setStreamBlockCache, off by default: on the emulator it read 4x the bytes for few hits): the paks' entries are small and
// neighbours are read together (a model, its skin, the next definition's), so reads go through
// 64 KB aligned blocks kept in a small LRU; a read larger than a block goes straight to the file.
class FsStream final : public IStream {
public:
    static constexpr size_t kBlock = 64 * 1024;

    FsStream(Handle h, size_t size) : h_(h), size_(size) {
        const size_t n = ctr::g_cacheBytes / kBlock;
        blocks_.resize(n);
    }
    ~FsStream() override { FSFILE_Close(h_); }
    size_t size() override { return size_; }
    bool readAt(size_t offset, void* out, size_t n) override {
        if (offset > size_ || n > size_ - offset) return false;
        u8* dst = static_cast<u8*>(out);
        if (blocks_.empty() || n > kBlock) return readFile(offset, dst, n);
        while (n > 0) {
            const size_t first = offset / kBlock * kBlock;
            const Block* b = block(first);
            if (!b) return false;
            const size_t in = offset - first;
            const size_t take = std::min(n, b->data.size() - in);
            if (take == 0) return false;
            std::memcpy(dst, b->data.data() + in, take);
            dst += take;
            offset += take;
            n -= take;
        }
        return true;
    }

private:
    struct Block {
        size_t start = ~static_cast<size_t>(0);
        unsigned lastUse = 0;
        std::vector<u8> data;
    };

    bool readFile(size_t offset, u8* dst, size_t n) {
        const u64 t0 = svcGetSystemTick();
        size_t done = 0;
        while (done < n) {
            u32 got = 0;
            if (R_FAILED(FSFILE_Read(h_, &got, offset + done, dst + done, static_cast<u32>(n - done))) || got == 0) break;
            done += got;
        }
        ctr::StreamReadStats& st = ctr::g_reads;
        st.ticks += svcGetSystemTick() - t0;
        st.bytes += done;
        ++st.reads;
        return done == n;
    }

    const Block* block(size_t start) {
        Block* victim = &blocks_[0];
        for (Block& b : blocks_) {
            if (b.start == start) {
                b.lastUse = ++use_;
                ++ctr::g_reads.cacheHits;
                return &b;
            }
            if (b.lastUse < victim->lastUse) victim = &b;
        }
        const size_t len = std::min(kBlock, size_ - start);
        victim->data.resize(len);
        victim->start = ~static_cast<size_t>(0);
        if (!readFile(start, victim->data.data(), len)) return nullptr;
        victim->start = start;
        victim->lastUse = ++use_;
        return victim;
    }

    Handle h_;
    size_t size_;
    std::vector<Block> blocks_;
    unsigned use_ = 0;
};

std::unique_ptr<IStream> openFsStream(const std::string& path) {
    static const char kPrefix[] = "sdmc:";
    if (path.compare(0, sizeof kPrefix - 1, kPrefix) != 0) return nullptr;
    const std::string inArchive = path.substr(sizeof kPrefix - 1);
    Handle h = 0;
    if (R_FAILED(FSUSER_OpenFileDirectly(&h, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
                                         fsMakePath(PATH_ASCII, inArchive.c_str()), FS_OPEN_READ, 0)))
        return nullptr;
    u64 size = 0;
    if (R_FAILED(FSFILE_GetSize(h, &size)) || size > 0x7FFFFFFFull) {
        FSFILE_Close(h);
        return nullptr;
    }
    return std::unique_ptr<IStream>(new FsStream(h, static_cast<size_t>(size)));
}

} // namespace

std::unique_ptr<IStream> openPlatformStream(const std::string& path) {
    std::unique_ptr<IStream> s = openFsStream(path);
    if (!s) s = openFileStream(path);
    if (!s) AS3D_ERROR("openPlatformStream: cannot open '%s'", path.c_str());
    return s;
}

bool readPlatformFile(const std::string& path, Blob& out) {
    out.clear();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    bool ok = std::fseek(f, 0, SEEK_END) == 0;
    long size = ok ? std::ftell(f) : -1;
    ok = ok && size >= 0 && size <= (64 << 20) && std::fseek(f, 0, SEEK_SET) == 0;
    if (ok) {
        out.resize(static_cast<size_t>(size));
        ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    }
    std::fclose(f);
    if (!ok) out.clear();
    return ok;
}

} // namespace as3d
