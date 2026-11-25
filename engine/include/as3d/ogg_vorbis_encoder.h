// An Ogg Vorbis encoder over libvorbis and libogg (engine/src/audio/vorbisenc/, built only where
// tools/fetch_third_party.sh vorbis has put their source trees: the web page's music worker and
// audio_tool). Encodes 16-bit interleaved PCM into an in-memory .ogg file, as oggenc would:
// variable bit rate at a quality on oggenc's -q scale, the samples read as s / 32768.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace as3d::audio {

class OggVorbisEncoder {
public:
    OggVorbisEncoder();
    ~OggVorbisEncoder();
    OggVorbisEncoder(const OggVorbisEncoder&) = delete;
    OggVorbisEncoder& operator=(const OggVorbisEncoder&) = delete;

    // `quality`: oggenc's -q (-1 .. 10; 4 is about 100 kbps at 32 to 44 kHz stereo). The stream's
    // serial number is fixed, so the same samples give the same bytes. False with error() set.
    bool begin(int channels, int rate, double quality);
    // Any number of frames per call; the output does not depend on how the samples are split.
    bool write(const std::int16_t* interleaved, std::size_t frames);
    // Ends the stream; the file is then in bytes().
    bool finish();

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }
    std::vector<std::uint8_t> takeBytes() { return std::move(bytes_); }
    const std::string& error() const { return error_; }

private:
    void drain(bool flushAll);

    struct Impl;
    Impl* impl_ = nullptr;
    std::vector<std::uint8_t> bytes_;
    std::string error_;
    int channels_ = 0;
};

}  // namespace as3d::audio
