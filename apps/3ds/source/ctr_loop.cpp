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
#include "as3d/world.h"
#include "ctr_context.h"
#include "ctr_input.h"

namespace ctrapp {
namespace {

using as3d::ctr::CtrContext;

constexpr int kMaxCatchUp = 2; // steps per displayed frame; the rest of a stall is dropped
constexpr double kMaxFrameSeconds = 0.25;

double mb(size_t b) { return static_cast<double>(b) / (1024.0 * 1024.0); }

// The state the APT hook needs (it runs on the main thread, inside aptMainLoop).
struct AptState {
    as3d_game::GameSession* session = nullptr;
    Perf* perf = nullptr;
    const std::function<void()>* saveHook = nullptr;
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
            if (st->saveHook && *st->saveHook) (*st->saveHook)();
            if (st->perf) st->perf->flush();
            break;
        case APTHOOK_ONRESTORE:
        case APTHOOK_ONWAKEUP:
            AS3D_INFO("apt: %s", hook == APTHOOK_ONRESTORE ? "restore" : "wake up");
            if (st->session && st->pausedBySuspend) st->session->world().setPaused(false);
            st->pausedBySuspend = false;
            st->resumed = true;
            break;
        default: break;
    }
}

// Colour for the frame: a hue wheel, one turn in about 6 s.
u32 frameColour(unsigned frame, int salt) {
    const float h = std::fmod(static_cast<float>(frame) * 1.0f + salt * 60.0f, 360.0f) / 60.0f;
    const int i = static_cast<int>(h);
    const float f = h - static_cast<float>(i);
    const float v = 0.35f; // dark enough to read as a status colour
    const float p = 0.0f, q = v * (1.0f - f), t = v * f;
    float r = v, g = t, b = p;
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    auto byte = [](float x) { return static_cast<u32>(x * 255.0f) & 0xFF; };
    return (byte(r) << 24) | (byte(g) << 16) | (byte(b) << 8) | 0xFF;
}

// The hook of M1.2: no renderer yet, so the screens are cleared to a colour that cycles with the
// frame count (life sign), the bottom screen offset by half a turn.
class ColourHook final : public FrameHook {
public:
    void drawTop(CtrContext& ctx, int eye, const FrameInfo& info) override {
        C3D_RenderTarget* t = ctx.topTarget(eye);
        C3D_RenderTargetClear(t, C3D_CLEAR_ALL, frameColour(info.frame, eye), 0);
        C3D_FrameDrawOn(t);
    }
    void drawBottom(CtrContext& ctx, const FrameInfo& info) override {
        C3D_RenderTarget* t = ctx.bottomTarget();
        C3D_RenderTargetClear(t, C3D_CLEAR_ALL, frameColour(info.frame, 3), 0);
        C3D_FrameDrawOn(t);
    }
};

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

} // namespace

int runCtrLoop(const CtrLoopOptions& opts) {
    using namespace as3d;
    const RunConfig& run = opts.run;
    const bool sim = run.mode == Mode::Sim;
    const bool bot = run.mode != Mode::Play;
    Perf perf;
    perf.open(run.perf);

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
    const double loadMs = ticksToMs(nowTicks() - loadStart);
    perf.setLoadMs(loadMs);
    AS3D_INFO("mission %d loaded in %.0f ms", run.level, loadMs);

    AptState apt;
    apt.session = session.get();
    apt.perf = &perf;
    apt.saveHook = &opts.saveHook;
    aptSetSleepAllowed(true);
    aptHookCookie cookie;
    aptHook(&cookie, aptCallback, &apt);

    ColourHook colourHook;
    FrameHook* hook = opts.hook ? opts.hook : &colourHook;

    const double dt = session->world().config().dt;
    const unsigned maxSteps = static_cast<unsigned>(run.effectiveFrames());
    unsigned frame = 0, steps = 0, dropped = 0;
    double acc = 0.0;
    uint64_t last = nowTicks(), lastFrame = 0;

    CtrInput input(run, new3ds);

    auto stepOnce = [&]() {
        FrameInput in = bot ? botInput(steps) : input.frame(session->world().paused());
        session->step(in);
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
            last = nowTicks();
            acc = 0.0;
            lastFrame = 0;
        }
        const uint64_t now = nowTicks();
        acc += std::min(kMaxFrameSeconds, ticksToMs(now - last) / 1000.0);
        last = now;
        int n = 0;
        const uint64_t s0 = nowTicks();
        while (acc >= dt && n < kMaxCatchUp && !(maxSteps && steps >= maxSteps)) {
            acc -= dt;
            stepOnce();
            ++n;
        }
        if (acc >= dt) { // bounded catch-up: a long stall is not replayed
            dropped += static_cast<unsigned>(acc / dt);
            acc = 0.0;
        }
        sample.stepMs = static_cast<float>(ticksToMs(nowTicks() - s0));

        FrameInfo info;
        info.frame = frame;
        info.step = steps;
        info.eyes = ctx->eyeCount();
        info.slider = ctx->sliderValue();
        info.paused = session->world().paused();
        info.session = session.get();

        hook->prepare(info);
        if (ctx->beginFrame()) {
            for (int eye = 0; eye < info.eyes; ++eye) hook->drawTop(*ctx, eye, info);
            hook->drawBottom(*ctx, info);
            ctx->endFrame();
        }
        sample.cpuMs = C3D_GetProcessingTime();
        sample.gpuMs = C3D_GetDrawingTime();
        hook->stats(sample.stats);
        if (sample.stats.eyes == 0) sample.stats.eyes = info.eyes;
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
    aptUnhook(&cookie);
    apt.session = nullptr;
    session.reset();
    gl.reset(); // C3D_Fini, gfxExit
    perf.close();
    return 0;
}

} // namespace ctrapp
