// Acceptance tests for the audio module (WP-1A). See docs/audio.md.
//
// Whitebox: this file reaches directly into engine/src/audio/{wav,mixer}.h
// via relative includes to unit-test the WAV parser and mixer precisely
// (exact sample values, exact voice bookkeeping), alongside blackbox tests
// of the public as3d::Audio API for sound/music loading. Everything here
// uses the Null backend, so it is fully deterministic and needs no real
// audio device.
#include "doctest.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "as3d/audio.h"
#include "as3d/core.h"
#include "as3d/vfs.h"
#include "test_data.h"

#include "../../engine/src/audio/mixer.h"
#include "../../engine/src/audio/wav.h"

using as3d::Blob;
using as3d::i16;
using as3d::u16;
using as3d::u32;
using as3d::u8;
using as3d::audio::Mixer;
using as3d::audio::Sample;
using as3d::PlayParams;

namespace {

void putU32(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>(x & 0xFF));
    v.push_back(static_cast<u8>((x >> 8) & 0xFF));
    v.push_back(static_cast<u8>((x >> 16) & 0xFF));
    v.push_back(static_cast<u8>((x >> 24) & 0xFF));
}
void putU16(std::vector<u8>& v, u16 x) {
    v.push_back(static_cast<u8>(x & 0xFF));
    v.push_back(static_cast<u8>((x >> 8) & 0xFF));
}
void putTag(std::vector<u8>& v, const char* tag) { v.insert(v.end(), tag, tag + 4); }

