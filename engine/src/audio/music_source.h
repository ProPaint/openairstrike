// What the mixer pulls music from: a tracker module (MusicDecoder, libopenmpt) or, on the 3DS,
// a pre-rendered Ogg Vorbis file (OggMusic, tremor; docs/3ds-port.md M2.4). The mixer owns the
// source and calls it from the audio thread under its music mutex.
#pragma once

namespace as3d::audio {

class MusicSource {
public:
    virtual ~MusicSource() = default;
    // Writes exactly `frames` interleaved stereo float frames into `out` (2*frames floats).
    // Pads with silence and returns false once a non-looping source has ended. Must not
    // allocate (audio thread).
    virtual bool read(int sampleRate, float* out, int frames) = 0;
    // Pattern orders (a pre-rendered file: 1, or 36 with its game-over part: the game jumps
    // to kGameOverMusicOrder).
    virtual int numOrders() const = 0;
    virtual bool setOrder(int order) = 0;
    virtual int currentOrder() const = 0;
};

}  // namespace as3d::audio
