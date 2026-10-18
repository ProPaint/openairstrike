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
#include <cmath>
#include <memory>
#include <string>

#include "as3d/console_menu.h"
#include "as3d/console_pad.h"
#include "as3d/core.h"
#include "as3d/frontend.h"
#include "as3d/game_data.h"
#include "as3d/game_profile.h"
#include "fps_counter.h"
#include "as3d/input.h"
#include "as3d/world.h"
#include "build_info.h"
#include "ctr_audio.h"
#include "ctr_fatal.h"
#include "ctr_context.h"
#include "ctr_frame.h"
#include "ctr_input.h"
#include "ctr_mem.h"
#include "ctr_name_entry.h"
#include "quality_profile.h"
#include "ctr_loop.h"
#include "game_stack.h"
#include "name_entry.h"

namespace ctrapp {
namespace {

using as3d::ctr::CtrContext;



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
    setFramePacing(run);
    GameFrame* frame = opts.frame;

    bool new3ds = false;
    APT_CheckNew3DS(&new3ds);
    if (new3ds) osSetSpeedupEnable(true);
    AS3D_INFO("openairstrike 3ds: mode=%s (front end) game=%s frames=%d perf=%d menu_bot=%d bot_weapons=%d log_lazy=%d, "
              "%s 3DS%s state_check=%d probe_every=%d arena_poison=%d build=%s from=%s",
              modeName(run.mode), run.game.c_str(), run.frames, run.perf ? 1 : 0, run.menuBot ? 1 : 0,
              run.botWeapons ? 1 : 0, run.logLazy ? 1 : 0, new3ds ? "New" : "Old", new3ds ? ", speed-up on" : "",
              run.stateCheck, run.effectiveProbeEvery(), run.arenaPoison ? 1 : 0, buildCommit(), run.launchedFrom.c_str());
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
        setFatal("cannot create the graphics context");
        perf.close();
        return 1;
    }
    AS3D_INFO("graphics: %s", gl->description().c_str());
    QualityProfile quality = selectQuality(run.quality, new3ds);
    applyRunOverrides(quality, run);
    applyQuality(quality, new3ds);
    startPacing(quality.renderHz);

