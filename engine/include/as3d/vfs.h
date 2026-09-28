// Virtual file system over pak archives and directories. See docs/spec/pak.md.
// Owned by the orchestrator.
#pragma once

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

private:
    std::vector<std::unique_ptr<IFileSource>> sources_;
};

} // namespace as3d
