// MusicDecoder without libopenmpt (AS3D_WITH_OPENMPT=OFF): load() always
// fails, so Audio::playMusic and AudioBridge::startLevel log their usual
// warning and continue without music.
#include "music_decoder.h"

namespace as3d::audio {

struct MusicDecoder::Impl {};

MusicDecoder::MusicDecoder() : impl_(new Impl()) {}
MusicDecoder::~MusicDecoder() { delete impl_; }

bool MusicDecoder::load(const Blob&) {
    AS3D_WARN("audio: built without libopenmpt (AS3D_WITH_OPENMPT=OFF), tracker music unavailable");
    return false;
}
void MusicDecoder::setLoop(bool) {}
bool MusicDecoder::read(int, float* out, int frames) {
    for (int i = 0; i < frames * 2; ++i) out[i] = 0.0f;
    return false;
}
double MusicDecoder::durationSeconds() const { return 0.0; }
std::string MusicDecoder::title() const { return std::string(); }
int MusicDecoder::numChannels() const { return 0; }
int MusicDecoder::numOrders() const { return 0; }
int MusicDecoder::numPatterns() const { return 0; }
bool MusicDecoder::setOrder(int) { return false; }
int MusicDecoder::currentOrder() const { return -1; }

}  // namespace as3d::audio
