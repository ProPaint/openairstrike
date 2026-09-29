// `as3d_viewer particles <system> --time <s> --out file.png [--sheet [--cells N]] [--size WxH]
//  [--seed N] [--step s] [--yaw d] [--pitch d] [--dist f] [--target-z f]`:
// simulates one particle system from t = 0 with a fixed step and renders it headless.
// `--list` prints every system name. `--sheet` renders a contact sheet of the system at
// several times between 0 and --time.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "as3d/gfx.h"
#include "as3d/image.h"
#include "as3d/particle_render.h"
#include "as3d/platform.h"
#include "common.h"
#include "registry.h"

#include "as3d/defs.h"

namespace {

struct Opts {
    std::string name;
    std::string out;
    float time = 1.0f;
    float step = 1.0f / 60.0f;
    bool sheet = false;
    bool list = false;
    int cells = 6;
    int width = 480;
    int height = 360;
    unsigned seed = 1;
    float yaw = 0.0f;
    float pitch = 30.0f;
    float dist = 0.6f;
    float targetZ = 25.0f;
};

bool parseSize(const char* s, int& w, int& h) {
    return std::sscanf(s, "%dx%d", &w, &h) == 2 && w > 0 && h > 0 && w <= 4096 && h <= 4096;
}

bool parse(int argc, char** argv, Opts& o) {
    for (int i = 0; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](const char*& v) { if (i + 1 >= argc) return false; v = argv[++i]; return true; };
        const char* v = nullptr;
        if (a == "--time") { if (!next(v)) return false; o.time = static_cast<float>(std::atof(v)); }
        else if (a == "--out") { if (!next(v)) return false; o.out = v; }
        else if (a == "--step") { if (!next(v)) return false; o.step = static_cast<float>(std::atof(v)); }
        else if (a == "--cells") { if (!next(v)) return false; o.cells = std::atoi(v); }
        else if (a == "--seed") { if (!next(v)) return false; o.seed = static_cast<unsigned>(std::strtoul(v, nullptr, 10)); }
        else if (a == "--yaw") { if (!next(v)) return false; o.yaw = static_cast<float>(std::atof(v)); }
        else if (a == "--pitch") { if (!next(v)) return false; o.pitch = static_cast<float>(std::atof(v)); }
        else if (a == "--dist") { if (!next(v)) return false; o.dist = static_cast<float>(std::atof(v)); }
        else if (a == "--target-z") { if (!next(v)) return false; o.targetZ = static_cast<float>(std::atof(v)); }
        else if (a == "--size") { if (!next(v) || !parseSize(v, o.width, o.height)) return false; }
        else if (a == "--sheet") o.sheet = true;
        else if (a == "--list") o.list = true;
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return false; }
        else o.name = a;
    }
    if (!(o.step > 0.0f) || o.step > 0.5f || !(o.time >= 0.0f) || o.time > 600.0f || o.cells < 1 || o.cells > 64)
        return false;
    return true;
}

