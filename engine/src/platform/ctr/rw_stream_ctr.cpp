// as3d::openPlatformStream and readPlatformFile for the 3DS: plain stdio on sdmc:/ (the SDL_RWops
// of the desktop backend is not needed; openFileStream already works there).
#include <cstdio>

#include "as3d/core.h"
#include "as3d/platform.h"

namespace as3d {

std::unique_ptr<IStream> openPlatformStream(const std::string& path) {
    std::unique_ptr<IStream> s = openFileStream(path);
    if (!s) AS3D_ERROR("openPlatformStream: cannot open '%s'", path.c_str());
    return s;
}

bool readPlatformFile(const std::string& path, Blob& out) {
    out.clear();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    bool ok = std::fseek(f, 0, SEEK_END) == 0;
    long size = ok ? std::ftell(f) : -1;
    ok = ok && size >= 0 && size <= (64 << 20) && std::fseek(f, 0, SEEK_SET) == 0;
    if (ok) {
        out.resize(static_cast<size_t>(size));
        ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    }
    std::fclose(f);
    if (!ok) out.clear();
    return ok;
}

} // namespace as3d
