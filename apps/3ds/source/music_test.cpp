#include "music_test.h"

#include <3ds.h>
#include <malloc.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "as3d/audio.h"
#include "as3d/core.h"
#include "as3d/defs.h"
#include "as3d/vfs.h"
#include "audio_device_ndsp.h"

namespace musictest {
namespace {

using namespace as3d;

constexpr int kRate = 32728;
constexpr double kUsableShare = 25.0; // the plan's rule: at most 25 % of core 1

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
size_t heapUsed() { return static_cast<size_t>(mallinfo().uordblks); }

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string levelMusic(const DefDatabase& db, const char* id) {
    for (const LevelDef& d : db.levels())
        if (d.id == id) return d.music;
    return std::string();
}

// The menu's and mission 1's modules; the largest one in music\ when they are the same.
std::vector<std::string> pickModules(Vfs& vfs, const DefDatabase& db, const std::string& files) {
    std::vector<std::string> out;
    auto add = [&](const std::string& p) {
        if (p.empty()) return;
        for (const std::string& q : out)
            if (lower(q) == lower(p)) return;
        out.push_back(p);
    };
    if (!files.empty()) {
        size_t a = 0;
        while (a <= files.size()) {
            size_t b = files.find(',', a);
            if (b == std::string::npos) b = files.size();
            add(files.substr(a, b - a));
            a = b + 1;
        }
        return out;
    }
    const std::string menu = levelMusic(db, "intro1");
    const std::string m1 = levelMusic(db, "mission1");
    say("menu (intro1) module: %s; mission 1 module: %s\n", menu.empty() ? "-" : menu.c_str(),
        m1.empty() ? "-" : m1.c_str());
    add(menu);
    add(m1);
    if (out.size() < 2) {
        std::string best;
        size_t bestSize = 0;
        for (const std::string& p : vfs.list("music")) {
            const std::string l = lower(p);
            if (l.size() < 4 || (l.compare(l.size() - 4, 4, ".mo3") != 0 && l.compare(l.size() - 3, 3, ".xm") != 0 &&
                                 l.compare(l.size() - 3, 3, ".it") != 0))
                continue;
            Blob b;
            if (vfs.read(p, b) && b.size() > bestSize) {
                bestSize = b.size();
                best = p;
            }
        }
        if (!best.empty()) say("the largest module: %s (%u bytes)\n", best.c_str(), static_cast<unsigned>(bestSize));
        add(best);
    }
    return out;
}

struct PassResult {
    std::string module;
    std::string decoder;
    bool fast = true;
    bool loaded = false;
    double loadMs = 0;
    double share = 0;      // decoder time / audio decoded, %
    double worstShare = 0; // the worst second
    double maxCallMs = 0;  // the longest decode of one mixer call
    int maxCallFrames = 0;
    double feederCpu = 0;  // the feeder's time / wall time over the pass, %
    double maxFillMs = 0;  // the longest buffer fill (since the device opened)
    unsigned underruns = 0;
    bool device = false;
};

double sharePct(u64 us, u64 frames) {
    return frames ? 100.0 * static_cast<double>(us) / (1e6 * static_cast<double>(frames) / kRate) : 0.0;
}

PassResult pass(Audio& audio, bool device, const std::string& module, bool fast, int frames) {
    PassResult r;
    r.module = module;
    r.fast = fast;
    r.device = device;
    const size_t heap0 = heapUsed();
    const u64 l0 = svcGetSystemTick();
    r.loaded = audio.playMusic(module.c_str(), true);
    r.loadMs = ms(svcGetSystemTick() - l0);
    r.decoder = audio.musicDecoderName();
    say("-- %s, %s, decoder %s: load %s in %.0f ms, heap +%.0f KB (file and decoded data), title '%s'\n",
        module.c_str(), fast ? "current clock" : "268 MHz (speedup off)", r.decoder.c_str(), r.loaded ? "ok" : "FAILED",
        r.loadMs,
        (static_cast<double>(heapUsed()) - static_cast<double>(heap0)) / 1024.0, audio.musicTitle().c_str());
    if (!r.loaded) return r;

    MusicDecodeStats prev = audio.musicDecodeStats(true);
    const MusicDecodeStats start = prev;
    audio::ndsp::NdspStats nprev = audio::ndsp::stats(true);
    const audio::ndsp::NdspStats nstart = nprev;
    const u64 t0 = svcGetSystemTick();
    std::vector<float> buf;
    if (!device) buf.resize(2 * 1024);
    int f = 0;
    for (; f < frames && aptMainLoop(); ++f) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        if (device) {
            gspWaitForVBlank();
        } else {
            // Null device: the audio of one displayed frame (545.5 frames), rendered here.
            const int n = (f % 2) ? 546 : 545;
            audio.render(buf.data(), n);
        }
        if ((f + 1) % 60 == 0) {
            const MusicDecodeStats d = audio.musicDecodeStats(true);
            const double s = sharePct(d.decodeUs - prev.decodeUs, d.frames - prev.frames);
            r.worstShare = std::max(r.worstShare, s);
            if (d.maxCallUs / 1000.0 > r.maxCallMs) {
                r.maxCallMs = d.maxCallUs / 1000.0;
                r.maxCallFrames = d.maxCallFrames;
            }
            const audio::ndsp::NdspStats n = audio::ndsp::stats(true);
            if (n.open) {
                const double cpu = n.openUs > nprev.openUs ? 100.0 * static_cast<double>(n.renderUs - nprev.renderUs) /
                                                                 static_cast<double>(n.openUs - nprev.openUs)
                                                           : 0.0;
                say("[s%d] decoder %.1f%% (%.1f ms for %llu frames) max_call %.2f ms (%d frames) | fills +%lu "
                    "underruns %lu (+%lu) max_fill %.2f ms feeder_cpu %.1f%% peak %d\n",
                    (f + 1) / 60, s, (d.decodeUs - prev.decodeUs) / 1000.0,
                    static_cast<unsigned long long>(d.frames - prev.frames), d.maxCallUs / 1000.0, d.maxCallFrames,
                    static_cast<unsigned long>(n.fills - nprev.fills), static_cast<unsigned long>(n.underruns),
                    static_cast<unsigned long>(n.underruns - nprev.underruns), n.maxFillUs / 1000.0, cpu, n.peak);
                nprev = n;
            } else {
                say("[s%d] decoder %.1f%% (%.1f ms for %llu frames) max_call %.2f ms (%d frames) | null device\n",
                    (f + 1) / 60, s, (d.decodeUs - prev.decodeUs) / 1000.0,
                    static_cast<unsigned long long>(d.frames - prev.frames), d.maxCallUs / 1000.0, d.maxCallFrames);
            }
            prev = d;
        }
    }
    const double wallMs = ms(svcGetSystemTick() - t0);
    const MusicDecodeStats d = audio.musicDecodeStats();
    r.share = sharePct(d.decodeUs - start.decodeUs, d.frames - start.frames);
    const audio::ndsp::NdspStats n = audio::ndsp::stats();
    if (n.open) {
        r.feederCpu = n.openUs > nstart.openUs ? 100.0 * static_cast<double>(n.renderUs - nstart.renderUs) /
                                                     static_cast<double>(n.openUs - nstart.openUs)
                                               : 0.0;
        r.maxFillMs = n.maxFillUs / 1000.0;
        r.underruns = n.underruns - nstart.underruns;
    }
    say("   %d frames in %.0f ms, %.2f s of music decoded\n", f, wallMs,
        static_cast<double>(d.frames - start.frames) / kRate);
    audio.stopMusic();
    return r;
}

void verdict(const PassResult& r) {
    if (!r.loaded) {
        say("MUSICTEST module=%s clock=%s decoder=%s load=FAILED\n", r.module.c_str(), r.fast ? "current" : "268MHz",
            r.decoder.c_str());
        return;
    }
    say("MUSICTEST module=%s clock=%s decoder=%s load_ms=%.0f decoder_share=%.1f%% worst_second=%.1f%% max_call_ms=%.2f "
        "(%d frames) feeder_cpu=%.1f%% max_fill_ms=%.2f underruns=%u device=%s -> %s (rule: <= %.0f%% of core 1)\n",
        r.module.c_str(), r.fast ? "current" : "268MHz", r.decoder.c_str(), r.loadMs, r.share, r.worstShare, r.maxCallMs,
        r.maxCallFrames, r.feederCpu, r.maxFillMs, r.underruns, r.device ? "ndsp" : "null",
        r.worstShare <= kUsableShare ? "USABLE" : "TOO SLOW", kUsableShare);
}

} // namespace

