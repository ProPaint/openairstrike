// Minimal RIFF/WAVE parser. See docs/audio.md for the sound inventory this
// was validated against.
#pragma once

#include <vector>

#include "as3d/core.h"

namespace as3d::audio {

// Decoded, ready-to-mix sample data. Always 16-bit signed PCM regardless of
// the source bit depth (8-bit unsigned sources are rescaled and centred).
struct Sample {
    int channels = 0;
    int rate = 0;
    std::vector<i16> pcm;  // interleaved, `channels` values per frame

    // From an optional `smpl` chunk; hasLoopPoints is false if none was
    // present or it described something we don't support (looping is then
    // driven entirely by PlayParams::loop wrapping the whole sample).
    u32 loopStartFrame = 0;
    u32 loopEndFrame = 0;  // exclusive
    bool hasLoopPoints = false;

    size_t frameCount() const {
        return channels > 0 ? pcm.size() / static_cast<size_t>(channels) : 0;
    }
};

// Parses a RIFF/WAVE blob holding 8-bit unsigned or 16-bit signed PCM, mono
// or stereo, any sample rate. Returns false on anything else (IMA/MS ADPCM,
// truncated data, bad headers, zero-length audio) or malformed input; never
// throws and never reads past the end of `data`.
bool parseWav(const Blob& data, Sample& out);

}  // namespace as3d::audio
