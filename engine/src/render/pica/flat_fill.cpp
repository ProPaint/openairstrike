// flat_fill.h.
#include "flat_fill.h"

#include <citro3d.h>

#include "pica_frame.h"
#include "pica_space.h"
#include "shader_set.h"
#include "tev.h"

namespace as3d::pica {

void fillRect(float x0, float y0, float x1, float y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
              const MaterialKey& key) {
    const FrameTarget& t = currentTarget();
    auto* v = frameArena().allocArray<FlatVertex>(4);
    if (!v) return;
    const float xs[4] = {x0, x1, x0, x1}, ys[4] = {y1, y1, y0, y0};
    for (int i = 0; i < 4; ++i) {
        v[i].pos[0] = xs[i];
        v[i].pos[1] = ys[i];
        v[i].pos[2] = 0.0f;
        v[i].uv[0] = v[i].uv[1] = 0.0f;
        v[i].rgba[0] = v[i].rgba[1] = v[i].rgba[2] = v[i].rgba[3] = 255;
    }
    disableFog();
    applyMaterialState(key);
    const Mat4 px = picaTiltMatrix() * ortho(0.0f, static_cast<float>(t.width), static_cast<float>(t.height), 0.0f, -1.0f, 1.0f);
    ShaderSet& sh = shaders();
    sh.bind(Shader::Flat);
    sh.setMvp(px);
    sh.setVertices(v);
    applyTev(tevConstantColor(r, g, b, a));
    C3D_DrawArrays(GPU_TRIANGLE_STRIP, 0, 4);
}

void fillRect(float x0, float y0, float x1, float y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    MaterialKey key;
    key.blend = a == 255 ? RenderBlend::None : RenderBlend::Alpha;
    key.depthTest = false;
    key.depthWrite = false;
    key.cull = CullMode::Off;
    fillRect(x0, y0, x1, y1, r, g, b, a, key);
}

} // namespace as3d::pica
