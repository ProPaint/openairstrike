// Virtual file system over pak archives and directories. See docs/spec/pak.md.
// Owned by the orchestrator.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"

namespace as3d {

// Random-access byte source. Backends: stdio now, Android assets through the
// platform layer later.
class IStream {
public:
    virtual ~IStream() = default;
    virtual size_t size() = 0;
    // Reads exactly n bytes at offset. Returns false on any short read.
    virtual bool readAt(size_t offset, void* out, size_t n) = 0;
};

std::unique_ptr<IStream> openFileStream(const std::string& osPath);
std::unique_ptr<IStream> makeMemoryStream(Blob data);

class PrefetchSync;

// A mounted collection of files. Paths given to a source are already normalised.
class IFileSource {
public:
    virtual ~IFileSource() = default;
    virtual bool exists(const std::string& path) = 0;
    virtual bool read(const std::string& path, Blob& out) = 0;
    virtual void list(std::vector<std::string>& out) = 0;
    // Optional read-ahead (the 3DS, where each read of the SD card costs milliseconds): read the
    // files of `paths` this source holds in as few large reads as it can and keep them until
    // dropPrefetched() (a file is often read more than once). Returns the bytes kept. The default does nothing.
    virtual size_t prefetch(const std::vector<std::string>& paths) {
        (void)paths;
        return 0;
    }
    // Frees the prefetched files (`paths` null: all of them; else those of the list).
    virtual void dropPrefetched(const std::vector<std::string>* paths = nullptr) { (void)paths; }

    // Read-ahead on another thread (Vfs::prefetchAsync; the 3DS level start, docs/3ds-port.md
    // "Prefetch overlap (round 4, R4.10)"). A source that keeps read-ahead data takes part:
    // - setPrefetchSync: from then on read(), prefetch(), dropPrefetched() and the steps below
    //   guard the kept data with `sync` (its data lock around the map, its stream lock around
    //   the file) and a read() of a pending file waits on it; null = no guards (one thread).
    // - prefetchPlan: plans the read-ahead of `paths` (its order = the order they will be read)
    //   as steps, each one read of a run of entries, and marks the files pending; `firstUse`
    //   (optional) gets, per step, the index in `paths` of the first file it brings, so that the
    //   caller runs the steps in the readers' order. Returns the step count.
    // - prefetchStep(i): runs step i (any thread): reads its run and keeps its files, which are
    //   no longer pending whether the read succeeded or not. Returns the bytes kept.
    // - prefetchEnd: forgets the plan; a still pending file is read from the file by read().
    virtual void setPrefetchSync(PrefetchSync* sync) { (void)sync; }
    virtual size_t prefetchPlan(const std::vector<std::string>& paths, std::vector<size_t>* firstUse) {
        (void)paths;
        (void)firstUse;
        return 0;
    }
    virtual size_t prefetchStep(size_t i) {
        (void)i;
        return 0;
    }
    virtual void prefetchEnd() {}
};

// What a source needs to share its read-ahead data between the reader thread and its readers
// (IFileSource::setPrefetchSync): a data lock with a condition, and a stream lock (the pak file
// is read by one thread at a time: the reader's runs, a reader's miss).
class PrefetchSync {
public:
    virtual ~PrefetchSync() = default;
    virtual void lock() = 0;      // the data lock
    virtual void unlock() = 0;
    virtual void wait() = 0;      // with the data lock held: sleeps until notifyAll(); holds it again on return
    virtual void notifyAll() = 0; // with the data lock held
    virtual void lockStream() = 0;
    virtual void unlockStream() = 0;
    // A read that waited for a pending file calls it once, when it has the file (statistics).
    virtual void waited() {}
};

// The thread of Vfs::prefetchAsync with its locks, from the platform: makeStdPrefetchThreading()
// (std::thread; the desktop and the tests), the 3DS's libctru one (engine/src/platform/ctr,
// as3d::ctr::makePrefetchThreading). The vfs itself stays platform-neutral.
class PrefetchThreading : public PrefetchSync {
public:
    // Runs fn(arg) on a new thread; false when none could be made (the prefetch then runs on the
    // caller, as Vfs::prefetch does). One thread at a time: join() before the next start().
    virtual bool start(void (*fn)(void*), void* arg) = 0;
    virtual void join() = 0;
    // For the log: where the thread runs ("std::thread", "core 1 prio 0x2F").
    virtual const char* description() const = 0;
};
// Null where there is no std::thread (the 3DS, the web).
std::unique_ptr<PrefetchThreading> makeStdPrefetchThreading();

// Returns null when the stream is not a valid pak.
std::unique_ptr<IFileSource> makePakSource(std::unique_ptr<IStream> stream);
// Serves loose files under a directory, matching names case-insensitively.
std::unique_ptr<IFileSource> makeDirSource(const std::string& osDir);

class Vfs {
public:
    Vfs();
    ~Vfs(); // joins the read-ahead thread first
    Vfs(const Vfs&) = delete;
    Vfs& operator=(const Vfs&) = delete;
    Vfs(Vfs&& other) noexcept; // the other's reader joined first
    Vfs& operator=(Vfs&& other) noexcept;

