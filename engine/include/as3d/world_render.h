// Draws the live game world (as3d/world.h) each frame in the pass order of
// docs/spec/render-pipeline.md 1.1, seen through the world's own camera structure
// (engine-behaviour.md 9). Every pass is one of the engine's renderers (terrain, water,
// meshes, ground marks, shadows, particles, sprites) fed from the entity fields; the
// renderer only reads the world, so rendering can never change the simulation.
//
// Particles are part of the simulation (World::particles(), as3d/world_particles.h): the
// renderer draws the world's particle-system instances and simulates nothing itself.
//
// Passes: terrain (with dynamic lights), ground marks, shadows, the opaque list, water, the
// transparent and effect lists (lit, dynamic lights, environment maps), particles, sprites,
// lightning bolts (7.1) after the effect list, and the brightness overlay after the 2D layer.
//
// Needs a current GLES 3.0 context for everything except worldViewOf, worldRenderOrder and the
// eye functions (eyeViewMatrix, eyePosition, eyeProjection, eyeCullFrustum).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/math.h"
#include "as3d/particles.h"
#include "as3d/render_rules.h"
#include "as3d/skid_render.h"
#include "as3d/terrain_grid.h"
#include "as3d/world.h"
#include "as3d/world_particles.h"

namespace as3d {

class Vfs;
class DefDatabase;

// ---------------------------------------------------------------------------------------
// Camera.
// ---------------------------------------------------------------------------------------

struct WorldView {
    Mat4 view;
    Mat4 projection;
    Vec3 eye;
    float fovY = 60.0f;
    float nearPlane = 4.0f;
    float farPlane = 2000.0f;
};

// The world's camera (view origin 15..17, view angles 18..20, field of view 14): view =
// Rx(a0) Ry(a1) Rz(a2) T(-origin) (render-pipeline.md 2.1), perspective with `aspect`,
// near 4, far = max(fog end, 1000) with fog, else 2000.
WorldView worldViewOf(const World& world, float aspect);

// The order in which the entity pass thinks the entities, which is the order they submit
// their render records (render-pipeline.md 1.2): newest pool entity first, each followed by
// its definition children (parents before children); an entity attached with AttachEntity
// comes after its root, which thinks first (engine-behaviour.md 4.1). Removed entities are
// left out; emitter holders are included. `visited` is scratch space.
void worldRenderOrder(const World& world, std::vector<int>& order, std::vector<char>& visited);

// ---------------------------------------------------------------------------------------
// Eyes (stereoscopic drawing, engine/src/render/eye_view.cpp; no GL).
// ---------------------------------------------------------------------------------------

// One eye of the world camera. EyeView{} is the world camera itself (mono).
struct EyeView {
    float eyeOffset = 0.0f;     // world units along the camera's right axis, +right
    float focalDistance = 0.0f; // zero-parallax distance in front of the camera; 0 = parallel eyes
};

// The eye's view matrix: the world camera moved by eyeOffset along its right axis, same
// orientation. In eye space that is a translation of -eyeOffset in x after the world view:
//
//     eyeView = T(-eyeOffset, 0, 0) * view.view      (only the element col 3, row 0 changes)
//
// eyeOffset 0 returns view.view itself.
Mat4 eyeViewMatrix(const WorldView& view, const EyeView& eye);
// The eye's position in the world: view.eye + right * eyeOffset (right = row 0 of the view).
Vec3 eyePosition(const WorldView& view, const EyeView& eye);

// The eye's projection, used with eyeViewMatrix. With f = 1 / tan(fovY / 2), A = f / aspect
// (the x scale of the symmetric projection), n and F the near and far planes:
//
//     | A  0  B   0 |      B = -A * eyeOffset / focalDistance   (B = 0 when focalDistance
//     | 0  f  0   0 |                                            is 0 or eyeOffset is 0)
//     | 0  0  C   D |      C = (F + n) / (n - F),  D = 2 F n / (n - F)
//     | 0  0 -1   0 |
//
// That is the asymmetric (off-axis) frustum with, at the near plane,
// left = -n tan(fovX / 2) - eyeOffset n / focalDistance and right = n tan(fovX / 2) -
// eyeOffset n / focalDistance (tan(fovX / 2) = aspect tan(fovY / 2)): its window at the
// distance focalDistance is the world camera's, so a point straight ahead of the world camera
// at that distance projects to x = 0 in both eyes (zero parallax); nearer points come out of
// the screen, farther ones go in. Equivalently x_ndc = x_ndc(symmetric projection, same eye
// position) - B, a constant horizontal image shift. Eyes +e and -e have opposite B (mirror
// frusta). With B = 0 the matrix is as3d::perspective(fovY, aspect, near, far), the very call
// worldViewOf makes, so EyeView{} gives today's projection bit for bit.
//
// citro3d (M1.3): Mtx_PerspStereoTilt(mtx, fovy, aspect, near, far, iod, screen, false) takes
// the interocular distance `iod` (eyes at -iod/2 and +iod/2; the 3DS spike passes -iod for the
// left eye) and the zero-parallax distance `screen`, in world units, and puts the eye's
// translation into the projection itself, so it pairs with the central view (view.view), not
// with eyeViewMatrix. The mapping:
//
//     eyeOffset = iod / 2  (the left eye negative),   focalDistance = screen
//     Mtx_PerspStereoTilt(.., iod, screen, ..) * view.view
//         ~ tilt * eyeProjection(view, {iod / 2, screen}, aspect) * eyeViewMatrix(view, {iod / 2, screen})
//
// with `tilt` the 90 degree rotation of the 3DS screen that Mtx_PerspTilt also applies. The
// sign of iod and the scale of its translation term are to be confirmed numerically in M1.3
// against citro3d's mtx_perspstereotilt.c (project a few points through both).
Mat4 eyeProjection(const WorldView& view, const EyeView& eye, float aspect);

// A world-space culling volume: six planes (x, y, z) . p + w >= 0 inside, unit normals pointing
// inwards (0 left, 1 right, 2 bottom, 3 top, 4 near, 5 far).
struct CullFrustum {
    Vec4 planes[6];
    // True when the sphere touches the volume (conservative).
    bool sphereVisible(const Vec3& centre, float radius) const;
};

// The volume holding the frustum of every eye with |eyeOffset| <= maxEyeOffset and a focal
// distance >= minFocalDistance (0: parallel eyes only). In the world camera's eye space
// (looking down -z, depth d = -z), with t = tan(fovX / 2) = aspect tan(fovY / 2) and
// s = maxEyeOffset / minFocalDistance (0 for parallel eyes):
//
//     left, right:  |x| <= maxEyeOffset + (t + s) d
//     bottom, top:  |y| <= tan(fovY / 2) d
//     near, far:    near <= d <= far
//
// The eye at offset e with focal distance D sees |x - e (1 - d / D)| <= t d, and
// |e (1 - d / D)| <= m + m d / D, so both eyes are inside at every depth (for parallel eyes,
// s = 0, the left and right planes are exactly those of the outer eye frusta). The top and
// bottom planes contain the camera's right axis, so moving the eye along it does not move them.
// maxEyeOffset 0 gives the world camera's own frustum.
CullFrustum eyeCullFrustum(const WorldView& view, float aspect, float maxEyeOffset = 0.0f,
                           float minFocalDistance = 0.0f);

// ---------------------------------------------------------------------------------------
// The renderer.
// ---------------------------------------------------------------------------------------

struct WorldRenderOptions {
    bool terrain = true;
    bool models = true;
    bool shadows = true;
    bool lights = true;    // dynamic lights (PlaceLight and definition lights)
    bool particles = true;
    bool sprites = true;   // sprites and ground marks
};

// The rectangle of the bound framebuffer the world is drawn into (GL convention, y from the
// bottom), and what the eyes drawn from one prepare() need.
struct Viewport {
    int x = 0, y = 0, width = 0, height = 0;
    // The largest |EyeView::eyeOffset| and the smallest non-zero EyeView::focalDistance of
    // the draw() calls after this prepare() (0, 0: mono, today's behaviour). The prepared
    // cull frustum is widened by them (eyeCullFrustum).
    float maxEyeOffset = 0.0f;
    float minFocalDistance = 0.0f;
};

struct WorldRenderStats {
    int models = 0;       // mesh records submitted
    int dropped = 0;      // records dropped because a list was full (render-pipeline.md 1.2)
    int sprites = 0;
    int marks = 0;
    int shadows = 0;
    int lights = 0;
    int emitters = 0;
    int particles = 0;
    int bolts = 0;        // lightning bolts (render-pipeline.md 7.1)
    int skidTrails = 0;   // skid trails drawn (the sequels, pass 5)
    int waterChunks = 0;
    int healthBars = 0;   // enemy health bars (engine-behaviour.md 6.5)
    int terrainChunks = 0;
};

class WorldRenderer {
public:
    WorldRenderer();
    ~WorldRenderer();
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    // Compiles shaders. `vfs` and `db` must outlive the renderer.
    bool init(Vfs& vfs, const DefDatabase& db, std::string* error);
    // Builds the terrain and water of the world's current level, generates the shadow
    // silhouettes of the placed objects. Call after every
    // World::loadLevel.
    bool beginLevel(const World& world, std::string* error);

