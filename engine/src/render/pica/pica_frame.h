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
#include <cstdint>

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

// Mid-frame submission (run.ini gpu_kick=1, docs/3ds-port.md "Hardware tuning"): citro3d
// queues the frame's command lists and starts the GPU at C3D_FrameEnd, so the GPU idles while
// the CPU records. kickGpu() (the composer calls it after each eye) flushes the arena's new
// vertices, splits the command list (flushed) and starts citro3d's queue: the GPU draws that eye
// while the CPU records the next. Every later split must then be flushed by itself
// (splitCommandList, the clears) and the frame ends with C3D_FrameEnd(GX_CMDLIST_FLUSH): the
// command list is flushed with its last split, the arena by frameEnd(), every other GPU buffer
// when it was written. A no-op while off (the default).
void setGpuKick(bool on);
bool gpuKick();
void kickGpu();
unsigned gpuKicks(); // kicks since start
// The arena's new vertices flushed, then C3D_FrameSplit(GX_CMDLIST_FLUSH) (render_state's clears).
void splitCommandList();

// How clear() fills a target that has colour and depth (docs/3ds-port.md "Console session of
// 2026-10-11"): Split (the default) issues the depth fill and the colour fill as two GX memory
// fills, each on the PSC0 engine whose completion libctru's GX queue waits for; Joint is citro3d's
// one fill with the depth on PSC1, whose completion the queue ignores (gxqueue.c), so a slow depth
// fill can still be running when the terrain draws; Quad draws a full-screen quad instead (no
// fill engine at all, strictly ordered in the command list).
enum class ClearMode { Split = 0, Joint = 1, Quad = 2 };
void setClearMode(ClearMode mode);
ClearMode clearMode();

LinearArena& frameArena();
ShaderSet& shaders();

// The GPU fence (docs/3ds-port.md "Frame synchronisation"). Each frameBegin() opens a new frame
// serial (1, 2, ...). C3D_FrameBegin has just waited for citro3d's GX queue to drain: the
// completion interrupt of every command list, fill and transfer submitted so far, the lists
// kicked in the middle of a frame (kickGpu) included. frameBegin() checks that the queue is idle
// (AS3D_FENCE and a blocking wait when it is not) and marks every earlier serial complete; the
// arena half it takes was last read in serial - 2. Outside a frame, gpuDone() polls the queue.
uint32_t frameSerial();         // the frame open, or the last one recorded (0 before the first)
uint32_t gpuCompletedSerial();  // every frame up to this one is finished on the GPU
// True when the GPU finished every frame up to `serial` (non-blocking; inside a citro3d frame
// only what frameBegin() established counts).
bool gpuDone(uint32_t serial);
// Outside a citro3d frame: waits until the GX queue is idle (every frame recorded so far done).
void waitGpuIdle();
// Between frameBegin() and frameEnd().
bool inFrame();
// A cache flush of a buffer the GPU will read: now outside a frame, else with frameEnd()'s (or the
// next gpu_kick split's), before the GPU can read it: a mesh uploaded inside a frame makes no GSP
// call inside the command stream (GpuMesh::upload).
void deferFlush(const void* p, size_t bytes);

// Frees a linearAlloc block the GPU may still read (a destroyed mesh): tagged with the current
// frame serial and freed once gpuDone(tag), at a later frameBegin() or collectDeferred().
// Freed at once when the backend is not initialised (no frame can reference it);
// shutdownBackend() frees what is still pending (call it after the last C3D_FrameEnd).
void deferLinearFree(void* p);
// The same for a texture's memory (Texture2D::destroy): C3D_TexDelete on a copy of `tex`.
void deferTextureDelete(const C3D_Tex& tex);
// Frees the deferred blocks the GPU is done with (frameBegin() does it; also outside frames).
// waitIfTextures: first wait for the GPU when textures are pending (their VRAM is wanted).
void collectDeferred(bool waitIfTextures = false);
// The number of blocks (and textures) waiting for their fence (tests).
size_t deferredFreeCount();

// run.ini arena_poison=1 (docs/3ds-port.md "Frame synchronisation"): the arena half frameBegin()
// takes, and every deferred block when it is freed, are filled with finite garbage (random
// floats of +-3000 whose bytes are random colours) and flushed to memory, so a GPU still reading
// them draws visible garbage at once.
void setArenaPoison(bool on);
bool arenaPoison();
void poisonLinear(void* p, size_t bytes);

