// as3d_sim: headless simulation of one level (no GL, no SDL, no audio).
//
//   as3d_sim --level 1 --frames 3600 [--seed S] [--difficulty D] [--players N]
//            [--dump-state state.json] [--builtin-report report.json] [--data ROOT]
//
// Game data comes from ROOT/assets_extracted, ROOT from --data or $AS3D_DATA_ROOT.
// Prints a summary: entity counts, script errors, and the builtin call counts sorted by
// count, stubs marked.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/defs.h"
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
                 "usage: as3d_sim --level N --frames N [--seed S] [--difficulty 0..4] [--players 1|2]\n"
                 "                [--dump-state FILE] [--builtin-report FILE] [--data ROOT]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    std::string level = "1", dumpPath, reportPath, dataRoot;
    long frames = 600;
    bool bot = false;
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
        else if (a == "--dump-state" && next(v)) dumpPath = v;
        else if (a == "--builtin-report" && next(v)) reportPath = v;
        else if (a == "--data" && next(v)) dataRoot = v;
        else if (a == "--bot") bot = true;
        else return usage();
    }
    if (frames < 0 || frames > 10'000'000) return usage();
    if (dataRoot.empty()) {
        const char* env = std::getenv("AS3D_DATA_ROOT");
        dataRoot = env && *env ? env : ".";
    }

    Vfs vfs;
    vfs.mount(makeDirSource(dataRoot + "/assets_extracted"));
    DefDatabase db;
    if (!db.load(vfs)) {
        std::fprintf(stderr, "as3d_sim: cannot load definitions from %s/assets_extracted\n", dataRoot.c_str());
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
    int maxList = world.listCount();
    for (long f = 0; f < frames; ++f) {
        if (bot) {
            // A deterministic test pilot: fire held, missiles and power-ups pulsed, weaving
            // left and right every 2 s, hint boxes confirmed.
            u32 a = ACT_FIRE;
            if ((f / 120) % 2) a |= ACT_LEFT;
            else a |= ACT_RIGHT;
            if ((f / 30) % 2) a |= ACT_MISSILE;
            if (f % 600 == 300) a |= ACT_POWERUP;
            input.action[0] = input.action[1] = a;
            input.confirm = true;
        }
        world.step(input);
        maxList = std::max(maxList, world.listCount());
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
