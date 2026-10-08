// The front end on the 3DS (M2.1, ctr_loop.h runCtrMenuLoop): GameStack with GameFlow over the
// session, the frame composer (ctr_frame.h) as its view.
//
// One iteration:
//   aptMainLoop -> readPad (+ run.ini pad_script) -> quit chord? -> the screen name (AS3D_SCREEN)
//   -> GameFlow::uiFrame(wall-clock dt, UiInput): while a menu is up the pad and touch
//      (D-pad = arrows, A = Enter, B / START = Escape, touch = pointer through the inverse of
//      computeMapping(320, 240)); during play only the pause button (START, or the touch pause
//      button seen by the last step) as Escape: the in-game menu
//   -> up to 2 fixed steps of GameFlow::step (the pad, or with menu_bot=1 the bot during a
//      mission) while the flow runs a world; a level load resets the clock
//   -> hook.prepare (GameFlow::draw -> the view's layers) -> both screens -> perf row.
// GameFlow loads levels from inside uiFrame (Start, Restart, Continue, Quit to the attract
// level); the view's loading frames are drawn from there through GameFlow::loadingHook.
// APT suspend / sleep: GameFlow::onBackground (the in-game menu opens during play, the profile
// is saved), sound paused; at exit the profile is saved again (GameStack::teardown).
#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <memory>
#include <string>

#include "as3d/console_pad.h"
#include "as3d/core.h"
#include "as3d/frontend.h"
#include "as3d/game_data.h"
#include "as3d/game_profile.h"
#include "as3d/input.h"
#include "as3d/world.h"
#include "ctr_audio.h"
#include "ctr_context.h"
#include "ctr_frame.h"
#include "ctr_input.h"
#include "ctr_mem.h"
#include "quality_profile.h"
#include "ctr_loop.h"
#include "game_stack.h"

