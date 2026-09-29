// as3d::openPlatformStream (as3d/platform.h): an IStream over SDL_RWops. On Android
// SDL_RWFromFile resolves a relative path through the APK's asset manager, so the
// original pak archives are read in place from the APK.
#include <SDL.h>

#include <mutex>

#include "as3d/core.h"
#include "as3d/platform.h"

namespace as3d {
namespace {

class RwStream final : public IStream {
public:
    RwStream(SDL_RWops* rw, size_t size) : rw_(rw), size_(size) {}
    ~RwStream() override {
        if (rw_) SDL_RWclose(rw_);
    }

    size_t size() override { return size_; }

    bool readAt(size_t offset, void* out, size_t n) override {
        if (n == 0) return true;
        if (offset > size_ || n > size_ - offset) return false;
        // One cursor per stream: the asset manager's handles are not thread-safe.
        std::lock_guard<std::mutex> lock(mutex_);
        if (SDL_RWseek(rw_, static_cast<Sint64>(offset), RW_SEEK_SET) < 0) return false;
        size_t got = 0;
        auto* dst = static_cast<Uint8*>(out);
        while (got < n) {
            size_t chunk = SDL_RWread(rw_, dst + got, 1, n - got);
            if (chunk == 0) break;
            got += chunk;
        }
        return got == n;
    }

private:
    SDL_RWops* rw_;
    size_t size_;
    std::mutex mutex_;
};

} // namespace

std::unique_ptr<IStream> openPlatformStream(const std::string& path) {
    SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw) {
        AS3D_ERROR("openPlatformStream: cannot open '%s': %s", path.c_str(), SDL_GetError());
        return nullptr;
    }
    Sint64 size = SDL_RWsize(rw);
    if (size < 0) {
        AS3D_ERROR("openPlatformStream: no size for '%s': %s", path.c_str(), SDL_GetError());
        SDL_RWclose(rw);
        return nullptr;
    }
    return std::unique_ptr<IStream>(new RwStream(rw, static_cast<size_t>(size)));
}

} // namespace as3d
