// PAK archive source. See docs/spec/pak.md. Reference: tools/paktool.py.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <unordered_set>
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

// The read-ahead (prefetch, prefetchPlan): the entries sorted by offset and read in runs,
// neighbours closer than kGap read together (the bytes between them are read and dropped), a
// run stops at kMaxRun. The runs of an asynchronous plan (docs/3ds-port.md "Prefetch overlap
// (round 4, R4.10)") are read by the reader thread while the owner reads: the data lock of the
// sync guards prefetched_ and pending_, the stream lock the file (one reader of it at a time).
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
        if (takePrefetched(path, out)) { // read ahead: in memory, encrypted as in the file
            decrypt(e, out);
            return true;
        }
        out.resize(e.size);
        bool ok = true;
        if (e.size != 0) {
            lockStream();
            ok = stream_->readAt(e.offset, out.data(), e.size);
            unlockStream();
        }
        if (!ok) {
            out.clear();
            return false;
        }
        decrypt(e, out);
        return true;
    }

    void list(std::vector<std::string>& out) override {
        out.reserve(out.size() + entries_.size());
        for (const auto& kv : entries_) out.push_back(kv.first);
    }

    size_t prefetch(const std::vector<std::string>& paths) override {
        std::vector<Run> runs;
        lock();
        planRuns(paths, runs, nullptr);
        unlock();
        size_t kept = 0;
        for (Run& r : runs) kept += readRun(r);
        return kept;
    }

    void dropPrefetched(const std::vector<std::string>* paths) override {
        lock();
        if (!paths) {
            prefetched_.clear();
            prefetchedBytes_ = 0;
        } else {
            for (const std::string& p : *paths) {
                auto it = prefetched_.find(p);
                if (it == prefetched_.end()) continue;
                prefetchedBytes_ -= it->second.size();
                prefetched_.erase(it);
            }
        }
        unlock();
    }

    void setPrefetchSync(PrefetchSync* sync) override { sync_ = sync; }

    size_t prefetchPlan(const std::vector<std::string>& paths, std::vector<size_t>* firstUse) override {
        lock();
        steps_.clear();
        planRuns(paths, steps_, firstUse);
        for (const Run& r : steps_)
            for (const Want& w : r.files) pending_.insert(*w.path);
        unlock();
        return steps_.size();
    }

    size_t prefetchStep(size_t i) override {
        if (i >= steps_.size()) return 0;
        return readRun(steps_[i]);
    }

    void prefetchEnd() override {
        lock();
        steps_.clear();
        const bool woken = !pending_.empty();
        pending_.clear();
        if (woken && sync_) sync_->notifyAll();
        unlock();
    }

private:
    static constexpr u32 kGap = 64 * 1024;
    static constexpr u32 kMaxRun = 2u << 20;

    struct Want {
        const std::string* path;
        PakEntry e;
        size_t index; // in the list given
    };
    struct Run {
        u32 start = 0, end = 0;
        std::vector<Want> files;
    };

    void lock() {
        if (sync_) sync_->lock();
    }
    void unlock() {
        if (sync_) sync_->unlock();
    }
    void lockStream() {
        if (sync_) sync_->lockStream();
    }
    void unlockStream() {
        if (sync_) sync_->unlockStream();
    }

    void decrypt(const PakEntry& e, Blob& b) const {
        if (!e.flag) return;
        for (size_t j = 0; j < b.size(); ++j) b[j] ^= key_[j % kKeySize];
    }

    // The file from memory when it was read ahead (a copy: it is kept until dropPrefetched, a
    // file is often read twice, a skin and its shadow). A pending file (its run not read yet)
    // waits for it; a file outside the plan returns false at once.
    bool takePrefetched(const std::string& path, Blob& out) {
        if (!sync_) {
            auto pf = prefetched_.find(path);
            if (pf == prefetched_.end()) return false;
            out = pf->second;
            return true;
        }
        bool waited = false, hit = false;
        sync_->lock();
        for (;;) {
            auto pf = prefetched_.find(path);
            if (pf != prefetched_.end()) {
                out = pf->second;
                hit = true;
                break;
            }
            if (pending_.find(path) == pending_.end()) break;
            waited = true;
            sync_->wait();
        }
        sync_->unlock();
        if (waited) sync_->waited();
        return hit;
    }

    // The runs of `paths` (those this pak holds, not read ahead or pending already), in offset
    // order; `firstUse` gets each run's smallest index in `paths`. With the data lock held.
    void planRuns(const std::vector<std::string>& paths, std::vector<Run>& runs, std::vector<size_t>* firstUse) {
        std::vector<Want> want;
        want.reserve(paths.size());
        for (size_t k = 0; k < paths.size(); ++k) {
            const std::string& p = paths[k];
            auto it = entries_.find(p);
            if (it == entries_.end() || it->second.size == 0 || prefetched_.count(p) || pending_.count(p)) continue;
            want.push_back(Want{&it->first, it->second, k});
        }
        std::sort(want.begin(), want.end(), [](const Want& a, const Want& b) { return a.e.offset < b.e.offset; });
        if (firstUse) firstUse->clear();
        for (size_t i = 0; i < want.size();) {
            Run r;
            r.start = want[i].e.offset;
            r.end = r.start + want[i].e.size;
            size_t j = i + 1;
            while (j < want.size() && want[j].e.offset <= r.end + kGap &&
                   std::max(r.end, want[j].e.offset + want[j].e.size) - r.start <= kMaxRun) {
                r.end = std::max(r.end, want[j].e.offset + want[j].e.size);
                ++j;
            }
            size_t use = want[i].index;
            for (size_t k = i; k < j; ++k) {
                r.files.push_back(want[k]);
                use = std::min(use, want[k].index);
            }
            if (firstUse) firstUse->push_back(use);
            runs.push_back(std::move(r));
            i = j;
        }
    }

    // Reads a run (the stream lock around the file) and keeps its files (the data lock around the
    // map); its files are no longer pending whether the read succeeded or not.
    size_t readRun(const Run& r) {
        Blob run(r.end - r.start);
        lockStream();
        const bool ok = stream_->readAt(r.start, run.data(), run.size());
        unlockStream();
        size_t kept = 0;
        lock();
        for (const Want& w : r.files) {
            if (ok && !prefetched_.count(*w.path)) {
                const PakEntry& e = w.e;
                Blob b(run.begin() + (e.offset - r.start), run.begin() + (e.offset - r.start + e.size));
                kept += b.size();
                prefetched_[*w.path] = std::move(b);
            }
            pending_.erase(*w.path);
        }
        prefetchedBytes_ += kept;
        if (sync_) sync_->notifyAll();
        unlock();
        return kept;
    }

    std::unordered_map<std::string, Blob> prefetched_;
    std::unordered_set<std::string> pending_; // planned (prefetchPlan), not read yet
    std::vector<Run> steps_;                  // the asynchronous plan's runs
    size_t prefetchedBytes_ = 0;
    PrefetchSync* sync_ = nullptr;
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
