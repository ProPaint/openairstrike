// Particle billboards, GL part. The quads and runs come from buildParticleQuads
// (engine/src/render/particle_build.cpp). See docs/spec/render-pipeline.md sections 4.1, 6.6, 10.
#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>

#include "as3d/gfx.h"
#include "as3d/image.h"
#include "as3d/particle_render.h"

namespace as3d {

extern const char* const kParticleVertexSrc;
extern const char* const kParticleFragmentSrc;

struct ParticleRenderer::Impl {
    Vfs* vfs = nullptr;
    ShaderProgram program;
    VertexBuffer vbo;
    VertexArray vao;
    std::vector<ParticleVertex> verts;
    std::vector<DrawRun> runs;
    std::unordered_map<std::string, std::unique_ptr<Texture2D>> textures;
    std::unordered_map<std::string, bool> warned;
    Texture2D fallback;

    const Texture2D* texture(const std::string& path, int& missing) {
        std::string key = normalizePath(path);
        auto it = textures.find(key);
        if (it != textures.end()) return it->second.get();
        Blob blob;
        Image image;
        bool ok = vfs && vfs->read(path, blob) && decodeTga(blob.data(), blob.size(), image);
        auto tex = std::make_unique<Texture2D>();
        if (ok) {
            TextureOptions opts;
            opts.mipmaps = true; // particle textures are mipmapped (4.4), repeat wrap
            tex->create(image, opts);
        } else {
            if (!warned[key]) {
                warned[key] = true;
                AS3D_WARN("ParticleRenderer: texture '%s' could not be loaded, using white", path.c_str());
            }
            missing++;
            Image white;
            white.width = white.height = 1;
            white.rgba = {255, 255, 255, 255};
            tex->create(white);
        }
        const Texture2D* raw = tex.get();
        textures[key] = std::move(tex);
        return raw;
    }
};

ParticleRenderer::ParticleRenderer() : impl_(new Impl) {}
ParticleRenderer::~ParticleRenderer() = default;

bool ParticleRenderer::init(Vfs& vfs, std::string* error) {
    Impl& im = *impl_;
    im.vfs = &vfs;
    if (!im.program.compile(kParticleVertexSrc, kParticleFragmentSrc, error)) return false;
    // Placeholder storage so the VAO can be created before the first upload.
    ParticleVertex dummy{};
    im.vbo.upload(&dummy, sizeof dummy, true);
    VertexLayout layout;
    layout.strideBytes = static_cast<int>(sizeof(ParticleVertex));
    layout.attribs = {{0, 3, 0, false}, {1, 2, 3 * sizeof(float), false}, {2, 4, 5 * sizeof(float), false}};
    im.vao.create(im.vbo, layout);
    return true;
}

void ParticleRenderer::preload(const std::string& path) {
    if (path.empty() || !impl_->vfs) return;
    impl_->texture(path, missing_);
}

void ParticleRenderer::draw(const ParticleEmitter* const* emitters, size_t count, const ParticleViewParams& params) {
    Impl& im = *impl_;
    lastParticles_ = 0;
    if (!im.program.valid()) return;

    im.verts.clear();
    im.runs.clear();
    lastParticles_ = buildParticleQuads(
        emitters, count, params.view, [&](const std::string& path) { return im.texture(path, missing_); }, im.verts,
        im.runs);
    if (im.runs.empty()) return;

    im.vbo.upload(im.verts.data(), im.verts.size() * sizeof(ParticleVertex), true);
    im.program.use();
    im.program.setMat4("uView", params.view);
    im.program.setMat4("uProj", params.projection);
    const bool fogOn = params.fogEnd > params.fogStart;
    im.program.setFloat("uFogStart", fogOn ? params.fogStart : 0.0f);
    im.program.setFloat("uFogEnd", fogOn ? params.fogEnd : 1.0e30f);
    im.program.setInt("uTex", 0);
    im.vao.bind();

    for (const DrawRun& r : im.runs) {
        applyMaterialState(r.key);
        im.program.setVec3("uFogColor", fogColorFor(r.key.fog, params.fogColour));
        im.program.setInt("uTexMode", r.texMode);
        if (r.texture) r.texture->bind(0);
        glDrawArrays(GL_TRIANGLES, r.first, r.count);
    }
    glDisable(GL_BLEND);
    setDepth(true, true);
}

} // namespace as3d
