// The first game's water on the PICA (water_renderer_pica.h).
#include "water_renderer_pica.h"

#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "as3d/core.h"
#include "as3d/draw_run.h"
#include "as3d/image.h"
#include "as3d/level.h"
#include "as3d/material_key.h"
#include "as3d/render_state.h"
#include "fx_draw_pica.h"
#include "pica_frame.h"
#include "texture_budget.h"

namespace as3d::pica {

namespace {

WaterPicaStats g_stats;

constexpr float kQuadHeight = 320.0f; // world units of map length per quad (one per 8-row chunk)

// The texture name has its extension replaced by .tga; a missing file gives a magenta placeholder
// and a warning (as the GL).
Image loadWaterTexture(Vfs& vfs, const std::string& name, int* missing) {
    std::string path = name;
    const size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) path.resize(dot);
    path += ".tga";
    Blob blob;
    Image img;
    if (!vfs.read(path, blob) || !decodeTga(blob.data(), blob.size(), img)) {
        AS3D_WARN("water: missing texture '%s'", path.c_str());
        ++*missing;
        img = Image();
        img.width = img.height = 4;
        img.rgba.assign(64, 0);
        for (int i = 0; i < 16; i++) {
            img.rgba[i * 4] = 255;
            img.rgba[i * 4 + 2] = 255;
            img.rgba[i * 4 + 3] = 255;
        }
    }
    return img;
}

uint8_t byteOf(float f) { return static_cast<uint8_t>(std::min(std::max(f, 0.0f), 1.0f) * 255.0f + 0.5f); }

// Per-frame vertices (arena) of this frame.
const FlatVertex* g_arena = nullptr;
unsigned g_arenaFrame = ~0u;

} // namespace

const WaterPicaStats& waterPicaStats() { return g_stats; }

void WaterPass::reset() {
    active_ = false;
    texture_ = Texture2D();
    quads_ = 0;
    g_stats = WaterPicaStats();
    g_arena = nullptr;
    g_arenaFrame = ~0u;
}

bool WaterPass::build(const Terrain& terrain, Vfs& vfs, bool sequelGrid, std::string* error) {
    (void)error;
    reset();
    if (!terrain.style().hasWater || terrain.style().waterTexture.empty()) return true;
    if (sequelGrid) {
        AS3D_INFO("pica water: TODO M5.1, the sequels' wave-grid water (two textures) is not drawn yet");
        return true;
    }
    style_ = terrain.style();
    int missing = 0;
    const Image img = loadWaterTexture(vfs, style_.waterTexture, &missing);
    TextureOptions to;
    to.mipmaps = true;
    to.wrapS = Wrap::Repeat;
    to.wrapT = Wrap::Repeat;
    to.cls = TextureClass::Opaque; // RGB565: the plane's alpha is the style's, not the texture's
    texture_.create(img, to);
    width_ = static_cast<float>(terrain.width()) * kHmapCellSize;
    const float length = static_cast<float>(terrain.height()) * kHmapCellSize;
    quads_ = static_cast<int>(std::ceil(length / kQuadHeight));
    active_ = texture_.valid() && quads_ > 0;
    g_stats.active = active_;
    g_stats.quads = quads_;
    g_stats.textureWidth = texture_.width();
    g_stats.textureHeight = texture_.height();
    g_stats.textureBytes = texture_.bytes();
    g_stats.missingTextures = missing;
    Placement pl = Placement::Linear;
    textureInfo(texture_, &pl, nullptr, nullptr, nullptr, nullptr);
    g_stats.textureVram = pl == Placement::Vram;
    AS3D_INFO("pica water: %d quads, texture '%s' %dx%d %.1f KB in %s", quads_, style_.waterTexture.c_str(),
              texture_.width(), texture_.height(), static_cast<double>(texture_.bytes()) / 1024.0,
              g_stats.textureVram ? "VRAM" : "linear memory");
    return true;
}