    // Nothing to do: the world advances its own particles (kept for callers written when
    // the renderer simulated them).
    void step(const World&, float) {}

    // Draws the world (3D passes 0 to 12 of render-pipeline.md 1.1) into the currently bound
    // framebuffer with viewport (0, 0, width, height). Leaves depth test on, blending off.
    void render(const World& world, int width, int height, const WorldRenderOptions& options = {});
    // Same, into the rectangle (x, y, width, height) of the bound framebuffer (GL convention,
    // y from the bottom): only that rectangle is cleared and drawn, with its own aspect (the
    // 4:3 screen mode draws the world in the centred 4:3 area).
    void render(const World& world, int x, int y, int width, int height, const WorldRenderOptions& options = {});

    // render() in two halves, to draw one frame several times (the two eyes of a stereo
    // screen): render(world, x, y, w, h, o) is exactly prepare(world, {x, y, w, h}, o) then
    // draw(EyeView{}).
    //
    // prepare() reads the world, once per frame: follows the terrain changes (craters),
    // gathers the dynamic lights, runs the entity pass that fills the opaque, transparent and
    // effect mesh lists, the shadows, sprites, health bars and ground marks (rebuilding the
    // mark geometry when the set changed), collects the skid trails, lightning bolts and
    // particle emitters, and computes the cull volume (preparedFrustum(), widened for the eyes
    // `viewport` announces). It may draw into offscreen targets (the shadow silhouettes of new
    // models) and load textures and meshes, never into the bound framebuffer. Sets the list
    // counts of lastStats().
    //
    // draw() runs passes 0 to 12 for one eye from the prepared lists into the prepared
    // viewport, any number of times until the next prepare(). It does not read the world, but
    // the emitters it draws are the world's particle systems: the world must not step between
    // prepare() and the last draw(). It refreshes the per-draw counts of lastStats() (terrain
    // and water chunks, skid trails, bolts, emitters, particles), the same at every draw of
    // the same eye. Does nothing without a prepare() of a non-empty viewport.
    //
    // An eye with a focal distance (off-axis projection) is drawn with the equivalent
    // symmetric projection over a viewport widened by the image shift and scissored to the
    // prepared rectangle (the renderers' Camera-based mesh pass has no off-axis projection);
    // the shift is rounded to whole pixels.
    void prepare(const World& world, const Viewport& viewport, const WorldRenderOptions& options = {});
    void draw(const EyeView& eye = EyeView{});
    // The same, into the prepared rectangle moved to (x, y) (same size): the desktop's
    // side-by-side stereo view (as3d_game --stereo-debug) draws the second eye beside the first.
    void draw(const EyeView& eye, int x, int y);
    // The cull volume of the last prepare(): the frustum of every eye its Viewport allows.
    const CullFrustum& preparedFrustum() const;
    // Pass 14, after the 2D layer: dst * b + b * dst over the whole framebuffer
    // (render-pipeline.md 1.5; config.ini Brightness, 0.5 neutral, default 0.6).
    void drawBrightness(int width, int height, float brightness);

    // Terrain heights changed (TerraMorph): at every prepare() the renderer reads the world's
    // changes after the revision it saw last (World::terrainChangesSince) and the terrain and
    // water meshes re-upload the touched chunks from the world's terrain; ground marks cut
    // afterwards and skid trails lie on the new heights. The world is only read.
    //
    // Skid trails (pass 5 of the sequels' frame): render() draws the world's live trails
    // (World::liveSkidTrail, as3d/world_skid.h) through SkidTrailRenderer.
    // The game's render rules chosen at beginLevel from world.rules() (as3d/render_rules.h).
    const RenderRules& rules() const;

    const WorldRenderStats& lastStats() const { return stats_; }
    // Shadow silhouettes held, and how many were generated after the level load (entities
    // created by scripts whose model no placement uses).
    int shadowMapCount() const;
    int lateShadowMaps() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    WorldRenderStats stats_;
};

} // namespace as3d
