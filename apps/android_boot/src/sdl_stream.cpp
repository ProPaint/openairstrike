#include "sdl_stream.h"

#include <SDL.h>

#include "as3d/core.h"

namespace as3d_boot {
namespace {

class SdlStream : public as3d::IStream {
public:
    SdlStream(SDL_RWops* rw, size_t size) : rw_(rw), size_(size) {}
    ~SdlStream() override {
        if (rw_) SDL_RWclose(rw_);
    }

    size_t size() override { return size_; }

    bool readAt(size_t offset, void* out, size_t n) override {
        if (n == 0) return true;
        if (offset > size_ || n > size_ - offset) return false;
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
};

}  // namespace

std::unique_ptr<as3d::IStream> makeSdlStream(const std::string& rwPath) {
    SDL_RWops* rw = SDL_RWFromFile(rwPath.c_str(), "rb");
    if (!rw) {
        AS3D_ERROR("sdl_stream: SDL_RWFromFile('%s') failed: %s", rwPath.c_str(), SDL_GetError());
        return nullptr;
    }
    Sint64 size = SDL_RWsize(rw);
    if (size < 0) {
        AS3D_ERROR("sdl_stream: SDL_RWsize('%s') failed: %s", rwPath.c_str(), SDL_GetError());
        SDL_RWclose(rw);
        return nullptr;
    }
    return std::make_unique<SdlStream>(rw, static_cast<size_t>(size));
}

}  // namespace as3d_boot
