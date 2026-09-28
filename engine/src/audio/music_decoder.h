// Thin wrapper over libopenmpt's C API (chosen and justified in
// docs/audio.md). Deliberately uses the plain C API, not the C++ wrapper:
// this translation unit is compiled without exceptions/RTTI like the rest
// of the engine, and the C API reports failures through return values
// instead of throwing.
#pragma once

#include <string>

#include "as3d/core.h"

namespace as3d::audio {

class MusicDecoder {
public:
    MusicDecoder();
    ~MusicDecoder();
    MusicDecoder(const MusicDecoder&) = delete;
    MusicDecoder& operator=(const MusicDecoder&) = delete;

    // Parses/loads the module from an owned copy of `data`. Returns false
    // (and logs) on anything libopenmpt rejects; never throws.
    bool load(const Blob& data);

    void setLoop(bool loop);
    // Writes exactly `frames` interleaved stereo float frames into `out`
    // (2*frames floats). Pads with silence and returns false once the
    // (non-looping) module has ended; used on the audio thread, does not
    // allocate.
    bool read(int sampleRate, float* out, int frames);

    double durationSeconds() const;
    std::string title() const;
    int numChannels() const;
    int numOrders() const;
    int numPatterns() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace as3d::audio
