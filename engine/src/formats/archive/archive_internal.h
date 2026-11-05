// Shared between the readers of the archive module (as3d/archive.h): the Inno Setup reader's
// entry points, the loader table search the detector and the reader both use, and small
// helpers. Not part of the public interface.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "as3d/archive.h"

namespace as3d {
namespace archive_internal {

// The Inno Setup loader's offset table ("rDlPtS" + 6 bytes, docs/spec/archive.md section 3),
// found in the executable's stub with its CRC-32 checked. `offset0` is where the setup
// header data starts, `offset1` where the file data starts (0: the data is in separate
// .bin files). Returns false when no table with a valid CRC is in the first `limit` bytes.
bool findInnoLoaderTable(IStream& stream, std::uint64_t limit, std::uint64_t* offset0, std::uint64_t* offset1,
                         std::uint64_t* tablePos);

// Opens an Inno Setup installer (the kind was decided by the caller).
std::unique_ptr<ArchiveReader> openInno(std::unique_ptr<IStream> stream, const std::string& name, std::string* error);

inline std::uint16_t le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
inline std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}
inline std::uint64_t le64(const std::uint8_t* p) { return le32(p) | (static_cast<std::uint64_t>(le32(p + 4)) << 32); }

} // namespace archive_internal
} // namespace as3d
