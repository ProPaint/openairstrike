// Small portable file helpers (plain C stdio / POSIX, no SDL): shared by the SDL platform and
// the console backends.
#pragma once

#include <cstdio>
#include <string>

namespace as3d {

// Replaces `dst` with `tmp` (the tmp-then-rename save pattern). rename(tmp, dst); when that
// fails and dst exists (FAT on a console SD card refuses to rename over a file), dst is
// removed and the rename retried. Returns success; a missing tmp fails without touching dst.
bool replaceFile(const std::string& tmp, const std::string& dst);
// Test hook: while set, the first rename of every replaceFile call fails as on FAT.
void setReplaceFileTestFailFirstRename(bool fail);

// mkdir -p for an absolute or relative path ('/'-separated); true if the directory exists
// afterwards. A device prefix ("sdmc:") is skipped, not created.
bool makeDirectories(const std::string& dir);

// Writes `size` bytes to `dst` safely: <dst>.tmp in the same directory (created when missing),
// flushed to the medium (fflush + fsync), closed, then replaceFile. The temporary is removed on
// every failure, so the destination is either the old or the new complete file. `why` (optional)
// gets the failing step. Runs inside a FileWriteScope.
bool writeFileAtomic(const std::string& dst, const void* data, size_t size, std::string* why = nullptr);

// fflush + fsync of an open stream; true when the data reached the medium (or the platform has
// no fsync).
bool syncStream(std::FILE* f);

// While a write is in progress the platform may want to hold something (the 3DS: sleep mode is
// refused, so the lid closing cannot cut an SD write). The hook gets true when the outermost
// FileWriteScope opens and false when it closes.
void setFileWriteHook(void (*hook)(bool writing));
class FileWriteScope {
public:
    FileWriteScope();
    ~FileWriteScope();
    FileWriteScope(const FileWriteScope&) = delete;
    FileWriteScope& operator=(const FileWriteScope&) = delete;
};

} // namespace as3d
