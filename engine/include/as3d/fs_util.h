// Small portable file helpers (plain C stdio / POSIX, no SDL): shared by the SDL platform and
// the console backends.
#pragma once

#include <string>

namespace as3d {

// Replaces `dst` with `tmp` (the tmp-then-rename save pattern). rename(tmp, dst); when that
// fails and dst exists (FAT on a console SD card refuses to rename over a file), dst is
// removed and the rename retried. Returns success; a missing tmp fails without touching dst.
bool replaceFile(const std::string& tmp, const std::string& dst);
// Test hook: while set, the first rename of every replaceFile call fails as on FAT.
void setReplaceFileTestFailFirstRename(bool fail);

// mkdir -p for an absolute or relative path ('/'-separated); true if the directory exists
// afterwards.
bool makeDirectories(const std::string& dir);

} // namespace as3d
