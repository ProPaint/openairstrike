// audio_tool: command line utility for the audio module (WP-1A).
// See docs/audio.md for usage examples.
//
// Commands:
//   audio_tool info
//       Inventory of all shipped sounds and tracker tracks.
//   audio_tool render <game path> --seconds N --out file.wav
//       Offline render (Null backend, deterministic) of a sound or track to
//       a 16-bit PCM stereo WAV file.
//   audio_tool play <game path> [--seconds N]
//       Plays a sound or track through the host's real audio device (Sdl
//       backend), if one is available.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "as3d/audio.h"
#include "as3d/core.h"
#include "as3d/vfs.h"

// Whitebox reuse of the audio module's internal WAV parser and music
// decoder wrapper for richer inventory output (original sample layout,
// tracker metadata) than the public Audio API exposes.
#include "../../../engine/src/audio/music_decoder.h"
#include "../../../engine/src/audio/wav.h"

namespace {

std::string dataRoot() {
    const char* env = std::getenv("AS3D_DATA_ROOT");
    if (env && *env) return env;
    return AS3D_REPO_ROOT;
}

// Mounts assets_extracted/ (loose files, if present) and the three original
// pak archives (if present), in that order so the paks -- the real game
// data format -- take priority.
void buildVfs(as3d::Vfs& vfs) {
    std::string root = dataRoot();
    vfs.mount(as3d::makeDirSource(root + "/assets_extracted"));
    for (int i = 0; i < 3; ++i) {
        std::string path = root + "/third_party_local/original/data/pak" + std::to_string(i) + ".apk";
        auto stream = as3d::openFileStream(path);
        if (!stream) continue;
        auto source = as3d::makePakSource(std::move(stream));
        if (source) vfs.mount(std::move(source));
    }
}

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isMusicPath(const std::string& path) {
    std::string p = toLower(path);
    static const char* kExt[] = {".mo3", ".it", ".xm", ".s3m", ".mod"};
    for (const char* ext : kExt) {
        size_t elen = std::strlen(ext);
        if (p.size() >= elen && p.compare(p.size() - elen, elen, ext) == 0) return true;
    }
    return false;
}

bool writeWavStereo16(const std::string& path, int rate, const std::vector<float>& interleaved, int frames) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto putU32 = [&](std::uint32_t v) {
        char b[4] = {char(v & 0xFF), char((v >> 8) & 0xFF), char((v >> 16) & 0xFF), char((v >> 24) & 0xFF)};
        f.write(b, 4);
    };
    auto putU16 = [&](std::uint16_t v) {
        char b[2] = {char(v & 0xFF), char((v >> 8) & 0xFF)};
        f.write(b, 2);
    };
    const int channels = 2;
    const int bits = 16;
    const std::uint32_t dataSize = static_cast<std::uint32_t>(frames) * channels * (bits / 8);