void WaterPass::prepare(float timeSeconds) {
    g_stats.quadsDrawn = g_stats.draws = 0;
    g_stats.arenaBytes = 0;
    g_arena = nullptr;
    g_arenaFrame = ~0u;
    if (!active_) return;
    time_ = timeSeconds;
    params_ = computeWaterParams(style_, timeSeconds);
    g_stats.planeZ = params_.planeZ;
    g_stats.uvOffset = params_.texOffset;
}

int WaterPass::draw(const Mat4& view, const Mat4& projection, const Vec3& fogColour, float fogStart, float fogEnd) {
    if (!active_ || !backendReady()) return 0;
    LinearArena& arena = frameArena();
    if (!g_arena || g_arenaFrame != arena.frames()) {
        g_arenaFrame = arena.frames();
        auto* v = arena.allocArray<FlatVertex>(static_cast<size_t>(quads_) * 6);
        g_arena = v;
        if (!v) return 0;
        const uint8_t r = byteOf(params_.colour.x), g = byteOf(params_.colour.y), b = byteOf(params_.colour.z),
                      a = byteOf(params_.alpha);
        const float uMax = width_ / 160.0f, vMax = kQuadHeight / 160.0f;
        const Vec2 off = params_.texOffset;
        for (int k = 0; k < quads_; ++k) {
            const float y0 = kQuadHeight * static_cast<float>(k), y1 = y0 + kQuadHeight;
            const float xs[4] = {0.0f, width_, width_, 0.0f}, ys[4] = {y0, y0, y1, y1};
            const float us[4] = {0.0f, uMax, uMax, 0.0f}, vs[4] = {0.0f, 0.0f, vMax, vMax};
            const int order[6] = {0, 1, 2, 0, 2, 3}; // a b c, a c d
            for (int i = 0; i < 6; ++i) {
                FlatVertex& o = v[k * 6 + i];
                const int c = order[i];
                o.pos[0] = xs[c];
                o.pos[1] = ys[c];
                o.pos[2] = params_.planeZ;
                o.uv[0] = us[c] + off.x;
                o.uv[1] = vs[c] + off.y;
                o.rgba[0] = r;
                o.rgba[1] = g;
                o.rgba[2] = b;
                o.rgba[3] = a;
            }
        }
        g_stats.arenaBytes = static_cast<size_t>(quads_) * 6 * sizeof(FlatVertex);
    }
    if (!g_arena) return 0;

    // The GL's conservative clip-space test of each quad's 4 corners against this eye's matrix.
    const Mat4 m = projection * view;
    std::vector<DrawRun> runs;
    const MaterialKey key = blendKey(RenderBlend::Alpha, true, true, CullMode::Off);
    int drawn = 0;
    for (int k = 0; k < quads_; ++k) {
        const float y0 = kQuadHeight * static_cast<float>(k), y1 = y0 + kQuadHeight;
        const float xs[2] = {0.0f, width_}, ys[2] = {y0, y1};
        int outs[6] = {0, 0, 0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            const Vec4 c = m * Vec4{xs[i & 1], ys[i >> 1], params_.planeZ, 1.0f};
            if (c.x < -c.w) outs[0]++;
            if (c.x > c.w) outs[1]++;
            if (c.y < -c.w) outs[2]++;
            if (c.y > c.w) outs[3]++;
            if (c.z < -c.w) outs[4]++;
            if (c.z > c.w) outs[5]++;
        }
        bool anyIn = true;
        for (int i = 0; i < 6; ++i)
            if (outs[i] == 4) anyIn = false;
        if (!anyIn) continue;
        DrawRun r;
        r.key = key;
        r.texture = &texture_;
        r.first = k * 6;
        r.count = 6;
        appendDrawRun(runs, r);
        ++drawn;
    }
    g_stats.quadsDrawn = drawn;
    if (runs.empty()) return 0;
    FxEyeParams eye;
    eye.view = view;
    eye.projection = projection;
    eye.fogColour = fogColour;
    eye.fogStart = fogStart;
    eye.fogEnd = fogEnd;
    g_stats.draws = drawFlatRuns(g_arena, runs, eye);
    MaterialKey plain;
    applyMaterialState(plain);
    return g_stats.draws;
}

} // namespace as3d::pica
