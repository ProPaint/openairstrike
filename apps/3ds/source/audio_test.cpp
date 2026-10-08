#include "audio_test.h"

#include <3ds.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "as3d/audio.h"
#include "as3d/core.h"
#include "as3d/defs.h"
#include "as3d/game_profile.h"
#include "as3d/precache.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "audio_device_ndsp.h"

namespace audiotest {
namespace {

using namespace as3d;

std::FILE* g_log = nullptr;

void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::fputs(buf, stdout);
    if (g_log) {
        std::fputs(buf, g_log);
        std::fflush(g_log);
    }
}

double ms(u64 ticks) { return static_cast<double>(ticks) * 1000.0 / SYSCLOCK_ARM11; }

constexpr const char* kTonePath = "audiotest\\tone.wav";
constexpr int kToneRate = 32728;

// One in-memory file: the generated tone.
class ToneSource final : public IFileSource {
public:
    explicit ToneSource(Blob wav) : wav_(std::move(wav)) {}
    bool exists(const std::string& path) override { return path == kTonePath; }
    bool read(const std::string& path, Blob& out) override {
        if (path != kTonePath) return false;
        out = wav_;
        return true;
    }
    void list(std::vector<std::string>& out) override { out.push_back(kTonePath); }

private:
    Blob wav_;
};

void put16(Blob& b, unsigned v) {
    b.push_back(static_cast<u8>(v & 0xFF));
    b.push_back(static_cast<u8>((v >> 8) & 0xFF));
}
void put32(Blob& b, unsigned v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}
void putTag(Blob& b, const char* t) { b.insert(b.end(), t, t + 4); }

// 1 s of 440 Hz, mono PCM16 at half scale.
Blob makeTone() {
    const int n = kToneRate;
    Blob b;
    putTag(b, "RIFF");
    put32(b, 36 + 2 * n);
    putTag(b, "WAVE");
    putTag(b, "fmt ");
    put32(b, 16);
    put16(b, 1); // PCM
    put16(b, 1); // mono
    put32(b, kToneRate);
    put32(b, kToneRate * 2);
    put16(b, 2);
    put16(b, 16);
    putTag(b, "data");
    put32(b, 2 * n);
    for (int i = 0; i < n; ++i) {
        const double v = 0.5 * std::sin(2.0 * M_PI * 440.0 * i / kToneRate);
        put16(b, static_cast<unsigned>(static_cast<int>(std::lround(v * 32767.0))) & 0xFFFF);
    }
    return b;
}

// std::thread and std::mutex under libctru: two threads add 100000 each under one mutex.
void checkStdThread() {
    std::mutex m;
    long count = 0;
    const u64 t0 = svcGetSystemTick();
    auto work = [&]() {
        for (int i = 0; i < 100000; ++i) {
            std::lock_guard<std::mutex> lock(m);
            ++count;
        }
    };
    std::thread t(work);
    work();
    t.join();
    say("std::thread + std::mutex: count %ld (expected 200000) in %.1f ms: %s\n", count, ms(svcGetSystemTick() - t0),
        count == 200000 ? "ok" : "WRONG");
}

// Mission 1's precache sounds; the looping rotor first, a gun second, then the rest.
std::vector<std::string> missionSounds(Vfs& vfs, const DefDatabase& db, const GameProfile& game) {
    WorldConfig wc;
    wc.rules = &game.rules;
    auto world = std::make_unique<World>();
    world->init(vfs, db, wc);
    std::string err;
    if (!world->loadLevel("1", &err)) {
        say("mission 1: load failed: %s\n", err.c_str());
        return {};
    }
    world->precacheScripts();
    std::vector<std::string> all;
    if (const PrecacheSet* s = world->precacheSet()) all = s->sounds;
    say("mission 1 precache: %d sounds\n", static_cast<int>(all.size()));
    if (g_log)
        for (const std::string& s : all) std::fprintf(g_log, "  %s\n", s.c_str());
    auto pick = [&](std::initializer_list<const char*> keys) -> std::string {
        for (const char* k : keys)
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i].find(k) != std::string::npos) {
                    std::string r = all[i];
                    all.erase(all.begin() + static_cast<long>(i));
                    return r;
                }
        return std::string();
    };
    std::vector<std::string> out;
    std::string a = pick({"rotor", "heli", "engine"});
    std::string b = pick({"gun", "mg", "shot", "fire"});
    if (a.empty() && !all.empty()) a = pick({""});
    if (b.empty() && !all.empty()) b = pick({""});
    std::string c = pick({"expl", ""});
    for (const std::string* s : {&a, &b, &c})
        if (!s->empty()) out.push_back(*s);
    return out;
}

