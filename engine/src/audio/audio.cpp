#include "as3d/audio.h"

#include <mutex>
#include <unordered_map>
#include <vector>

#include "audio_device.h"
#include "mixer.h"
#include "music_decoder.h"
#include "wav.h"

namespace as3d {

struct Audio::Impl {
    Vfs* vfs = nullptr;
    AudioBackend backend = AudioBackend::Null;
    std::unique_ptr<audio::IAudioDevice> device;
    audio::Mixer mixer;

    // Sound cache: owns decoded PCM for the lifetime of the Audio instance.
    // Mixer voices hold raw pointers into `sounds`, which is safe because we
    // never remove or reallocate-in-place entries (a std::vector of
    // unique_ptr<Sample> keeps each Sample's address stable across growth).
    std::mutex cacheMutex;
    std::unordered_map<std::string, SoundId> pathToId;
    std::vector<std::unique_ptr<audio::Sample>> sounds;  // index 0 unused

    std::string musicTitle;
    bool musicLoop = true;
};

Audio::Audio() : impl_(new Impl()) {}
Audio::~Audio() { shutdown(); }

bool Audio::init(Vfs& vfs, AudioBackend backend, int preferredRate) {
    shutdown();
    impl_->vfs = &vfs;
    impl_->backend = backend;
    impl_->device = backend == AudioBackend::Sdl ? audio::makeSdlAudioDevice() : audio::makeNullAudioDevice();
    if (!impl_->device->open(preferredRate, &impl_->mixer)) {
        impl_->device.reset();
        return false;
    }
    return true;
}

void Audio::shutdown() {
    if (impl_->device) {
        impl_->device->close();
        impl_->device.reset();
    }
    impl_->mixer.stopAll();
    impl_->mixer.clearMusic();
    std::lock_guard<std::mutex> lock(impl_->cacheMutex);
    impl_->pathToId.clear();
    impl_->sounds.clear();
    impl_->vfs = nullptr;
}

SoundId Audio::loadSound(const char* gamePath) {
    if (!gamePath || !*gamePath || !impl_->vfs) return kInvalidSoundId;
    std::string norm = normalizePath(gamePath);

    std::lock_guard<std::mutex> lock(impl_->cacheMutex);
    auto it = impl_->pathToId.find(norm);
    if (it != impl_->pathToId.end()) return it->second;

    Blob data;
    if (!impl_->vfs->read(norm, data)) {
        AS3D_WARN("audio: sound not found: %s", norm.c_str());
        return kInvalidSoundId;
    }
    auto sample = std::make_unique<audio::Sample>();
    if (!audio::parseWav(data, *sample)) {
        AS3D_WARN("audio: failed to parse WAV: %s", norm.c_str());
        return kInvalidSoundId;
    }
    impl_->sounds.push_back(std::move(sample));
    SoundId id = static_cast<SoundId>(impl_->sounds.size());  // 1-based
    impl_->pathToId.emplace(norm, id);
    return id;
}

VoiceId Audio::play(SoundId id, const PlayParams& params) {
    if (id == kInvalidSoundId) return kInvalidVoiceId;
    const audio::Sample* sample = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl_->cacheMutex);
        if (id > impl_->sounds.size()) return kInvalidVoiceId;
        sample = impl_->sounds[id - 1].get();
    }
    return impl_->mixer.startVoice(sample, params);
}

void Audio::stop(VoiceId voice) { impl_->mixer.stopVoice(voice); }
void Audio::setVoiceParams(VoiceId voice, const PlayParams& params) { impl_->mixer.setVoiceParams(voice, params); }
void Audio::stopAll() { impl_->mixer.stopAll(); }

bool Audio::playMusic(const char* gamePath, bool loop) {
    if (!gamePath || !*gamePath || !impl_->vfs) return false;
    std::string norm = normalizePath(gamePath);
    Blob data;
    if (!impl_->vfs->read(norm, data)) {
        AS3D_WARN("audio: music not found: %s", norm.c_str());
        return false;
    }
    auto decoder = std::make_unique<audio::MusicDecoder>();
    if (!decoder->load(data)) {
        AS3D_WARN("audio: failed to load music: %s", norm.c_str());
        return false;
    }
    decoder->setLoop(loop);
    impl_->musicTitle = decoder->title();
    impl_->musicLoop = loop;
    impl_->mixer.setMusic(decoder.release());  // Mixer now owns it
    return true;
}

void Audio::stopMusic() {
    impl_->mixer.clearMusic();
    impl_->musicTitle.clear();
}

bool Audio::isMusicPlaying() const { return impl_->mixer.hasMusic(); }
bool Audio::jumpMusicToOrder(int order) { return impl_->mixer.jumpMusicToOrder(order); }
int Audio::musicOrder() const { return impl_->mixer.musicOrder(); }
std::string Audio::musicTitle() const { return impl_->musicTitle; }

void Audio::setSfxVolume(float linear) { impl_->mixer.setSfxVolume(linear); }
void Audio::setMusicVolume(float linear) { impl_->mixer.setMusicVolume(linear); }

void Audio::pause(bool paused) {
    impl_->mixer.setPaused(paused);
    if (impl_->device) impl_->device->setPaused(paused);
}

void Audio::render(float* interleavedStereo, int frames) {
    if (impl_->backend != AudioBackend::Null) {
        AS3D_WARN("audio: render() called on a non-Null backend; ignored");
        return;
    }
    impl_->mixer.render(interleavedStereo, frames);
}

int Audio::deviceRate() const { return impl_->device ? impl_->device->rate() : 0; }

AudioStats Audio::stats() const {
    AudioStats s;
    s.activeVoices = impl_->mixer.activeVoiceCount();
    s.maxVoices = audio::kMaxVoices;
    s.totalVoicesStarted = impl_->mixer.totalVoicesStartedCount();
    s.voicesStolen = impl_->mixer.voicesStolenCount();
    s.underruns = 0;  // see docs/audio.md: this mixer synthesizes synchronously, so there is
                       // no ring buffer to starve; kept for API completeness / future use.
    s.musicPlaying = impl_->mixer.hasMusic();
    return s;
}

}  // namespace as3d
