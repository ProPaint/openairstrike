// Counters and small helpers shared by the PICA particle and sprite renderers (fx_pica.h).
#include "fx_pica.h"
#include "quality_pica.h"

#include <citro3d.h>

#include <algorithm>

#include "as3d/material_key.h"
#include "as3d/render_state.h"
#include "fx_draw_pica.h"
#include "pica_frame.h"
#include "state_check_pica.h"
#include "tev.h"

namespace as3d::pica {

namespace detail {

FxPicaStats& fxStats() {
    static FxPicaStats s;
    return s;
}

void planesOfProjection(const float* m, float* nearPlane, float* farPlane) {
    // as3d::Mat4 is column-major: element (col, row) is m[col * 4 + row].
    const float a = m[2 * 4 + 2], b = m[3 * 4 + 2];
    float n = 4.0f, f = 2000.0f;
    if (a != 0.0f && a != 1.0f) {
        n = b / (a - 1.0f);
        f = b / a;
    }
    if (!(n > 0.0f) || !(f > n)) {
        n = 4.0f;
        f = 2000.0f;
    }
    *nearPlane = n;
    *farPlane = f;
}

} // namespace detail

const FxPicaStats& fxPicaStats() { return detail::fxStats(); }

namespace {
RenderQuality g_quality;
}
void setRenderQuality(const RenderQuality& q) { g_quality = q; }
const RenderQuality& renderQuality() { return g_quality; }

namespace {
int g_frameEyes = 1;
}
void setFrameEyes(int eyes) { g_frameEyes = eyes; }
int frameEyes() { return g_frameEyes; }
bool terrainLodActive() {
    switch (g_quality.terrainLod) {
    case RenderQuality::TerrainLod::Off: return false;
    case RenderQuality::TerrainLod::Stereo: return g_frameEyes == 2;
    case RenderQuality::TerrainLod::Always: return true;
    }
    return false;
}

namespace {
uint8_t toByte(float v) { return static_cast<uint8_t>(std::min(std::max(v, 0.0f), 1.0f) * 255.0f + 0.5f); }

// Vertex colour alone, RGB and alpha (a run without texture).
TevConfig tevPrimaryOnly() {
    TevConfig c;
    c.count = 1;
    c.stages[0].srcRgb[0] = TevSrc::Primary;
    c.stages[0].srcA[0] = TevSrc::Primary;
    c.stages[0].funcRgb = TevFunc::Replace;
    c.stages[0].funcA = TevFunc::Replace;
    return c;
}
} // namespace

FlatVertex* uploadBuilderVertices(const float* v, size_t count) {
    LinearArena& arena = frameArena();
    auto* out = arena.allocArray<FlatVertex>(count);
    if (!out) {
        ++detail::fxStats().arenaFailures;
        return nullptr;
    }
    for (size_t i = 0; i < count; ++i, v += 9) {
        FlatVertex& o = out[i];
        o.pos[0] = v[0];
        o.pos[1] = v[1];
        o.pos[2] = v[2];
        o.uv[0] = v[3];
        o.uv[1] = v[4];
        o.rgba[0] = toByte(v[5]);
        o.rgba[1] = toByte(v[6]);
        o.rgba[2] = toByte(v[7]);
        o.rgba[3] = toByte(v[8]);
    }
    return out;
}

int drawFlatRuns(const FlatVertex* verts, const std::vector<DrawRun>& runs, const FxEyeParams& eye) {
    if (!verts || runs.empty() || !backendReady()) return 0;
    const Mat4 vp = eye.projection * eye.view;
    float nearPlane, farPlane;
    detail::planesOfProjection(eye.projection.m, &nearPlane, &farPlane);
    setDepthMapping(FogDepthMap::WBuffer, nearPlane);
    if (eye.fogStart < 1.0e7f && eye.fogEnd > eye.fogStart) setFog(nearPlane, farPlane, eye.fogStart, eye.fogEnd);
    else disableFog();
    setLevelFogColor(eye.fogColour);

    ShaderSet& sh = shaders();
    sh.bind(Shader::Flat);
    sh.setMvp(vp);
    sh.setVertices(verts);
    int draws = 0;
    MaterialKey last;
    for (const DrawRun& r : runs) {
        applyMaterialState(r.key);
        last = r.key;
        if (r.texture && r.texture->valid()) {
            applyTev(r.texMode == kTexClampAdd ? tevClampAddTexColor() : tevModulateTexColor());
            r.texture->bind(0);
        } else {
            applyTev(tevPrimaryOnly());
        }
        if (eye.check && draws == 0) checkPassState(*eye.check);
        C3D_DrawArrays(r.strip ? GPU_TRIANGLE_STRIP : GPU_TRIANGLES, r.first, r.count);
        ++draws;
    }
    if (eye.probe && draws > 0) probeGpuState(eye.probe);
    last.blend = RenderBlend::None;
    last.fog = FogColorMode::Level;
    last.depthTest = true;
    last.depthWrite = true;
    applyMaterialState(last);
    return draws;
}

} // namespace as3d::pica
