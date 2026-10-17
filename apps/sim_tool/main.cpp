// as3d_sim: headless simulation of one level (no GL, no SDL, no audio).
//
//   as3d_sim --level 1 --frames 3600 [--seed S] [--difficulty D] [--players N] [--heli 0..9]
//            [--dump-state state.json] [--builtin-report report.json] [--data ROOT]
//            [--bot | --pilot | --input-script FILE] [--bot-weapons] [--record FILE] [--god] [--trace-player FILE]
//            [--upgrades N] [--items N] [--powerup T] [--missile T] [--pause-every K] [--fault-player F]
//            [--time] [--step-times [N]] [--watch NAME [N]]
//
// Game data comes from ROOT/assets_extracted, ROOT from --data or $AS3D_DATA_ROOT; --game KEY
// (as3d, as2, gulf; default $AS3D_GAME, then as3d) picks the game, --paks DIR mounts its paks
// instead, --list-games prints the games found (as3d/game_data.h).
// Prints a summary: entity counts, script errors, and the builtin call counts sorted by
// count, stubs marked.
//
// --bot flies the scripted test pilot of as3d/input.h (the same input as `as3d_game --bot`);
// --pilot the pilot that looks at the world (stays in the lower middle, dodges, collects);
// --bot-weapons adds the weapon tour of as3d/input.h applyWeaponTour on top of --bot or --pilot
// (as as3d_game --bot-weapons and the 3DS's run.ini bot_weapons=1: the 3DS soak's end states are
// compared against this, tools/3ds_soak_compare.py);
// --record saves the input of the run as an input script, which `as3d_game --input-script`
// replays identically; --input-script plays an input script (as3d/input.h format; pause edges toggle the pause
// like the game does). --god turns on god mode (the `iwannabe` cheat of engine-behaviour.md
// 14). --trace-player writes one line per frame with player 1's position, its projected
// screen rectangle on the 800x600 collision viewport and its on-screen bit 0x08.
//
// Developer options (docs/spec/issues/165): --upgrades N sets every weapon upgrade slot to N
// and --items N every power-up and missile count to N (both re-applied every 10 s, a respawn
// clears the items); --powerup T / --missile T give 9 of that one type and select it;
// --pause-every K pauses the world for 90 frames every K frames; --fault-player F makes
// player 1's script fault as it is killed at frame F and every 25 s after, which exercises
// the respawn watchdog. --time measures each world step (wall clock) and prints one
// `time:` line (mean, median, 99th percentile and worst step in ms, the level load) and one
// `phases:` line (mean ms per step of each World::step phase, StepPhaseTimes); the clock
// reads never touch the simulation (tools/sim_bench.sh). The summary ends with the deepest handler nesting, the forced
// respawns, the entities stopped after a script error, and any faulted entity still around.
// --step-times [N] (implies the clock reads of --time) prints one `steps:` row per window of N
// steps (default 1200): mean and p95 ms per step, the phase means, the list entities and slots
// in use at the window's end (docs/3ds-port.md "Simulation step growth"): how the step cost
// grows through a mission.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/defs.h"
#include "as3d/game_data.h"
#include "as3d/platform.h"
#include "as3d/input.h"
#include "as3d/script.h"
#include "as3d/script_host.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "as3d/world_particles.h"
#include "as3d/particles.h"
#include <map>

using namespace as3d;