namespace ctrapp {
namespace {

using as3d::ctr::CtrContext;


constexpr double kMaxFrameSeconds = 0.25;

struct MenuApt {
    as3d_game::GameFlow* flow = nullptr;
    Perf* perf = nullptr;
    CtrAudio* audio = nullptr;
    bool resumed = false;
};

void menuAptCallback(APT_HookType hook, void* param) {
    MenuApt* st = static_cast<MenuApt*>(param);
    switch (hook) {
        case APTHOOK_ONSUSPEND:
        case APTHOOK_ONSLEEP:
            AS3D_INFO("apt: %s", hook == APTHOOK_ONSUSPEND ? "suspend" : "sleep");
            // During play the in-game menu opens (the game comes back paused), then the save.
            if (st->flow) st->flow->onBackground();
            if (st->audio) st->audio->setSuspended(true);
            if (st->perf) st->perf->flush();
            break;
        case APTHOOK_ONRESTORE:
        case APTHOOK_ONWAKEUP:
            AS3D_INFO("apt: %s", hook == APTHOOK_ONRESTORE ? "restore" : "wake up");
            if (st->audio) st->audio->setSuspended(false);
            st->resumed = true;
            break;
        default: break;
    }
}

// The screen name of game_loop.cpp's AS3D_SCREEN markers; the intro pages by number (intro1, ...:
// the logo pages of Settings.xml, then the sequels' comic pages).
std::string screenOf(as3d_game::GameFlow& flow) {
    const as3d::ui::Frontend& fe = flow.frontend();
    if (fe.state() == as3d::ui::FrontendState::Intro) return "intro" + std::to_string(fe.introPage() + 1);
    if (fe.menuOpen()) return as3d::ui::screenName(fe.topScreen());
    if (fe.state() == as3d::ui::FrontendState::Playing) return fe.paused() ? "paused" : "playing";
    return "none";
}

// The top menu's visible items in the virtual 800x600 screen (id@x,y,w,h), for scripted taps.
std::string menuItems(as3d_game::GameFlow& flow) {
    const as3d::ui::Menu* m = flow.frontend().menus().top();
    if (!m) return std::string();
    std::string items;
    char buf[64];
    for (const as3d::ui::MenuItem& it : m->items) {
        if (it.hidden() || it.hit.w <= 0 || it.hit.h <= 0) continue;
        std::snprintf(buf, sizeof buf, " %d@%.0f,%.0f,%.0f,%.0f%s", it.id, it.hit.x, it.hit.y, it.hit.w, it.hit.h,
                      it.disabled() ? "d" : "");
        items += buf;
    }
    return items;
}

// FlowConfig of the console (the Android template, android_main.cpp: touch mode without the
// front end's own MENU button, no two players, no mouse control) plus consoleOptions (no key
// bindings, Screen or touch rows) and the install's files next to the paks.
as3d_game::FlowConfig consoleFlow(const RunConfig& run, const as3d::GameProfile& game, const std::string& dir) {
    as3d_game::FlowConfig f;
    f.touch = true;
    f.twoPlayerMode = false;
    f.mouseControlOption = false;
    f.touchMenuButton = false; // START and the touch pause button open the in-game menu
    f.screenOptionAlways = false;
    f.consoleOptions = true;
    f.profilePath = as3d_game::defaultProfilePath(); // GameFlow::init: <userDataDir>/<game>/profile.bin
    f.settingsXml = dir + "/Settings.xml";
    if (game.textsFile && *game.textsFile) f.textsPath = dir + "/" + game.textsFile;
    f.attract = run.attract;
    f.showLogo = run.logo;
    return f;
}

} // namespace

int runCtrMenuLoop(const CtrLoopOptions& opts) {
    using namespace as3d;
    const RunConfig& run = opts.run;
    Perf perf;
    perf.open(run.perf);
    GameFrame* frame = opts.frame;

    bool new3ds = false;
    APT_CheckNew3DS(&new3ds);
    if (new3ds) osSetSpeedupEnable(true);
    AS3D_INFO("openairstrike 3ds: mode=%s (front end) game=%s frames=%d perf=%d menu_bot=%d, %s 3DS%s", modeName(run.mode),
              run.game.c_str(), run.frames, run.perf ? 1 : 0, run.menuBot ? 1 : 0, new3ds ? "New" : "Old",
              new3ds ? ", speed-up on" : "");
    AS3D_INFO("memory: APPLICATION %.1f MB free, linear free %.1f MB",
              static_cast<double>(osGetMemRegionFree(MEMREGION_APPLICATION)) / 1048576.0,
              static_cast<double>(linearSpaceFree()) / 1048576.0);
    if (!frame) {
        AS3D_ERROR("the front end needs the frame composer");
        perf.close();
        return 1;
    }

    // The game selector (ctr_selector.cpp) lends its context and input to the front end.
    std::unique_ptr<GraphicsContext> ownGl;
    if (!opts.context) ownGl = createGraphicsContext(GraphicsConfig());
    GraphicsContext* gl = opts.context ? opts.context : ownGl.get();
    CtrContext* ctx = as3d::ctr::ctrContext();
    if (!gl || !ctx) {
        AS3D_ERROR("cannot create the graphics context");
        perf.close();
        return 1;
    }
    AS3D_INFO("graphics: %s", gl->description().c_str());
    const QualityProfile quality = selectQuality(run.quality, new3ds);
    applyQuality(quality, new3ds);

    const GameProfile* game = findGameProfile(run.game.c_str());
    if (!game) {
        AS3D_ERROR("unknown game '%s'", run.game.c_str());
        perf.close();
        return 1;
    }
    const std::string dir = std::string(kDataDir) + "/" + game->key;
    as3d_game::StackConfig sc;
    sc.game.game = game;
    sc.game.extractedDir = dir;
    for (const char* const* p = game->paks; *p; ++p) sc.game.paks.push_back(dir + "/" + *p);
    sc.game.startLevel = false; // the front end starts the levels
    sc.game.levelFlow = false;  // and moves between them
    if (run.difficulty >= 0) sc.game.world.difficulty = run.difficulty;
    sc.frontend = true;
    sc.flow = consoleFlow(run, *game, dir);
    sc.flow.changeGame = opts.changeGameOffered; // the main menu's "Change game" (more than one game installed)
    sc.makeView = [frame, ctx]() { return frame->makeFlowView(*ctx); };
    sc.noAudio = !run.audio;

    as3d_game::GameStack stack;
    const uint64_t loadStart = nowTicks();
    std::string err;
    if (!stack.build(sc, &err)) {
        AS3D_ERROR("cannot start the game: %s", err.c_str());
        frame->end();
        stack.teardown();
        ownGl.reset();
        perf.close();
        return 1;
    }
    as3d_game::GameSession& ses = *stack.session;
    as3d_game::GameFlow& flow = *stack.flow;
    // Music (M2.4): run.ini music, and only in a build with a decoder (CtrAudio::setMusic).
    stack.audio.setMusicEnabled(run.music && (AS3D_WITH_OPENMPT || AS3D_WITH_TREMOR));
    frame->setFlow(&flow);
    flow.setScreenSize(CtrInput::kTouchW, CtrInput::kTouchH);
    flow.frontend().setHudTarget(ui::HudTarget::Bottom320x240);
    bool levelLoaded = false;
    flow.loadingHook = [frame](float progress, bool intermission) { frame->loadingScreen(progress, intermission); };
    flow.levelLoadedHook = [&]() {
        levelLoaded = true;
        AS3D_INFO("AS3D_LEVEL_LOADED mission=%d intermission=%d name=\"%s\"", ses.mission(),
                  ses.hasLevel() && ses.world().intermission() ? 1 : 0, ses.levelName().c_str());
    };
    CtrAudio audio;
    audio.setMusic(run.music, run.musicTaps, dir + "/music");
    if (run.audio) audio.attach(stack.audio);
    const double loadMs = ticksToMs(nowTicks() - loadStart);
    perf.setLoadMs(loadMs);
    AS3D_INFO("front end: data, view and audio in %.0f ms; profile %s; attract level %d", loadMs,
              flow.config().profilePath.c_str(), flow.attractLevel());
    if (run.unlockHeli >= 0 && run.unlockHeli < ses.rules().helicopterCount && run.unlockHeli < kMaxHelicopters) {
        flow.frontend().profile().progress.helicopterUnlocked[run.unlockHeli] = true;
        AS3D_INFO("unlock_heli: helicopter %d unlocked", run.unlockHeli);
    }
    flow.boot();

    MenuApt apt;
    apt.flow = &flow;
    apt.perf = &perf;
    apt.audio = &audio;
    aptSetSleepAllowed(true);
    aptHookCookie cookie;
    aptHook(&cookie, menuAptCallback, &apt);

    const ui::Mapping bottomMap = ui::computeMapping(CtrInput::kTouchW, CtrInput::kTouchH);
    std::unique_ptr<CtrInput> ownInput;
    if (!opts.input) ownInput.reset(new CtrInput(run, new3ds));
    CtrInput& input = opts.input ? *opts.input : *ownInput;
    const unsigned maxFrames = static_cast<unsigned>(std::max(run.frames, 0));
    unsigned frameNo = 0, steps = 0, dropped = 0, runStep = 0, levelSteps = 0;
    double acc = 0.0;
    double dt = 1.0 / 60.0;
    uint64_t last = nowTicks(), lastFrame = 0, lastUi = 0;
    float lastCpu = 0.0f, lastGpu = 0.0f;
    bool queuedEscape = false;
    int cycleState = 0; // run.ini cycle=: 0 waiting for the main menu, 1 in a mission, 2 back in the attract level
    int reportedLoads = -1; // run.ini report_at=: the level load already reported
    size_t cycleIdx = 0;
    unsigned cycleAt = 0;
    std::string screen;
    int focusedItem = -1;

    while (aptMainLoop()) {
        const PadState& pad = input.readPad();
        if ((pad.held & (pad::Start | pad::Select)) == (pad::Start | pad::Select)) {
            AS3D_INFO("quit: START+SELECT");
            break;
        }
        if (apt.resumed) { // back from HOME or sleep: no replay of the time away
            apt.resumed = false;
            last = nowTicks();
            lastUi = 0;
            acc = 0.0;
            lastFrame = 0;
        }

        FrameSample sample;
        sample.frame = frameNo;

        // The front end's frame (menu time on the wall clock).
        const uint64_t nowUi = nowTicks();
        float uiDt = lastUi ? static_cast<float>(std::min(0.1, ticksToMs(nowUi - lastUi) / 1000.0)) : 0.0f;
        if (run.uiFixedDt) uiDt = 1.0f / 60.0f;
        lastUi = nowUi;
        ui::UiInput uin = flow.playing() ? input.playingUiInputs() : input.uiInputs(bottomMap);
        if (queuedEscape) {
            queuedEscape = false;
            if (flow.playing()) uin.key(ui::keys::Escape);
        }
        // run.ini cycle=: the memory growth check starts and leaves the missions itself.
        if (!run.cycle.empty()) {
            bool resetClock = false;
            if (cycleState == 0 && frameNo >= 120 && screen == "main" && cycleIdx < run.cycle.size()) {
                ui::MissionStart ms;
                ms.mission = run.cycle[cycleIdx] - 1;
                if (run.difficulty >= 0) ms.difficulty = run.difficulty;
                AS3D_INFO("cycle: start mission %d (%zu of %zu)", run.cycle[cycleIdx], cycleIdx + 1, run.cycle.size());
                flow.startMission(ms);
                cycleState = 1;
                cycleAt = frameNo;
                resetClock = true;
            } else if (cycleState == 1 && frameNo >= cycleAt + static_cast<unsigned>(run.cycleFrames)) {
                AS3D_INFO("cycle: leave mission %d", run.cycle[cycleIdx]);
                flow.loadAttract();
                cycleState = 2;
                cycleAt = frameNo;
                resetClock = true;
            } else if (cycleState == 2 && frameNo >= cycleAt + 60) {
                cycleState = 0;
                if (++cycleIdx >= run.cycle.size()) {
                    AS3D_INFO("cycle: done");
                    break;
                }
            }
            if (resetClock) {
                last = nowTicks();
                acc = 0.0;
                lastFrame = 0;
                botStep = 0;
                levelSteps = 0;
                if (ses.hasLevel()) dt = ses.world().config().dt;
            }
        }
        const int loads = flow.levelLoads();
        levelLoaded = false;
        flow.uiFrame(uiDt, uin);
        if (flow.quitRequested()) {
            AS3D_INFO("quit: the front end's Exit");
            break;
        }
        if (flow.changeGameRequested()) {
            AS3D_INFO("front end: Change game (profile saved)");
            opts.changeGameRequested = true;
            break;
        }
        if (flow.levelLoads() != loads || levelLoaded) {
            // A level was loaded (seconds, behind the loading screen): not a frame time.
            last = nowTicks();
            acc = 0.0;
            lastFrame = 0;
            runStep = 0;
            levelSteps = 0;
            if (ses.hasLevel()) dt = ses.world().config().dt;
        }
        {
            const std::string s = screenOf(flow);
            if (s != screen) {
                screen = s;
                AS3D_INFO("AS3D_SCREEN name=%s frame=%u mission=%d", s.c_str(), frameNo, ses.hasLevel() ? ses.mission() : 0);
                if (flow.frontend().menuOpen()) AS3D_INFO("AS3D_MENU name=%s items=%s", s.c_str(), menuItems(flow).c_str());
            }
            // The focused item (pad navigation), on every change.
            const ui::Menu* top = flow.frontend().menus().top();
            const int f = top && top->focused >= 0 ? top->items[static_cast<size_t>(top->focused)].id : -1;
            if (f != focusedItem) {
                focusedItem = f;
                if (top) AS3D_INFO("AS3D_FOCUS screen=%s item=%d frame=%u", s.c_str(), f, frameNo);
            }
        }

        // The simulation, while the flow runs a world (attract level or mission).
        const uint64_t now = nowTicks();
        acc += std::min(kMaxFrameSeconds, ticksToMs(now - last) / 1000.0);
        last = now;
        int n = 0;
        const uint64_t s0 = nowTicks();
        while (acc >= dt && n < quality.maxCatchUp()) {
            if (!flow.worldRunning()) {
                acc = 0.0;
                break;
            }
            acc -= dt;
            FrameInput in = input.frame(ses.world().paused()); // also drains the touch buttons
            // The mission's running steps (runStep, reset at every level load): the world's clock,
            // hint boxes included (they pause the world in mode=bot too), but not while the front
            // end holds it (the sequels' start dialogue, the in-game menu, the end screens), so
            // that the N-th one is the N-th step of `mode=bot` (as3d_sim --bot). menu_bot: the bot
            // flies them (its confirm also closes the hint boxes); menu_bot_steps and end_at count
            // them.
            bool running = false;
            if (flow.frontend().state() == ui::FrontendState::Playing && ses.hasLevel() && !ses.world().intermission()) {
                const ui::Frontend& fe = flow.frontend();
                const bool hint = ses.world().hintShowing();
                const bool frontPaused = fe.menuOpen() && !(hint && fe.topScreen() == ui::Screen::Hint);
                running = !frontPaused && (!ses.world().paused() || hint) && !ses.world().levelComplete() &&
                          !ses.world().gameOver();
                if (run.menuBot) {
                    const bool pause = in.pausePressed;
                    in = running ? botInput(runStep) : FrameInput();
                    in.pausePressed = pause;
                }
            // menu_bot: the bot flies the mission (its confirm also closes the hint boxes; the
            // flow ignores its controls while a menu is up).
            if (run.menuBot && flow.frontend().state() == ui::FrontendState::Playing && !ses.world().intermission()) {
                const bool pause = in.pausePressed;
                // The bot's clock is the mission's: it does not run while the sequels' opening
                // dialogue holds the world (as3d_sim has none), so that the end state after N
                // bot steps is the desktop's.
                in = botInput(screen == "dialogue" ? botStep : botStep++);
                in.pausePressed = pause;
            }
            const int before = flow.levelLoads();
            flow.step(in);
            if (running) {
                ++runStep;
                if (run.menuBotSteps > 0 && runStep == static_cast<unsigned>(run.menuBotSteps)) {
                    AS3D_INFO("AS3D_BOT_END mission=%d steps=%u bot=%d frame=%u", ses.mission(), runStep, run.menuBot ? 1 : 0,
                              frameNo);
                    logEndState(ses, ses.mission(), runStep);
                }
                if (run.endAt > 0 && runStep == static_cast<unsigned>(run.endAt) && !ses.world().levelComplete() &&
                    !ses.world().gameOver()) {
                    // As the desktop flow tests: EndLevel, or every life lost (the next step reports it).
                    AS3D_INFO("AS3D_FORCE_END %s mission=%d steps=%u", run.endGameOver ? "gameover" : "complete",
                              ses.mission(), runStep);
                    if (run.endScore >= 0) ses.world().player(0).scores = static_cast<float>(run.endScore);
                    if (run.endGameOver) {
                        ses.world().player(0).lives = -1.0f;
                        ses.world().player(1).lives = -1.0f;
                    } else {
                        ses.world().endLevel();
                    }
                }
            }
            // The touch pause button during play: the in-game menu on the next UI frame.
            if (in.pausePressed && flow.playing()) queuedEscape = true;
            if (ses.hasLevel()) audio.afterStep(ses.world());
            ++steps;
            ++n;
            if (ses.hasLevel() && !ses.world().intermission()) ++levelSteps;
            // run.ini report_at=N: the end-state lines of the mission once, when its world has
            // run N frames (dialogues and hint boxes hold the world, not the step count).
            if (run.reportAt > 0 && run.menuBot && ses.hasLevel() && !ses.world().intermission() &&
                botStep == static_cast<unsigned>(run.reportAt) && reportedLoads != flow.levelLoads()) {
                reportedLoads = flow.levelLoads();
                AS3D_INFO("report_at: mission %d after %u bot steps (%u loop steps)", ses.mission(), botStep, levelSteps);
                logEndState(ses, ses.mission(), botStep);
            }
            if (flow.levelLoads() != before) {
                acc = 0.0;
                last = nowTicks();
                break;
            }
        }
        if (acc >= dt) { // bounded catch-up: a long stall is not replayed
            dropped += static_cast<unsigned>(acc / dt);
            acc = 0.0;
        }
        sample.stepMs = static_cast<float>(ticksToMs(nowTicks() - s0));
        sample.simSteps = n;

        FrameInfo info;
        info.frame = frameNo;
        info.step = steps;
        info.eyes = quality.stereoAllowed ? ctx->eyeCount() : 1;
        info.slider = ctx->sliderValue();
        if (run.slider >= 0.0f) {
            info.slider = run.slider;
            info.eyes = run.slider > 0.02f && quality.stereoAllowed ? 2 : 1;
        }
        info.paused = ses.hasLevel() && ses.world().paused();
        info.session = &ses;
        info.touch = &input.touch();
        info.lastCpuMs = lastCpu;
        info.lastGpuMs = lastGpu;
        info.screen = screen.c_str();
        info.playing = flow.playing();

        frame->prepare(info);
        if (ctx->beginFrame()) {
            for (int eye = 0; eye < info.eyes; ++eye) frame->drawTop(*ctx, eye, info);
            frame->drawBottom(*ctx, info);
            ctx->endFrame();
        }
        sample.cpuMs = C3D_GetProcessingTime();
        sample.gpuMs = C3D_GetDrawingTime();
        lastCpu = sample.cpuMs;
        lastGpu = sample.gpuMs;
        frame->stats(sample.stats);
        if (sample.stats.eyes == 0) sample.stats.eyes = info.eyes;
        frame->afterFrame(*ctx, info);
        const uint64_t end = nowTicks();
        sample.periodMs = lastFrame ? static_cast<float>(ticksToMs(end - lastFrame)) : 0.0f;
        lastFrame = end;
        perf.record(sample);
        ++frameNo;
        if (maxFrames && frameNo >= maxFrames) break;
    }

    // Exit: the profile saved, the end state of the level running (if any), release.
    flow.saveNow();
    AS3D_INFO("stopped after %u frames, %u steps (%u in the last level), screen %s", frameNo, steps, levelSteps,
              screen.c_str());
    if (ses.hasLevel()) logEndState(ses, ses.mission(), levelSteps);
    perf.writeSummary(frameNo, steps, dropped);
    audio.logSummary();
    aptUnhook(&cookie);
    apt.flow = nullptr;
    apt.audio = nullptr;
    audio.stop();
    frame->end();     // GPU resources, while the graphics context exists
    stack.teardown(); // profile, front end, audio device, view, session
    ownGl.reset();
    logMem("exit", game->key, 0);
    perf.close();
    return 0;
}

} // namespace ctrapp
