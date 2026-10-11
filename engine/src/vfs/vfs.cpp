#include "as3d/vfs.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "as3d/load_stats.h"

namespace as3d {

namespace {
double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
} // namespace

// The sync the sources see: the threading's locks, the waits timed and counted (the main
// thread's: the reads that found their file pending).
class Vfs::SyncAdapter final : public PrefetchSync {
public:
    explicit SyncAdapter(PrefetchThreading& t) : t_(t) {}
    void lock() override { t_.lock(); }
    void unlock() override { t_.unlock(); }
    void wait() override {
        const auto t0 = std::chrono::steady_clock::now();
        t_.wait();
        waitMs += msSince(t0);
    }
    void notifyAll() override { t_.notifyAll(); }
    void lockStream() override { t_.lockStream(); }
    void unlockStream() override { t_.unlockStream(); }
    void waited() override { ++waits; }
    void reset() {
        waitMs = 0.0;
        waits = 0;
    }
    double waitMs = 0.0;
    unsigned waits = 0;

private:
    PrefetchThreading& t_;
};

Vfs::Vfs() = default;

Vfs::~Vfs() {
    prefetchJoin();
    for (auto& s : sources_) s->setPrefetchSync(nullptr);
}

Vfs::Vfs(Vfs&& other) noexcept { *this = std::move(other); }

Vfs& Vfs::operator=(Vfs&& other) noexcept {
    if (this == &other) return *this;
    prefetchJoin();
    other.prefetchJoin();
    sources_ = std::move(other.sources_);
    observer_ = std::move(other.observer_);
    threading_ = std::move(other.threading_);
    sync_ = std::move(other.sync_); // the sources keep pointing at it: it moved with them
    steps_ = std::move(other.steps_);
    stats_ = other.stats_;
    result_ = other.result_;
    readerRunning_ = false;
    readerStop_ = false;
    return *this;
}

void Vfs::mount(std::unique_ptr<IFileSource> source) {
    if (!source) return;
    source->setPrefetchSync(sync_.get());
    sources_.push_back(std::move(source));
}

bool Vfs::exists(const std::string& path) {
    const std::string p = normalizePath(path);
    for (auto it = sources_.rbegin(); it != sources_.rend(); ++it) {
        if ((*it)->exists(p)) return true;
    }
    return false;
}

bool Vfs::read(const std::string& path, Blob& out) {
    const std::string p = normalizePath(path);
    for (auto it = sources_.rbegin(); it != sources_.rend(); ++it) {
        if ((*it)->read(p, out)) {
            LoadStats::fileRead(out.size(), p.c_str());
            if (observer_) observer_(p);
            return true;
        }
    }
    return false;
}

size_t Vfs::prefetch(const std::vector<std::string>& paths) {
    // Each path to the source that serves it (the last mounted one that has it).
    std::vector<std::vector<std::string>> per(sources_.size());
    for (const std::string& path : paths) {
        const std::string p = normalizePath(path);
        for (size_t i = sources_.size(); i-- > 0;) {
            if (sources_[i]->exists(p)) {
                per[i].push_back(p);
                break;
            }
        }
    }
    size_t bytes = 0;
    for (size_t i = 0; i < sources_.size(); ++i)
        if (!per[i].empty()) bytes += sources_[i]->prefetch(per[i]);
    return bytes;
}

void Vfs::dropPrefetched(const std::vector<std::string>* paths) {
    if (!paths) {
        prefetchJoin();
        for (auto& s : sources_) s->dropPrefetched(nullptr);
        return;
    }
    std::vector<std::string> norm;
    norm.reserve(paths->size());
    for (const std::string& p : *paths) norm.push_back(normalizePath(p));
    for (auto& s : sources_) s->dropPrefetched(&norm);
}

void Vfs::setPrefetchThreading(std::unique_ptr<PrefetchThreading> threading) {
    prefetchJoin();
    sync_.reset();
    threading_ = std::move(threading);
    if (threading_) sync_.reset(new SyncAdapter(*threading_));
    for (auto& s : sources_) s->setPrefetchSync(sync_.get());
}

void Vfs::prefetchAsync(const std::vector<std::string>& paths) {
    prefetchJoin();
    stats_ = PrefetchStats();
    result_ = ReaderResult();
    if (sync_) sync_->reset();
    steps_.clear();
    // Each path to the source that serves it, with its index in the list (the order of use).
    std::vector<std::vector<std::string>> per(sources_.size());
    std::vector<std::vector<size_t>> index(sources_.size());
    for (size_t k = 0; k < paths.size(); ++k) {
        const std::string p = normalizePath(paths[k]);
        for (size_t i = sources_.size(); i-- > 0;) {
            if (sources_[i]->exists(p)) {
                per[i].push_back(p);
                index[i].push_back(k);
                ++stats_.files;
                break;
            }
        }
    }
    for (size_t i = 0; i < sources_.size(); ++i) {
        if (per[i].empty()) continue;
        std::vector<size_t> firstUse;
        const size_t n = sources_[i]->prefetchPlan(per[i], &firstUse);
        for (size_t j = 0; j < n; ++j) {
            const size_t use = j < firstUse.size() && firstUse[j] < index[i].size() ? index[i][firstUse[j]] : 0;
            steps_.push_back(PrefetchStep{i, j, use});
        }
    }
    // The runs in the order of their first use (a stable sort: a source's own order for ties).
    std::stable_sort(steps_.begin(), steps_.end(),
                     [](const PrefetchStep& a, const PrefetchStep& b) { return a.firstUse < b.firstUse; });
    stats_.steps = steps_.size();
    readerStop_ = false;
    if (!steps_.empty() && threading_ && threading_->start(&Vfs::readerEntry, this)) {
        stats_.threaded = true;
        readerRunning_ = true;
        return;
    }
    readerLoop(); // no thread: on the caller, as prefetch()
}

void Vfs::readerEntry(void* self) { static_cast<Vfs*>(self)->readerLoop(); }

void Vfs::readerLoop() {
    const auto t0 = std::chrono::steady_clock::now();
    size_t bytes = 0, run = 0;
    for (const PrefetchStep& s : steps_) {
        if (readerStop_.load()) break;
        bytes += sources_[s.source]->prefetchStep(s.step);
        ++run;
    }
    // Whatever was not read is no longer pending: a read of it goes to the file.
    for (auto& s : sources_) s->prefetchEnd();
    result_.bytes = bytes;
    result_.stepsRun = run;
    result_.readMs = msSince(t0);
    result_.done = true; // read by the owner after the join (or inline): never concurrently
}

void Vfs::prefetchJoin() {
    if (!readerRunning_) return;
    readerStop_ = true;
    threading_->join();
    readerRunning_ = false;
    readerStop_ = false;
}

Vfs::PrefetchStats Vfs::prefetchStats() const {
    PrefetchStats s = stats_;
    if (!readerRunning_) { // the reader's fields only once it is joined (or ran inline)
        s.stepsRun = result_.stepsRun;
        s.bytes = result_.bytes;
        s.readMs = result_.readMs;
        s.done = result_.done;
    }
    if (sync_) {
        s.waitMs = sync_->waitMs;
        s.waits = sync_->waits;
    }
    return s;
}

const char* Vfs::prefetchThreadDescription() const { return threading_ ? threading_->description() : "none"; }

std::vector<std::string> Vfs::list(const std::string& prefix) {
    const std::string p = normalizePath(prefix);
    std::vector<std::string> all;
    for (auto& src : sources_) {
        std::vector<std::string> names;
        src->list(names);
        for (auto& n : names) {
            if (n.compare(0, p.size(), p) == 0) all.push_back(std::move(n));
        }
    }
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    return all;
}

} // namespace as3d
