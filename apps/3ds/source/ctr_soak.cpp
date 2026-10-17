#include "ctr_soak.h"

#include <3ds.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/fs_util.h"
#include "as3d/load_stats.h"
#include "as3d/platform.h"
#include "build_info.h"
#include "ctr_context.h"
#include "ctr_fatal.h"
#include "ctr_frame.h"
#include "ctr_input.h"
#include "ctr_install.h"
#include "ctr_loop.h"
#include "ctr_perf.h"
#include "mesh_renderer_pica.h"
#include "pica_frame.h"
#include "soak_report.h"
#include "state_check_pica.h"
#include "texture_budget.h"
#include "water_renderer_pica.h"

namespace ctrapp {
namespace {

std::string markerPath() { return std::string(kDataDir) + "/soak.running"; }

void writeMarker(const std::string& text) {
    // A plain write (no writer thread: the marker must be on the card before the stage starts).
    if (std::FILE* f = std::fopen(markerPath().c_str(), "w")) {
        std::fputs(text.c_str(), f);
        std::fclose(f);
    }
}

// The log of the soak itself, between the stages (each stage's loop opens and closes its own).
template <typename Fn>
void withLog(Fn fn) {
    Perf log;
    log.open(false);
    fn();
    log.close();
}

} // namespace

std::string takeSoakCrashMarker() {
    std::FILE* f = std::fopen(markerPath().c_str(), "rb");
    if (!f) return std::string();
    char buf[256] = {};
    const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
    std::fclose(f);
    std::remove(markerPath().c_str());
    std::string s(buf, n);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s.empty() ? std::string("unknown stage") : s;
}

int runSoak(const RunConfig& base, const std::string& previousCrash) {
    bool new3ds = false;
    APT_CheckNew3DS(&new3ds);
    if (new3ds) osSetSpeedupEnable(true);

    // The installed games: a bot run needs the paks only.
    std::vector<std::string> installed;
    for (const GameInstall& gi : detectInstalls(kDataDir))
        if (gi.paksPresent) installed.push_back(gi.game->key);
    const std::vector<SoakStage> stages = base.soak.empty() ? defaultSoakStages(installed) : base.soak;
    const int probeEvery = base.effectiveProbeEvery();
    std::vector<std::string> notes;
    if (!previousCrash.empty()) notes.push_back("the previous launch stopped inside a stage (" + previousCrash + ")");

    std::unique_ptr<as3d::GraphicsContext> gl;
    withLog([&]() {
        std::string list, games;
        for (const SoakStage& s : stages) list += (list.empty() ? "" : ",") + soakStageName(s);
        for (const std::string& g : installed) games += (games.empty() ? "" : ",") + g;
        AS3D_INFO("AS3D_SOAK start build=%s stages=%d frames=%d probe_every=%d installed=%s list=%s from=%s", buildCommit(),
                  static_cast<int>(stages.size()), base.soakFrames, probeEvery, games.empty() ? "none" : games.c_str(),
                  list.c_str(), base.launchedFrom.c_str());
        if (!previousCrash.empty()) AS3D_WARN("AS3D_SOAK_PREVIOUS the last launch died in stage: %s", previousCrash.c_str());
        gl = as3d::createGraphicsContext(as3d::GraphicsConfig());
    });
    if (!gl || !as3d::ctr::ctrContext()) {
        setFatal("cannot create the graphics context");
        return 1;
    }
    CtrInput input(base, new3ds);

    std::vector<SoakStageResult> results;
    bool aborted = false;
    int n = 0;
    for (const SoakStage& st : stages) {
        ++n;
        SoakStageResult r;
        r.stage = st;
        // The stage's own run.ini overrides (soak=game:level:eyes:key=value...), soak_frames included.
        RunConfig stageBase = base;
        applyRunConfig(stageBase, soakStageOptionsText(st));
        r.targetSteps = static_cast<unsigned>(stageBase.soakFrames > 0 ? stageBase.soakFrames : RunConfig::kSoakStepCap);
        if (aborted) {
            r.skipped = "(aborted before it)";
            results.push_back(r);
            continue;
        }
        if (std::find(installed.begin(), installed.end(), st.game) == installed.end()) {
            r.skipped = "(not installed)";
            results.push_back(r);
            continue;
        }
        RunConfig sr = stageBase;
        sr.mode = Mode::Bot;
        sr.game = st.game;
        sr.gameGiven = true;
        sr.level = st.level;
        sr.levelGiven = true;
        // soak_frames=0 (the default): the whole mission, the step cap as the safety net.
        sr.frames = stageBase.soakFrames > 0 ? stageBase.soakFrames : RunConfig::kSoakStepCap;
        sr.stopAtLevelEnd = stageBase.soakFrames <= 0;
        sr.slider = st.stereo ? 0.5f : 0.0f; // the hardware slider is ignored
        sr.botWeapons = true;                // the player's weapons fire as in play
        sr.perf = true;
        sr.stateCheck = std::max(1, stageBase.stateCheck);
        sr.markKey = true; // L marks the wash in a soak (run.ini mark_key; off in play)
        sr.probeEvery = stageBase.effectiveProbeEvery();
        sr.washCheck = stageBase.effectiveWashCheck() ? 1 : 0; // the wash detector: on in the soak unless wash_check=0
        sr.soakStage = n;
        sr.dumpAt = 0;
        sr.dumpEvery = 0;
        sr.dumpScreens = false;
        sr.fixedSteps = 0;
        char marker[160];
        std::snprintf(marker, sizeof marker, "stage %d of %d: %s, %d frames, build %s\n", n, static_cast<int>(stages.size()),
                      soakStageName(st).c_str(), sr.frames, buildCommit());
        writeMarker(marker);
        withLog([&]() {
            AS3D_INFO("AS3D_SOAK_STAGE n=%d of=%d game=%s level=%d eyes=%s slider=%.2f frames=%d bot_weapons=1 "
                      "state_check=%d probe_every=%d audio=%d whole_mission=%d opts=%s",
                      n, static_cast<int>(stages.size()), st.game.c_str(), st.level, st.stereo ? "stereo" : "mono",
                      static_cast<double>(sr.slider), sr.frames, sr.stateCheck, sr.probeEvery, sr.audio ? 1 : 0,
                      sr.stopAtLevelEnd ? 1 : 0, soakStageOptions(st).c_str());
        });
        // The last stage's precache armed the lazy-load log: this stage's session start is not play.
        as3d::LoadStats::setLogLazy(false);
        as3d::LoadStats::setFrame(0);
        const unsigned fence0 = as3d::pica::fenceStats().queueBusyAtBegin;
        const unsigned tex0 = as3d::pica::textureInFrameStats().creates;
        // The process-wide renderer switches main() sets once from the file (ctr_main.cpp), again
        // from the stage's own keys, so that a stage's mesh_merge=, mesh_sort=, depthmap=, fog=,
        // water=, water_tile= reach it (before R4.2 a stage's mesh_merge=1 was ignored).
        as3d::pica::setWBufferAsZBuffer(sr.depthMapZ);
        as3d::pica::setFogForcedOff(!sr.fog);
        as3d::pica::setMeshStateSort(sr.meshSort);
        as3d::pica::setMeshMerge(sr.meshMerge);
        as3d::pica::setWaterOptions(sr.water, sr.waterTile >= 0.0f ? sr.waterTile : as3d::pica::kDefaultWaterTile);
        LoopReport lr;
        {
            GameFrame frame(sr);
            CtrLoopOptions o;
            o.run = sr;
            o.hook = &frame;
            o.frame = &frame;
            o.context = gl.get();
            o.input = &input;
            o.report = &lr;
            runCtrLoop(o);
        }
        const as3d::pica::StateCheckCounts sc = as3d::pica::stateCheckCounts();
        r.ran = true;
        r.finished = sr.stopAtLevelEnd ? lr.levelEnded : lr.finished;
        r.levelEnded = lr.levelEnded;
        r.gameOver = lr.gameOver;
        r.levelEndStep = lr.levelEndStep;
        r.frames = lr.frames;
        r.steps = lr.steps;
        r.hzEnd = lr.pace.hz;
        r.graceFrames = lr.pace.graceFrames;
        r.graceAt60 = lr.pace.graceAt60;
        r.graceAt30 = lr.pace.graceAt30;
        r.misses60 = lr.pace.misses60;
        r.misses30 = lr.pace.misses30;
        r.excused60 = lr.pace.excused60;
        r.paceChanges = lr.pace.changes;
        r.gpuLate60 = lr.pace.gpuLate60;
        r.hitches60 = lr.pace.hitches60;
        r.behindFrames = lr.pace.behindFrames;
        r.gpuAvg = lr.gpuAvg;
        r.gpuP95 = lr.gpuP95;
        r.lazyLoads = lr.lazyLoads;
        r.lazyStamps = lr.lazyStamps;
        r.gpuMismatches = sc.gpuMismatches;
        r.terrainGpuMismatches = sc.terrainGpuMismatches;
        r.stateMismatches = sc.mismatches;
        r.probes = sc.probes;
        r.autoMarks = sc.autoMarks;
        r.primeGpuMismatches = sc.primeGpuMismatches;
        r.washEvals = sc.washEvals;
        r.washFrames = sc.washFrames;
        r.washEpisodes = sc.washEpisodes;
        r.washLongest = sc.washLongest;
        r.washFirstFrame = sc.washFirstFrame;
        r.washMaxSamples = sc.washMaxSamples;
        r.mismatchProbes = sc.mismatchProbes;
        r.rereadChanged = sc.rereadChanged;
        r.canaryOk = sc.canaryOk;
        r.canaryStale = sc.canaryStale;
        r.gpuBusy = sc.busyFrames;
        r.fence = as3d::pica::fenceStats().queueBusyAtBegin - fence0;
        r.texInFrame = as3d::pica::textureInFrameStats().creates - tex0;
        const std::string fatal = takeFatal();
        if (lr.quit) {
            aborted = true;
            notes.push_back("aborted in stage " + std::to_string(n) + " (" + soakStageName(st) + "): START+SELECT or the system");
        }
        if (!fatal.empty()) notes.push_back("stage " + std::to_string(n) + " (" + soakStageName(st) + ") could not run: " + fatal);
        withLog([&]() {
            AS3D_INFO("AS3D_SOAK_RESULT n=%d game=%s level=%d eyes=%s finished=%d level_end=%s@%u frames=%u steps=%u hz_end=%d after_grace=%u "
                      "at60=%u at30=%u misses60=%u misses30=%u excused60=%u probe_excused=%u pace_changes=%u gpu_avg=%.2f "
                      "gpu_p95=%.2f lazy=%lld lazy_stamps=%lld gpu_mismatches=%u terrain_gpu_mismatches=%u state_mismatches=%u probes=%u "
                      "probe_ms_avg_max=%.2f/%.2f auto_marks=%u fence=%u gpu_busy=%u tex_in_frame=%u context=%s "
                      "prime_mismatches=%u wash_evals=%u wash_frames=%u wash_episodes=%u wash_first=%u wash_longest=%u "
                      "wash_max_samples=%u wash_judge_ms_avg_max=%.3f/%.3f mismatch_probes=%u reread_changed=%u "
                      "canary_ok=%u canary_stale=%u prime_marks=%u details=%u opts=%s",
                      n, st.game.c_str(), st.level, st.stereo ? "stereo" : "mono", r.finished ? 1 : 0,
                      !r.levelEnded ? "none" : r.gameOver ? "game_over" : "complete", r.levelEndStep, r.frames, r.steps,
                      r.hzEnd, r.graceFrames, r.graceAt60, r.graceAt30, r.misses60, r.misses30, r.excused60,
                      lr.pace.probeExcused, r.paceChanges, r.gpuAvg, r.gpuP95, r.lazyLoads, r.lazyStamps, r.gpuMismatches,
                      r.terrainGpuMismatches, r.stateMismatches, r.probes,
                      sc.probes ? sc.probeMs / sc.probes : 0.0, sc.probeMaxMs, r.autoMarks, r.fence, r.gpuBusy, r.texInFrame,
                      sc.contextRead ? "read" : "not read", r.primeGpuMismatches, r.washEvals, r.washFrames,
                      r.washEpisodes, r.washFirstFrame, r.washLongest, r.washMaxSamples,
                      sc.washEvals ? sc.washMs / sc.washEvals : 0.0, sc.washMaxMs, sc.mismatchProbes, sc.rereadChanged,
                      sc.canaryOk, sc.canaryStale, sc.primeMarks, sc.details, soakStageOptions(st).c_str());
            // R4.2: its own line (the result line above is at the log's line length already).
            AS3D_INFO("AS3D_SOAK_PACE n=%d misses60=%u gpu_late60=%u hitches60=%u behind_frames=%u repaid=%u forced=%u "
                      "dropped=%u lag_max=%d catchup=%s pace_rule=%s opts=%s",
                      n, r.misses60, lr.pace.gpuLate60, lr.pace.hitches60, lr.pace.behindFrames, lr.pace.repaid,
                      lr.pace.forced, lr.pace.dropped, lr.pace.lagMax, framePacing().catchUp.name().c_str(),
                      framePacing().rule.name().c_str(), soakStageOptions(st).c_str());
        });
        results.push_back(r);
    }

    const std::string verdict = soakVerdict(results, notes);
    std::string why;
    const bool written = as3d::writeFileAtomic(std::string(kDataDir) + "/soak.txt", verdict.data(), verdict.size(), &why);
    if (written) noteRunFile("soak.txt");
    std::remove(markerPath().c_str());
    withLog([&]() {
        size_t at = 0;
        while (at < verdict.size()) {
            size_t nl = verdict.find('\n', at);
            if (nl == std::string::npos) nl = verdict.size();
            AS3D_INFO("AS3D_SOAK_VERDICT %s", verdict.substr(at, nl - at).c_str());
            at = nl + 1;
        }
        if (!written) AS3D_WARN("AS3D_SOAK cannot write soak.txt: %s", why.c_str());
        AS3D_INFO("AS3D_SOAK_DONE stages=%d aborted=%d", static_cast<int>(stages.size()), aborted ? 1 : 0);
    });
    gl.reset();
    return 0;
}

} // namespace ctrapp