    // run.ini ui_text (R4.4): the console text sizes, or the scaled text of before.
    ui::UiAssets::setConsoleTextDefault(run.uiText);
    const GameProfile* game = findGameProfile(run.game.c_str());
    if (!game) {
        setFatal("unknown game '" + run.game + "' in run.ini", "game= takes as3d, as2, gulf or select");
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
        setFatal(std::string("cannot start ") + game->title + " (" + dir + "/)", err);
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
    // R4.5: the console menus (null with ui_menus=0), and the tutorial hints' key names as the
    // console's buttons ({Ctrl} -> {A}, ...).
    ui::ConsoleMenuView* menuView = frame->menuView();
    flow.frontend().setHintFilter([](const std::string& t) { return ui::consoleKeyNames(t); });
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
    input.setGame(game->id); // R4.7: the first game's HUD tiles are its touch controls
    const unsigned maxFrames = static_cast<unsigned>(std::max(run.frames, 0));
    unsigned frameNo = 0, steps = 0, dropped = 0, runStep = 0, levelSteps = 0;
    FrameClock clock;
    uint64_t lastFrame = 0, lastUi = 0;
    float lastCpu = 0.0f, lastGpu = 0.0f;
    bool queuedEscape = false;
    as3d_game::FpsCounter fpsCounter; // the options' Show FPS (FrameInfo::fps); the HOME-menu play runs here, not in runCtrLoop
    unsigned droppedShown = 0;
    bool queuedEnter = false; // the name entry: the keyboard's name is in the field, Enter commits it
    bool nameAsked = false;   // the keyboard was shown for this name entry
    int cycleState = 0; // run.ini cycle=: 0 waiting for the main menu, 1 in a mission, 2 back in the attract level
    int reportedLoads = -1; // run.ini report_at=: the level load already reported
    size_t cycleIdx = 0;
    unsigned cycleAt = 0;
    std::string screen;
    int focusedItem = -1;
    int hintHeld = 0; // run.ini hint_hold: steps the current hint box has been held

    while (aptMainLoop()) {
        const PadState& pad = input.readPad();
        if ((pad.held & (pad::Start | pad::Select)) == (pad::Start | pad::Select)) {
            AS3D_INFO("quit: START+SELECT");
            break;
        }
        if (apt.resumed) { // back from HOME or sleep: no replay of the time away
            apt.resumed = false;
            lastUi = 0;
            clock.reset();
            lastFrame = 0;
        }

        FrameSample sample;
        sample.frame = frameNo;

        // The front end's frame (menu time on the wall clock).
        const uint64_t nowUi = nowTicks();
        float uiDt = lastUi ? static_cast<float>(std::min(0.1, ticksToMs(nowUi - lastUi) / 1000.0)) : 0.0f;
        if (run.uiFixedDt) uiDt = 1.0f / 60.0f;
        lastUi = nowUi;
        if (menuView) menuView->applyHits(flow.frontend()); // the hit rectangles of the menu on screen
        ui::UiInput uin = flow.playing() ? input.playingUiInputs() : input.uiInputs(bottomMap, menuView, &flow.frontend());
        if (queuedEscape) {
            queuedEscape = false;
            if (flow.playing()) uin.key(ui::keys::Escape);
        }
        if (queuedEnter) {
            queuedEnter = false;
            if (as3d_game::nameEntryField(flow.frontend())) uin.key(ui::keys::Enter);
        }
        // run.ini ui_taps (R4.4): a tap at the centre of a menu item, wherever the layout put it.
        // R4.5: with the console menus, a finger on the touch screen there from the next frame (the
        // path of a real touch: the view's targets and the tap-through fix); else a virtual tap.
        for (const RunConfig::UiTap& t : run.uiTaps) {
            if (static_cast<unsigned>(t.frame) != frameNo) continue;
            const ui::Menu* top = flow.frontend().menus().top();
            const ui::MenuItem* hit = nullptr;
            if (top && (t.screen.empty() || t.screen == screen))
                for (const ui::MenuItem& it : top->items)
                    if (it.id == t.item && !it.hidden() && it.hit.w > 0 && it.hit.h > 0) hit = &it;
            int tx = -1, ty = -1;
            if (hit && menuView) {
                tx = static_cast<int>(std::lround(bottomMap.toFbX(hit->hit.x + hit->hit.w * 0.5f)));
                ty = static_cast<int>(std::lround(bottomMap.toFbY(hit->hit.y + hit->hit.h * 0.5f)));
                input.queueTouch(tx, ty);
            } else if (hit) {
                uin.tap(hit->hit.x + hit->hit.w * 0.5f, hit->hit.y + hit->hit.h * 0.5f);
            }
            AS3D_INFO("AS3D_UI_TAP frame=%u screen=%s item=%d %s touch=%d,%d", frameNo, screen.c_str(), t.item,
                      hit ? "tapped" : "not found", tx, ty);
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
                clock.reset();
                lastFrame = 0;
                runStep = 0;
                levelSteps = 0;
            }
        }
        const int loads = flow.levelLoads();
        levelLoaded = false;
        flow.uiFrame(uiDt, uin);
        if (menuView) menuView->applyHits(flow.frontend()); // a new menu: its rectangles for the log and the taps
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
            clock.reset();
            lastFrame = 0;
            runStep = 0;
            levelSteps = 0;
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

        // The name entry (all three games, ctr_name_entry.h): the software keyboard when the screen
        // opens, and on Y after a Cancel; between frames (no C3D frame is open here). The applet
        // suspends the app (APT hooks: the profile saved, the sound paused, the clock reset).
        if (ui::MenuItem* field = as3d_game::nameEntryField(flow.frontend())) {
            const bool ask = !nameAsked || (pad.down & pad::Y);
            if (ask && (run.nameApplet || !run.nameScript.empty())) {
                nameAsked = true;
                std::string name, kept;
                int detail = 0;
                NameKeyboard r = NameKeyboard::Ok;
                const uint64_t t0 = nowTicks();
                if (!run.nameScript.empty()) name = run.nameScript;
                else r = askName(field->text, field->w, &name, &detail);
                if (r == NameKeyboard::Ok && as3d_game::submitName(flow.frontend(), name, &kept)) queuedEnter = true;
                AS3D_INFO("AS3D_NAME keyboard=%s result=%s code=%d name=\"%s\" kept=\"%s\" ms=%.0f frame=%u",
                          run.nameScript.empty() ? "applet" : "script",
                          r == NameKeyboard::Ok ? "ok" : (r == NameKeyboard::Cancel ? "cancel" : "failed"), detail,
                          name.c_str(), kept.c_str(), ticksToMs(nowTicks() - t0), frameNo);
                if (r != NameKeyboard::Ok) AS3D_INFO("name entry: the touch keyboard stays (Y: the keyboard again)");
            }
        } else {
            nameAsked = false;
        }

        // The simulation, while the flow runs a world (attract level or mission).
        // The steps of this frame: the vblanks since the last one (ctr_loop.h FrameClock).
        const int due = clock.due(&dropped);
        int n = 0;
        // R4.7: what the HUD tiles select from (the world before this frame's steps).
        if (ses.hasLevel()) input.setInventory(touchInventoryOf(ses.world()));
        const uint64_t s0 = nowTicks();
        while (n < due) {
            if (!flow.worldRunning()) break;
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
                    // run.ini hint_hold (R4.4): the hint box stays up that many steps first.
                    if (hint && hintHeld++ < run.hintHold) in = FrameInput();
                    in.pausePressed = pause;
                }
                if (!hint) hintHeld = 0;
                // run.ini hud_stock (R4.7, the gallery): stock at the mission's first step.
                if (run.hudStock > 0 && running && runStep == 0) {
                    PlayerRecord& pr = ses.world().player(0);
                    for (int t : {0, 1, 3}) pr.missiles[t] = run.hudStock;
                    for (int k : {0, 1}) pr.powerups[k] = run.hudStock;
                    AS3D_INFO("hud_stock: %d of missiles 0, 1, 3 and power-ups 0, 1", run.hudStock);
                }
                if (run.botWeapons && running) applyWeaponTour(ses.world(), runStep, in);
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
                runStep == static_cast<unsigned>(run.reportAt) && reportedLoads != flow.levelLoads()) {
                reportedLoads = flow.levelLoads();
                AS3D_INFO("report_at: mission %d after %u bot steps (%u loop steps)", ses.mission(), runStep, levelSteps);
                logEndState(ses, ses.mission(), runStep);
            }
            if (flow.levelLoads() != before) {
                clock.reset();
                break;
            }
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
        info.padDown = pad.down;
        if (fpsCounter.valid()) {
            info.fps = static_cast<float>(fpsCounter.fps());
            info.fpsDropped = fpsCounter.dropped();
        }
        info.screen = screen.c_str();
        info.playing = flow.playing();

        drawFrame(*ctx, *frame, info, sample);
        lastCpu = sample.cpuMs;
        lastGpu = sample.gpuMs;
        const uint64_t end = nowTicks();
        sample.periodMs = lastFrame ? static_cast<float>(ticksToMs(end - lastFrame)) : 0.0f;
        lastFrame = end;
        if (sample.periodMs > 0.0f) {
            fpsCounter.frame(static_cast<double>(sample.periodMs) / 1000.0, static_cast<int>(dropped - droppedShown));
            droppedShown = dropped;
        }
        perf.record(sample);
        ++frameNo;
        if (maxFrames && frameNo >= maxFrames) break;
    }

    // Exit: the profile saved, the end state of the level running (if any), release.
    flow.saveNow();
    AS3D_INFO("stopped after %u frames, %u steps (%u in the last level), screen %s", frameNo, steps, levelSteps,
              screen.c_str());
    if (ses.hasLevel()) logEndState(ses, ses.mission(), levelSteps);
    logPaceSummary();
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
