// Sprite quads (backend neutral), docs/spec/render-pipeline.md 3.3.
#include "as3d/sprite_render.h"

#include <algorithm>
#include <cmath>

namespace as3d {

void spriteBillboardAxes(const Mat4& view, Vec3& right, Vec3& up) {
    right = {view.at(0, 0), view.at(1, 0), view.at(2, 0)};
    up = {view.at(0, 1), view.at(1, 1), view.at(2, 1)};
}

void spriteUvs(const SpriteInstance& sp, Vec2 uv[4]) {
    float s0 = sp.minS, s1 = sp.maxS, t0 = sp.minT, t1 = sp.maxT;
    if (sp.frameCols > 0) {
        int cols = sp.frameCols, rows = std::max(sp.frameRows, 1);
        float du = 1.0f / static_cast<float>(cols), dv = 1.0f / static_cast<float>(rows);
        int f = std::max(sp.frame, 0);
        s0 = static_cast<float>(f % cols) * du;
        s1 = s0 + du;
        t0 = 1.0f - dv - static_cast<float>(f / cols) * dv;
        t1 = t0 + dv;
    }
    // Original t counts from the bottom of the picture; the engine's v from the top.
    uv[0] = {s0, 1.0f - t0};
    uv[1] = {s1, 1.0f - t0};
    uv[2] = {s1, 1.0f - t1};
    uv[3] = {s0, 1.0f - t1};
}

SpriteQuad buildSpriteQuad(const SpriteInstance& sp, const Vec3& right, const Vec3& up) {
    SpriteQuad q;
    spriteUvs(sp, q.uv);
    const float lx[4] = {sp.minX, sp.maxX, sp.maxX, sp.minX};
    const float ly[4] = {sp.minY, sp.minY, sp.maxY, sp.maxY};
    float a = degToRad(sp.yawDegrees);
    float c = std::cos(a), s = std::sin(a);
    float k = sp.scale > 0.001f ? sp.scale : 1.0f;
    for (int i = 0; i < 4; i++) {
        switch (sp.kind) {
            case SpriteKind::Billboard: {
                float x = (lx[i] * c - ly[i] * s) * k;
                float y = (lx[i] * s + ly[i] * c) * k;
                q.pos[i] = sp.origin + right * x + up * y;
                break;
            }
            case SpriteKind::Horizontal: // quad in the XY plane, turned about Z
                q.pos[i] = sp.origin + Vec3{lx[i] * c - ly[i] * s, lx[i] * s + ly[i] * c, 0.0f};
                break;
            case SpriteKind::Vertical: // vertex (x, 0, y) turned about Z: local y is height
                q.pos[i] = sp.origin + Vec3{lx[i] * c, lx[i] * s, ly[i]};
                break;
        }
    }
    return q;
}

int buildSpriteBatches(const SpriteInstance* sprites, size_t count, const Mat4& view, bool cullBackFaces,
                       std::vector<SpriteVertex>& verts, std::vector<DrawRun>& runs) {
    count = std::min(count, kMaxSprites);
    Vec3 right, up;
    spriteBillboardAxes(view, right, up);
    int built = 0;
    for (size_t i = 0; i < count; i++) {
        const SpriteInstance& sp = sprites[i];
        if (!sp.texture) continue;
        const size_t first = verts.size();
        SpriteQuad q = buildSpriteQuad(sp, right, up);
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k : order)
            verts.push_back({q.pos[k].x, q.pos[k].y, q.pos[k].z, q.uv[k].x, q.uv[k].y, sp.colour.x, sp.colour.y,
                             sp.colour.z, sp.colour.w});
        DrawRun r;
        r.key = blendKey(renderBlendOf(sp.blend), !sp.noDepthTest, !sp.noDepthWrite,
                         cullBackFaces ? CullMode::Back : CullMode::Off); // as2 delta 3.3: the sequels draw sprites unculled
        r.texture = sp.texture;
        r.first = static_cast<int>(first);
        r.count = 6;
        appendDrawRun(runs, r);
        built++;
    }
    return built;
}

} // namespace as3d
