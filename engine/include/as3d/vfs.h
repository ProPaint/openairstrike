// Virtual file system over pak archives and directories. See docs/spec/pak.md.
// Owned by the orchestrator.
#pragma once

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
};

// Returns null when the stream is not a valid pak.
std::unique_ptr<IFileSource> makePakSource(std::unique_ptr<IStream> stream);
// Serves loose files under a directory, matching names case-insensitively.
std::unique_ptr<IFileSource> makeDirSource(const std::string& osDir);

class Vfs {
public:
    // Sources mounted later take priority over earlier ones.
    void mount(std::unique_ptr<IFileSource> source);
    bool exists(const std::string& path);
    bool read(const std::string& path, Blob& out);
    // Every distinct path, normalised and sorted. Optional prefix such as "scripts\\".
    std::vector<std::string> list(const std::string& prefix = "");
    // Read-ahead of files that are about to be read (IFileSource::prefetch): each path goes to
    // the source that would serve it. The data read is the same as without. Returns the bytes
    // kept; dropPrefetched() frees them.
    size_t prefetch(const std::vector<std::string>& paths);
    void dropPrefetched(const std::vector<std::string>* paths = nullptr);
    // Called with the normalised path of every successful read (null: none); for recording what
    // a level start reads.
    void setReadObserver(std::function<void(const std::string&)> observer) { observer_ = std::move(observer); }

private:
    std::vector<std::unique_ptr<IFileSource>> sources_;
    std::function<void(const std::string&)> observer_;
};

} // namespace as3d
