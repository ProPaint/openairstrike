// The 3DS's pre-rendered music: which files, and the render (as3d/music_prerender.h).
#include "as3d/music_prerender.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <initializer_list>

#include "as3d/audio.h"
#include "as3d/vfs.h"
#include "music_decoder.h"

namespace as3d::audio {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

bool isTrackerModulePath(const std::string& path) {
    const std::string p = lower(path);
    static const char* kExt[] = {".mo3", ".it", ".xm", ".s3m", ".mod"};
    for (const char* ext : kExt) {
        const size_t n = std::strlen(ext);
        if (p.size() >= n && p.compare(p.size() - n, n, ext) == 0) return true;
    }
    return false;
}

std::vector<PrerenderJob> prerenderJobs(Vfs& vfs) {
    std::vector<PrerenderJob> jobs;
    for (const std::string& name : vfs.list("music\\")) {
        if (!isTrackerModulePath(name)) continue;
        std::string base = name.substr(name.find_last_of("\\/") + 1);
        base = lower(base.substr(0, base.find_last_of('.')));
        jobs.push_back({name, base + ".ogg", 0});
        Blob data;
        MusicDecoder dec;
        if (vfs.read(name, data) && dec.load(data) && dec.numOrders() > kGameOverMusicOrder)
            jobs.push_back({name, base + "_gameover.ogg", kGameOverMusicOrder});
    }
    return jobs;
}

bool renderModule(const Blob& module, int rate, int order,
                  const std::function<bool(const std::int16_t*, std::size_t)>& sink, RenderStats& stats,
                  std::string& error, const std::function<bool(double)>& progress) {
    stats = RenderStats{};
    MusicDecoder dec;
    if (!dec.load(module)) {
        error = "cannot load the module";
        return false;
    }
    dec.setLoop(false);
    if (order > 0 && !dec.setOrder(order)) {
        error = "no pattern order " + std::to_string(order) + " (" + std::to_string(dec.numOrders()) + " orders)";
        return false;
    }
    const double duration = dec.durationSeconds();
    constexpr int kChunk = 4096;
    const size_t maxFrames = static_cast<size_t>(kPrerenderedMusicMaxSeconds * rate);
    std::vector<float> chunk(2 * kChunk);
    std::vector<std::int16_t> out;
    out.reserve(2 * kChunk);
    size_t rendered = 0;       // frames from the decoder (as audio_tool counted them)
    size_t pendingSilence = 0; // digital silence not handed on yet: kept only if sound follows
    size_t sinceProgress = 0;
    const std::int16_t zeros[2 * 256] = {};
    auto flushSilence = [&]() -> bool {
        while (pendingSilence > 0) {
            const size_t n = std::min<size_t>(pendingSilence, 256);
            if (!sink(zeros, n)) return false;
            stats.frames += n;
            pendingSilence -= n;
        }
        return true;
    };
    while (rendered < maxFrames) {
        if (!dec.read(rate, chunk.data(), kChunk)) break;  // the module ended (padded with silence)
        rendered += kChunk;
        out.clear();
        for (int i = 0; i < kChunk; ++i) {
            const float l = chunk[2 * static_cast<size_t>(i)], r = chunk[2 * static_cast<size_t>(i) + 1];
            if (l == 0.0f && r == 0.0f) {
                ++pendingSilence;
                continue;
            }
            if (pendingSilence > 0) {
                if (!out.empty()) {
                    if (!sink(out.data(), out.size() / 2)) {
                        error = "stopped";
                        return false;
                    }
                    stats.frames += out.size() / 2;
                    out.clear();
                }
                if (!flushSilence()) {
                    error = "stopped";
                    return false;
                }
            }
            for (const float s0 : {l, r}) {
                const float a = std::fabs(s0);
                stats.peak = std::max(stats.peak, a);
                stats.clipped += a > 1.0f ? 1 : 0;
                const float s = std::max(-1.0f, std::min(1.0f, s0));
                out.push_back(static_cast<std::int16_t>(std::lround(s * 32767.0f)));
            }
        }
        if (!out.empty()) {
            if (!sink(out.data(), out.size() / 2)) {
                error = "stopped";
                return false;
            }
            stats.frames += out.size() / 2;
        }
        sinceProgress += kChunk;
        if (progress && sinceProgress >= static_cast<size_t>(rate)) {
            sinceProgress = 0;
            const double f = duration > 0 ? static_cast<double>(rendered) / (duration * rate) : 0.0;
            if (!progress(std::min(1.0, f))) {
                error = "stopped";
                return false;
            }
        }
    }
    if (stats.frames == 0) {
        error = "rendered nothing";
        return false;
    }
    return true;
}

}  // namespace as3d::audio