void run(Vfs& vfs, const DefDatabase& db, const std::string& dir, const Options& o) {
    g_log = std::fopen((dir + "/musictest_log.txt").c_str(), "w");
    const int frames = o.frames > 0 ? o.frames : 600;
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    say("music test (M2.4): %s 3DS, %d frames per pass, interpolation %d taps%s\n", n3ds ? "New" : "Old", frames,
        o.taps, o.taps ? "" : " (libopenmpt's default, 8)");
    say("built %s libopenmpt (AS3D_WITH_OPENMPT=%d), %s tremor (AS3D_WITH_TREMOR=%d)\n",
        AS3D_WITH_OPENMPT ? "with" : "WITHOUT", AS3D_WITH_OPENMPT, AS3D_WITH_TREMOR ? "with" : "without",
        AS3D_WITH_TREMOR);
    setLogLevel(LogLevel::Info);
    if (n3ds) osSetSpeedupEnable(true);

    const std::vector<std::string> modules = pickModules(vfs, db, o.files);
    Audio audio;
    const bool device = audio.init(vfs, AudioBackend::Platform, kRate);
    if (!device) {
        say("platform device: none (ndspInit failed?): the Null device, decoding on the main thread\n");
        audio.init(vfs, AudioBackend::Null, kRate);
    } else {
        say("platform device: %s\n", audio::ndsp::description());
    }
    audio.setMusicInterpolation(o.taps);
    audio.setMusicVolume(0.5f); // config.ini MusicVolume as shipped

    // Each module pre-rendered (<dir>/<game>/music/<name>.ogg, when the file is there) and as a
    // module (libopenmpt).
    const std::string oggDir = dir + "/" + o.game + "/music";
    std::vector<std::pair<std::string, bool>> runs; // module, pre-rendered
    for (const std::string& m : modules) {
        std::string base = m.substr(m.find_last_of("\\/") + 1);
        base = lower(base.substr(0, base.find_last_of('.')));
        const std::string ogg = oggDir + "/" + base + ".ogg";
        if (std::FILE* f = std::fopen(ogg.c_str(), "rb")) {
            std::fclose(f);
            if (AS3D_WITH_TREMOR) runs.emplace_back(m, true);
            say("%s: pre-rendered %s%s\n", m.c_str(), ogg.c_str(), AS3D_WITH_TREMOR ? "" : " (ignored: no tremor)");
        } else {
            say("%s: no pre-rendered %s\n", m.c_str(), ogg.c_str());
        }
        if (AS3D_WITH_OPENMPT) runs.emplace_back(m, false);
    }
    std::vector<PassResult> results;
    for (int clock = 0; clock < (n3ds ? 2 : 1); ++clock) {
        if (clock == 1) osSetSpeedupEnable(false);
        for (const auto& run : runs) {
            if (!aptMainLoop()) break;
            audio.setPrerenderedMusicDir(run.second ? oggDir : std::string());
            results.push_back(pass(audio, device, run.first, clock == 0, frames));
        }
    }
    if (n3ds) osSetSpeedupEnable(true);
    for (const PassResult& r : results) verdict(r);
    audio.shutdown();
    say("music test done\n");
    if (g_log) std::fclose(g_log);
    g_log = nullptr;
}

} // namespace musictest
