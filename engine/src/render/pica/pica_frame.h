// The frame interface of the PICA backend (3DS only): the shared objects every PICA renderer
// draws through, and the hooks the frame composer calls. The backend never creates render
// targets or calls gfxInit/C3D_Init: the platform context (engine/src/platform/ctr, M1.2)
// owns them, and the composer (apps/3ds/ctr_frame.cpp, M1.8) connects the two:
//
//     C3D_Init(...); targets = ctx.create...();             // platform (M1.2)
//     pica::initBackend();                                  // once, after C3D_Init
//     loop:
//       C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
//       pica::frameBegin();                                 // arena half switch
//       for each eye / screen:
//         pica::drawOn({target, 400, 240, true});           // C3D_FrameDrawOn + state reset
//         ... renderers draw (render_state.h, tev.h, shaders(), frameArena()) ...
//       pica::frameEnd();                                   // one arena cache flush
//       C3D_FrameEnd(0);
//     pica::shutdownBackend();
#pragma once

#include <citro3d.h>

#include <cstddef>

#include "as3d/fog_lut.h"
#include "as3d/math.h"
#include "as3d/world_render.h"
#include "linear_arena.h"
#include "shader_set.h"

namespace as3d::pica {

// A render target as the player sees it: `width` x `height` is the upright screen size (400 x
// 240 for the top eyes, 320 x 240 for the bottom screen); the target itself is created rotated
// (240 x width, see pica_space.h).
struct FrameTarget {
    C3D_RenderTarget* target = nullptr;
    int width = 400;
    int height = 240;
    bool hasDepth = true;
};

// Loads the shaders and allocates the frame arena (two halves of `arenaHalfBytes`), and sets
// the default state (depth mapping Z-buffer, fog off, cull back). Needs C3D_Init. False when
// the shaders or the linear memory are missing.
bool initBackend(size_t arenaHalfBytes = LinearArena::kDefaultHalfBytes);
void shutdownBackend();
bool backendReady();

// After C3D_FrameBegin: switches the arena half.
void frameBegin();
// Selects `t` for the next draws: C3D_FrameDrawOn, then the viewport (full target) and the
// scissor (off) are reset, as C3D_FrameDrawOn itself resets the viewport.
void drawOn(const FrameTarget& t);
const FrameTarget& currentTarget();
// Before C3D_FrameEnd: flushes the arena's used range. (C3D_FrameEnd(0) also flushes the whole
// linear heap unless GX_CMDLIST_FLUSH is passed; this flush is what makes the latter safe.)
void frameEnd();

LinearArena& frameArena();
ShaderSet& shaders();

// Frees a linearAlloc block the GPU may still read (a destroyed mesh): at the next frameBegin(),
// C3D_FrameBegin having waited for the GPU to finish every command list submitted so far.
// Freed at once when the backend is not initialised (no frame can reference it);
// shutdownBackend() frees what is still pending (call it after the last C3D_FrameEnd).
void deferLinearFree(void* p);
// The number of blocks waiting for the next frameBegin() (tests).
size_t deferredFreeCount();

// Depth mapping (C3D_DepthMap). ZBuffer is citro3d's default C3D_DepthMap(true, -1, 0); WBuffer
// is C3D_DepthMap(false, -1/near, 0), a depth linear in the eye distance (as3d/fog_lut.h has
// the formulas). Both store 1 at the near plane and 0 at the far plane: depth test GREATER,
// clear depth to 0. The fog table follows the mapping (fog_pica.cpp).
void setDepthMapping(FogDepthMap map, float nearPlane);
FogDepthMap depthMapping();
float depthMappingNear();
// The bias applyMaterialState adds for MaterialKey::polygonOffset (GL glPolygonOffset(-1, -1)),
// towards the viewer. C3D_DepthMap's offset is added before the W multiply (as Azahar emulates
// the rasterizer; hardware assumed the same), so with the W-buffer mapping the world uses, the
// bias is offset * w depth units, i.e. offset * w * (far - near) world units. 2^-22 is 4 * w
// steps of the 24-bit buffer (16 at the near plane 4) and 0.24 world units at w = 1000 with
// planes 4..1000; the Z-buffer mapping adds it unscaled (4 steps). M1.4 (Azahar, mission 1 road
// tiles at a grazing angle): the terrain overlays, drawn with the terrain's own vertices, give
// the same image with 0 and with 2^-18, so the bias only absorbs rounding; a small one limits
// how far tiles can show through ridges in the distance.
constexpr float kPolygonOffsetDepth = 1.0f / 4194304.0f;
// Re-issues C3D_DepthMap for the current mapping with `offset` added (0 = none).
void applyDepthMap(float offset);

// Fog (fog_pica.cpp). setFog builds and binds the table for these planes when they changed
// (C3D_FogLutBind / FogLut_FromArray) and turns the unit on (C3D_FogGasMode(GPU_FOG,
// GPU_PLAIN_DENSITY, false); the gas mode is irrelevant in fog mode). disableFog turns it off.
void setFog(float nearPlane, float farPlane, float fogStart, float fogEnd);
void disableFog();
bool fogEnabled();
// The level's fog colour; applyMaterialState sets C3D_FogColor(fogColorFor(key.fog, this)).
void setLevelFogColor(const Vec3& rgb);
Vec3 levelFogColor();
// C3D_FogColor of an RGB colour in [0, 1].
void applyFogColor(const Vec3& rgb);

// Projection of one eye for a tilted 400x240 (or 320x240) target, built by citro3d
// (Mtx_PerspStereoTilt, or Mtx_PerspTilt for the mono eye) with the parameters of
// pica_space.h picaStereoParams. Pairs with the central view.view (not eyeViewMatrix): the
// eye translation is inside. eye_projection_pica.cpp.
Mat4 picaEyeProjection(const WorldView& view, const EyeView& eye, float aspect);

namespace detail {
// render_state_pica.cpp: forget the viewport/scissor of the previous target (drawOn) and
// re-issue the tracked blend/depth/cull state (initBackend).
void resetViewState();
void applyDefaultState();
} // namespace detail

} // namespace as3d::pica
