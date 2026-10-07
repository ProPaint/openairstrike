// SDL2 audio backend: opens a real device and calls Mixer::render() directly
// from SDL's own audio callback thread. Requests float32 stereo; SDL_OpenAudioDevice
// performs any int16<->float conversion transparently to the hardware when
// SDL_AUDIO_ALLOW_FORMAT_CHANGE is *not* passed, so the mixer always deals in
// float regardless of what the underlying device natively supports.
#include <SDL.h>

#include "as3d/core.h"
#include "audio_device.h"
#include "mixer.h"

namespace as3d::audio {
namespace {

constexpr int kDesiredSamples = 1024;  // callback buffer size, per channel

void sdlAudioCallback(void* userdata, Uint8* stream, int len) {
    Mixer* mixer = static_cast<Mixer*>(userdata);
    int frames = len / static_cast<int>(sizeof(float) * 2);
    mixer->render(reinterpret_cast<float*>(stream), frames);
}

class SdlAudioDevice : public IAudioDevice {
public:
    ~SdlAudioDevice() override { close(); }

    bool open(int preferredRate, Mixer* mixer) override {
        if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
                AS3D_ERROR("audio: SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
                return false;
            }
            ownsSubsystem_ = true;
        }

        SDL_AudioSpec desired{};
        desired.freq = preferredRate > 0 ? preferredRate : 44100;
        desired.format = AUDIO_F32SYS;
        desired.channels = 2;
        desired.samples = kDesiredSamples;
        desired.callback = &sdlAudioCallback;
        desired.userdata = mixer;

        SDL_AudioSpec obtained{};
        device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (device_ == 0) {
            AS3D_ERROR("audio: SDL_OpenAudioDevice failed: %s", SDL_GetError());
            if (ownsSubsystem_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
            ownsSubsystem_ = false;
            return false;
        }
        rate_ = obtained.freq;
        mixer->setDeviceRate(rate_);
        SDL_PauseAudioDevice(device_, 0);
        return true;
    }

    void close() override {
        if (device_ != 0) {
            SDL_CloseAudioDevice(device_);
            device_ = 0;
        }
        if (ownsSubsystem_) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            ownsSubsystem_ = false;
        }
    }

    int rate() const override { return rate_; }

    void setPaused(bool paused) override {
        if (device_ != 0) SDL_PauseAudioDevice(device_, paused ? 1 : 0);
    }

private:
    SDL_AudioDeviceID device_ = 0;
    int rate_ = 44100;
    bool ownsSubsystem_ = false;
};

}  // namespace

std::unique_ptr<IAudioDevice> makeSdlAudioDevice() { return std::make_unique<SdlAudioDevice>(); }

}  // namespace as3d::audio
