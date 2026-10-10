#include "ctr_loop.h"

#include <3ds.h>
#include <citro3d.h>
#include <malloc.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "as3d/console_pad.h"
#include "as3d/core.h"
#include "as3d/game_data.h"
#include "as3d/game_profile.h"
#include "as3d/input.h"
#include "as3d/fs_util.h"
#include "as3d/load_stats.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "ctr_audio.h"
#include "ctr_context.h"
#include "ctr_input.h"
#include "quality_profile.h"
#include "texture_budget.h"

namespace ctrapp {
namespace {

using as3d::ctr::CtrContext;



double mb(size_t b) { return static_cast<double>(b) / (1024.0 * 1024.0); }

// The state the APT hook needs (it runs on the main thread, inside aptMainLoop).
struct AptState {
    as3d_game::GameSession* session = nullptr;
    Perf* perf = nullptr;
    const std::function<void()>* saveHook = nullptr;
    CtrAudio* audio = nullptr; // M2.3: paused while suspended
    bool pausedBySuspend = false;
    bool resumed = false; // the loop resets its clock
};

void aptCallback(APT_HookType hook, void* param) {
    AptState* st = static_cast<AptState*>(param);
    switch (hook) {
        case APTHOOK_ONSUSPEND:
        case APTHOOK_ONSLEEP:
            AS3D_INFO("apt: %s", hook == APTHOOK_ONSUSPEND ? "suspend" : "sleep");
            if (st->session && st->session->hasLevel() && !st->session->world().paused()) {
                st->session->world().setPaused(true);
                st->pausedBySuspend = true;
            }
            if (st->audio) st->audio->setSuspended(true);
            if (st->saveHook && *st->saveHook) (*st->saveHook)();
            if (st->perf) st->perf->flush();
            break;
        case APTHOOK_ONRESTORE:
        case APTHOOK_ONWAKEUP:
            AS3D_INFO("apt: %s", hook == APTHOOK_ONRESTORE ? "restore" : "wake up");
            if (st->session && st->pausedBySuspend) st->session->world().setPaused(false);
            st->pausedBySuspend = false;
            if (st->audio) st->audio->setSuspended(false);
            st->resumed = true;
            break;
        default: break;
    }
}

} // namespace

// The summary lines of as3d_sim, to compare an end state against the desktop's.
void logEndState(const as3d_game::GameSession& ses, int level, unsigned steps) {
    using namespace as3d;
    const World& w = ses.world();
    const WorldStats& st = w.stats();
    AS3D_INFO("level %d: %u frames, map_pos %.1f, list entities %d, slots %d (max %d)", level, steps,
              static_cast<double>(w.mapPos()), w.listCount(), w.slotsInUse(), st.maxSlotsInUse);
    AS3D_INFO("created %llu, freed %llu, refused %llu, script errors %llu, stalls %llu",
              static_cast<unsigned long long>(st.entitiesCreated), static_cast<unsigned long long>(st.entitiesFreed),
              static_cast<unsigned long long>(st.spawnRefused), static_cast<unsigned long long>(st.scriptErrors),
              static_cast<unsigned long long>(st.stalls));
    const int pe = w.playerEntityIndex(0);
    AS3D_INFO("p_scores %.0f, kills %d of %d, player (%.1f, %.1f), health %.0f", static_cast<double>(w.player(0).scores),
              w.player(0).kills, w.enemiesInLevel(),
              pe >= 0 ? static_cast<double>(w.entity(pe).f(F_ORIGIN)) : 0.0,
              pe >= 0 ? static_cast<double>(w.entity(pe).f(F_ORIGIN + 1)) : 0.0,
              pe >= 0 ? static_cast<double>(w.entity(pe).f(F_HEALTH)) : 0.0);
}

namespace {

FramePacing g_pacing;

// The vblank period of the screens in system ticks (268,111,856 Hz / 59.83 Hz); the clock rounds
// an interval to whole periods, so the exact value only needs to be within a few percent.
constexpr double kVblankTicks = 4481136.0;

// The frame clock (ctr_loop.h FrameClock): the sync timestamps of the last two frames.
struct ClockState {
    uint64_t lastSync = 0;  // nowTicks() right after the vblank wait of the last frame
    uint64_t prevSync = 0;  // the one the last due() counted from
    int fresh = 2;          // due() calls left that return one tick (start, reset)
    int lastInterval = 1;   // vblanks between the last two frames (uncapped)
};
ClockState g_clock;

// pace=auto (ctr_loop.h): the miss ring at 60 Hz, the fit streak at 30 Hz, the excuses.
struct PacerState {
    int baseHz = 60;   // the quality profile's render rate
    int hz = 60;       // the current one
    bool autoMode = true;
    static constexpr int kWindow = 120;
    uint8_t miss[kWindow] = {};
    int missAt = 0, misses = 0;
    int fitStreak = 0;
    unsigned changes = 0;
    int grace = 300;          // frames after a start / level load whose misses do not count
    uint64_t lastSync = 0;    // the sync of the last frame the pacer saw
    FrameSample prev;         // that frame's columns (the miss logs)
    long long lazyLoads = 0;  // LoadStats total at the last frame (a late load excuses a miss)
    bool loadBefore = false;  // the last frame had a late load (excuses this one's miss as well)
    int eyes = 1;             // of the last frame (pace=auto: stereo starts at 30 Hz)
    int fitNeeded = 120;      // fitting frames in a row to go to 60 Hz (doubled after a quick fall back)
    unsigned promotedAt = 0, frameNo = 0;
    // The summary.
    unsigned frames60 = 0, misses60 = 0, excused60 = 0, frames30 = 0, misses30 = 0, logged = 0;
};
PacerState g_pacer;

void setRenderHz(int hz, const char* why);

// After each frame: the vblanks since the last frame's sync decide a miss (2+ at 60 Hz, 3+ at
// 30). Not counted (excused): the 300 frames after a start or a level load, a frame that waited
// for an SD write of the log (perf=1), a dump, or a late load. A miss is logged with the columns
// of the frame before (what ran between the two syncs) and this frame's step and prepare.
// pace=auto: two eyes start at 30 Hz (the console's stereo GPU is 13.5 ms avg, 16.5 p95), one
// eye at 60 Hz; 3 counted misses in 120 frames at 60 Hz go to 30 Hz; at 30 Hz, fitNeeded (120)
// frames in a row whose CPU path (one step + prepare + recording + end) stays under 13 ms and
// whose GPU time (from the first command list to the end of the queue) under 14 ms go to 60 Hz;
// a fall back within 600 frames of that doubles fitNeeded (at most 3600).
void updatePacer(const FrameSample& sample) {
    PacerState& p = g_pacer;
    const bool io = takePerfIoFlag();
    const long long lazy = as3d::LoadStats::lazyLoads().total();
    // A load inside the frame's submit (or a late texture built after it) delays the next sync:
    // the frame after a late load is excused too.
    const bool loadNow = lazy != p.lazyLoads || sample.lateTextures > 0;
    const bool lateLoad = loadNow || p.loadBefore;
    p.loadBefore = loadNow;
    p.lazyLoads = lazy;
    int interval = 1;
    if (p.lastSync != 0)
        interval = static_cast<int>(static_cast<double>(g_clock.lastSync - p.lastSync) / kVblankTicks + 0.5);
    p.lastSync = g_clock.lastSync;
    const int expected = 60 / p.hz;
    const bool late = interval > expected;
    const bool excused = p.grace > 0 || io || lateLoad || p.prev.afterMs > 5.0f;
    if (p.grace > 0) --p.grace;
    if (p.hz == 60) ++p.frames60;
    else ++p.frames30;
    if (late) {
        if (excused) {
            if (p.hz == 60) ++p.excused60;
        } else if (p.hz == 60) {
            ++p.misses60;
        } else {
            ++p.misses30;
        }
        if (!excused && (p.logged < 40 || (p.misses60 + p.misses30) % 100 == 0)) {
            ++p.logged;
            const FrameSample& q = p.prev;
            AS3D_INFO("AS3D_MISS hz=%d vblanks=%d prev: submit=%.1f end=%.1f after=%.1f gpu_tail=%.1f; now: steps=%d "
                      "step=%.1f prepare=%.1f wait=%.1f",
                      p.hz, interval, static_cast<double>(q.submitMs), static_cast<double>(q.endMs),
                      static_cast<double>(q.afterMs), static_cast<double>(q.gpuMs), sample.simSteps,
                      static_cast<double>(sample.stepMs), static_cast<double>(sample.stats.prepareMs),
                      static_cast<double>(sample.waitMs));
        }
    }
    // The previous frame's GPU time: from its first command list to the end of the queue (the
    // tail C3D_GetDrawingTime gives now is the previous frame's).
    const FrameSample& q = p.prev;
    const uint64_t gpuStart = q.stats.gpuStartTick ? q.stats.gpuStartTick : q.endTick;
    const float gpuFull = (q.endTick > gpuStart ? static_cast<float>(ticksToMs(q.endTick - gpuStart)) : 0.0f) + sample.gpuMs;
    p.prev = sample;
    ++p.frameNo;
    if (!p.autoMode) return;
    const int eyes = sample.stats.eyes > 0 ? sample.stats.eyes : 1;
    if (eyes != p.eyes) {
        p.eyes = eyes;
        p.fitNeeded = 120;
        if (eyes == 2 && p.hz == 60) setRenderHz(30, "stereo");
        else if (eyes == 1 && p.hz == 30) setRenderHz(60, "mono");
        p.grace = std::max(p.grace, 60);
        return;
    }
    if (p.hz == 60) {
        const uint8_t m = late && !excused ? 1 : 0;
        p.misses += m - p.miss[p.missAt];
        p.miss[p.missAt] = m;
        p.missAt = (p.missAt + 1) % PacerState::kWindow;
        if (p.misses >= 3) {
            if (p.promotedAt && p.frameNo - p.promotedAt < 600) p.fitNeeded = std::min(p.fitNeeded * 2, 3600);
            setRenderHz(30, "misses");
        }
    } else {
        const float steps = sample.simSteps > 0 ? static_cast<float>(sample.simSteps) : 1.0f;
        const float cpu = sample.stepMs / steps + sample.stats.prepareMs + sample.submitMs + sample.endMs;
        const bool fits = cpu < 13.0f && gpuFull < 14.0f;
        if (!excused) p.fitStreak = fits ? p.fitStreak + 1 : 0;
        if (p.fitStreak >= p.fitNeeded) {
            p.promotedAt = p.frameNo;
            setRenderHz(60, "fits");
        }
    }
}

void logPaceSummaryImpl() {
    const PacerState& p = g_pacer;
    AS3D_INFO("PERF_SUMMARY pace changes=%u frames60=%u misses60=%u (%.2f%%) excused60=%u frames30=%u misses30=%u",
              p.changes, p.frames60, p.misses60, p.frames60 ? 100.0 * p.misses60 / p.frames60 : 0.0, p.excused60,
              p.frames30, p.misses30);
}

void setRenderHz(int hz, const char* why) {
    if (g_pacer.hz == hz) return;
    g_pacer.hz = hz;
    C3D_FrameRate(static_cast<float>(hz));
    ++g_pacer.changes;
    for (uint8_t& m : g_pacer.miss) m = 0;
    g_pacer.misses = 0;
    g_pacer.fitStreak = 0;
    AS3D_INFO("AS3D_PACE hz=%d reason=%s changes=%u", hz, why, g_pacer.changes);
}

} // namespace

void logPaceSummary() { logPaceSummaryImpl(); }

void setFramePacing(const RunConfig& run) {
    g_pacing.gpuKick = run.gpuKick;
    g_pacing.cmdlistFlush = run.cmdlistFlush || run.gpuKick;
    g_pacing.pace = run.pace;
    as3d::ctr::setStreamBlockCache(static_cast<size_t>(run.pakCacheKb) * 1024);
    AS3D_INFO("AS3D_PACING pace=%s gpu_kick=%d end_flush=%s pak_cache_kb=%d",
              run.pace == 0 ? "auto" : (run.pace == 30 ? "30" : "60"), g_pacing.gpuKick ? 1 : 0,
              g_pacing.cmdlistFlush ? "cmdlist" : "all", run.pakCacheKb);
}

const FramePacing& framePacing() { return g_pacing; }

void startPacing(int profileHz) {
    g_pacer = PacerState();
    g_pacer.baseHz = profileHz;
    g_pacer.autoMode = g_pacing.pace == 0 && profileHz == 60;
    const int want = g_pacing.pace == 30 ? 30 : (g_pacing.pace == 60 ? 60 : profileHz);
    g_pacer.eyes = 1; // the first stereo frame switches to 30 Hz (pace=auto)
    C3D_FrameRate(static_cast<float>(want));
    g_pacer.hz = want;
    g_pacer.lazyLoads = as3d::LoadStats::lazyLoads().total();
    g_clock = ClockState();
    AS3D_INFO("AS3D_PACE hz=%d reason=start auto=%d", want, g_pacer.autoMode ? 1 : 0);
}

int renderHz() { return g_pacer.hz; }
int maxCatchUp() { return g_pacer.hz == 30 ? 4 : 2; }
unsigned paceChanges() { return g_pacer.changes; }

void FrameClock::reset() {
    g_clock.fresh = 2;
    g_pacer.grace = 300; // a level load: its first seconds' misses do not count
    g_pacer.lastSync = 0;
}

int FrameClock::due(unsigned* dropped) {
    const int perTick = 60 / g_pacer.hz;
    // One tick of steps for the first two frames after a reset: the first sync after it is the base.
    if (g_clock.fresh > 0 || g_clock.lastSync == 0) {
        if (g_clock.fresh > 0) --g_clock.fresh;
        g_clock.prevSync = g_clock.lastSync;
        g_clock.lastInterval = perTick;
        return perTick;
    }
    const double vb = static_cast<double>(g_clock.lastSync - g_clock.prevSync) / kVblankTicks;
    g_clock.prevSync = g_clock.lastSync;
    int n = static_cast<int>(vb + 0.5);
    if (n < 1) n = 1; // a sync always waits for a new vblank
    g_clock.lastInterval = n;
    const int cap = maxCatchUp();
    if (n > cap) {
        if (dropped) *dropped += static_cast<unsigned>(n - cap);
        n = cap;
    }
    return n;
}

int FrameClock::lastInterval() const { return g_clock.lastInterval; }

void drawFrame(CtrContext& ctx, FrameHook& hook, const FrameInfo& info, FrameSample& sample) {
    as3d::LoadStats::setFrame(static_cast<long>(info.frame)); // the AS3D_LAZY lines (log_lazy=1)
    hook.prepare(info);
    // The vblank (C3D_FrameSync: the next tick of citro3d's frame counter, every vblank at 60 Hz,
    // every other one at 30), its timestamp for the frame clock, then the frame (C3D_FrameBegin
    // waits for the GPU to finish the previous frame).
    const uint64_t w0 = nowTicks();
    C3D_FrameSync();
    const uint64_t ws = nowTicks();
    g_clock.lastSync = ws;
    sample.syncMs = static_cast<float>(ticksToMs(ws - w0));
    if (ctx.beginFrame(0)) {
        const uint64_t w1 = nowTicks();
        for (int eye = 0; eye < info.eyes; ++eye) hook.drawTop(ctx, eye, info);
        hook.drawBottom(ctx, info);
        const uint64_t w2 = nowTicks();
        sample.endTick = w2;
        ctx.endFrame(g_pacing.cmdlistFlush ? GX_CMDLIST_FLUSH : 0);
        const uint64_t w3 = nowTicks();
        sample.waitMs = static_cast<float>(ticksToMs(w1 - ws));
        sample.submitMs = static_cast<float>(ticksToMs(w2 - w1));
        sample.endMs = static_cast<float>(ticksToMs(w3 - w2));
        // Textures created inside this frame (loads the precache missed) were placeholders:
        // built now, outside the command stream, at most one (or 2 ms) per frame (texture_budget.h).
        if (as3d::pica::lateTexturesPending() > 0) {
            sample.lateTextures = as3d::pica::drainLateTextures();
            sample.lateMs = static_cast<float>(ticksToMs(nowTicks() - w3));
        }
    }
    sample.cpuMs = C3D_GetProcessingTime();
    sample.gpuMs = C3D_GetDrawingTime();
    hook.stats(sample.stats);
    if (sample.stats.eyes == 0) sample.stats.eyes = info.eyes;
    const uint64_t a0 = nowTicks();
    hook.afterFrame(ctx, info);
    sample.afterMs = static_cast<float>(ticksToMs(nowTicks() - a0));
    sample.renderHz = g_pacer.hz;
    updatePacer(sample);
}

namespace {
std::string planPath(const std::string& key) { return std::string(kDataDir) + "/cache/" + key + ".plan"; }
constexpr const char* kPlanHeader = "openairstrike read plan 1";
} // namespace

void ReadPlan::begin(as3d::Vfs& vfs, const std::string& key, bool enabled) {
    end();
    if (!enabled) return;
    vfs_ = &vfs;
    key_ = key;
    loaded_.clear();
    recorded_.clear();
    seen_.clear();
    planned_.clear();
    start_ = nowTicks();
    if (std::FILE* f = std::fopen(planPath(key).c_str(), "rb")) {
        char line[512];
        bool header = false;
        while (std::fgets(line, sizeof line, f)) {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (!header) {
                header = s == kPlanHeader;
                if (!header) break; // another version: record a new one
                continue;
            }
            if (!s.empty()) loaded_.push_back(s);
        }
        std::fclose(f);
    }
    planned_.insert(loaded_.begin(), loaded_.end());
    if (!loaded_.empty()) {
        const as3d::ctr::StreamReadStats r0 = as3d::ctr::streamReadStats();
        const uint64_t t0 = nowTicks();
        const size_t bytes = vfs.prefetch(loaded_);
        const as3d::ctr::StreamReadStats& r1 = as3d::ctr::streamReadStats();
        AS3D_INFO("AS3D_LOAD phase=prefetch plan=%s files=%d kb=%.0f ms=%.0f (%lu file reads, %.0f KB read)", key.c_str(),
                  static_cast<int>(loaded_.size()), static_cast<double>(bytes) / 1024.0, ticksToMs(nowTicks() - t0),
                  static_cast<unsigned long>(r1.reads - r0.reads), static_cast<double>(r1.bytes - r0.bytes) / 1024.0);
    } else {
        AS3D_INFO("AS3D_LOAD phase=prefetch plan=%s none yet (recording it)", key.c_str());
    }
    vfs.setReadObserver([this](const std::string& path) {
        if (seen_.insert(path).second) recorded_.push_back(path);
    });
}

void ReadPlan::end() {
    if (!vfs_) return;
    vfs_->setReadObserver(nullptr);
    vfs_->dropPrefetched();
    int hits = 0;
    for (const std::string& p : recorded_) hits += planned_.count(p) ? 1 : 0;
    const bool changed = recorded_ != loaded_;
    bool stored = false;
    if (changed && !recorded_.empty()) {
        std::string text = std::string(kPlanHeader) + "\n";
        for (const std::string& p : recorded_) text += p + "\n";
        as3d::makeDirectories(std::string(kDataDir) + "/cache");
        // The writer thread writes it (ctr_perf.h queueFileWrite), not the level start.
        stored = queueFileWrite(planPath(key_), std::vector<uint8_t>(text.begin(), text.end())) ||
                 as3d::writeFileAtomic(planPath(key_), text.data(), text.size());
    }
    AS3D_INFO("AS3D_READPLAN plan=%s read=%d planned=%d hits=%d %s in %.0f ms", key_.c_str(),
              static_cast<int>(recorded_.size()), static_cast<int>(loaded_.size()), hits,
              changed ? (stored ? "stored" : "not stored") : "unchanged", ticksToMs(nowTicks() - start_));
    vfs_ = nullptr;
    loaded_.clear();
    recorded_.clear();
    seen_.clear();
    planned_.clear();
}

int runCtrLoop(const CtrLoopOptions& opts) {
    using namespace as3d;
    const RunConfig& run = opts.run;
    as3d::LoadStats::setLogLazy(run.logLazy);
    if (run.menu()) return runCtrMenuLoop(opts);
    const bool sim = run.mode == Mode::Sim;
    const bool bot = run.mode != Mode::Play;
    Perf perf;
    perf.open(run.perf);
    setFramePacing(run);

    bool new3ds = false;
    APT_CheckNew3DS(&new3ds);
    if (new3ds) osSetSpeedupEnable(true);
    AS3D_INFO("openairstrike 3ds: mode=%s game=%s level=%d frames=%d perf=%d bot_weapons=%d log_lazy=%d, %s 3DS%s "
              "state_check=%d arena_poison=%d",
              modeName(run.mode), run.game.c_str(), run.level, run.effectiveFrames(), run.perf ? 1 : 0,
              run.botWeapons ? 1 : 0, run.logLazy ? 1 : 0, new3ds ? "New" : "Old", new3ds ? ", speed-up on" : "",
              run.stateCheck, run.arenaPoison ? 1 : 0);
    AS3D_INFO("memory: APPLICATION %.1f MB free, linear free %.1f MB", mb(osGetMemRegionFree(MEMREGION_APPLICATION)),
              mb(linearSpaceFree()));

    // The screens: not for sim, which draws nothing.
    std::unique_ptr<GraphicsContext> gl;
    CtrContext* ctx = nullptr;
    if (!sim) {
        gl = createGraphicsContext(GraphicsConfig());
        ctx = as3d::ctr::ctrContext();
        if (!gl || !ctx) {
            AS3D_ERROR("cannot create the graphics context");
            perf.close();
            return 1;
        }
        AS3D_INFO("graphics: %s", gl->description().c_str());
    }
    QualityProfile quality = sim ? newProfile() : selectQuality(run.quality, new3ds);
    applyRunOverrides(quality, run);
    if (!sim) {
        applyQuality(quality, new3ds);
        startPacing(quality.renderHz);
    }

    // The game.
    const GameProfile* game = findGameProfile(run.game.c_str());
    if (!game) {
        AS3D_ERROR("unknown game '%s'", run.game.c_str());
        perf.close();
        return 1;
    }
    as3d_game::GameOptions go;
    go.game = game;
    go.extractedDir = std::string(kDataDir) + "/" + game->key;
    for (const char* const* p = game->paks; *p; ++p) go.paks.push_back(go.extractedDir + "/" + *p);
    go.mission = run.level;
    go.startLevel = true;  // the front end is off: the mission starts at once
    go.levelFlow = false;  // no restart or next mission: the run ends where the mission does
    if (run.difficulty >= 0) go.world.difficulty = run.difficulty;
    // The read plan of this start (the session's definitions and level, the renderer's level
    // start and precache, the sounds): prefetched right after the mount, recorded until the
    // first frame.
    ReadPlan plan;
    go.afterMount = [&plan, &run, game, sim](as3d::Vfs& vfs) {
        plan.begin(vfs, std::string(game->key) + "_m" + std::to_string(run.level) + (sim ? "_sim" : ""), run.prefetch);
    };

    auto session = std::make_unique<as3d_game::GameSession>();
    const uint64_t loadStart = nowTicks();
    std::string err;
    if (!session->init(go, &err)) {
        AS3D_ERROR("cannot start the game: %s", err.c_str());
        session.reset();
        perf.close();
        return 1;
    }
    const double simLoadMs = ticksToMs(nowTicks() - loadStart);
    AS3D_INFO("mission %d loaded in %.0f ms", run.level, simLoadMs);
    {
        const as3d_game::GameSession::InitTimes& it = session->initTimes();
        const as3d::ctr::StreamReadStats& rd = as3d::ctr::streamReadStats();
        AS3D_INFO("AS3D_LOAD phase=session ms=%.0f mount=%.0f defs=%.0f world_init=%.0f level=%.0f; pak reads so far %.0f ms "
                  "(%lu file reads, %.0f KB, %lu block cache hits)",
                  simLoadMs, it.mountMs, it.defsMs, it.worldMs, it.levelMs, ticksToMs(rd.ticks),
                  static_cast<unsigned long>(rd.reads), static_cast<double>(rd.bytes) / 1024.0,
                  static_cast<unsigned long>(rd.cacheHits));
    }

    FrameHook* hook = opts.hook;
    if (!sim && !hook) {
        AS3D_ERROR("mode %s needs a frame hook", modeName(run.mode));
        session.reset();
        gl.reset();
        perf.close();
        return 1;
    }
    if (hook && !sim && !hook->begin(*ctx, *session)) {
        AS3D_ERROR("the level start failed");
        hook->end();
        session.reset();
        gl.reset();
        perf.close();
        return 1;
    }
    const double loadMs = ticksToMs(nowTicks() - loadStart);
    perf.setLoadMs(loadMs);
    if (!sim) AS3D_INFO("level start (mission and renderer) %.0f ms", loadMs);

    // Sound (M2.3, ctr_audio.h): bot and play, not sim.
    CtrAudio audio;
    audio.setMusic(run.music, run.musicTaps, std::string(kDataDir) + "/" + game->key + "/music");
    if (!sim && run.audio) {
        audio.start(*session, run.precache);
        // The sounds are part of the level-start precache: the hook's begin() closed the
        // precache window (LoadStats::reset) before they loaded; close it again after them, so
        // that only loads during play count as lazy.
        if (run.precache) LoadStats::reset();
    }

    plan.end(); // the level start's reads are done: store the plan, free what was not read

    AptState apt;
    apt.session = session.get();
    apt.perf = &perf;
    apt.saveHook = &opts.saveHook;
    apt.audio = &audio;
    aptSetSleepAllowed(true);
    aptHookCookie cookie;
    aptHook(&cookie, aptCallback, &apt);

    const unsigned maxSteps = static_cast<unsigned>(run.effectiveFrames());
    unsigned frame = 0, steps = 0, dropped = 0;
    FrameClock clock;
    uint64_t lastFrame = 0;
    float lastCpu = 0.0f, lastGpu = 0.0f;

    CtrInput input(run, new3ds);

    auto stepOnce = [&]() {
        FrameInput in = bot ? botInput(steps) : input.frame(session->world().paused());
        if (run.botWeapons) applyWeaponTour(session->world(), steps, in);
        const int ev = session->step(in);
        if (ev & as3d_game::GameSession::kLevelStarted) audio.levelStarted(*session);
        audio.afterStep(session->world());
        ++steps;
    };

    while (aptMainLoop()) {
        const PadState& pad = input.readPad();
        if ((pad.held & (pad::Start | pad::Select)) == (pad::Start | pad::Select)) {
            AS3D_INFO("quit: START+SELECT");
            break;
        }

        FrameSample sample;
        sample.frame = frame;

        if (sim) {
            // Back to back, no clock: the end state must equal the desktop's.
            const uint64_t t0 = nowTicks();
            stepOnce();
            sample.stepMs = static_cast<float>(ticksToMs(nowTicks() - t0));
            const uint64_t now = nowTicks();
            sample.periodMs = frame ? static_cast<float>(ticksToMs(now - lastFrame)) : 0.0f;
            lastFrame = now;
            perf.record(sample);
            ++frame;
            if (maxSteps && steps >= maxSteps) break;
            continue;
        }

        if (apt.resumed) { // back from HOME or sleep: no replay of the time away
            apt.resumed = false;
            clock.reset();
            lastFrame = 0;
        }
        // The steps of this frame: the vblanks since the last one (ctr_loop.h FrameClock).
        int due = clock.due(&dropped);
        // run.ini fixed_steps=N: N steps per frame whatever the clock (A/B dumps and profiles at
        // the same simulation steps in two builds).
        if (run.fixedSteps > 0) due = run.fixedSteps;
        int n = 0;
        const uint64_t s0 = nowTicks();
        while (n < due && !(maxSteps && steps >= maxSteps)) {
            stepOnce();
            ++n;
        }
        sample.stepMs = static_cast<float>(ticksToMs(nowTicks() - s0));
        sample.simSteps = n;
        audio.followPause(session->world().paused());

        FrameInfo info;
        info.frame = frame;
        info.step = steps;
        info.eyes = quality.stereoAllowed ? ctx->eyeCount() : 1;
        info.slider = ctx->sliderValue();
        if (run.slider >= 0.0f) { // run.ini slider=: forced (the emulator, comparisons)
            info.slider = run.slider;
            info.eyes = run.slider > 0.02f && quality.stereoAllowed ? 2 : 1;
        }
        info.paused = session->world().paused();
        info.session = session.get();
        info.touch = &input.touch();
        info.lastCpuMs = lastCpu;
        info.lastGpuMs = lastGpu;
        info.padDown = pad.down;

        drawFrame(*ctx, *hook, info, sample);
        lastCpu = sample.cpuMs;
        lastGpu = sample.gpuMs;
        const uint64_t end = nowTicks();
        sample.periodMs = lastFrame ? static_cast<float>(ticksToMs(end - lastFrame)) : 0.0f;
        lastFrame = end;
        perf.record(sample);
        ++frame;
        if (maxSteps && steps >= maxSteps) break;
    }

    // Exit: pause, save, report, release in reverse order of creation.
    if (session->hasLevel()) session->world().setPaused(true);
    if (opts.saveHook) opts.saveHook();
    AS3D_INFO("stopped after %u frames, %u steps", frame, steps);
    logEndState(*session, run.level, steps);
    if (!sim) logPaceSummary();
    perf.writeSummary(frame, steps, dropped);
    if (!sim) audio.logSummary();
    if (hook && !sim) hook->end();
    aptUnhook(&cookie);
    apt.session = nullptr;
    apt.audio = nullptr;
    audio.stop();
    session.reset();
    gl.reset(); // C3D_Fini, gfxExit
    perf.close();
    return 0;
}

} // namespace ctrapp
