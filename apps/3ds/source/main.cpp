// The Nintendo 3DS feasibility spike (docs/3ds-spike.md).
//
// 1. Mounts the AirStrike 3D paks from sdmc:/3ds/openairstrike/as3d/, loads the definitions.
// 2. Runs missions 1, 6 and 14 headless, 600 frames each with the scripted test pilot
//    (botInput, as `as3d_sim --bot`), measuring the step time with the system tick counter
//    and the heap in use. Prints to the bottom screen and to sdmc:/3ds/openairstrike/spike_log.txt;
//    the engine's own log goes to sdmc:/3ds/openairstrike/engine_log.txt.
// 3. Reloads mission 1 and, on the top screen, draws its terrain and the player's helicopter
//    with citro3d for 300 frames while the world keeps stepping, measuring CPU and GPU time.
// START quits at the end. With mode=probe in sdmc:/3ds/openairstrike/run.ini the GPU probe (probe.cpp) runs instead,
// with mode=terraintest the terrain test (terrain_test.cpp).
#include <3ds.h>
#include <citro3d.h>
#include <malloc.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/defs.h"
#include "as3d/game_data.h"
#include "as3d/game_profile.h"
#include "as3d/input.h"
#include "as3d/vfs.h"
#include "as3d/world.h"

#include "pica_test.h"
#include "probe.h"
#include "render.h"
#include "spike.h"
#include "terrain_test.h"

extern "C" {
extern u32 __ctru_heap_size;
extern u32 __ctru_linear_heap_size;
}

namespace {

const char* kDir = "sdmc:/3ds/openairstrike";
std::FILE* g_log = nullptr;

void out(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void out(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::fputs(buf, stdout);
    if (g_log) {
        std::fputs(buf, g_log);
        std::fflush(g_log);
    }
}

constexpr double kTicksPerMs = SYSCLOCK_ARM11 / 1000.0;
double ms(u64 ticks) { return static_cast<double>(ticks) / kTicksPerMs; }

// Bytes allocated by malloc/new right now.
size_t heapInUse() { return static_cast<size_t>(mallinfo().uordblks); }
size_t heapArena() { return static_cast<size_t>(mallinfo().arena); }

double mb(size_t b) { return static_cast<double>(b) / (1024.0 * 1024.0); }

void memLine(const char* what) {
    out("[mem] %-14s heap %6.2f MB (arena %6.2f), linear free %5.2f MB\n", what, mb(heapInUse()), mb(heapArena()),
        mb(linearSpaceFree()));
}

void waitStart() {
    out("\nSTART quits.\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
}

struct RunResult {
    bool ok = false;
    double loadMs = 0;
};

// Loads `level` into a fresh world and steps it `frames` frames with the test pilot.
std::unique_ptr<as3d::World> runMission(as3d::Vfs& vfs, const as3d::DefDatabase& db, const as3d::GameProfile& game,
                                        const char* level, int frames, RunResult* res) {
    using namespace as3d;
    WorldConfig cfg;
    cfg.rules = &game.rules;
    size_t heap0 = heapInUse();
    u64 t0 = svcGetSystemTick();
    auto world = std::make_unique<World>();
    world->init(vfs, db, cfg);
    std::string err;
    if (!world->loadLevel(level, &err)) {
        out("mission %s: load failed: %s\n", level, err.c_str());
        return nullptr;
    }
    res->loadMs = ms(svcGetSystemTick() - t0);
    size_t heapLoaded = heapInUse();
    out("\n== mission %s: load %.0f ms, +%.2f MB\n", level, res->loadMs, mb(heapLoaded - heap0));

    u64 sum = 0, worst = 0;
    int worstFrame = -1, over16 = 0, over33 = 0, maxList = world->listCount();
    size_t peakHeap = heapLoaded;
    for (int f = 0; f < frames; ++f) {
        PlayerInput in = botInput(static_cast<u32>(f)).toPlayerInput();
        u64 a = svcGetSystemTick();
        world->step(in);
        u64 d = svcGetSystemTick() - a;
        sum += d;
        if (d > worst) {
            worst = d;
            worstFrame = f;
        }
        if (ms(d) > 16.667) over16++;
        if (ms(d) > 33.333) over33++;
        maxList = std::max(maxList, world->listCount());
        if (f % 10 == 0) peakHeap = std::max(peakHeap, heapInUse());
        if (!aptMainLoop()) break;
    }
    const WorldStats& st = world->stats();
    out("step avg %.2f ms, worst %.2f ms (f%d)\n", ms(sum) / frames, ms(worst), worstFrame);
    out(">16.7ms: %d, >33.3ms: %d frames\n", over16, over33);
    out("peak heap %.2f MB, list max %d\n", mb(peakHeap), maxList);
    // The same summary lines as as3d_sim, to compare against the desktop.
    out("level %s: %d frames, map_pos %.1f, list entities %d (max %d), slots %d (max %d)\n", level, frames,
        static_cast<double>(world->mapPos()), world->listCount(), maxList, world->slotsInUse(), st.maxSlotsInUse);
    out("created %llu, freed %llu, refused %llu, script errors %llu, stalls %llu\n",
        static_cast<unsigned long long>(st.entitiesCreated), static_cast<unsigned long long>(st.entitiesFreed),
        static_cast<unsigned long long>(st.spawnRefused), static_cast<unsigned long long>(st.scriptErrors),
        static_cast<unsigned long long>(st.stalls));
    int pe = world->playerEntityIndex(0);
    out("p_scores %.0f, kills %d of %d, player (%.1f, %.1f), health %.0f\n",
        static_cast<double>(world->player(0).scores), world->player(0).kills, world->enemiesInLevel(),
        pe >= 0 ? static_cast<double>(world->entity(pe).f(F_ORIGIN)) : 0.0,
        pe >= 0 ? static_cast<double>(world->entity(pe).f(F_ORIGIN + 1)) : 0.0,
        pe >= 0 ? static_cast<double>(world->entity(pe).f(F_HEALTH)) : 0.0);
    res->ok = true;
    return world;
}

} // namespace

int runSpike() {
    using namespace as3d;
    // vramSpaceFree() reports 0 until the VRAM heap has served its first allocation.
    if (void* warm = vramAlloc(0x100)) vramFree(warm);
    const u32 vramAtStart = vramSpaceFree();
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, nullptr);
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);