void appendChunk(std::vector<u8>& out, const char* tag, const std::vector<u8>& body) {
    putTag(out, tag);
    putU32(out, static_cast<u32>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    if (body.size() % 2) out.push_back(0);
}

std::vector<u8> makeFmtChunk(int channels, int bits, int rate) {
    std::vector<u8> fmt;
    putU16(fmt, 1);  // PCM
    putU16(fmt, static_cast<u16>(channels));
    putU32(fmt, static_cast<u32>(rate));
    u32 byteRate = static_cast<u32>(rate * channels * (bits / 8));
    putU32(fmt, byteRate);
    putU16(fmt, static_cast<u16>(channels * (bits / 8)));
    putU16(fmt, static_cast<u16>(bits));
    return fmt;
}

// Builds a minimal PCM WAV holding a sine wave, for exact, reproducible
// mixer/parser assertions.
Blob makeSineWav(int channels, int bits, int rate, int frameCount, float freqHz, float amplitude,
                  bool fmtFirst = true) {
    std::vector<u8> dataChunk;
    for (int i = 0; i < frameCount; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(rate);
        float s = amplitude * std::sin(2.0f * 3.14159265358979323846f * freqHz * t);
        for (int c = 0; c < channels; ++c) {
            if (bits == 16) {
                auto v = static_cast<i16>(std::lround(s * 32767.0f));
                dataChunk.push_back(static_cast<u8>(v & 0xFF));
                dataChunk.push_back(static_cast<u8>((v >> 8) & 0xFF));
            } else {
                auto v = static_cast<u8>(std::lround((s * 0.5f + 0.5f) * 255.0f));
                dataChunk.push_back(v);
            }
        }
    }
    std::vector<u8> fmtChunk = makeFmtChunk(channels, bits, rate);

    std::vector<u8> out;
    putTag(out, "RIFF");
    putU32(out, 0);  // patched below
    putTag(out, "WAVE");
    if (fmtFirst) {
        appendChunk(out, "fmt ", fmtChunk);
        appendChunk(out, "data", dataChunk);
    } else {
        appendChunk(out, "data", dataChunk);
        appendChunk(out, "fmt ", fmtChunk);
    }
    u32 riffSize = static_cast<u32>(out.size() - 8);
    out[4] = static_cast<u8>(riffSize & 0xFF);
    out[5] = static_cast<u8>((riffSize >> 8) & 0xFF);
    out[6] = static_cast<u8>((riffSize >> 16) & 0xFF);
    out[7] = static_cast<u8>((riffSize >> 24) & 0xFF);
    return Blob(out.begin(), out.end());
}

float rmsOf(const float* v, size_t n) {
    if (n == 0) return 0.0f;
    double sum = 0;
    for (size_t i = 0; i < n; ++i) sum += static_cast<double>(v[i]) * v[i];
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

std::uint64_t fnv1a(const void* data, size_t n) {
    const auto* p = static_cast<const unsigned char*>(data);
    std::uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace

// ---------------------------------------------------------------------------
// WAV parser
// ---------------------------------------------------------------------------

TEST_CASE("wav: 16-bit mono parses") {
    Blob wav = makeSineWav(1, 16, 22050, 1000, 440.0f, 0.5f);
    Sample s;
    REQUIRE(as3d::audio::parseWav(wav, s));
    CHECK(s.channels == 1);
    CHECK(s.rate == 22050);
    CHECK(s.frameCount() == 1000);
}

TEST_CASE("wav: 8-bit stereo parses") {
    Blob wav = makeSineWav(2, 8, 11025, 500, 220.0f, 0.5f);
    Sample s;
    REQUIRE(as3d::audio::parseWav(wav, s));
    CHECK(s.channels == 2);
    CHECK(s.rate == 11025);
    CHECK(s.frameCount() == 500);
}

TEST_CASE("wav: 16-bit stereo parses") {
    Blob wav = makeSineWav(2, 16, 44100, 200, 1000.0f, 0.8f);
    Sample s;
    REQUIRE(as3d::audio::parseWav(wav, s));
    CHECK(s.channels == 2);
    CHECK(s.frameCount() == 200);
}

TEST_CASE("wav: odd chunk order (data before fmt) still parses") {
    Blob wav = makeSineWav(1, 16, 8000, 100, 100.0f, 0.5f, /*fmtFirst=*/false);
    Sample s;
    REQUIRE(as3d::audio::parseWav(wav, s));
    CHECK(s.frameCount() == 100);
}

TEST_CASE("wav: truncated data is clamped, not rejected") {
    Blob wav = makeSineWav(1, 16, 8000, 100, 100.0f, 0.5f);
    wav.resize(wav.size() - 50);  // file cut short; `data`'s declared size now lies
    Sample s;
    REQUIRE(as3d::audio::parseWav(wav, s));
    CHECK(s.frameCount() < 100);
}

TEST_CASE("wav: zero-length data is valid, not a crash") {
    Blob wav = makeSineWav(1, 16, 8000, 0, 100.0f, 0.5f);
    Sample s;
    REQUIRE(as3d::audio::parseWav(wav, s));
    CHECK(s.frameCount() == 0);
}

TEST_CASE("wav: absurd declared chunk size does not crash or over-allocate") {
    Blob wav = makeSineWav(1, 16, 8000, 10, 100.0f, 0.5f);
    bool patched = false;
    for (size_t i = 0; i + 8 <= wav.size(); ++i) {
        if (std::memcmp(&wav[i], "data", 4) == 0) {
            wav[i + 4] = 0xFF;
            wav[i + 5] = 0xFF;
            wav[i + 6] = 0xFF;
            wav[i + 7] = 0x7F;
            patched = true;
            break;
        }
    }
    REQUIRE(patched);
    Sample s;
    bool ok = as3d::audio::parseWav(wav, s);  // must not crash regardless of outcome
    if (ok) CHECK(s.pcm.size() * 2 <= wav.size());
}

TEST_CASE("wav: garbage or empty input is rejected without crashing") {
    Blob junk(64, 0xAB);
    Sample s;
    CHECK_FALSE(as3d::audio::parseWav(junk, s));
    Blob empty;
    CHECK_FALSE(as3d::audio::parseWav(empty, s));
    Blob tiny(4, 0);
    CHECK_FALSE(as3d::audio::parseWav(tiny, s));
}

TEST_CASE("wav: unsupported format tag (e.g. ADPCM) is rejected cleanly") {
    // Same layout as makeSineWav's fmt chunk but with formatTag = 2 (MS-ADPCM).
    std::vector<u8> fmtChunk = makeFmtChunk(1, 16, 8000);
    fmtChunk[0] = 2;
    fmtChunk[1] = 0;
    std::vector<u8> out;
    putTag(out, "RIFF");
    putU32(out, 0);
    putTag(out, "WAVE");
    appendChunk(out, "fmt ", fmtChunk);
    std::vector<u8> dataChunk(20, 0);
    appendChunk(out, "data", dataChunk);
    Sample s;
    CHECK_FALSE(as3d::audio::parseWav(Blob(out.begin(), out.end()), s));
}

TEST_CASE("wav: all shipped sound files load") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    as3d::Vfs vfs;
    vfs.mount(as3d::makeDirSource(testdata::extractedDir()));
    auto names = vfs.list("sounds\\");
    CHECK(names.size() == static_cast<size_t>(testdata::expectedInt("sounds.files")));
    int loaded = 0;
    for (auto& n : names) {
        Blob data;
        REQUIRE(vfs.read(n, data));
        Sample s;
        bool ok = as3d::audio::parseWav(data, s);
        CHECK_MESSAGE(ok, "failed to parse " << n);
        if (ok) ++loaded;
    }
    CHECK(loaded == static_cast<int>(names.size()));
}

// ---------------------------------------------------------------------------
// Mixer
// ---------------------------------------------------------------------------

TEST_CASE("mixer: sine at volume 1, centre pan gives the expected RMS on both channels") {
    Sample s;
    s.channels = 1;
    s.rate = 44100;
    const int n = 4410;  // 100 exact cycles of a 1000 Hz tone at 44100 Hz
    const float amplitude = 0.8f;
    s.pcm.resize(n);
    for (int i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / 44100.0f;
        s.pcm[i] = static_cast<i16>(std::lround(amplitude * std::sin(2.0f * 3.14159265f * 1000.0f * t) * 32767.0f));
    }

    Mixer mixer;
    mixer.setDeviceRate(44100);
    PlayParams params;
    params.volume = 1.0f;
    params.pan = 0.0f;
    params.loop = false;
    as3d::VoiceId v = mixer.startVoice(&s, params);
    CHECK(v != as3d::kInvalidVoiceId);

    std::vector<float> out(static_cast<size_t>(n) * 2);
    mixer.render(out.data(), n);

    std::vector<float> left(n), right(n);
    for (int i = 0; i < n; ++i) {
        left[i] = out[i * 2 + 0];
        right[i] = out[i * 2 + 1];
    }
    // Full-scale sine RMS is amplitude/sqrt(2); centre pan applies a further
    // cos(pi/4) = sin(pi/4) = 0.70710678 gain to each channel. tanh() barely
    // touches values this far from +-1, so a modest tolerance covers both
    // quantization and the limiter's slight compression.
    float expected = (amplitude / std::sqrt(2.0f)) * 0.70710678f;
    CHECK(rmsOf(left.data(), n) == doctest::Approx(expected).epsilon(0.03));
    CHECK(rmsOf(right.data(), n) == doctest::Approx(expected).epsilon(0.03));
    CHECK(rmsOf(left.data(), n) == doctest::Approx(rmsOf(right.data(), n)).epsilon(0.001));
}

TEST_CASE("mixer: full left pan silences the right channel") {
    Sample s;
    s.channels = 1;
    s.rate = 44100;
    s.pcm.assign(512, 12000);

    Mixer mixer;
    mixer.setDeviceRate(44100);
    PlayParams params;
    params.pan = -1.0f;
    mixer.startVoice(&s, params);

    std::vector<float> out(512 * 2);
    mixer.render(out.data(), 512);
    for (int i = 0; i < 512; ++i) {
        CHECK(out[i * 2 + 1] == doctest::Approx(0.0f).epsilon(1e-6));
    }
}

TEST_CASE("mixer: looping continues past the sample end") {
    Sample s;
    s.channels = 1;
    s.rate = 44100;
    s.pcm.assign(100, 10000);  // constant non-zero "sample"

    Mixer mixer;
    mixer.setDeviceRate(44100);
    PlayParams params;
    params.loop = true;
    as3d::VoiceId v = mixer.startVoice(&s, params);
    REQUIRE(v != as3d::kInvalidVoiceId);

    std::vector<float> out(250 * 2, 0.0f);
    mixer.render(out.data(), 250);  // 2.5x the sample length
    CHECK(mixer.activeVoiceCount() == 1);
    // Audio past the natural end must still be non-silent.
    CHECK(std::fabs(out[240 * 2]) > 0.01f);
}

TEST_CASE("mixer: a non-looping voice ends and frees its slot") {
    Sample s;
    s.channels = 1;
    s.rate = 44100;
    s.pcm.assign(100, 10000);

    Mixer mixer;
    mixer.setDeviceRate(44100);
    PlayParams params;
    params.loop = false;
    mixer.startVoice(&s, params);

    std::vector<float> out(200 * 2, 0.0f);
    mixer.render(out.data(), 200);
    CHECK(mixer.activeVoiceCount() == 0);
}

TEST_CASE("mixer: 40 simultaneous plays never exceed 32 voices and do not crash") {
    Sample s;
    s.channels = 1;
    s.rate = 44100;
    s.pcm.assign(1000, 5000);

    Mixer mixer;
    mixer.setDeviceRate(44100);
    PlayParams params;
    params.loop = true;
    for (int i = 0; i < 40; ++i) {
        as3d::VoiceId v = mixer.startVoice(&s, params);
        CHECK(v != as3d::kInvalidVoiceId);
    }
    CHECK(mixer.activeVoiceCount() <= as3d::audio::kMaxVoices);
    CHECK(mixer.voicesStolenCount() >= 8);  // 40 - 32

    std::vector<float> out(64 * 2, 0.0f);
    mixer.render(out.data(), 64);  // must not crash
    for (float f : out) CHECK_FALSE(std::isnan(f));
}

TEST_CASE("mixer: resampling 22050 -> 44100 doubles the frame count within one frame") {
    Sample s;
    s.channels = 1;
    s.rate = 22050;
    s.pcm.assign(10, 9000);  // 10 source frames

    Mixer mixer;
    mixer.setDeviceRate(44100);
    PlayParams params;
    params.loop = false;
    mixer.startVoice(&s, params);

    std::vector<float> out(19 * 2, 0.0f);
    mixer.render(out.data(), 19);
    CHECK(mixer.activeVoiceCount() == 1);  // not finished yet after 19 output frames

    std::vector<float> tail(2, 0.0f);
    mixer.render(tail.data(), 1);  // the 20th output frame (exactly double the 10 source frames) finishes it
    CHECK(mixer.activeVoiceCount() == 0);
}

// ---------------------------------------------------------------------------
// Music (libopenmpt / MO3)
// ---------------------------------------------------------------------------

namespace {
as3d::Vfs makeGameVfs() {
    as3d::Vfs vfs;
    vfs.mount(as3d::makeDirSource(testdata::extractedDir()));
    return vfs;
}
}  // namespace

TEST_CASE("music: each of the five tracks loads and renders plausible audio") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    for (int i = 1; i <= 5; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "music\\track%02d.mo3", i);

        as3d::Vfs vfs = makeGameVfs();
        as3d::Audio audio;
        REQUIRE(audio.init(vfs, as3d::AudioBackend::Null, 44100));
        CAPTURE(path);
        REQUIRE(audio.playMusic(path, /*loop=*/false));

        const int frames = 44100 * 10;
        std::vector<float> out(static_cast<size_t>(frames) * 2);
        audio.render(out.data(), frames);

        std::vector<float> left(frames), right(frames);
        bool anyNan = false, anyClip = false;
        for (int f = 0; f < frames; ++f) {
            left[f] = out[f * 2 + 0];
            right[f] = out[f * 2 + 1];
            if (std::isnan(left[f]) || std::isnan(right[f])) anyNan = true;
            if (std::fabs(left[f]) > 1.0f || std::fabs(right[f]) > 1.0f) anyClip = true;
        }
        CHECK_FALSE(anyNan);
        CHECK_FALSE(anyClip);
        CHECK(rmsOf(left.data(), frames) > 0.001f);
        CHECK(rmsOf(right.data(), frames) > 0.001f);
    }
}

