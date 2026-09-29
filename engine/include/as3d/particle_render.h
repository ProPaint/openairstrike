// OpenGL ES 3.0 renderer for particle emitters (as3d/particles.h). Spec:
// docs/spec/render-pipeline.md 4.1 (blend table), 6.6 (drawing), 10 (unlit program).
// Needs a current GLES 3.0 context.
//
// World space is Z-up, `view`/`projection` are ordinary GL matrices (as3d/math.h).
// Emitters are drawn in the order given, particles in pool order: like the original
// there is no depth sorting. Particles test depth (unless RF_NODEPTHTEST) but never
// write it.
#pragma once

#include <memory>
#include <string>

#include "as3d/gfx.h"
#include "as3d/math.h"
#include "as3d/particles.h"
#include "as3d/vfs.h"

namespace as3d {

struct ParticleViewParams {
    Mat4 view;
    Mat4 projection;
    Vec3 fogColour;
    float fogStart = 1.0e9f; // linear fog; set fogEnd <= fogStart to disable
    float fogEnd = 1.0e9f;
};

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

    // Stats of the last draw call.
    int lastParticleCount() const { return lastParticles_; }
    int missingTextures() const { return missing_; }

    // Camera right and up as the original derives them (rows 0 and 1 of the view
    // rotation). Exposed for tests.
    static void billboardAxes(const Mat4& view, Vec3& right, Vec3& up);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int lastParticles_ = 0;
    int missing_ = 0;
};

} // namespace as3d