    f.write("RIFF", 4);
    putU32(36 + dataSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    putU32(16);
    putU16(1);  // PCM
    putU16(static_cast<std::uint16_t>(channels));
    putU32(static_cast<std::uint32_t>(rate));
    putU32(static_cast<std::uint32_t>(rate * channels * (bits / 8)));
    putU16(static_cast<std::uint16_t>(channels * (bits / 8)));
    putU16(static_cast<std::uint16_t>(bits));
    f.write("data", 4);
    putU32(dataSize);

    for (int i = 0; i < frames * channels; ++i) {
        float s = interleaved[static_cast<size_t>(i)];
        s = std::max(-1.0f, std::min(1.0f, s));
        auto v = static_cast<std::int16_t>(std::lround(s * 32767.0f));
        char b[2] = {char(v & 0xFF), char((v >> 8) & 0xFF)};
        f.write(b, 2);
    }
    return f.good();
}

float rmsOf(const std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    double sum = 0;
    for (float x : v) sum += static_cast<double>(x) * x;
    return static_cast<float>(std::sqrt(sum / v.size()));
}

int cmdInfo() {
    as3d::Vfs vfs;
    buildVfs(vfs);

    auto sounds = vfs.list("sounds\\");
    std::printf("== Sounds (%zu) ==\n", sounds.size());
    for (auto& name : sounds) {
        as3d::Blob data;
        if (!vfs.read(name, data)) {
            std::printf("%-32s READ FAILED\n", name.c_str());
            continue;
        }
        as3d::audio::Sample s;
        if (!as3d::audio::parseWav(data, s)) {
            std::printf("%-32s PARSE FAILED (%zu bytes)\n", name.c_str(), data.size());
            continue;
        }
        double dur = s.rate > 0 ? static_cast<double>(s.frameCount()) / s.rate : 0.0;
        std::printf("%-32s ch=%d rate=%dHz frames=%zu dur=%.3fs%s\n", name.c_str(), s.channels, s.rate,
                    s.frameCount(), dur, s.hasLoopPoints ? " loop" : "");
    }

    auto tracks = vfs.list("music\\");
    std::printf("\n== Music (%zu) ==\n", tracks.size());
    for (auto& name : tracks) {
        as3d::Blob data;
        if (!vfs.read(name, data)) {
            std::printf("%-32s READ FAILED\n", name.c_str());
            continue;
        }
        as3d::audio::MusicDecoder dec;
        if (!dec.load(data)) {
            std::printf("%-32s DECODE FAILED (%zu bytes)\n", name.c_str(), data.size());
            continue;
        }
        std::printf("%-32s title='%s' dur=%.2fs channels=%d orders=%d patterns=%d\n", name.c_str(),
                    dec.title().c_str(), dec.durationSeconds(), dec.numChannels(), dec.numOrders(),
                    dec.numPatterns());
    }
    return 0;
}

int cmdRender(const std::string& gamePath, double seconds, const std::string& outPath) {
    as3d::Vfs vfs;
    buildVfs(vfs);
    as3d::Audio audio;
    if (!audio.init(vfs, as3d::AudioBackend::Null, 44100)) {
        std::fprintf(stderr, "audio_tool: failed to init Null audio backend\n");
        return 1;
    }

    if (isMusicPath(gamePath)) {
        if (!audio.playMusic(gamePath.c_str(), /*loop=*/false)) {
            std::fprintf(stderr, "audio_tool: failed to load music '%s'\n", gamePath.c_str());
            return 1;
        }
        std::printf("audio_tool: playing music '%s' (title='%s')\n", gamePath.c_str(), audio.musicTitle().c_str());
    } else {
        as3d::SoundId id = audio.loadSound(gamePath.c_str());
        if (id == as3d::kInvalidSoundId) {
            std::fprintf(stderr, "audio_tool: failed to load sound '%s'\n", gamePath.c_str());
            return 1;
        }
        audio.play(id, as3d::PlayParams{});
    }

    const int rate = audio.deviceRate();
    const int frames = static_cast<int>(seconds * rate);
    std::vector<float> buffer(static_cast<size_t>(frames) * 2, 0.0f);
    audio.render(buffer.data(), frames);

    std::vector<float> left(frames), right(frames);
    for (int i = 0; i < frames; ++i) {
        left[i] = buffer[static_cast<size_t>(i) * 2 + 0];
        right[i] = buffer[static_cast<size_t>(i) * 2 + 1];
    }

    if (!writeWavStereo16(outPath, rate, buffer, frames)) {
        std::fprintf(stderr, "audio_tool: failed to write '%s'\n", outPath.c_str());
        return 1;
    }
    std::printf("audio_tool: wrote %s (%.2fs @ %dHz, rms L=%.4f R=%.4f)\n", outPath.c_str(), seconds, rate,
                rmsOf(left), rmsOf(right));
    return 0;
}

int cmdPlay(const std::string& gamePath, double seconds) {
    as3d::Vfs vfs;
    buildVfs(vfs);
    as3d::Audio audio;
    if (!audio.init(vfs, as3d::AudioBackend::Sdl, 44100)) {
        std::fprintf(stderr, "audio_tool: failed to open an SDL audio device\n");
        return 1;
    }
    std::printf("audio_tool: device rate = %d Hz\n", audio.deviceRate());

    if (isMusicPath(gamePath)) {
        if (!audio.playMusic(gamePath.c_str(), /*loop=*/true)) {
            std::fprintf(stderr, "audio_tool: failed to load music '%s'\n", gamePath.c_str());
            return 1;
        }
        std::printf("audio_tool: playing music '%s' (title='%s') for %.1fs\n", gamePath.c_str(),
                    audio.musicTitle().c_str(), seconds);
    } else {
        as3d::SoundId id = audio.loadSound(gamePath.c_str());
        if (id == as3d::kInvalidSoundId) {
            std::fprintf(stderr, "audio_tool: failed to load sound '%s'\n", gamePath.c_str());
            return 1;
        }
        audio.play(id, as3d::PlayParams{});
        std::printf("audio_tool: playing sound '%s' for %.1fs\n", gamePath.c_str(), seconds);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(seconds * 1000)));
    audio.stopAll();
    audio.stopMusic();
    audio.shutdown();
    return 0;
}

void usage() {
    std::fprintf(stderr,
                  "usage:\n"
                  "  audio_tool info\n"
                  "  audio_tool render <game path> --seconds N --out file.wav\n"
                  "  audio_tool play <game path> [--seconds N]\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 1;
    }
    std::string cmd = argv[1];

    if (cmd == "info") return cmdInfo();

    if (cmd == "render") {
        if (argc < 3) {
            usage();
            return 1;
        }
        std::string gamePath = argv[2];
        double seconds = 10.0;
        std::string out;
        for (int i = 3; i < argc; ++i) {
            if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
                seconds = std::atof(argv[++i]);
            } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
                out = argv[++i];
            }
        }
        if (out.empty()) {
            std::fprintf(stderr, "audio_tool: render requires --out file.wav\n");
            return 1;
        }
        return cmdRender(gamePath, seconds, out);
    }

    if (cmd == "play") {
        if (argc < 3) {
            usage();
            return 1;
        }
        std::string gamePath = argv[2];
        double seconds = 10.0;
        for (int i = 3; i < argc; ++i) {
            if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
                seconds = std::atof(argv[++i]);
            }
        }
        return cmdPlay(gamePath, seconds);
    }

    usage();
    return 1;
}
