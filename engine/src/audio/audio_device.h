// Internal device abstraction. Public API only exposes AudioBackend
// (as3d/audio.h); these are the two concrete implementations behind it.
#pragma once

#include <memory>

namespace as3d::audio {

class Mixer;

class IAudioDevice {
public:
    virtual ~IAudioDevice() = default;
    // Opens the device at (approximately) `preferredRate` and, for real
    // devices, starts calling `mixer->render()` from the audio callback
    // thread. `mixer` must outlive the device. Returns false on failure.
    virtual bool open(int preferredRate, Mixer* mixer) = 0;
    virtual void close() = 0;
    virtual int rate() const = 0;
    virtual void setPaused(bool paused) = 0;
};

std::unique_ptr<IAudioDevice> makeNullAudioDevice();
std::unique_ptr<IAudioDevice> makeSdlAudioDevice();

}  // namespace as3d::audio
