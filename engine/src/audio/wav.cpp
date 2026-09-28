#include "wav.h"

#include <cstring>

namespace as3d::audio {
namespace {

u32 readU32LE(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}
u16 readU16LE(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }

constexpr u16 kFormatPcm = 1;
constexpr u16 kFormatExtensible = 0xFFFE;

}  // namespace

bool parseWav(const Blob& data, Sample& out) {
    if (data.size() < 12) return false;
    const u8* d = data.data();
    const size_t size = data.size();
    if (std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0) return false;

    bool haveFmt = false;
    bool haveData = false;
    u16 formatTag = 0;
    u16 channels = 0;
    u32 rate = 0;
    u16 bits = 0;
    size_t dataOffset = 0;
    u32 dataSize = 0;
    bool haveLoop = false;
    u32 loopStart = 0;
    u32 loopEnd = 0;

    size_t pos = 12;
    while (pos + 8 <= size) {
        const u8* chunkHeader = d + pos;
        u32 chunkSize = readU32LE(chunkHeader + 4);
        size_t bodyStart = pos + 8;
        bool isData = std::memcmp(chunkHeader, "data", 4) == 0;

        if (chunkSize > size - bodyStart) {
            // Chunk claims to run past the end of the file. Tolerate a
            // truncated `data` chunk (common with cut-off downloads/rips);
            // stop parsing anything after any other truncated chunk.
            if (isData) {
                chunkSize = static_cast<u32>(size - bodyStart);
            } else {
                break;
            }
        }

        if (std::memcmp(chunkHeader, "fmt ", 4) == 0) {
            if (chunkSize >= 16) {
                formatTag = readU16LE(d + bodyStart);
                channels = readU16LE(d + bodyStart + 2);
                rate = readU32LE(d + bodyStart + 4);
                bits = readU16LE(d + bodyStart + 14);
                if (formatTag == kFormatExtensible && chunkSize >= 40) {
                    formatTag = readU16LE(d + bodyStart + 24);
                }
                haveFmt = true;
            }
        } else if (isData) {
            dataOffset = bodyStart;
            dataSize = chunkSize;
            haveData = true;
        } else if (std::memcmp(chunkHeader, "smpl", 4) == 0) {
            if (chunkSize >= 36 + 24) {
                u32 numLoops = readU32LE(d + bodyStart + 28);
                if (numLoops >= 1) {
                    loopStart = readU32LE(d + bodyStart + 44);
                    loopEnd = readU32LE(d + bodyStart + 48) + 1;  // smpl's end is inclusive
                    haveLoop = true;
                }
            }
        }

        size_t next = bodyStart + chunkSize;
        if (chunkSize % 2 == 1) next += 1;  // RIFF chunks are word-aligned
        if (next <= pos) break;             // overflow / zero-progress guard
        pos = next;
    }

    if (!haveFmt || !haveData) return false;
    if (channels == 0 || channels > 2 || rate == 0) return false;
    if (formatTag != kFormatPcm) return false;  // no ADPCM in the shipped data; reject cleanly
    if (bits != 8 && bits != 16) return false;

    const size_t bytesPerSample = bits / 8;
    const size_t frameSize = bytesPerSample * static_cast<size_t>(channels);
    if (frameSize == 0) return false;

    size_t frameCount = static_cast<size_t>(dataSize) / frameSize;
    if (dataOffset > size || frameCount * frameSize > size - dataOffset) {
        frameCount = (size > dataOffset) ? (size - dataOffset) / frameSize : 0;
    }

    out = Sample{};
    out.channels = channels;
    out.rate = static_cast<int>(rate);
    out.pcm.assign(frameCount * channels, 0);

    const u8* src = d + dataOffset;
    const size_t totalSamples = frameCount * channels;
    if (bits == 16) {
        for (size_t i = 0; i < totalSamples; ++i) {
            out.pcm[i] = static_cast<i16>(readU16LE(src + i * 2));
        }
    } else {  // 8-bit unsigned PCM -> centred, scaled 16-bit signed
        for (size_t i = 0; i < totalSamples; ++i) {
            int v = static_cast<int>(src[i]) - 128;
            out.pcm[i] = static_cast<i16>(v * 256);
        }
    }

    if (haveLoop && loopEnd > loopStart && loopEnd <= frameCount) {
        out.loopStartFrame = loopStart;
        out.loopEndFrame = loopEnd;
        out.hasLoopPoints = true;
    }
    return true;  // a well-formed, possibly zero-length, sample
}

}  // namespace as3d::audio
