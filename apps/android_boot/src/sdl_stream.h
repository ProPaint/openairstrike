// SDL_RWops-backed as3d::IStream, so as3d::Vfs / as3d::makePakSource() can
// mount the original pak archives from Android APK assets (a relative path
// resolves through SDL's Android asset-manager backend) as well as from a
// plain filesystem path on desktop. Lives here rather than in
// engine/src/platform/ to avoid clashing with the EGL/SDL window work
// happening there in parallel; see docs/android.md.
#pragma once

#include <memory>
#include <string>

#include "as3d/vfs.h"

namespace as3d_boot {

// Returns null (and logs) if `rwPath` cannot be opened.
std::unique_ptr<as3d::IStream> makeSdlStream(const std::string& rwPath);

}  // namespace as3d_boot
