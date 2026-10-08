// Pre-rendered music (docs/3ds-port.md M2.4): an Ogg Vorbis file decoded with tremor (integer
// Vorbis, libvorbisidec), looping, as a MusicSource of the mixer. Built when AS3D_WITH_TREMOR
// (the 3DS with devkitPro's portlibs). The file is the module rendered once by
// `audio_tool render-music`; its optional game-over part (the module from pattern order 35) is a
// second file that setOrder(kGameOverMusicOrder) switches to.
#pragma once

#include "as3d/core.h"
#include "music_source.h"

namespace as3d::audio {

class OggMusic final : public MusicSource {
public:
    OggMusic();
    ~OggMusic() override;
    OggMusic(const OggMusic&) = delete;
    OggMusic& operator=(const OggMusic&) = delete;

    // Takes the file's bytes (kept in memory) and, optionally, the game-over part's. False (and
    // a log line) when the main file is not Ogg Vorbis; a bad game-over part is dropped.
    bool load(Blob main, Blob gameOver);
    int fileRate() const;

    bool read(int sampleRate, float* out, int frames) override;
    int numOrders() const override;  // 1, or kGameOverMusicOrder + 1 with a game-over part
    bool setOrder(int order) override;
    int currentOrder() const override;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace as3d::audio