struct Second {
    u32 fills = 0, underruns = 0;
};

void report(const char* tag, Audio& audio, Second& prev) {
    const audio::ndsp::NdspStats s = audio::ndsp::stats(true);
    const AudioStats a = audio.stats();
    if (!s.open) {
        say("[%s] null device: voices %d, started %llu\n", tag, a.activeVoices,
            static_cast<unsigned long long>(a.totalVoicesStarted));
        return;
    }
    say("[%s] fills %lu (+%lu) underruns %lu (+%lu) peak %d max_fill %.2f ms cpu %.1f%% dropped %lu voices %d\n", tag,
        static_cast<unsigned long>(s.fills), static_cast<unsigned long>(s.fills - prev.fills),
        static_cast<unsigned long>(s.underruns), static_cast<unsigned long>(s.underruns - prev.underruns), s.peak,
        s.maxFillUs / 1000.0, s.openUs ? 100.0 * static_cast<double>(s.renderUs) / static_cast<double>(s.openUs) : 0.0,
        static_cast<unsigned long>(s.droppedFrames), a.activeVoices);
    prev.fills = s.fills;
    prev.underruns = s.underruns;
}

} // namespace

void run(Vfs& vfs, const DefDatabase& db, const GameProfile& game, const std::string& dir, int frames) {
    g_log = std::fopen((dir + "/audiotest_log.txt").c_str(), "w");
    const int total = frames > 0 ? frames : 360;
    say("audio test (M2.3): %d frames\n", total);
    setLogLevel(LogLevel::Info);

    checkStdThread();

    const std::vector<std::string> sounds = missionSounds(vfs, db, game);
    vfs.mount(std::make_unique<ToneSource>(makeTone()));

    Audio audio;
    bool real = audio.init(vfs, AudioBackend::Platform, 32728);
    if (!real) {
        say("platform device: none (ndspInit failed?), Null device fallback\n");
        audio.init(vfs, AudioBackend::Null, 32728);
    } else {
        say("platform device: %s\n", audio::ndsp::description());
    }
    say("device rate %d Hz\n", audio.deviceRate());

    const SoundId tone = audio.loadSound(kTonePath);
    std::vector<SoundId> ids;
    for (const std::string& s : sounds) {
        const SoundId id = audio.loadSound(s.c_str());
        say("sound %s: %s\n", s.c_str(), id != kInvalidSoundId ? "loaded" : "MISSING");
        ids.push_back(id);
    }
    say("tone: %s (expected peak about 11000: 0.5 x centre pan x tanh limiter)\n",
        tone != kInvalidSoundId ? "loaded" : "MISSING");

    Second prev;
    VoiceId loop = kInvalidVoiceId;
    const u64 t0 = svcGetSystemTick();
    int f = 0;
    for (; f < total && aptMainLoop(); ++f) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        if (f == 0) audio.play(tone);
        if (f == 75 && ids.size() > 0) {
            PlayParams p;
            p.loop = true;
            loop = audio.play(ids[0], p);
        }
        if (f >= 90 && f < 210 && (f - 90) % 12 == 0 && ids.size() > 1) audio.play(ids[1]);
        if (f == 240 && ids.size() > 2) audio.play(ids[2]);
        if (f > 0 && f % 60 == 0) {
            char tag[16];
            std::snprintf(tag, sizeof tag, "s%d", f / 60);
            report(tag, audio, prev);
        }
        gspWaitForVBlank();
    }
    if (loop != kInvalidVoiceId) audio.stop(loop);
    say("ran %d frames in %.0f ms\n", f, ms(svcGetSystemTick() - t0));
    report("end", audio, prev);
    const u64 c0 = svcGetSystemTick();
    audio.shutdown();
    say("shutdown (feeder joined, ndspExit) in %.1f ms\n", ms(svcGetSystemTick() - c0));
    say("audio test done\n");
    if (g_log) std::fclose(g_log);
    g_log = nullptr;
}

} // namespace audiotest