namespace {

const char* statusName(script::BuiltinStatus s) {
    switch (s) {
        case script::BuiltinStatus::Implemented: return "implemented";
        case script::BuiltinStatus::Approximate: return "approximate";
        case script::BuiltinStatus::Stub: return "stub";
    }
    return "?";
}

bool writeFile(const std::string& path, const std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

int usage() {
    std::fprintf(stderr,
                 "usage: as3d_sim --level N --frames N [--seed S] [--difficulty 0..4] [--camera 0..3] [--players 1|2] [--heli 0..9]\n"
                 "                [--dump-state FILE] [--builtin-report FILE] [--data ROOT]\n"
                 "                [--bot | --pilot | --input-script FILE] [--bot-weapons] [--record FILE] [--god]\n"
                 "                [--trace-player FILE] [--game as3d|as2|gulf] [--paks DIR] [--list-games]\n"
                 "                [--upgrades N] [--items N] [--powerup T] [--missile T] [--pause-every K] [--fault-player F]\n"
                 "                [--time] [--step-times [N]] [--watch NAME [N]]\n");
    return 2;
}

// One line per frame for player 1 (see the header comment).
void tracePlayer(std::FILE* f, const World& w) {
    int pi = w.playerEntityIndex(0);
    if (pi < 0) {
        std::fprintf(f, "%u %.3f none\n", w.frame(), static_cast<double>(w.mapPos()));
        return;
    }
    const Entity& e = w.entity(pi);
    const ScreenRect& r = e.rect;
    std::fprintf(f, "%u %.3f %.3f %.3f %.3f %d %d %.3f %.3f %.5f %.3f %.3f %.5f %.1f %.3f %.3f %.3f\n", w.frame(),
                 static_cast<double>(w.mapPos()), static_cast<double>(e.f(F_ORIGIN)),
                 static_cast<double>(e.f(F_ORIGIN + 1)), static_cast<double>(e.f(F_ORIGIN + 2)),
                 (e.rt & RT_COLLIDABLE) ? 1 : 0, w.sphereInFrustum(e.v3(F_BASE_ORIGIN), e.radius) ? 1 : 0,
                 static_cast<double>(r.min[0]), static_cast<double>(r.min[1]), static_cast<double>(r.min[2]),
                 static_cast<double>(r.max[0]), static_cast<double>(r.max[1]), static_cast<double>(r.max[2]),
                 static_cast<double>(e.f(F_HEALTH)), static_cast<double>(e.f(F_ANGLES)),
                 static_cast<double>(e.f(F_ANGLES + 1)), static_cast<double>(e.f(F_ANGLES + 2)));
}

} // namespace

int main(int argc, char** argv) {
    std::string level = "1", dumpPath, reportPath, dataRoot, inputPath, tracePath, recordPath, gameKey, paksDir;
    bool listGames = false;
    long frames = 600;
    bool bot = false, pilot = false, timeSteps = false, botWeapons = false;
    long stepWindow = 0; // --step-times: window length in steps (0: off)
    std::string watchName; // --watch NAME [N]: every N frames, the entities of that definition name
    long watchEvery = 60;
    int upgrades = -1, items = -1, powerupType = -1, missileType = -1; long pauseEvery = 0, faultPlayerAt = -1; // developer options (see --help)
    long particleStats = 0; // --particle-stats [N]: live particles by emitter def every N frames
    WorldConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char*& out) {
            if (i + 1 >= argc) return false;
            out = argv[++i];
            return true;
        };
        const char* v = nullptr;
        if (a == "--level" && next(v)) level = v;
        else if (a == "--frames" && next(v)) frames = std::strtol(v, nullptr, 10);
        else if (a == "--seed" && next(v)) cfg.seed = static_cast<u32>(std::strtoul(v, nullptr, 10));
        else if (a == "--difficulty" && next(v)) cfg.difficulty = std::atoi(v);
        else if (a == "--camera" && next(v)) cfg.cameraMode = std::atoi(v); // the options' camera pitch 0..3
        else if (a == "--players" && next(v)) cfg.players = std::atoi(v);
        else if (a == "--heli" && next(v)) cfg.heli[0] = std::atoi(v);
        else if (a == "--dump-state" && next(v)) dumpPath = v;
        else if (a == "--builtin-report" && next(v)) reportPath = v;
        else if (a == "--data" && next(v)) dataRoot = v;
        else if (a == "--game" && next(v)) gameKey = v;
        else if (a == "--paks" && next(v)) paksDir = v;
        else if (a == "--list-games") listGames = true;
        else if (a == "--input-script" && next(v)) inputPath = v;
        else if (a == "--trace-player" && next(v)) tracePath = v;
        else if (a == "--record" && next(v)) recordPath = v;
        else if (a == "--bot") bot = true;
        else if (a == "--pilot") pilot = true;
        else if (a == "--bot-weapons") botWeapons = true;
        else if (a == "--god") cfg.godMode = true;
        else if (a == "--time") timeSteps = true;
        else if (a == "--watch" && next(v)) {
            watchName = v;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') watchEvery = std::strtol(argv[++i], nullptr, 10);
            if (watchEvery <= 0) return usage();
        }
        else if (a == "--step-times") {
            timeSteps = true;
            stepWindow = 1200;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') stepWindow = std::strtol(argv[++i], nullptr, 10);
            if (stepWindow <= 0) return usage();
        }
        else if (a == "--upgrades" && next(v)) upgrades = std::atoi(v);
        else if (a == "--items" && next(v)) items = std::atoi(v);
        else if (a == "--powerup" && next(v)) powerupType = std::atoi(v);
        else if (a == "--missile" && next(v)) missileType = std::atoi(v);
        else if (a == "--pause-every" && next(v)) pauseEvery = std::strtol(v, nullptr, 10);
        else if (a == "--particle-stats") {
            particleStats = 300;
            if (i + 1 < argc && argv[i + 1][0] != '-') particleStats = std::strtol(argv[++i], nullptr, 10);
        }
        else if (a == "--fault-player" && next(v)) faultPlayerAt = std::strtol(v, nullptr, 10);
        else return usage();
    }
    if (dataRoot.empty()) {
        const char* env = std::getenv("AS3D_DATA_ROOT");
        dataRoot = env && *env ? env : ".";
    }
    if (listGames) {
        std::fputs(describeGames(dataRoot).c_str(), stdout);
        return 0;
    }
    GameData data;
    {
        std::string gerr;
        if (!chooseGameData(dataRoot, gameKey, paksDir, &data, &gerr)) {
            std::fprintf(stderr, "as3d_sim: %s\n", gerr.c_str());
            return 2;
        }
    }
    cfg.rules = &data.game->rules;
    if (frames < 0 || frames > 10'000'000) return usage();
    if ((bot ? 1 : 0) + (pilot ? 1 : 0) + (inputPath.empty() ? 0 : 1) > 1) return usage();
    InputScript script;
    if (!inputPath.empty()) {
        std::string err;
        if (!script.load(inputPath, &err)) {
            std::fprintf(stderr, "as3d_sim: %s: %s\n", inputPath.c_str(), err.c_str());
            return 1;
        }
    }
    InputScriptPlayer scriptPlayer(script);
    std::FILE* trace = nullptr;
    if (!tracePath.empty()) {
        trace = std::fopen(tracePath.c_str(), "w");
        if (!trace) {
            std::fprintf(stderr, "as3d_sim: cannot write %s\n", tracePath.c_str());
            return 1;
        }
        std::fprintf(trace, "# frame map_pos x y z onscreen08 sphere_in_frustum rect_min_x rect_min_y rect_min_z "
                            "rect_max_x rect_max_y rect_max_z health angles0 angles1 angles2 (800x600, y up)\n");
    }

    Vfs vfs;
    std::string where = data.extractedDir;
    if (paksDir.empty() && data.hasExtracted) {
        vfs.mount(makeDirSource(data.extractedDir));
    } else {
        where = "the paks of " + std::string(data.game->key);
        for (const std::string& pak : data.paks) { // mount order: later ones override
            std::unique_ptr<IFileSource> src;
            if (auto stream = openFileStream(pak)) src = makePakSource(std::move(stream));
            if (!src) {
                std::fprintf(stderr, "as3d_sim: cannot mount %s\n", pak.c_str());
                return 1;
            }
            vfs.mount(std::move(src));
        }
    }
    DefDatabase db;
    if (!db.load(vfs)) {
        std::fprintf(stderr, "as3d_sim: cannot load definitions from %s\n", where.c_str());
        return 1;
    }
    World world;
    world.init(vfs, db, cfg);
    std::string err;
    using Clock = std::chrono::steady_clock;
    const Clock::time_point loadStart = Clock::now();
    if (!world.loadLevel(level, &err)) {
        std::fprintf(stderr, "as3d_sim: cannot load level %s: %s\n", level.c_str(), err.c_str());
        return 1;
    }
    const double loadMs = std::chrono::duration<double, std::milli>(Clock::now() - loadStart).count();
    std::vector<double> stepMs;
    if (timeSteps) {
        stepMs.reserve(static_cast<size_t>(frames));
        world.setPhaseTiming(true);
    }
    PlayerInput input;
    InputRecorder recorder;
    int maxList = world.listCount();
    // --step-times: the phase sums and the counts at each window's end.
    struct WindowMark {
        StepPhaseTimes phases;
        int list = 0, slots = 0;
    };
    std::vector<WindowMark> windowMarks;
    long completeFrame = -1, gameOverFrame = -1;
    for (long f = 0; f < frames; ++f) {
        if (f % 600 == 0) {
            // Developer options: weapon upgrades and item counts are re-applied (a respawn clears the items).
            for (int p = 0; p < cfg.players; ++p) {
                PlayerRecord& pr = world.player(p);
                if (upgrades >= 0) for (int& u : pr.upgrades) u = upgrades;
                if (items >= 0) {
                    for (int& n : pr.powerups) n = items;
                    for (int& n : pr.missiles) n = items;
                    if (pr.currentPowerup < 0) pr.currentPowerup = 0;
                    if (pr.currentMissile < 0) pr.currentMissile = 0;
                }
                if (powerupType >= 0 && powerupType < 16) { pr.powerups[powerupType] = 9; pr.currentPowerup = powerupType; }
                if (missileType >= 0 && missileType < 5) { pr.missiles[missileType] = 9; pr.currentMissile = missileType; }
            }
        }
        if (pauseEvery > 0 && f > 0 && (f % pauseEvery == 0 || f % pauseEvery == 90) && !world.hintShowing() && !world.levelComplete() && !world.gameOver()) {
            world.setPaused(!world.paused());
        }
        if (faultPlayerAt >= 0 && f >= faultPlayerAt && (f - faultPlayerAt) % 1500 == 0) {
            // Developer option: player 1's script faults as it dies, every 25 s (exercises the respawn watchdog).
            int pi = world.playerEntityIndex(0);
            if (pi >= 0) {
                world.entity(pi).scriptFaulted = true;
                world.damageEntity(pi, 1.0e6f, -1);
            }
        }
        if (bot || pilot) {
            FrameInput in = bot ? botInput(static_cast<u32>(f)) : botInput(world, static_cast<u32>(f));
            if (botWeapons) applyWeaponTour(world, static_cast<u32>(f), in);
            recorder.record(static_cast<u32>(f), in);
            input = in.toPlayerInput();
        } else if (!inputPath.empty()) {
            FrameInput in = scriptPlayer.frame(static_cast<u32>(f));
            // The P key (as3d_game's session): ignored while a hint box or a level end
            // holds the pause; unpausing clears p_action.
            if (in.pausePressed && !world.hintShowing() && !world.levelComplete() && !world.gameOver()) {
                bool nowPaused = !world.paused();
                world.setPaused(nowPaused);
                if (!nowPaused) {
                    for (int p = 0; p < kMaxPlayers; ++p) world.player(p).action = 0.0f;
                }
            }
            recorder.record(static_cast<u32>(f), in);
            input = in.toPlayerInput();
        } else {
            recorder.record(static_cast<u32>(f), FrameInput());
        }
        if (timeSteps) {
            const Clock::time_point t0 = Clock::now();
            world.step(input);
            stepMs.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
        } else {
            world.step(input);
        }
        if (trace) tracePlayer(trace, world);
        if (!watchName.empty() && (f + 1) % watchEvery == 0) {
            // The entities of the name: class, health, dead, runtime bits, state, origin and the
            // screen rectangle of the collision pass (a boss's part that should take hits).
            for (int i = 0; i < kMaxEntitySlots; ++i) {
                const Entity& e = world.entity(i);
                if (!e.inUse || e.name != watchName) continue;
                std::printf("watch: frame %d %s slot %d cls %g hp %g dead %g rt 0x%x state %d parent %d radius %.1f origin (%.1f, %.1f, %.1f) rect (%.1f, %.1f)-(%.1f, %.1f)\n",
                            f + 1, e.name.c_str(), i, static_cast<double>(e.f(F_CLASS)), static_cast<double>(e.f(F_HEALTH)),
                            static_cast<double>(e.f(F_DEAD)), e.rt, static_cast<int>(e.state), e.parent, static_cast<double>(e.radius),
                            static_cast<double>(e.f(F_ORIGIN)), static_cast<double>(e.f(F_ORIGIN + 1)), static_cast<double>(e.f(F_ORIGIN + 2)),
                            static_cast<double>(e.rect.min[0]), static_cast<double>(e.rect.min[1]), static_cast<double>(e.rect.max[0]),
                            static_cast<double>(e.rect.max[1]));
            }
        }
        if (particleStats > 0 && (f + 1) % particleStats == 0) {
            // Live particles by emitter def (what a renderer would draw as quads this frame),
            // the heaviest defs first, with the emitter count and the def's rate and life.
            std::vector<const ParticleEmitter*> ems;
            world.particles().collect(ems);
            struct Row { int live = 0, emitters = 0; const ParticleDesc* d = nullptr; };
            std::map<std::string, Row> rows;
            int total = 0;
            for (const ParticleEmitter* e : ems) {
                Row& r = rows[e->desc().name];
                r.d = &e->desc();
                r.emitters++;
                r.live += e->liveCount();
                total += e->liveCount();
            }
            std::vector<const std::pair<const std::string, Row>*> order;
            for (const auto& kv : rows) order.push_back(&kv);
            std::sort(order.begin(), order.end(), [](const auto* a, const auto* b) { return a->second.live > b->second.live; });
            std::printf("particles: frame %ld live %d emitters %d\n", f + 1, total, world.particles().emitterCount());
            for (size_t k = 0; k < order.size() && k < 8; ++k) {
                const Row& r = order[k]->second;
                if (r.live == 0) break;
                std::printf("  %6d  %3d x %-36s rate %d life %.2f pool %d%s\n", r.live, r.emitters, order[k]->first.c_str(),
                            r.d->emitRate, r.d->lifeTime, r.d->poolSize(), r.d->emitOnce ? " once" : "");
            }
        }
        if (completeFrame < 0 && world.levelComplete()) completeFrame = f + 1;
        if (gameOverFrame < 0 && world.gameOver()) gameOverFrame = f + 1;
        maxList = std::max(maxList, world.listCount());
        if (stepWindow > 0 && ((f + 1) % stepWindow == 0 || f + 1 == frames)) {
            windowMarks.push_back(WindowMark{world.phaseTimes(), world.listCount(), world.slotsInUse()});
        }
    }
    if (trace) std::fclose(trace);
    if (!recordPath.empty() && !recorder.script().save(recordPath)) {
        std::fprintf(stderr, "as3d_sim: cannot write %s\n", recordPath.c_str());
        return 1;
    }

    const WorldStats& st = world.stats();
    // Entities whose script faulted and that are still around (the player's helicopter is kept).
    for (int i : world.listEntities()) {
        const Entity& e = world.entity(i);
        if ((e.rt & RT_REMOVED) || !e.scriptFaulted) continue;
        std::printf("faulted entity %d '%s' script '%s' age %.1f at (%.0f, %.0f, %.0f)\n", i, e.name.c_str(), e.scriptPath.c_str(),
                    static_cast<double>(e.f(F_AGE)), static_cast<double>(e.f(F_ORIGIN)), static_cast<double>(e.f(F_ORIGIN + 1)),
                    static_cast<double>(e.f(F_ORIGIN + 2)));
    }
    std::printf("level %s: %ld frames, map_pos %.1f, list entities %d (max %d), slots %d (max %d)\n", level.c_str(),
                frames, static_cast<double>(world.mapPos()), world.listCount(), maxList, world.slotsInUse(),
                st.maxSlotsInUse);
    std::printf("created %llu, freed %llu, refused %llu, script errors %llu, stalls %llu\n",
                static_cast<unsigned long long>(st.entitiesCreated), static_cast<unsigned long long>(st.entitiesFreed),
                static_cast<unsigned long long>(st.spawnRefused), static_cast<unsigned long long>(st.scriptErrors),
                static_cast<unsigned long long>(st.stalls));
    std::printf("max dispatch depth %d, forced respawns %llu, faulted entities removed %llu\n", st.maxDispatchDepth,
                static_cast<unsigned long long>(st.forcedRespawns), static_cast<unsigned long long>(st.faultedRemoved));
    for (const std::string& e : st.firstErrors) std::printf("  error: %s\n", e.c_str());
    std::printf("level complete at frame %ld, game over at frame %ld (-1: never)\n", completeFrame, gameOverFrame);
    std::printf("paused %d, game over %d, level complete %d, p_lives %.0f, p_scores %.0f\n", world.paused() ? 1 : 0,
                world.gameOver() ? 1 : 0, world.levelComplete() ? 1 : 0, static_cast<double>(world.player(0).lives),
                static_cast<double>(world.player(0).scores));
    {
        int pe = world.playerEntityIndex(0);
        std::printf("game %s: kills %d of %d enemies, player origin (%.1f, %.1f), health %.0f, stamps %d\n",
                    gameProfile(world.game()).key, world.player(0).kills, world.enemiesInLevel(),
                    pe >= 0 ? static_cast<double>(world.entity(pe).f(F_ORIGIN)) : 0.0,
                    pe >= 0 ? static_cast<double>(world.entity(pe).f(F_ORIGIN + 1)) : 0.0,
                    pe >= 0 ? static_cast<double>(world.entity(pe).f(F_HEALTH)) : 0.0, world.terraMorphStampCount());
    }
    if (timeSteps && !stepMs.empty()) {
        double sum = 0.0;
        size_t worstAt = 0;
        for (size_t k = 0; k < stepMs.size(); ++k) {
            sum += stepMs[k];
            if (stepMs[k] > stepMs[worstAt]) worstAt = k;
        }
        std::vector<double> sorted = stepMs;
        std::sort(sorted.begin(), sorted.end());
        const size_t n = sorted.size();
        std::printf("time: steps %zu mean %.4f ms median %.4f ms p99 %.4f ms worst %.4f ms (frame %zu) load %.1f ms\n", n,
                    sum / static_cast<double>(n), sorted[n / 2], sorted[std::min(n - 1, n * 99 / 100)], stepMs[worstAt],
                    worstAt, loadMs);
        const StepPhaseTimes& ph = world.phaseTimes();
        const double k = ph.steps > 0 ? 1000.0 / static_cast<double>(ph.steps) : 0.0;
        std::printf("phases: camera %.4f spawn %.4f player %.4f entities %.4f collisions %.4f after %.4f (mean ms per step)\n",
                    ph.camera * k, ph.spawn * k, ph.player * k, ph.entities * k, ph.collisions * k, ph.after * k);
    }
    if (stepWindow > 0 && !stepMs.empty()) {
        std::printf("steps: %7s %8s %8s %8s %8s %8s %8s %8s %8s %5s %5s\n", "to", "mean_ms", "p95_ms", "camera", "spawn",
                    "player", "entities", "collide", "after", "list", "slots");
        StepPhaseTimes prev;
        size_t from = 0;
        for (const WindowMark& m : windowMarks) {
            const size_t to = static_cast<size_t>(m.phases.steps);
            if (to <= from) continue;
            std::vector<double> win(stepMs.begin() + static_cast<long>(from), stepMs.begin() + static_cast<long>(to));
            double sum = 0.0;
            for (double v : win) sum += v;
            std::sort(win.begin(), win.end());
            const double n = static_cast<double>(win.size());
            const double k = 1000.0 / n;
            std::printf("steps: %7zu %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f %5d %5d\n", to, sum / n,
                        win[std::min(win.size() - 1, win.size() * 95 / 100)], (m.phases.camera - prev.camera) * k,
                        (m.phases.spawn - prev.spawn) * k, (m.phases.player - prev.player) * k,
                        (m.phases.entities - prev.entities) * k, (m.phases.collisions - prev.collisions) * k,
                        (m.phases.after - prev.after) * k, m.list, m.slots);
            prev = m.phases;
            from = to;
        }
    }
    const std::vector<GameScriptHost::UnknownGlobal>& unknownGlobals = world.host().unknownGlobals();
    for (const auto& u : unknownGlobals) {
        std::printf("  unknown global: %s (%llu binds)\n", u.name.c_str(), static_cast<unsigned long long>(u.binds));
    }

    std::vector<script::BuiltinReport::Row> rows = world.report().rows();
    std::stable_sort(rows.begin(), rows.end(), [](const script::BuiltinReport::Row& a, const script::BuiltinReport::Row& b) {
        if (a.calls != b.calls) return a.calls > b.calls;
        return a.name < b.name;
    });
    std::printf("builtin calls (sorted by count):\n");
    for (const auto& r : rows) {
        std::printf("  %-20s %10llu  %s\n", r.name.c_str(), static_cast<unsigned long long>(r.calls), statusName(r.status));
    }
    if (!reportPath.empty()) {
        std::string j = "{\n  \"level\": \"" + level + "\",\n  \"frames\": " + std::to_string(frames) + ",\n  \"builtins\": [\n";
        for (size_t i = 0; i < rows.size(); ++i) {
            j += "    {\"name\": \"" + rows[i].name + "\", \"calls\": " + std::to_string(rows[i].calls) +
                 ", \"status\": \"" + statusName(rows[i].status) + "\"}" + (i + 1 < rows.size() ? ",\n" : "\n");
        }
        j += "  ],\n  \"game\": \"" + std::string(gameProfile(world.game()).key) + "\",\n";
        j += "  \"script_errors\": " + std::to_string(st.scriptErrors) + ",\n  \"stalls\": " + std::to_string(st.stalls) + ",\n";
        j += "  \"score\": " + std::to_string(static_cast<long long>(world.player(0).scores)) +
             ", \"kills\": " + std::to_string(world.player(0).kills) + ",\n  \"unknown_globals\": [";
        for (size_t i = 0; i < unknownGlobals.size(); ++i) j += (i ? ", \"" : "\"") + unknownGlobals[i].name + "\"";
        j += "]\n}\n";
        if (!writeFile(reportPath, j)) {
            std::fprintf(stderr, "as3d_sim: cannot write %s\n", reportPath.c_str());
            return 1;
        }
    }
    if (!dumpPath.empty() && !writeFile(dumpPath, world.dumpStateJson())) {
        std::fprintf(stderr, "as3d_sim: cannot write %s\n", dumpPath.c_str());
        return 1;
    }
    return 0;
}
