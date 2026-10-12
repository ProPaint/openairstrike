// OpenGL ES 3.0 renderer for particle emitters (as3d/particles.h). Spec:
// docs/spec/render-pipeline.md 4.1 (blend table), 6.6 (drawing), 10 (unlit program).
// Needs a current GLES 3.0 context.
//
// World space is Z-up, `view`/`projection` are ordinary GL matrices (as3d/math.h).
// Emitters are drawn in the order given, particles in pool order: like the original
// there is no depth sorting. Particles test depth (unless RF_NODEPTHTEST) but never
// write it.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "as3d/draw_run.h"
#include "as3d/texture.h"
#include "as3d/math.h"
#include "as3d/particles.h"
#include "as3d/vfs.h"

namespace as3d {

// The fade of the emitters that stand still (ParticleEmitter::stillSeconds): a continuous
// emitter whose origin has not moved for stillStart seconds draws a falling share of its live
// particles, down to `floor` at stillEnd seconds and after. The sequels' burning trees (three
// fires per tree, no lifetime, removed only once behind the camera) thin into embers instead of
// holding the particle cap for their whole stay on screen. The simulation is untouched; a moving
// emitter (trails, exhausts) and an EMIT_ONCE burst keep every particle.
struct ParticleFade {
    bool enabled = false;
    float stillStart = 5.0f;
    float stillEnd = 12.0f;
    float floor = 0.25f;
};

struct ParticleViewParams {
    Mat4 view;
    Mat4 projection;
    Vec3 fogColour;
    float fogStart = 1.0e9f; // linear fog; set fogEnd <= fogStart to disable
    float fogEnd = 1.0e9f;
    ParticleFade fade; // read by the PICA renderer (the desktop draws every particle)
};

// The unit of a particle share: shares are in 1/kParticleThinUnit of an emitter's live particles.
constexpr int kParticleThinUnit = 4096;

// Per emitter, the share of its live particles to build (1/kParticleThinUnit): the fade's
// weight first, then every share scaled down by the same factor when the weighted total
// exceeds `cap` (0 = no cap). Returns the particles that visitParticleQuads will build with
// these shares; `faded` (optional) the particles the fade alone removes.
int particleThinShares(const ParticleEmitter* const* emitters, size_t count, const ParticleFade& fade, int cap,
                       std::vector<int>& shares, int* faded = nullptr);

// Vertex of the particle quads: position, texture coordinates (engine convention, v = 0 at the
// image top) and the particle's colour. 9 floats, triangle list, two triangles (0 1 2, 0 2 3)
// per particle.
struct ParticleVertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};

// Camera right and up as the original derives them (rows 0 and 1 of the view rotation).
void particleBillboardAxes(const Mat4& view, Vec3& right, Vec3& up);

// A sphere holding every live particle of the emitter, each with its half extent (a quad of
// any orientation fits in size * sqrt 2): the frustum cull of emitters. Decart particles are
// world positions; Cilinder and Sphere ones lie within |p1| + |p2| (respectively |p2|) of the
// emitter's origin, which bounds them without trigonometry. False when no particle is live.
bool particleEmitterBounds(const ParticleEmitter& emitter, Vec3& centre, float& radius);

// Resolves ParticleDesc::texture to a texture (null is allowed); called once per emitter that
// has live particles, in emitter order.
using ParticleTextureResolver = std::function<const Texture2D*(const std::string& path)>;

// Builds every live particle of every emitter (emitters in the given order, particles in pool
// order) as quads appended to `verts`, and appends the draw runs: adjacent emitters with the
// same blend, depth flag, texture and texture mode share one run. Run state per the pass:
// blend from ParticleDesc::blend, depth test unless noDepthTest, never depth write, back
// faces culled, fog colour from the blend; texMode kTexClampAdd for Filter, else kTexModulate.
// Returns the number of particles built.
int buildParticleQuads(const ParticleEmitter* const* emitters, size_t count, const Mat4& view,
                       const ParticleTextureResolver& resolve, std::vector<ParticleVertex>& verts,
                       std::vector<DrawRun>& runs);

class ParticleRenderer {
public:
    ParticleRenderer();
    ~ParticleRenderer();
    ParticleRenderer(const ParticleRenderer&) = delete;
    ParticleRenderer& operator=(const ParticleRenderer&) = delete;

    // Compiles the shader and creates the dynamic vertex buffer. Textures are loaded
    // lazily from `vfs` (which must outlive the renderer) by ParticleDesc::texture.
    bool init(Vfs& vfs, std::string* error);

    // Builds every quad of every emitter into one vertex buffer and draws it.
    void draw(const ParticleEmitter* const* emitters, size_t count, const ParticleViewParams& params);
    void draw(const ParticleEmitter& emitter, const ParticleViewParams& params) {
        const ParticleEmitter* e = &emitter;
        draw(&e, 1, params);
    }

    // Loads and caches the texture of an emitter ahead of its first draw (a game path, as
    // ParticleDesc::texture). The level-start precache calls it; on the 3DS textures are never
    // created inside a frame, so a texture that was not preloaded is drawn white and counted.
    void preload(const std::string& path);

    // Stats of the last draw call.
    int lastParticleCount() const { return lastParticles_; }
    int missingTextures() const { return missing_; }

    // Camera right and up as the original derives them (rows 0 and 1 of the view
    // rotation). Exposed for tests.
    static void billboardAxes(const Mat4& view, Vec3& right, Vec3& up) { particleBillboardAxes(view, right, up); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int lastParticles_ = 0;
    int missing_ = 0;
};

} // namespace as3d