int run(int argc, char** argv) {
    Opts o;
    if (!parse(argc, argv, o) || (!o.list && (o.name.empty() || o.out.empty()))) {
        std::fprintf(stderr,
                     "usage: as3d_viewer particles <system-name> --time <seconds> --out file.png [--sheet] [--cells N] "
                     "[--size WxH] [--seed N] [--step s] [--yaw d] [--pitch d] [--dist f] [--target-z f]\n"
                     "       as3d_viewer particles --list\n");
        return 1;
    }
    as3d::Vfs vfs;
    if (!viewer::mountGameData(vfs)) { std::fprintf(stderr, "error: no game data (set AS3D_DATA_ROOT)\n"); return 1; }
    as3d::DefDatabase db;
    if (!db.load(vfs)) { std::fprintf(stderr, "error: could not load definitions\n"); return 1; }
    if (o.list) {
        for (const as3d::ParticleSystemDef& p : db.particleSystems()) std::printf("%s\n", p.name.c_str());
        return 0;
    }
    const as3d::ParticleSystemDef* def = db.findParticleSystem(o.name);
    if (!def) { std::fprintf(stderr, "error: no particle system '%s' (try --list)\n", o.name.c_str()); return 1; }
    as3d::ParticleDesc desc = as3d::ParticleDesc::fromDef(*def);

    as3d::GraphicsConfig cfg;
    cfg.headless = true;
    auto gl = as3d::createGraphicsContext(cfg);
    if (!gl) { std::fprintf(stderr, "error: could not create a headless graphics context\n"); return 1; }
    gl->makeCurrent();
    as3d::ParticleRenderer renderer;
    std::string err;
    if (!renderer.init(vfs, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }

    // Times to capture.
    std::vector<float> times;
    if (o.sheet) {
        for (int i = 0; i < o.cells; i++) times.push_back(o.time * static_cast<float>(i + 1) / static_cast<float>(o.cells));
    } else {
        times.push_back(o.time);
    }
    const int cellW = o.width, cellH = o.height;
    int cols = o.sheet ? (o.cells <= 3 ? o.cells : (o.cells <= 8 ? 3 : 4)) : 1;
    int rows = o.sheet ? (o.cells + cols - 1) / cols : 1;
    as3d::Image sheet;
    sheet.width = cols * cellW;
    sheet.height = rows * cellH;
    sheet.hasAlpha = true;
    if (static_cast<long long>(sheet.width) * sheet.height > 64LL * 1024 * 1024) {
        std::fprintf(stderr, "error: sheet too large\n");
        return 1;
    }
    sheet.rgba.assign(static_cast<size_t>(sheet.width) * sheet.height * 4, 255);

    as3d::RenderTarget target;
    if (!target.create(cellW, cellH, 4)) { std::fprintf(stderr, "error: cannot create render target\n"); return 1; }

    as3d::ParticleEmitter emitter(desc, o.seed);
    as3d::ParticleViewParams vp;
    const float dist = 130.0f * o.dist;
    const float yaw = as3d::degToRad(o.yaw), pitch = as3d::degToRad(o.pitch);
    as3d::Vec3 targetPos{0, 0, o.targetZ};
    as3d::Vec3 eye = targetPos + as3d::Vec3{std::sin(yaw) * std::cos(pitch), -std::cos(yaw) * std::cos(pitch),
                                            std::sin(pitch)} * dist;
    vp.view = as3d::lookAt(eye, targetPos, {0, 0, 1});
    vp.projection = as3d::perspective(60.0f, static_cast<float>(cellW) / static_cast<float>(cellH), 4.0f, 2000.0f);

    float now = 0.0f;
    for (size_t ti = 0; ti < times.size(); ti++) {
        // Fixed steps up to the capture time; the last step is shortened to land on it.
        while (now < times[ti] - 1.0e-6f) {
            float dt = std::min(o.step, times[ti] - now);
            emitter.update(dt);
            now += dt;
        }
        target.bind();
        as3d::clear({0.32f, 0.38f, 0.28f, 1.0f}, true);
        renderer.draw(emitter, vp);
        as3d::Image cell;
        if (!target.readPixels(cell)) { std::fprintf(stderr, "error: readback failed\n"); return 1; }
        int cx = static_cast<int>(ti) % cols, cy = static_cast<int>(ti) / cols;
        for (int y = 0; y < cellH; y++) {
            std::memcpy(&sheet.rgba[(static_cast<size_t>(cy * cellH + y) * sheet.width + cx * cellW) * 4],
                        &cell.rgba[static_cast<size_t>(y) * cellW * 4], static_cast<size_t>(cellW) * 4);
        }
        std::printf("t=%.3f live=%d drawn=%d\n", times[ti], emitter.liveCount(), renderer.lastParticleCount());
    }
    if (renderer.missingTextures()) std::fprintf(stderr, "warning: %d missing texture(s)\n", renderer.missingTextures());
    if (!as3d::writePng(o.out.c_str(), sheet)) { std::fprintf(stderr, "error: cannot write %s\n", o.out.c_str()); return 1; }
    std::printf("wrote %s (%dx%d)\n", o.out.c_str(), sheet.width, sheet.height);
    return 0;
}

} // namespace

AS3D_VIEWER_COMMAND("particles", "simulate and render a particle system (--time, --sheet, --list)", run);
