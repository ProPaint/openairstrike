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
#include "as3d/load_stats.h"
#include "as3d/world.h"
#include "ctr_audio.h"
#include "ctr_context.h"
#include "ctr_input.h"
#include "quality_profile.h"

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

// pace=auto (ctr_loop.h): the miss ring at 60 Hz, the fit streak at 30 Hz.
struct PacerState {
    int baseHz = 60;   // the quality profile's render rate
    int hz = 60;       // the current one
    bool autoMode = true;
    static constexpr int kWindow = 120;
    uint8_t miss[kWindow] = {};
    int missAt = 0, misses = 0;
    int fitStreak = 0;
    unsigned changes = 0;
};
PacerState g_pacer;

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
    C3D_FrameRate(static_cast<float>(want));
    g_pacer.hz = want;
    g_clock = ClockState();
    AS3D_INFO("AS3D_PACE hz=%d reason=start auto=%d", want, g_pacer.autoMode ? 1 : 0);
}

int renderHz() { return g_pacer.hz; }
int maxCatchUp() { return g_pacer.hz == 30 ? 4 : 2; }
unsigned paceChanges() { return g_pacer.changes; }

void FrameClock::reset() { g_clock.fresh = 2; }

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
        ctx.endFrame(g_pacing.cmdlistFlush ? GX_CMDLIST_FLUSH : 0);
        sample.waitMs = static_cast<float>(ticksToMs(w1 - ws));
        sample.submitMs = static_cast<float>(ticksToMs(w2 - w1));
        sample.endMs = static_cast<float>(ticksToMs(nowTicks() - w2));
    }
    sample.cpuMs = C3D_GetProcessingTime();
    sample.gpuMs = C3D_GetDrawingTime();
    hook.stats(sample.stats);
    if (sample.stats.eyes == 0) sample.stats.eyes = info.eyes;
    const uint64_t a0 = nowTicks();
    hook.afterFrame(ctx, info);
    sample.afterMs = static_cast<float>(ticksToMs(nowTicks() - a0));
    sample.renderHz = g_pacer.hz;
    // pace=auto: 60 Hz while frames fit, 30 Hz (two steps per frame) while they do not. A miss
    // is a frame shown for two vblanks or more at 60 Hz; 3 in the last 120 frames go to 30 Hz.
    // At 30 Hz a frame "fits" when the GPU wait, its recording, its end and the larger of the GPU
    // tail and one step + prepare stay under 14.5 ms; 240 such frames in a row go back to 60 Hz.
    if (g_pacer.autoMode) {
        PacerState& p = g_pacer;
        if (p.hz == 60) {
            const uint8_t m = g_clock.lastInterval >= 2 ? 1 : 0;
            p.misses += m - p.miss[p.missAt];
            p.miss[p.missAt] = m;
            p.missAt = (p.missAt + 1) % PacerState::kWindow;
            if (p.misses >= 3) setRenderHz(30, "misses");
        } else {
            const float steps = sample.simSteps > 0 ? static_cast<float>(sample.simSteps) : 1.0f;
            const float onePrepare = sample.stats.prepareMs + sample.stepMs / steps;
            const float work = sample.waitMs + sample.submitMs + sample.endMs + std::max(sample.gpuMs, onePrepare);
            p.fitStreak = work < 14.5f ? p.fitStreak + 1 : 0;
            if (p.fitStreak >= 240) setRenderHz(60, "fits");
        }
    }
}

int runCtrLoop(const CtrLoopOptions& opts) {
    using namespace as3d;
    const RunConfig& run = opts.run;
    if (run.menu()) return runCtrMenuLoop(opts);
    const bool sim = run.mode == Mode::Sim;
    const bool bot = run.mode != Mode::Play;
    Perf perf;
    perf.open(run.perf);
    setFramePacing(run);

    bool new3ds = false;
    APT_CheckNew3DS(&new3ds);
    if (new3ds) osSetSpeedupEnable(true);
    AS3D_INFO("openairstrike 3ds: mode=%s game=%s level=%d frames=%d perf=%d, %s 3DS%s", modeName(run.mode),
              run.game.c_str(), run.level, run.effectiveFrames(), run.perf ? 1 : 0, new3ds ? "New" : "Old",
              new3ds ? ", speed-up on" : "");
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
    const QualityProfile quality = sim ? newProfile() : selectQuality(run.quality, new3ds);
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
        const int due = clock.due(&dropped);
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