TEST_CASE("music: rendering is deterministic across independent instances") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    const int frames = 44100 * 5;
    std::vector<float> a(static_cast<size_t>(frames) * 2), b(static_cast<size_t>(frames) * 2);

    {
        as3d::Vfs vfs = makeGameVfs();
        as3d::Audio audio;
        REQUIRE(audio.init(vfs, as3d::AudioBackend::Null, 44100));
        REQUIRE(audio.playMusic("music\\track01.mo3", false));
        audio.render(a.data(), frames);
    }
    {
        as3d::Vfs vfs = makeGameVfs();
        as3d::Audio audio;
        REQUIRE(audio.init(vfs, as3d::AudioBackend::Null, 44100));
        REQUIRE(audio.playMusic("music\\track01.mo3", false));
        audio.render(b.data(), frames);
    }

    std::uint64_t hashA = fnv1a(a.data(), a.size() * sizeof(float));
    std::uint64_t hashB = fnv1a(b.data(), b.size() * sizeof(float));
    CHECK(hashA == hashB);
}

TEST_CASE("music: looping restarts playback after the track ends") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    as3d::Vfs vfs = makeGameVfs();
    as3d::Audio audio;
    REQUIRE(audio.init(vfs, as3d::AudioBackend::Null, 44100));
    REQUIRE(audio.playMusic("music\\track03.mo3", /*loop=*/true));  // the shortest track

    // track03 ("Fear: AirStrike Boss") is ~78s; render a couple of seconds
    // past its natural end and check the tail is still producing audio
    // instead of trailing off into silence.
    const int totalSeconds = 80;
    const int tailSeconds = 2;
    const int frames = 44100 * totalSeconds;
    std::vector<float> out(static_cast<size_t>(frames) * 2);
    audio.render(out.data(), frames);

    const int tailFrames = 44100 * tailSeconds;
    std::vector<float> tail(tailFrames);
    for (int i = 0; i < tailFrames; ++i) {
        int idx = frames - tailFrames + i;
        tail[i] = out[idx * 2 + 0];
    }
    CHECK(rmsOf(tail.data(), tailFrames) > 0.001f);
    CHECK(audio.isMusicPlaying());
}

