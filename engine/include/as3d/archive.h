// Reading the player's archives and installers (docs/spec/archive.md, docs/spec/issues/170
// section 4): one way for the importer (as3d/game_import.h) to list and extract the files of
// any container a game download comes in. Over libarchive (zip, RAR 4 and 5, 7z, tar, CAB,
// ISO 9660; engine/src/formats/archive.cmake builds it from source with liblzma and zlib) and
// an Inno Setup reader of our own (engine/src/formats/archive/inno_reader.cpp). A CAB, 7z, RAR
// or zip appended to a Windows executable (a self-extractor) is opened at its offset. NSIS
// installers and plain executables are recognised and refused with a message that says so.
// Not built on the 3DS (AS3D_WITH_ARCHIVES off): the console never imports.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "as3d/vfs.h"

namespace as3d {

enum class ArchiveKind {
    Unknown,
    Zip,
    Rar,
    SevenZip,
    Cab,
    Tar,
    Iso,
    Inno, // an Inno Setup installer (the loader's offset table found and checked)
    Nsis, // a Nullsoft installer: recognised, not opened
    Pe,   // a Windows executable with nothing this module opens
};
const char* archiveKindName(ArchiveKind kind); // "zip", "rar", "7z", "cab", "tar", "iso", "inno", "nsis", "pe", "unknown"

// What a file is, from its signatures: the head bytes of a plain container; for a Windows
// executable (MZ) the Inno Setup loader table (anywhere in the stub, with its CRC), the NSIS
// marker, or a container signature at the start of the executable's overlay (the bytes after
// its last section) or a zip end record at its end. `offset` (optional) receives where the
// container starts (0 for a plain file; the overlay's for a self-extractor).
ArchiveKind archiveKind(IStream& stream, std::uint64_t* offset = nullptr);
ArchiveKind archiveKind(const std::string& path, std::uint64_t* offset = nullptr);
// From bytes already in memory (a whole small file, or a head: a kind that needs the end of
// the file (self-extractors) is reported as Pe when `complete` is false).
ArchiveKind archiveKind(const std::uint8_t* data, size_t n, bool complete, std::uint64_t* offset = nullptr);

struct ArchiveEntry {
    std::string name;       // '/' separated, no leading '/'; an Inno Setup entry keeps its {app}/ prefix
    std::uint64_t size = 0; // uncompressed; 0 when the container does not record it
    size_t index = 0;       // position in entries()
    bool isDir = false;
};

// Receives the uncompressed bytes in order; false stops the extraction (which then fails).
using ArchiveSink = std::function<bool(const std::uint8_t* data, size_t n)>;

class ArchiveReader {
public:
    virtual ~ArchiveReader(); // removes the temporary file of a nested container, if any
    ArchiveReader(const ArchiveReader&) = delete;
    ArchiveReader& operator=(const ArchiveReader&) = delete;

    // Opens a file or a stream (`name` is for messages). Null and a message when the file is
    // not a container this module opens: an NSIS installer, a plain executable, an unknown
    // file, a truncated or corrupt archive, an Inno Setup installer of a version the reader
    // does not know, an encrypted one.
    static std::unique_ptr<ArchiveReader> open(const std::string& path, std::string* error);
    static std::unique_ptr<ArchiveReader> open(std::unique_ptr<IStream> stream, const std::string& name, std::string* error);

    virtual ArchiveKind kind() const = 0;
    const std::string& name() const { return name_; }
    const std::vector<ArchiveEntry>& entries() const { return entries_; }

    // Streams one entry's bytes to the sink, checking the container's own checksum of them.
    virtual bool extract(const ArchiveEntry& e, const ArchiveSink& sink, std::string* error) = 0;
    // Refuses entries larger than maxSize (before reading them when the size is recorded).
    bool extractToMemory(const ArchiveEntry& e, std::vector<std::uint8_t>* out, size_t maxSize, std::string* error);
    bool extractToFile(const ArchiveEntry& e, const std::string& path, std::string* error);

    // The kind of an entry that may itself be a container, from its first bytes (the kinds
    // that need the whole file are found by openEntry).
    ArchiveKind entryKind(const ArchiveEntry& e);
    // Opens an entry that is itself a container: extracted to memory below
    // kNestedMemoryLimit bytes, else to a temporary file under `tempDir` (empty: the
    // system's), which the returned reader removes when it is destroyed. The returned
    // reader's name is "<this name>: <entry name>".
    std::unique_ptr<ArchiveReader> openEntry(const ArchiveEntry& e, std::string* error, const std::string& tempDir = "");
    static constexpr std::uint64_t kNestedMemoryLimit = 64u << 20;

protected:
    ArchiveReader() = default;
    std::string name_;
    std::vector<ArchiveEntry> entries_;
    std::string tempFile_; // removed by the destructor of the owning reader (openEntry)
    void removeTempFile();
};

} // namespace as3d
