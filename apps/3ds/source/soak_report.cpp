#include "soak_report.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace ctrapp {

namespace {

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= s.size()) {
        size_t next = s.find(sep, at);
        if (next == std::string::npos) next = s.size();
        out.push_back(s.substr(at, next - at));
        at = next + 1;
    }
    return out;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

bool toInt(const std::string& v, long lo, long hi, long* out) {
    if (v.empty()) return false;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (*end != '\0' || n < lo || n > hi) return false;
    *out = n;
    return true;
}

std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

double pct(unsigned part, unsigned whole) { return whole ? 100.0 * part / whole : 0.0; }

} // namespace

bool parseSoakList(const std::string& text, std::vector<SoakStage>* out) {
    std::vector<SoakStage> stages;
    for (const std::string& raw : split(text, ',')) {
        const std::string entry = trim(raw);
        if (entry.empty()) continue;
        const std::vector<std::string> f = split(entry, ':');
        if (f.size() < 3) return false;
        SoakStage s;
        s.game = trim(f[0]);
        long level = 0;
        if (s.game.empty() || !toInt(trim(f[1]), 1, 99, &level)) return false;
        s.level = static_cast<int>(level);
        const std::string eyes = trim(f[2]);
        if (eyes == "mono") s.stereo = false;
        else if (eyes == "stereo") s.stereo = true;
        else return false;
        for (size_t i = 3; i < f.size(); ++i) {
            const std::string opt = trim(f[i]);
            const size_t eq = opt.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 >= opt.size()) return false;
            const std::string key = opt.substr(0, eq), val = opt.substr(eq + 1);
            for (char c : key)
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
            for (char c : val)
                if (c == ' ' || c == '\t' || c == '=' || static_cast<unsigned char>(c) < 0x20) return false;
            if (key == "mode" || key == "game" || key == "level" || key == "slider" || key == "soak" || key == "report_to")
                return false;
            s.options.emplace_back(key, val);
        }
        stages.push_back(s);
    }
    if (stages.empty()) return false;
    *out = stages;
    return true;
}

std::vector<SoakStage> defaultSoakStages(const std::vector<std::string>& gameKeys) {
    std::vector<SoakStage> out;
    for (const std::string& g : gameKeys)
        for (int level = 1; level <= 2; ++level)
            for (int stereo = 0; stereo < 2; ++stereo) out.push_back(SoakStage{g, level, stereo != 0, {}});
    return out;
}

std::string soakStageName(const SoakStage& s) {
    std::string n = s.game + ":" + std::to_string(s.level) + (s.stereo ? ":stereo" : ":mono");
    for (const auto& o : s.options) n += ":" + o.first + "=" + o.second;
    return n;
}

std::string soakStageOptionsText(const SoakStage& s) {
    std::string t;
    for (const auto& o : s.options) t += o.first + "=" + o.second + "\n";
    return t;
}

std::string soakStageOptions(const SoakStage& s) {
    std::string t;
    for (const auto& o : s.options) t += (t.empty() ? "" : ":") + o.first + "=" + o.second;
    return t.empty() ? std::string("none") : t;
}

