// as3d_sim: headless simulation of one level (no GL, no SDL, no audio).
//
//   as3d_sim --level 1 --frames 3600 [--seed S] [--difficulty D] [--players N] [--heli 0..9]
//            [--dump-state state.json] [--builtin-report report.json] [--data ROOT]
//            [--bot | --pilot | --input-script FILE] [--record FILE] [--god] [--trace-player FILE]
//
// Game data comes from ROOT/assets_extracted, ROOT from --data or $AS3D_DATA_ROOT; --game KEY
// (as3d, as2, gulf; default $AS3D_GAME, then as3d) picks the game, --paks DIR mounts its paks
// instead, --list-games prints the games found (as3d/game_data.h).
// Prints a summary: entity counts, script errors, and the builtin call counts sorted by
// count, stubs marked.
//
// --bot flies the scripted test pilot of as3d/input.h (the same input as `as3d_game --bot`);
// --pilot the pilot that looks at the world (stays in the lower middle, dodges, collects);
// --record saves the input of the run as an input script, which `as3d_game --input-script`
// replays identically; --input-script plays an input script (as3d/input.h format; pause edges toggle the pause
// like the game does). --god turns on god mode (the `iwannabe` cheat of engine-behaviour.md
// 14). --trace-player writes one line per frame with player 1's position, its projected
// screen rectangle on the 800x600 collision viewport and its on-screen bit 0x08.
#include <algorithm>
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
#include "as3d/vfs.h"
#include "as3d/world.h"

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
                 "usage: as3d_sim --level N --frames N [--seed S] [--difficulty 0..4] [--players 1|2] [--heli 0..9]\n"
                 "                [--dump-state FILE] [--builtin-report FILE] [--data ROOT]\n"
                 "                [--bot | --pilot | --input-script FILE] [--record FILE] [--god]\n"
                 "                [--trace-player FILE] [--game as3d|as2|gulf] [--paks DIR] [--list-games]\n");
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
    bool bot = false, pilot = false;
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
        else if (a == "--god") cfg.godMode = true;
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
    if (!world.loadLevel(level, &err)) {
        std::fprintf(stderr, "as3d_sim: cannot load level %s: %s\n", level.c_str(), err.c_str());
        return 1;
    }
    PlayerInput input;
    InputRecorder recorder;
    int maxList = world.listCount();
    long completeFrame = -1, gameOverFrame = -1;
    for (long f = 0; f < frames; ++f) {
        if (bot || pilot) {
            FrameInput in = bot ? botInput(static_cast<u32>(f)) : botInput(world, static_cast<u32>(f));
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
        world.step(input);
        if (trace) tracePlayer(trace, world);
        if (completeFrame < 0 && world.levelComplete()) completeFrame = f + 1;
        if (gameOverFrame < 0 && world.gameOver()) gameOverFrame = f + 1;
        maxList = std::max(maxList, world.listCount());
    }
    if (trace) std::fclose(trace);
    if (!recordPath.empty() && !recorder.script().save(recordPath)) {
        std::fprintf(stderr, "as3d_sim: cannot write %s\n", recordPath.c_str());
        return 1;
    }

    const WorldStats& st = world.stats();
    std::printf("level %s: %ld frames, map_pos %.1f, list entities %d (max %d), slots %d (max %d)\n", level.c_str(),
                frames, static_cast<double>(world.mapPos()), world.listCount(), maxList, world.slotsInUse(),
                st.maxSlotsInUse);
    std::printf("created %llu, freed %llu, refused %llu, script errors %llu, stalls %llu\n",
                static_cast<unsigned long long>(st.entitiesCreated), static_cast<unsigned long long>(st.entitiesFreed),
                static_cast<unsigned long long>(st.spawnRefused), static_cast<unsigned long long>(st.scriptErrors),
                static_cast<unsigned long long>(st.stalls));
    for (const std::string& e : st.firstErrors) std::printf("  error: %s\n", e.c_str());
    std::printf("level complete at frame %ld, game over at frame %ld (-1: never)\n", completeFrame, gameOverFrame);
    std::printf("paused %d, game over %d, level complete %d, p_lives %.0f, p_scores %.0f\n", world.paused() ? 1 : 0,
                world.gameOver() ? 1 : 0, world.levelComplete() ? 1 : 0, static_cast<double>(world.player(0).lives),
                static_cast<double>(world.player(0).scores));

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
        j += "  ]\n}\n";
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