    // Sources mounted later take priority over earlier ones.
    void mount(std::unique_ptr<IFileSource> source);
    bool exists(const std::string& path);
    bool read(const std::string& path, Blob& out);
    // Every distinct path, normalised and sorted. Optional prefix such as "scripts\\".
    std::vector<std::string> list(const std::string& prefix = "");
    // Read-ahead of files that are about to be read (IFileSource::prefetch): each path goes to
    // the source that would serve it. The data read is the same as without. Returns the bytes
    // kept; dropPrefetched() frees them (dropPrefetched(nullptr) joins the read-ahead thread first).
    size_t prefetch(const std::vector<std::string>& paths);
    void dropPrefetched(const std::vector<std::string>* paths = nullptr);
    // Called with the normalised path of every successful read (null: none); for recording what
    // a level start reads.
    void setReadObserver(std::function<void(const std::string&)> observer) { observer_ = std::move(observer); }

    // Read-ahead on another thread (docs/3ds-port.md "Prefetch overlap (round 4, R4.10)").
    // setPrefetchThreading: the platform's thread for prefetchAsync (null, the default: it reads
    // on the caller, as prefetch does). prefetchAsync(paths): the files of `paths`, in their
    // order (a level start's read plan lists them in the order the start reads them), are read
    // into memory by the thread, in runs as prefetch does but in the order of their first use,
    // while the caller goes on; a read of a listed file not yet in memory waits for it (the same
    // bytes either way); a file outside the list reads its source directly, without waiting.
    // prefetchJoin() stops the thread (the current run finishes, the rest is dropped):
    // dropPrefetched(nullptr), the next prefetchAsync, setPrefetchThreading and the destructor
    // call it. prefetchStats(): the last prefetchAsync's, complete after prefetchJoin().
    void setPrefetchThreading(std::unique_ptr<PrefetchThreading> threading);
    void prefetchAsync(const std::vector<std::string>& paths);
    void prefetchJoin();
    struct PrefetchStats {
        size_t files = 0;      // paths a source holds
        size_t steps = 0;      // runs planned
        size_t stepsRun = 0;   // runs read (fewer after an early prefetchJoin)
        size_t bytes = 0;      // bytes kept
        double readMs = 0.0;   // the reader's time from its start to its end (the caller's, synchronously)
        double waitMs = 0.0;   // how long reads waited for pending files
        unsigned waits = 0;    // how many reads waited
        bool threaded = false; // read by the thread (false: on the caller)
        bool done = false;     // the reader ended
    };
    PrefetchStats prefetchStats() const;
    const char* prefetchThreadDescription() const; // the threading's, "none" without one

private:
    struct PrefetchStep {
        size_t source = 0;
        size_t step = 0;
        size_t firstUse = 0; // the index in prefetchAsync's list of the first file it brings
    };
    class SyncAdapter; // forwards to threading_, counts the waits
    static void readerEntry(void* self);
    void readerLoop();

    std::vector<std::unique_ptr<IFileSource>> sources_;
    std::function<void(const std::string&)> observer_;
    std::unique_ptr<PrefetchThreading> threading_;
    std::unique_ptr<SyncAdapter> sync_;
    std::vector<PrefetchStep> steps_;
    PrefetchStats stats_;        // the planning fields (files, steps, threaded): the caller's
    struct ReaderResult {        // the reader's: read by the owner once the reader is joined
        size_t stepsRun = 0, bytes = 0;
        double readMs = 0.0;
        bool done = false;
    } result_;
    bool readerRunning_ = false; // a thread started and not yet joined
    std::atomic<bool> readerStop_{false};
};

} // namespace as3d
