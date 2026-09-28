#include "music_decoder.h"

#include <libopenmpt/libopenmpt.h>

#include <cstring>
#include <vector>

namespace as3d::audio {
namespace {

void logSink(const char* message, void* /*user*/) { AS3D_WARN("openmpt: %s", message ? message : ""); }

}  // namespace

struct MusicDecoder::Impl {
    openmpt_module* mod = nullptr;
    std::vector<char> owned;  // libopenmpt only needs `data` during create(); kept for reloads
};

MusicDecoder::MusicDecoder() : impl_(new Impl()) {}

MusicDecoder::~MusicDecoder() {
    if (impl_) {
        if (impl_->mod) openmpt_module_destroy(impl_->mod);
        delete impl_;
    }
}

bool MusicDecoder::load(const Blob& data) {
    if (impl_->mod) {
        openmpt_module_destroy(impl_->mod);
        impl_->mod = nullptr;
    }
    if (data.empty()) {
        AS3D_ERROR("music: empty module data");
        return false;
    }
    impl_->owned.assign(reinterpret_cast<const char*>(data.data()),
                         reinterpret_cast<const char*>(data.data()) + data.size());

    int error = 0;
    const char* errorMessage = nullptr;
    impl_->mod = openmpt_module_create_from_memory2(impl_->owned.data(), impl_->owned.size(), &logSink, nullptr,
                                                     nullptr, nullptr, &error, &errorMessage, nullptr);
    if (!impl_->mod) {
        AS3D_ERROR("music: failed to load module (error %d): %s", error,
                   errorMessage ? errorMessage : "unknown error");
        if (errorMessage) openmpt_free_string(errorMessage);
        return false;
    }
    if (errorMessage) openmpt_free_string(errorMessage);
    return true;
}

void MusicDecoder::setLoop(bool loop) {
    if (impl_->mod) openmpt_module_set_repeat_count(impl_->mod, loop ? -1 : 0);
}

bool MusicDecoder::read(int sampleRate, float* out, int frames) {
    if (!impl_->mod || frames <= 0) {
        if (out && frames > 0) std::memset(out, 0, sizeof(float) * 2 * static_cast<size_t>(frames));
        return false;
    }
    size_t got =
        openmpt_module_read_interleaved_float_stereo(impl_->mod, sampleRate, static_cast<size_t>(frames), out);
    if (got < static_cast<size_t>(frames)) {
        std::memset(out + got * 2, 0, sizeof(float) * 2 * (static_cast<size_t>(frames) - got));
    }
    return got > 0;
}

double MusicDecoder::durationSeconds() const {
    return impl_->mod ? openmpt_module_get_duration_seconds(impl_->mod) : 0.0;
}

int MusicDecoder::numChannels() const { return impl_->mod ? openmpt_module_get_num_channels(impl_->mod) : 0; }
int MusicDecoder::numOrders() const { return impl_->mod ? openmpt_module_get_num_orders(impl_->mod) : 0; }
int MusicDecoder::numPatterns() const { return impl_->mod ? openmpt_module_get_num_patterns(impl_->mod) : 0; }

std::string MusicDecoder::title() const {
    if (!impl_->mod) return {};
    const char* t = openmpt_module_get_metadata(impl_->mod, "title");
    std::string result = t ? t : "";
    if (t) openmpt_free_string(t);
    return result;
}

}  // namespace as3d::audio