struct FenceStats {
    unsigned queueBusyAtBegin = 0; // frameBegin() found the GX queue still running (and waited)
    unsigned halfHazards = 0;      // an arena half taken before its last frame was done (never expected)
    unsigned deferredMax = 0;      // most blocks waiting at once
    unsigned texturesDeferred = 0; // deferTextureDelete calls
    unsigned freedDeferred = 0;    // blocks freed after their fence
    unsigned poisonedHalves = 0;
};
const FenceStats& fenceStats();

// Depth mapping (C3D_DepthMap). ZBuffer is citro3d's default C3D_DepthMap(true, -1, 0); WBuffer
// is C3D_DepthMap(false, -1/near, 0), a depth linear in the eye distance (as3d/fog_lut.h has
// the formulas). Both store 1 at the near plane and 0 at the far plane: depth test GREATER,
// clear depth to 0. The fog table follows the mapping (fog_pica.cpp).
void setDepthMapping(FogDepthMap map, float nearPlane);
// Hardware A/B (run.ini depthmap=z): every later setDepthMapping(WBuffer, n) sets the Z-buffer
// mapping instead; the fog table follows depthMapping(), so it is rebuilt for Z.
void setWBufferAsZBuffer(bool on);
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
// The offset the GPU has now (the last applyDepthMap: setDepthMapping applies 0, the terrain's
// overlays and applyMaterialState their bias).
float appliedDepthOffset();

// Fog (fog_pica.cpp). setFog builds and binds the table for these planes when they changed
// (C3D_FogLutBind / FogLut_FromArray) and turns the unit on (C3D_FogGasMode(GPU_FOG,
// GPU_PLAIN_DENSITY, false); the gas mode is irrelevant in fog mode). disableFog turns it off.
void setFog(float nearPlane, float farPlane, float fogStart, float fogEnd);
void disableFog();
// run.ini fog=0: setFog leaves the fog unit off (hardware A/B of the fog table).
void setFogForcedOff(bool off);
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
// citro3d's fragment state without the redundant calls (render_state_pica.cpp): each call
// marks citro3d's effect block dirty, which re-emits it with the next draw; these skip a call
// whose arguments equal the last issued ones. Every PICA renderer sets this state through them
// (render_state, the 2D layer, fog, depth mapping); c3dInvalidate() forgets the last values
// (initBackend: other code may have set the state).
void c3dAlphaBlend(GPU_BLENDEQUATION colorEq, GPU_BLENDEQUATION alphaEq, GPU_BLENDFACTOR srcClr, GPU_BLENDFACTOR dstClr,
                   GPU_BLENDFACTOR srcAlpha, GPU_BLENDFACTOR dstAlpha);
void c3dDepthTest(bool enable, GPU_TESTFUNC function, GPU_WRITEMASK writemask);
void c3dCullFace(GPU_CULLMODE mode);
void c3dAlphaTest(bool enable, GPU_TESTFUNC function, int ref);
void c3dFogColor(u32 color);
void c3dDepthMap(bool zBuffer, float zScale, float zOffset);
void c3dInvalidate();
// The last values issued through the setters above (valid = issued since the last c3dInvalidate):
// the render-state self-check (state_check_pica.h) compares them with citro3d's context.
struct C3dShadow {
    bool blendValid = false, depthValid = false, cullValid = false, alphaValid = false, fogValid = false,
         mapValid = false;
    int blend[6] = {};
    bool depthOn = false;
    int depthFunc = 0, depthMask = 0;
    int cull = 0;
    bool alphaOn = false;
    int alphaFunc = 0, alphaRef = 0;
    u32 fog = 0;
    bool mapZ = false;
    float mapScale = 0.0f, mapOffset = 0.0f;
};
const C3dShadow& c3dShadow();
// citro3d's context (__C3D_Context; its first member is the GX queue): the self-check reads it.
void* c3dContextAddress();
// Texture2D::bind skips C3D_TexBind of the texture already bound to the unit (gfx_pica.cpp; a
// destroyed texture is forgotten). initBackend forgets every binding.
void forgetTextureBindings();
// render_state_pica.cpp: forget the viewport/scissor of the previous target (drawOn) and
// re-issue the tracked blend/depth/cull state (initBackend).
void resetViewState();
void applyDefaultState();
} // namespace detail

} // namespace as3d::pica
