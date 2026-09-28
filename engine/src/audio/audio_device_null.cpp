// Headless device: nothing pumps the mixer automatically. Audio::render()
// (the public, Null-backend-only API) calls Mixer::render() directly. This
// object only remembers the configured rate/pause state so Audio::deviceRate()
// and pause() behave consistently regardless of which backend is active.
#include "audio_device.h"
#include "mixer.h"

namespace as3d::audio {
namespace {

class NullAudioDevice : public IAudioDevice {
public:
    bool open(int preferredRate, Mixer* mixer) override {
        rate_ = preferredRate > 0 ? preferredRate : 44100;
        mixer_ = mixer;
        if (mixer_) mixer_->setDeviceRate(rate_);
        return true;
    }
    void close() override { mixer_ = nullptr; }
    int rate() const override { return rate_; }
    void setPaused(bool paused) override {
        if (mixer_) mixer_->setPaused(paused);
    }

private:
    Mixer* mixer_ = nullptr;
    int rate_ = 44100;
};

}  // namespace

std::unique_ptr<IAudioDevice> makeNullAudioDevice() { return std::make_unique<NullAudioDevice>(); }

}  // namespace as3d::audio
