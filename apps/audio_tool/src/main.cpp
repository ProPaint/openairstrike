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
//   audio_tool render-music <game path> <out.ogg|out.wav> [--rate R] [--order N] [--quality Q]
//   audio_tool render-music --all <out dir> [--rate R] [--quality Q]
//       Pre-renders tracker music for the 3DS (docs/3ds-port.md M2.4): the
//       module played once from pattern order N (default 0) at R Hz (default
//       32728, the 3DS DSP's rate), as Ogg Vorbis (oggenc, else ffmpeg's
//       libvorbis; quality Q, default 4) or 16-bit WAV. --all writes every
//       module of music\ as <name>.ogg, plus <name>_gameover.ogg (from order
//       35, where the game jumps on game over) when the module has that order.
//       The output is derived from the game data: never commit it.
//   --paks <dir> (any command, first): mount <dir>/pak0.apk .. pak9.apk
//       instead of the first game's (a sequel's paks).
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

std::string g_paksDir;  // --paks

std::string dataRoot() {
    const char* env = std::getenv("AS3D_DATA_ROOT");
    if (env && *env) return env;
    return AS3D_REPO_ROOT;
}

// Mounts assets_extracted/ (loose files, if present) and the three original
// pak archives (if present), in that order so the paks -- the real game
// data format -- take priority.
void buildVfs(as3d::Vfs& vfs) {
    if (!g_paksDir.empty()) {
        for (int i = 0; i < 10; ++i) {
            auto stream = as3d::openFileStream(g_paksDir + "/pak" + std::to_string(i) + ".apk");
            if (!stream) continue;
            auto source = as3d::makePakSource(std::move(stream));
            if (source) vfs.mount(std::move(source));
        }
        return;
    }
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

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && toLower(s.substr(s.size() - n)) == suffix;
}