    // run.ini mode=picatest: the PICA core test (pica_test.cpp) runs before any game data is read.
    {
        probe::RunConfig early = probe::readRunConfig(std::string(kDir) + "/run.ini");
        if (early.picatest) {
            picatest::run(kDir, early.holdFrames);
            C3D_Fini();
            gfxExit();
            return 0;
        }
    }

    std::string logPath = std::string(kDir) + "/spike_log.txt";
    g_log = std::fopen(logPath.c_str(), "w");
    std::string engineLog = std::string(kDir) + "/engine_log.txt";
    if (!std::freopen(engineLog.c_str(), "w", stderr)) out("(engine log not redirected)\n");
    setLogLevel(LogLevel::Warn);

    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    out("AirStrike 3DS spike. %s 3DS\n", n3ds ? "New" : "Old");
    out("APPLICATION %.1f MB: used %.1f, free %.1f\n", mb(osGetMemRegionSize(MEMREGION_APPLICATION)),
        mb(osGetMemRegionUsed(MEMREGION_APPLICATION)), mb(osGetMemRegionFree(MEMREGION_APPLICATION)));
    out("heap %.1f MB, linear heap %.1f MB\n", mb(__ctru_heap_size), mb(__ctru_linear_heap_size));
    memLine("start");

    const GameProfile* game = findGameProfile("as3d");
    Vfs vfs;
    u64 t0 = svcGetSystemTick();
    for (const char* const* p = game->paks; *p; ++p) {
        std::string path = std::string(kDir) + "/as3d/" + *p;
        std::unique_ptr<IFileSource> src;
        if (auto stream = openFileStream(path)) src = makePakSource(std::move(stream));
        if (!src) {
            out("cannot mount %s\n", path.c_str());
            if (g_log) std::fclose(g_log);
            waitStart();
            C3D_Fini();
            gfxExit();
            return 1;
        }
        vfs.mount(std::move(src));
    }
    out("paks mounted in %.0f ms\n", ms(svcGetSystemTick() - t0));
    {
        // game_data.cpp (std::filesystem) identifies the paks by content.
        const GameProfile* id = identifyPaks(std::string(kDir) + "/as3d");
        std::vector<GameData> found = detectGames(kDir);
        out("paks identify as %s; detectGames: %d\n", id ? id->key : "(none)", static_cast<int>(found.size()));
    }
    memLine("paks");
    t0 = svcGetSystemTick();
    DefDatabase db;
    if (!db.load(vfs)) {
        out("cannot load the definitions\n");
        waitStart();
        C3D_Fini();
        gfxExit();
        return 1;
    }
    out("definitions in %.0f ms\n", ms(svcGetSystemTick() - t0));
    memLine("defs");