std::string soakVerdict(const std::vector<SoakStageResult>& results, const std::vector<std::string>& failNotes) {
    std::string out;
    bool fail = false;
    auto line = [&](bool ok, const std::string& name, const std::string& value) {
        fail = fail || !ok;
        out += std::string(ok ? "PASS " : "FAIL ") + name + " " + value + "\n";
    };
    for (const std::string& n : failNotes) line(false, "note", n);

    unsigned terrain = 0, gpuAll = 0, probes = 0, marks = 0, fence = 0, busy = 0, texIn = 0, ran = 0, finished = 0;
    unsigned washEvals = 0, washFrames = 0, washEpisodes = 0, prime = 0;
    unsigned mismatchProbes = 0, reread = 0, canaryOk = 0, canaryStale = 0;
    long long lazy = 0, stamps = 0;
    for (const SoakStageResult& r : results) {
        if (!r.ran) continue;
        ++ran;
        finished += r.finished ? 1 : 0;
        terrain += r.terrainGpuMismatches;
        gpuAll += r.gpuMismatches;
        probes += r.probes;
        marks += r.autoMarks;
        washEvals += r.washEvals;
        washFrames += r.washFrames;
        washEpisodes += r.washEpisodes;
        prime += r.primeGpuMismatches;
        mismatchProbes += r.mismatchProbes;
        reread += r.rereadChanged;
        canaryOk += r.canaryOk;
        canaryStale += r.canaryStale;
        fence += r.fence;
        busy += r.gpuBusy;
        texIn += r.texInFrame;
        lazy += r.lazyLoads;
        stamps += r.lazyStamps;
    }
    // The water wash: the terrain's depth state lost on the GPU (any terrain GPU mismatch), or the
    // picture itself washed (the detector's washed frames). With neither a probe nor a judged frame
    // the check did not run.
    line(terrain == 0 && washFrames == 0 && (probes > 0 || washEvals > 0), "wash",
         fmt("terrain_gpu_mismatches=%u gpu_mismatches=%u probes=%u marks=%u wash_frames=%u wash_episodes=%u "
             "wash_evals=%u prime_mismatches=%u mismatch_probes=%u reread_changed=%u canary_ok=%u/%u canary_stale=%u",
             terrain, gpuAll, probes, marks, washFrames, washEpisodes, washEvals, prime, mismatchProbes, reread, canaryOk,
             probes, canaryStale));
    for (const SoakStageResult& r : results) {
        if (!r.ran || r.washEpisodes == 0) continue;
        line(false, "wash " + soakStageName(r.stage),
             fmt("first_frame=%u episodes=%u frames=%u longest=%u max_samples=%u", r.washFirstFrame, r.washEpisodes,
                 r.washFrames, r.washLongest, r.washMaxSamples));
    }
    for (const SoakStageResult& r : results) {
        if (!r.ran || !r.finished) continue;
        const std::string name = soakStageName(r.stage);
        if (r.graceFrames == 0) { // a stage shorter than the pacer's grace measures no rate
            out += "INFO " + std::string(r.stage.stereo ? "stereo " : "mono60 ") + name +
                   fmt(" no frames after the pacer's grace (%u frames in all) gpu_avg=%.2fms gpu_p95=%.2fms\n", r.frames,
                       r.gpuAvg, r.gpuP95);
            continue;
        }
        if (!r.stage.stereo) {
            const double at60 = pct(r.graceAt60, r.graceFrames);
            line(at60 >= kSoakMono60Pct, "mono60 " + name,
                 fmt("at60=%.1f%% (%u of %u frames after the grace) misses60=%u (%.2f%%) excused60=%u pace_changes=%u "
                     "hz_end=%d hitches60=%u (%.2f%%) gpu_late60=%u behind=%u",
                     at60, r.graceAt60, r.graceFrames, r.misses60, pct(r.misses60, r.graceAt60 ? r.graceAt60 : 1),
                     r.excused60, r.paceChanges, r.hzEnd, r.hitches60, pct(r.hitches60, r.graceAt60 ? r.graceAt60 : 1),
                     r.gpuLate60, r.behindFrames));
        } else {
            // Informational unless it never held 30 Hz.
            const double miss30 = pct(r.misses30, r.graceAt30);
            const bool held = r.graceAt60 > 0 || (r.graceAt30 > 0 && miss30 <= kSoakStereo30MissPct);
            line(held, "stereo " + name,
                 fmt("settled=%dHz at60=%u at30=%u misses30=%u (%.2f%%) misses60=%u gpu_avg=%.2fms gpu_p95=%.2fms",
                     r.hzEnd, r.graceAt60, r.graceAt30, r.misses30, miss30, r.misses60, r.gpuAvg, r.gpuP95));
        }
    }
    line(lazy == 0, "lazy_loads", fmt("total=%lld (TerraMorph stamps, not precachable: %lld)", lazy, stamps));
    line(fence == 0, "fence", fmt("AS3D_FENCE=%u", fence));
    line(busy == 0, "gpu_busy", fmt("AS3D_GPU_BUSY=%u", busy));
    line(texIn == 0, "tex_in_frame", fmt("AS3D_TEX_IN_FRAME=%u", texIn));
    std::string unfinished;
    for (const SoakStageResult& r : results)
        if (r.ran && !r.finished) unfinished += " " + soakStageName(r.stage);
    line(ran > 0 && finished == ran, "stages_finished",
         fmt("%u of %u%s%s", finished, ran, unfinished.empty() ? "" : " unfinished:", unfinished.c_str()));
    for (const SoakStageResult& r : results)
        if (r.ran && r.levelEnded)
            out += "INFO mission " + soakStageName(r.stage) + (r.gameOver ? " game over" : " complete") + " at step " +
                   std::to_string(r.levelEndStep) + " (" + std::to_string(r.steps) + " steps run)\n";
    for (const SoakStageResult& r : results)
        if (!r.ran) out += "INFO skipped " + soakStageName(r.stage) + " " + r.skipped + "\n";
    out += std::string("VERDICT ") + (fail ? "FAIL" : "PASS") + "\n";
    return out;
}

bool parseHostPort(const std::string& text, std::string* host, int* port) {
    const std::string t = trim(text);
    const size_t colon = t.rfind(':');
    if (colon == std::string::npos) return false;
    const std::string h = t.substr(0, colon);
    long p = 0;
    if (!toInt(t.substr(colon + 1), 1, 65535, &p)) return false;
    const std::vector<std::string> parts = split(h, '.');
    if (parts.size() != 4) return false;
    for (const std::string& part : parts) {
        long octet = 0;
        if (part.size() > 3 || !toInt(part, 0, 255, &octet)) return false;
    }
    *host = h;
    *port = static_cast<int>(p);
    return true;
}

bool validReportName(const std::string& name) {
    if (name.empty() || name.size() > 64 || name == "." || name == "..") return false;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '/' || c == '\\' || u < 0x20 || u == 0x7f) return false;
    }
    return name.find("..") == std::string::npos;
}

std::string reportFileHeader(const std::string& name, uint64_t size) {
    return name + "\n" + std::to_string(static_cast<unsigned long long>(size)) + "\n";
}

std::string frameReport(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out = kReportMagic;
    for (const auto& f : files) {
        if (!validReportName(f.first)) continue;
        out += reportFileHeader(f.first, f.second.size());
        out += f.second;
    }
    out += kReportEnd;
    return out;
}

bool isReportFile(const std::string& name) {
    if (!validReportName(name)) return false;
    if (name == "log.txt" || name == "perf.csv" || name == "soak.txt") return true;
    auto ends = [&](const char* suffix) {
        const std::string s(suffix);
        return name.size() >= s.size() && name.compare(name.size() - s.size(), s.size(), s) == 0;
    };
    return ends(".ppm") && (name.compare(0, 4, "mark") == 0 || name.compare(0, 5, "fatal") == 0);
}

} // namespace ctrapp
