// The M1.0 GPU probe. See probe.h.
#include "probe.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/defs.h"
#include "as3d/game_profile.h"
#include "as3d/input.h"
#include "as3d/vfs.h"
#include "as3d/world.h"

#include "render.h"

namespace probe {

namespace {

constexpr double kTicksPerMs = SYSCLOCK_ARM11 / 1000.0;
double ms(u64 ticks) { return static_cast<double>(ticks) / kTicksPerMs; }
double mb(double bytes) { return bytes / (1024.0 * 1024.0); }

constexpr int kWarmup = 10;
constexpr int kMeasured = 120;

std::FILE* g_file = nullptr;

// `toScreen` true: the bottom-screen console only (40 columns); false: the log file only (wide lines).
void emit(bool toScreen, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void emit(bool toScreen, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (toScreen) {
        std::fputs(buf, stdout);
    } else if (g_file) {
        std::fputs(buf, g_file);
        std::fflush(g_file);
    }
}

struct Stat {
    double avg = 0, p95 = 0, max = 0;
};

Stat stat(std::vector<double> v) {
    Stat s;
    if (v.empty()) return s;
    for (double x : v) s.avg += x;
    s.avg /= static_cast<double>(v.size());
    std::sort(v.begin(), v.end());
    size_t idx = (v.size() * 95 + 99) / 100; // ceil(0.95 n)
    s.p95 = v[std::min(v.size(), idx) - 1];
    s.max = v.back();
    return s;
}

struct Variant {
    char id;
    const char* what;
    spike::TexMode tex;
    bool right;   // second top target, gfxSet3D(true)
    bool bottom;  // bottom screen target
    int passes;   // terrain submissions per eye
    int quads;    // full-screen blended quads per eye
};

const Variant kVariants[] = {
    {'A', "baseline: RGBA8 textures in linear memory, one top target", spike::TexMode::LinearRGBA8, false, false, 1, 0},
    {'B', "RGBA8 textures in VRAM", spike::TexMode::VramRGBA8, false, false, 1, 0},
    {'C', "RGB565 textures in VRAM", spike::TexMode::VramRGB565, false, false, 1, 0},
    {'D', "C + second top target (stereo, iod 0)", spike::TexMode::VramRGB565, true, false, 1, 0},
    {'E', "D with the terrain drawn 3 times", spike::TexMode::VramRGB565, true, false, 3, 0},
    {'F', "C + 4 full-screen alpha-blended quads", spike::TexMode::VramRGB565, false, false, 1, 4},
    {'G', "C + CPU mip chain, trilinear", spike::TexMode::VramRGB565Mip, false, false, 1, 0},
    {'H', "C + bottom screen target (textured quad)", spike::TexMode::VramRGB565, false, true, 1, 0},
};

struct Result {
    char id = '?';
    bool ok = false;
    std::string error;
    Stat gpu, cpu, frame, step;
    double vramFree = 0, linearFree = 0; // MB, after the variant's setup, before drawing
    int verts = 0, chunks = 0;
};

std::string timestamp() {
    time_t t = time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", localtime(&t));
    return buf;
}

// Steps the world `n` frames without drawing; returns the average step time in ms.
double headlessStep(as3d::World& w, int n, u32* frame) {
    u64 sum = 0;
    for (int i = 0; i < n; ++i) {
        as3d::PlayerInput in = as3d::botInput((*frame)++).toPlayerInput();
        u64 a = svcGetSystemTick();
        w.step(in);
        sum += svcGetSystemTick() - a;
    }
    return ms(sum) / n;
}

Result runVariant(const Variant& v, as3d::World& world, u32* frame, int hold) {
    Result r;
    std::string rep;
    if (!spike::renderSetTargets(v.right, v.bottom)) {
        r.error = "cannot create the render target";
        return r;
    }
    if (!spike::renderSetTextures(v.tex, &rep)) {
        r.error = "cannot create the textures: " + rep;
        return r;
    }
    spike::FrameOptions fo;
    fo.terrainPasses = v.passes;
    fo.blendQuads = v.quads;
    fo.iod = 0.0f;
    spike::renderSetOptions(fo);
    r.vramFree = mb(static_cast<double>(vramSpaceFree()));
    r.linearFree = mb(static_cast<double>(linearSpaceFree()));
    emit(false, "  setup: %s", rep.c_str());

    std::vector<double> gpu, cpu, fr, st;
    u64 last = svcGetSystemTick();
    const int total = kWarmup + kMeasured + hold;
    for (int f = 0; f < total && aptMainLoop(); ++f) {
        u64 a = svcGetSystemTick();
        world.step(as3d::botInput((*frame)++).toPlayerInput());
        double stepMs = ms(svcGetSystemTick() - a);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        spike::DrawStats ds = spike::renderFrame(world);
        C3D_FrameEnd(0);
        u64 now = svcGetSystemTick();
        double frameMs = ms(now - last);
        last = now;
        r.verts = ds.vertices;
        r.chunks = ds.terrainChunks;
        if (f >= kWarmup && f < kWarmup + kMeasured) {
            gpu.push_back(C3D_GetDrawingTime());
            cpu.push_back(C3D_GetProcessingTime());
            fr.push_back(frameMs);
            st.push_back(stepMs);
        }
        if (f == kWarmup + kMeasured - 1 && hold > 0) emit(false, "  HOLD %c\n", v.id); // screenshot cue
    }
    r.gpu = stat(gpu);
    r.cpu = stat(cpu);
    r.frame = stat(fr);
    r.step = stat(st);
    r.ok = !gpu.empty();
    if (!r.ok) r.error = "no frames drawn";
    return r;
}

} // namespace

RunConfig readRunConfig(const std::string& path) {
    RunConfig c;
    std::FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return c;
    char line[128];
    while (std::fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == ';' || *p == '\n' || *p == '\0') continue;
        char* eq = std::strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        std::string key = p, val = eq + 1;
        while (!val.empty() && (val.back() == '\n' || val.back() == '\r' || val.back() == ' ')) val.pop_back();
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        if (key == "mode") {
            c.probe = val == "probe";
            c.picatest = val == "picatest";
            c.terraintest = val == "terraintest";
            c.fxtest = val == "fxtest";
            c.meshtest = val == "meshtest";
            c.uitest = val == "uitest";
            c.audiotest = val == "audiotest";
            c.musictest = val == "musictest";
            c.shadowtest = val == "shadowtest";
            c.markstest = val == "markstest";
            c.watertest = val == "watertest";
            c.envtest = val == "envtest";
            c.savetest = val == "savetest";
            c.sequeltest = val == "sequeltest";
        } else if (key == "only") c.only = val;
        else if (key == "game") {
            if (!val.empty()) c.game = val;
        }
        else if (key == "hold") c.holdFrames = std::max(0, std::atoi(val.c_str()));
        else if (key == "frames") c.botFrames = std::max(0, std::atoi(val.c_str()));
        else if (key == "level") c.level = std::max(1, std::atoi(val.c_str()));
        else if (key == "precache") c.precache = std::atoi(val.c_str()) != 0;
        else if (key == "music_taps") c.musicTaps = std::max(0, std::atoi(val.c_str()));
        else if (key == "music_files") c.musicFiles = val;
        else if (key == "shadow_rotations") c.shadowRotations = std::atoi(val.c_str()) != 0;
    }
    std::fclose(f);
    return c;
}

void run(as3d::Vfs& vfs, const as3d::DefDatabase& db, const as3d::GameProfile& game, const std::string& logPath,
         unsigned vramAtStart, const RunConfig& cfg) {
    using namespace as3d;
    g_file = std::fopen(logPath.c_str(), "a");
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    emit(false, "\n==== GPU probe %s, New3DS=%d ====\n", timestamp().c_str(), n3ds ? 1 : 0);
    emit(true, "GPU probe, %s 3DS\n", n3ds ? "New" : "Old");
    emit(false, "vramSpaceFree before gfxInit=%.2f MB, now=%.2f MB; linearSpaceFree=%.2f MB\n", mb(vramAtStart),
         mb(vramSpaceFree()), mb(static_cast<double>(linearSpaceFree())));

    WorldConfig wc;
    wc.rules = &game.rules;
    wc.godMode = true; // the pilot must survive 8 variants of mission 1
    auto world = std::make_unique<World>();
    world->init(vfs, db, wc);
    std::string err;
    if (!world->loadLevel("1", &err)) {
        emit(true, "mission 1: %s\n", err.c_str());
        emit(false, "mission 1: %s\n", err.c_str());
        if (g_file) std::fclose(g_file);
        return;
    }

    // Clock: headless step time before and after osSetSpeedupEnable(true).
    u32 frame = 0;
    headlessStep(*world, 20, &frame); // warm-up
    double before = headlessStep(*world, 60, &frame);
    double after = before;
    if (n3ds) {
        osSetSpeedupEnable(true);
        headlessStep(*world, 10, &frame);
        after = headlessStep(*world, 60, &frame);
    }
    if (n3ds) {
        emit(true, "clock %.2f -> %.2f ms/step (x%.2f)\n", before, after, before / after);
        emit(false, "clock: osSetSpeedupEnable(true) applied; 60 headless frames, step avg %.3f ms before, %.3f ms after (x%.2f)\n",
             before, after, before / after);
    } else {
        emit(true, "clock %.2f ms/step (Old 3DS)\n", before);
        emit(false, "clock: Old 3DS, speedup not applicable; 60 headless frames, step avg %.3f ms\n", before);
    }

    std::string report;
    if (!spike::renderInit(&err) || !spike::renderLoadScene(vfs, *world, &report)) {
        emit(true, "render init failed: %s\n", err.c_str());
        emit(false, "render init failed: %s\n", err.c_str());
        if (g_file) std::fclose(g_file);
        return;
    }
    emit(false, "scene: %s", report.c_str());

    std::vector<Result> results;
    for (const Variant& v : kVariants) {
        if (!aptMainLoop()) break;
        if (!cfg.only.empty() && cfg.only.find(v.id) == std::string::npos) continue;
        if (!v.bottom) std::printf("%c ", v.id); // the bottom screen is the target of H
        emit(false, "variant %c: %s\n", v.id, v.what);
        // A fresh mission 1 and the same pilot for every variant: identical camera and draw load.
        world.reset();
        world = std::make_unique<World>();
        world->init(vfs, db, wc);
        if (!world->loadLevel("1", &err)) {
            emit(false, "  FAILED: mission 1: %s\n", err.c_str());
            break;
        }
        frame = 0;
        Result r = runVariant(v, *world, &frame, cfg.holdFrames);
        r.id = v.id;
        if (r.ok) {
            emit(false,
                 "  gpu avg %.3f p95 %.3f max %.3f ms | cpu avg %.3f p95 %.3f ms | frame avg %.3f p95 %.3f ms | step avg "
                 "%.3f ms | vramFree %.2f MB linearFree %.2f MB | %d chunks, %d vertices/frame\n",
                 r.gpu.avg, r.gpu.p95, r.gpu.max, r.cpu.avg, r.cpu.p95, r.frame.avg, r.frame.p95, r.step.avg, r.vramFree,
                 r.linearFree, r.chunks, r.verts);
        } else {
            emit(false, "  FAILED: %s\n", r.error.c_str());
        }
        results.push_back(r);
    }

    // The table: back to the console on the bottom screen.
    spike::renderSetTargets(false, false);
    consoleInit(GFX_BOTTOM, nullptr);
    consoleClear();
    emit(true, "GPU probe %s 3DS (ms avg/p95)\n", n3ds ? "New" : "Old");
    emit(false, "\n| var | gpu avg | gpu p95 | cpu avg | cpu p95 | frame avg | frame p95 | vram free MB | linear free MB | verts |\n");
    emit(false, "|---|---|---|---|---|---|---|---|---|---|\n");
    for (size_t i = 0; i < results.size(); ++i) {
        const Result& r = results[i];
        char id = r.id;
        if (!r.ok) {
            emit(true, "%c FAILED: %.30s\n", id, r.error.c_str());
            emit(false, "| %c | FAILED: %s |\n", id, r.error.c_str());
            continue;
        }
        emit(true, "%c gpu %.2f/%.2f cpu %.2f/%.2f\n  frm %.2f/%.2f vr %.1f ln %.1f\n", id, r.gpu.avg, r.gpu.p95, r.cpu.avg,
             r.cpu.p95, r.frame.avg, r.frame.p95, r.vramFree, r.linearFree);
        emit(false, "| %c | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %d |\n", id, r.gpu.avg, r.gpu.p95,
             r.cpu.avg, r.cpu.p95, r.frame.avg, r.frame.p95, r.vramFree, r.linearFree, r.verts);
    }
    emit(true, "\nlog: probe_log.txt   START quits\n");
    if (g_file) std::fclose(g_file);
    g_file = nullptr;
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
    spike::renderShutdown();
}

} // namespace probe