    // run.ini: mode=probe runs the M1.0 GPU probe (probe.cpp) instead of the spike.
    probe::RunConfig runCfg = probe::readRunConfig(std::string(kDir) + "/run.ini");
    if (runCfg.probe) {
        if (g_log) std::fclose(g_log);
        g_log = nullptr;
        probe::run(vfs, db, *game, std::string(kDir) + "/probe_log.txt", vramAtStart, runCfg);
        std::fflush(stderr);
        C3D_Fini();
        gfxExit();
        return 0;
    }
    // mode=terraintest: the M1.4 terrain test (terrain_test.cpp).
    if (runCfg.terraintest) {
        if (g_log) std::fclose(g_log);
        g_log = nullptr;
        terraintest::run(vfs, db, *game, kDir, runCfg.level, runCfg.holdFrames);
        std::fflush(stderr);
        C3D_Fini();
        gfxExit();
        return 0;
    }

    const char* levels[] = {"1", "6", "14"};
    for (const char* lv : levels) {
        RunResult r;
        std::unique_ptr<World> w = runMission(vfs, db, *game, lv, 600, &r);
        memLine("after run");
        w.reset();
        memLine("freed");
        if (!aptMainLoop()) break;
    }

    // Question 3: draw mission 1 while it keeps running.
    out("\n== render: mission 1 on the top screen\n");
    RunResult r;
    std::unique_ptr<World> world = runMission(vfs, db, *game, "1", 120, &r);
    std::string err, report;
    u32 vram0 = vramSpaceFree();
    size_t linear0 = linearSpaceFree();
    if (world && spike::renderInit(&err)) {
        u32 vramTarget = vramSpaceFree();
        spike::renderLoadScene(vfs, *world, &report);
        out("%s", report.c_str());
        out("VRAM free: before %.2f, after target %.2f, after scene %.2f MB; linear used by scene %.2f MB\n",
            mb(vram0), mb(vramTarget), mb(vramSpaceFree()), mb(linear0 - linearSpaceFree()));
        double sumStep = 0, sumCpu = 0, sumGpu = 0, sumFrame = 0, worstGpu = 0, worstFrame = 0, worstCpu = 0;
        int chunks = 0, verts = 0;
        const int kFrames = 300;
        u64 last = svcGetSystemTick();
        for (int f = 0; f < kFrames && aptMainLoop(); ++f) {
            u64 a = svcGetSystemTick();
            world->step(botInput(static_cast<u32>(120 + f)).toPlayerInput());
            double st = ms(svcGetSystemTick() - a);
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            spike::DrawStats ds = spike::renderFrame(*world);
            C3D_FrameEnd(0);
            u64 now = svcGetSystemTick();
            double frameMs = ms(now - last);
            last = now;
            if (f >= 2) { // the first frames carry the texture uploads
                sumStep += st;
                sumCpu += C3D_GetProcessingTime();
                sumGpu += C3D_GetDrawingTime();
                sumFrame += frameMs;
                worstGpu = std::max(worstGpu, static_cast<double>(C3D_GetDrawingTime()));
                worstCpu = std::max(worstCpu, static_cast<double>(C3D_GetProcessingTime()));
                worstFrame = std::max(worstFrame, frameMs);
            }
            chunks = ds.terrainChunks;
            verts = ds.vertices;
        }
        int n = kFrames - 2;
        out("%d frames: step %.2f ms, C3D cpu %.2f (worst %.2f)\n", n, sumStep / n, sumCpu / n, worstCpu);
        out("GPU draw %.2f ms (worst %.2f), frame %.2f ms (worst %.2f)\n", sumGpu / n, worstGpu, sumFrame / n, worstFrame);
        out("last frame: %d chunks, %d vertices, cmdbuf %.0f%%\n", chunks, verts, C3D_GetCmdBufUsage() * 100.0);
        memLine("rendering");
        out("heap high water (arena) %.2f MB\n", mb(heapArena()));
        if (g_log) std::fclose(g_log);
        g_log = nullptr;
        std::fflush(stderr);
        out("\nSTART quits.\n");
        for (u32 f = 120 + kFrames; aptMainLoop(); ++f) { // keep flying until START
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
            world->step(botInput(f).toPlayerInput());
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            spike::renderFrame(*world);
            C3D_FrameEnd(0);
        }
    } else {
        out("render init failed: %s\n", err.c_str());
        if (g_log) std::fclose(g_log);
        g_log = nullptr;
        waitStart();
    }
    spike::renderShutdown();
    world.reset();
    C3D_Fini();
    gfxExit();
    return 0;
}
