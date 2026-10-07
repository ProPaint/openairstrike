// MaterialKey: the backend-neutral description of the fixed-function state one draw needs
// (blend, depth, cull, polygon offset, fog colour, alpha test). Every pass in
// engine/src/render/gles sets one of these combinations; the GL backend applies a key with
// applyMaterialState (engine/src/render/gles/material_state.cpp), a PICA backend maps it to
// C3D_* calls. Keys compare and hash cheaply so a backend can sort or batch draws by key.
//
// materialKeyFor(Material) holds the model path's flag -> state mapping (formerly the GL
// kBlendTable; docs/spec/obj.md blend/rflag tables, docs/graphics.md "Material state table").
#pragma once

#include <cstddef>

#include "as3d/core.h"
#include "as3d/math.h"
#include "as3d/render_state.h"

namespace as3d {

struct Material;

// Blend equations the passes use (source, destination factors in GL terms):
//   None       blending off
//   Alpha      SRC_ALPHA, ONE_MINUS_SRC_ALPHA
//   Add        ONE, ONE                     (render-pipeline.md 4.1)
//   Filter     DST_COLOR, ZERO              (BLEND_FILTER, multiplicative: dst *= src)
//   Brightness DST_COLOR, SRC_COLOR         (the full-screen brightness quad: dst *= 2 src)
enum class RenderBlend : u8 { None, Alpha, Add, Filter, Brightness };

// Which colour the linear fog fades to: the level's fog colour, black (additive draws fade
// out instead of adding fog) or white (filter draws fade to "no change"). render-pipeline.md 2.2.
enum class FogColorMode : u8 { Level, Black, White };

struct MaterialKey {
    RenderBlend blend = RenderBlend::None;
    bool depthTest = true;
    bool depthWrite = true;
    CullMode cull = CullMode::Back;
    // glPolygonOffset(-1, -1) on filled polygons (ground marks, skid marks, terrain overlays).
    bool polygonOffset = false;
    FogColorMode fog = FogColorMode::Level;
    // Alpha-tested cutout. The model path never sets it (render-pipeline.md 4.1: the original
    // never enables the alpha test); a backend with an alpha-test unit honours it, the GL
    // backend's shaders have no alpha test and ignore it.
    bool alphaTest = false;

    // Every field in one integer: equal keys give equal values and vice versa.
    u32 packed() const {
        return static_cast<u32>(blend) | (depthTest ? 1u : 0u) << 3 | (depthWrite ? 1u : 0u) << 4 |
               static_cast<u32>(cull) << 5 | (polygonOffset ? 1u : 0u) << 7 | static_cast<u32>(fog) << 8 |
               (alphaTest ? 1u : 0u) << 10;
    }
};

inline bool operator==(const MaterialKey& a, const MaterialKey& b) { return a.packed() == b.packed(); }
inline bool operator!=(const MaterialKey& a, const MaterialKey& b) { return !(a == b); }

struct MaterialKeyHash {
    size_t operator()(const MaterialKey& k) const { return static_cast<size_t>(k.packed()); }
};

// The fog colour mode that goes with a blend: Add fades to black, Filter and Brightness to
// white, the rest to the level colour.
FogColorMode fogColorModeFor(RenderBlend blend);
// The colour to fog towards for `mode`, `levelFog` being the level's fog colour.
Vec3 fogColorFor(FogColorMode mode, const Vec3& levelFog);

// The state a model Material draws with: blend from Material::blend, depth test/write from
// RF_NODEPTHTEST/RF_NODEPTHWRITE, cull Back unless RF_NOCULLING (or the debug override,
// setCullDebugOverride in as3d/scene.h), fog colour from the blend, alphaTest copied from the
// material (always false for game data). No polygon offset.
MaterialKey materialKeyFor(const Material& material);

// Backend: sets blend, depth test/write, cull and polygon offset for `key`. The fog colour
// and the alpha test are shader inputs: the caller passes fogColorFor(key.fog, ...) itself.
void applyMaterialState(const MaterialKey& key);

} // namespace as3d