TEST_CASE("music: title metadata is readable") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    as3d::Vfs vfs = makeGameVfs();
    as3d::Audio audio;
    REQUIRE(audio.init(vfs, as3d::AudioBackend::Null, 44100));
    REQUIRE(audio.playMusic("music\\track01.mo3", false));
    CHECK(audio.musicTitle().size() > 0);
}

TEST_CASE("music: the game-over jump to pattern order 35 (or 0 for a shorter module)") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    as3d::Vfs vfs = makeGameVfs();
    as3d::Audio audio;
    REQUIRE(audio.init(vfs, as3d::AudioBackend::Null, 44100));
    CHECK_FALSE(audio.jumpMusicToOrder(as3d::kGameOverMusicOrder)); // no music yet
    CHECK(audio.musicOrder() == -1);
    int jumped35 = 0;
    for (int i = 1; i <= 5; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "music\\track%02d.mo3", i);
        CAPTURE(path);
        REQUIRE(audio.playMusic(path, true));
        std::vector<float> out(2 * 4410);
        audio.render(out.data(), 4410);
        CHECK(audio.musicOrder() == 0);
        REQUIRE(audio.jumpMusicToOrder(as3d::kGameOverMusicOrder));
        int o = audio.musicOrder();
        CHECK((o == 35 || o == 0));
        jumped35 += o == 35 ? 1 : 0;
        audio.render(out.data(), 4410); // keeps playing from there
        CHECK(audio.isMusicPlaying());
        CHECK(audio.jumpMusicToOrder(100000)); // out of range: order 0
        CHECK(audio.musicOrder() == 0);
    }
    MESSAGE("modules with a game-over section at order 35: " << jumped35 << " of 5");
}
