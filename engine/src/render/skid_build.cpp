// Skid-mark trail geometry (backend neutral), docs/spec/as2/render-pipeline.delta.md 7.7
// (as3d/skid_render.h).
#include "as3d/skid_render.h"

#include <algorithm>
#include <cmath>

namespace as3d {

SkidTrailParams skidTrailParams(const GameRules& rules) {
    SkidTrailParams p;
    if (rules.skidTrailPool > 0) p.maxTrails = rules.skidTrailPool;
    if (rules.skidMaxNodes > 0) p.maxNodes = rules.skidMaxNodes;
    if (rules.skidLife > 0.0f) {
        p.life = rules.skidLife;
        p.fadeStart = rules.skidFadeStart;
    }
    p.heightOffset = rules.skidHeightOffset;
    return p;
}

float skidNodeAlpha(float age, const SkidTrailParams& p) {
    if (age <= p.fadeStart) return 1.0f;
    if (age >= p.life) return 0.0f;
    const float span = p.life - p.fadeStart;
    return span > 0.0f ? 1.0f - (age - p.fadeStart) / span : 0.0f;
}

size_t buildSkidTrailStrip(const SkidTrail& trail, const TerrainGridView& grid, const SkidTrailParams& params,
                           std::vector<SkidVertex>& out) {
    const size_t n = trail.nodes.size();
    const size_t keep = std::min(n, static_cast<size_t>(std::max(params.maxNodes, 0)));
    if (keep < 2) return 0;
    const size_t first = n - keep;
    const size_t before = out.size();
    for (size_t i = first; i < n; ++i) {
        const SkidNode& nd = trail.nodes[i];
        Vec2 fwd = nd.direction;
        const float len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y);
        if (len > 0.0f) fwd = Vec2{fwd.x / len, fwd.y / len};
        const Vec2 lat{fwd.y, -fwd.x}; // X̂: the vehicle's right-hand side
        const float hw = nd.width * 0.5f;
        const float a = skidNodeAlpha(nd.age, params);
        const float v = nd.width != 0.0f ? nd.distance / nd.width : 0.0f;
        const Vec2 p0{nd.position.x - lat.x * hw, nd.position.y - lat.y * hw};
        const Vec2 p1{nd.position.x + lat.x * hw, nd.position.y + lat.y * hw};
        out.push_back({p0.x, p0.y, grid.heightAt(p0.x, p0.y) + params.heightOffset, 0.0f, v, a});
        out.push_back({p1.x, p1.y, grid.heightAt(p1.x, p1.y) + params.heightOffset, 1.0f, v, a});
    }
    return out.size() - before;
}

size_t skidVertexCapacity(const SkidTrailParams& params) {
    return static_cast<size_t>(std::max(params.maxTrails, 1)) * static_cast<size_t>(std::max(params.maxNodes, 2)) * 2;
}

int buildSkidTrails(const SkidTrail* trails, size_t count, const TerrainGridView& grid, const SkidTrailParams& params,
                    const SkidTextureResolver& resolve, std::vector<SkidVertex>& verts, std::vector<DrawRun>& runs) {
    if (!grid.valid() || count == 0) return 0;
    const size_t capacity = skidVertexCapacity(params);
    const size_t maxTrails = static_cast<size_t>(std::max(params.maxTrails, 0));
    const size_t base = verts.size();
    int built = 0;
    for (size_t i = 0; i < count && static_cast<size_t>(built) < maxTrails; ++i) {
        const SkidTrail& t = trails[i];
        if (t.texture.empty()) continue;
        const size_t first = verts.size();
        const size_t n = buildSkidTrailStrip(t, grid, params, verts);
        if (n == 0) continue;
        if (verts.size() - base > capacity) { // cannot happen with the params of init()
            verts.resize(first);
            break;
        }
        DrawRun r;
        r.key = blendKey(RenderBlend::Alpha, true, false, CullMode::Back, true);
        r.texture = resolve ? resolve(t.texture) : nullptr;
        r.first = static_cast<int>(first);
        r.count = static_cast<int>(n);
        r.strip = true;
        appendDrawRun(runs, r);
        ++built;
    }
    return built;
}

} // namespace as3d