bool haveCommand(const char* name) {
    std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

// Renders one module from `order` to its end (no loop) and writes `outPath` (.ogg or .wav).
int renderMusicFile(as3d::Vfs& vfs, const std::string& gamePath, const std::string& outPath, int rate, int order,
                    double quality) {
    as3d::Blob data;
    if (!vfs.read(as3d::normalizePath(gamePath), data)) {
        std::fprintf(stderr, "audio_tool: music not found: %s\n", gamePath.c_str());
        return 1;
    }
    as3d::audio::MusicDecoder dec;
    if (!dec.load(data)) {
        std::fprintf(stderr, "audio_tool: cannot load module %s\n", gamePath.c_str());
        return 1;
    }
    dec.setLoop(false);
    if (order > 0 && !dec.setOrder(order)) {
        std::fprintf(stderr, "audio_tool: %s has no pattern order %d (%d orders)\n", gamePath.c_str(), order,
                     dec.numOrders());
        return 1;
    }
    constexpr int kChunk = 4096;
    constexpr double kMaxSeconds = 20.0 * 60.0;
    std::vector<float> pcm;
    std::vector<float> chunk(2 * kChunk);
    while (pcm.size() / 2 < static_cast<size_t>(kMaxSeconds * rate)) {
        if (!dec.read(rate, chunk.data(), kChunk)) break;  // the module ended (padded with silence)
        pcm.insert(pcm.end(), chunk.begin(), chunk.end());
    }
    // Trailing digital silence (the padding of the last read).
    size_t frames = pcm.size() / 2;
    while (frames > 0 && pcm[2 * frames - 1] == 0.0f && pcm[2 * frames - 2] == 0.0f) --frames;
    if (frames == 0) {
        std::fprintf(stderr, "audio_tool: %s rendered nothing\n", gamePath.c_str());
        return 1;
    }
    float peak = 0.0f;
    size_t clipped = 0;
    for (size_t i = 0; i < 2 * frames; ++i) {
        const float a = std::fabs(pcm[i]);
        peak = std::max(peak, a);
        clipped += a > 1.0f ? 1 : 0;
    }
    const bool ogg = endsWith(outPath, ".ogg");
    const std::string wavPath = ogg ? outPath + ".tmp.wav" : outPath;
    if (!writeWavStereo16(wavPath, rate, pcm, static_cast<int>(frames))) {
        std::fprintf(stderr, "audio_tool: failed to write '%s'\n", wavPath.c_str());
        return 1;
    }
    if (ogg) {
        char q[32];
        std::snprintf(q, sizeof q, "%.1f", quality);
        std::string cmd;
        if (haveCommand("oggenc"))
            cmd = std::string("oggenc -Q -q ") + q + " -o '" + outPath + "' '" + wavPath + "'";
        else if (haveCommand("ffmpeg"))
            cmd = std::string("ffmpeg -y -loglevel error -i '") + wavPath + "' -c:a libvorbis -q:a " + q + " '" +
                  outPath + "'";
        if (cmd.empty()) {
            std::fprintf(stderr, "audio_tool: no Ogg Vorbis encoder (oggenc or ffmpeg); kept %s\n", wavPath.c_str());
            return 1;
        }
        const int rc = std::system(cmd.c_str());
        std::remove(wavPath.c_str());
        if (rc != 0) {
            std::fprintf(stderr, "audio_tool: the encoder failed (%d): %s\n", rc, cmd.c_str());
            return 1;
        }
    }
    std::printf("audio_tool: %s from order %d -> %s (%.1f s at %d Hz, peak %.3f%s)\n", gamePath.c_str(), order,
                outPath.c_str(), static_cast<double>(frames) / rate, rate, peak,
                clipped ? ", CLIPPED: some samples above full scale" : "");
    return 0;
}

int cmdRenderMusic(int argc, char** argv) {
    // argv[2..]: <game path> <out> | --all <dir>, then options
    if (argc < 4) return -1;
    const bool all = std::strcmp(argv[2], "--all") == 0;
    const std::string first = argv[2], out = argv[3];
    int rate = 32728, order = 0;
    double quality = 4.0;
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "--rate") == 0 && i + 1 < argc) rate = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--order") == 0 && i + 1 < argc) order = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--quality") == 0 && i + 1 < argc) quality = std::atof(argv[++i]);
        else return -1;
    }
    if (rate < 8000 || rate > 192000) return -1;
    as3d::Vfs vfs;
    buildVfs(vfs);
    if (!all) return renderMusicFile(vfs, first, out, rate, order, quality);

    int failures = 0, written = 0;
    for (const std::string& name : vfs.list("music\\")) {
        if (!isMusicPath(name)) continue;
        std::string base = name.substr(name.find_last_of("\\/") + 1);
        base = toLower(base.substr(0, base.find_last_of('.')));
        const std::string dst = out + "/" + base + ".ogg";
        failures += renderMusicFile(vfs, name, dst, rate, 0, quality) != 0;
        ++written;
        as3d::Blob data;
        as3d::audio::MusicDecoder dec;
        if (vfs.read(name, data) && dec.load(data) && dec.numOrders() > as3d::kGameOverMusicOrder) {
            failures += renderMusicFile(vfs, name, out + "/" + base + "_gameover.ogg", rate,
                                        as3d::kGameOverMusicOrder, quality) != 0;
            ++written;
        }
    }
    std::printf("audio_tool: %d files, %d failures; copy them to sdmc:/3ds/openairstrike/<game>/music/\n", written,
                failures);
    return failures ? 1 : (written ? 0 : 1);
}

void usage() {
    std::fprintf(stderr,
                  "usage:\n"
                  "  audio_tool info\n"
                  "  audio_tool render <game path> --seconds N --out file.wav\n"
                  "  audio_tool play <game path> [--seconds N]\n"
                  "  audio_tool render-music <game path> <out.ogg|out.wav> [--rate R] [--order N] [--quality Q]\n"
                  "  audio_tool render-music --all <out dir> [--rate R] [--quality Q]\n"
                  "  (any command may start with --paks <dir>: that directory's pak0..9.apk)\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 1;
    }
    if (argc >= 3 && std::strcmp(argv[1], "--paks") == 0) {
        g_paksDir = argv[2];
        argv += 2;
        argc -= 2;
        if (argc < 2) {
            usage();
            return 1;
        }
    }
    std::string cmd = argv[1];

    if (cmd == "info") return cmdInfo();

    if (cmd == "render-music") {
        const int rc = cmdRenderMusic(argc, argv);
        if (rc < 0) usage();
        return rc < 0 ? 1 : rc;
    }

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
