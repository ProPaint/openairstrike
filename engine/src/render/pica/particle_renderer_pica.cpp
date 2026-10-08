// Particle billboards on the PICA (3DS). The quads and runs come from the neutral
// buildParticleQuads (engine/src/render/particle_build.cpp), capped at kMaxParticleQuadsPica,
// converted to FlatVertex in the frame arena and drawn through the flat shader; the per-eye
// contract (what is built once per frame, what per eye) is in fx_pica.h.
#include <3ds.h>
#include <citro3d.h>

#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "as3d/core.h"
#include "as3d/gfx.h"
#include "as3d/image.h"
#include "as3d/load_stats.h"
#include "as3d/particle_render.h"
#include "fx_draw_pica.h"
#include "fx_pica.h"
#include "pica_frame.h"

namespace as3d {

struct ParticleRenderer::Impl {
    Vfs* vfs = nullptr;
    std::vector<ParticleVertex> verts;
    std::vector<DrawRun> runs;
    std::unordered_map<std::string, std::unique_ptr<Texture2D>> textures;
    std::unordered_map<std::string, bool> warned;
    Texture2D fallback;

    // The build of the current arena frame, shared by the eyes.
    pica::FlatVertex* flat = nullptr;
    unsigned builtFrame = ~0u;
    const ParticleEmitter* const* builtEmitters = nullptr;
    size_t builtCount = 0;
    Vec3 builtRight, builtUp;
    int builtParticles = 0;

    // Loads `path` unless cached; outside a frame only. Returns the cached texture.
    const Texture2D* load(const std::string& path, int& missing) {
        const std::string key = normalizePath(path);
        auto it = textures.find(key);
        if (it != textures.end()) return it->second.get();
        LoadStats::Site site;
        LoadStats::count(LoadKind::Texture);
        Blob blob;
        Image image;
        const bool ok = vfs && !path.empty() && vfs->read(path, blob) && decodeTga(blob.data(), blob.size(), image);
        auto tex = std::make_unique<Texture2D>();
        if (ok) {
            TextureOptions opts;
            opts.mipmaps = true; // as the GL renderer (4.4), repeat wrap; the format follows the pixels (Auto)
            tex->create(image, opts);
        }
        if (!ok || !tex->valid()) {
            if (!warned[key]) {
                warned[key] = true;
                AS3D_WARN("ParticleRenderer: texture '%s' could not be loaded, using white", path.c_str());
            }
            missing++;
            return fallbackTexture();
        }
        pica::FxPicaStats& st = pica::detail::fxStats();
        ++st.particleTextures;
        st.particleTextureBytes += tex->bytes();
        const Texture2D* raw = tex.get();
        textures[key] = std::move(tex);
        return raw;
    }

    const Texture2D* fallbackTexture() {
        if (!fallback.valid()) {
            Image white;
            white.width = white.height = 8;
            white.rgba.assign(8 * 8 * 4, 255);
            fallback.create(white);
        }
        return &fallback;
    }
};

ParticleRenderer::ParticleRenderer() : impl_(new Impl) {}
ParticleRenderer::~ParticleRenderer() = default;

bool ParticleRenderer::init(Vfs& vfs, std::string* error) {
    Impl& im = *impl_;
    im.vfs = &vfs;
    if (!pica::backendReady()) {
        if (error) *error = "ParticleRenderer: the PICA backend is not initialised";
        return false;
    }
    im.fallbackTexture(); // created outside any frame
    return true;
}

void ParticleRenderer::preload(const std::string& path) {
    if (path.empty()) return;
    impl_->load(path, missing_);
}

void ParticleRenderer::draw(const ParticleEmitter* const* emitters, size_t count, const ParticleViewParams& params) {
    Impl& im = *impl_;
    pica::FxPicaStats& st = pica::detail::fxStats();
    lastParticles_ = 0;
    st.particleDraws = 0;
    if (!pica::backendReady()) return;

    Vec3 right, up;
    particleBillboardAxes(params.view, right, up);
    const unsigned frame = pica::frameArena().frames();
    const bool reuse = im.builtFrame == frame && im.builtEmitters == emitters && im.builtCount == count &&
                       im.builtRight.x == right.x && im.builtRight.y == right.y && im.builtRight.z == right.z &&
                       im.builtUp.x == up.x && im.builtUp.y == up.y && im.builtUp.z == up.z;
    if (!reuse) {
        im.builtFrame = frame;
        im.builtEmitters = emitters;
        im.builtCount = count;
        im.builtRight = right;
        im.builtUp = up;
        im.flat = nullptr;
        im.builtParticles = 0;
        st.particleQuads = st.particleQuadsDropped = 0;
        st.particleArenaBytes = 0;

        im.verts.clear();
        im.runs.clear();
        int built = buildParticleQuads(
            emitters, count, params.view,
            [&](const std::string& path) -> const Texture2D* {
                auto it = im.textures.find(normalizePath(path));
                if (it != im.textures.end()) return it->second.get();
                ++st.particleLateTextures; // never created inside a frame
                ++missing_;
                return im.fallbackTexture();
            },
            im.verts, im.runs);
        if (built > pica::kMaxParticleQuadsPica) {
            st.particleQuadsDropped = built - pica::kMaxParticleQuadsPica;
            const int kept = pica::capRuns(im.runs, static_cast<int>(im.verts.size()), pica::kMaxParticleQuadsPica * 6);
            im.verts.resize(static_cast<size_t>(kept));
            built = pica::kMaxParticleQuadsPica;
        }
        if (!im.runs.empty()) {
            im.flat = pica::uploadBuilderVertices(reinterpret_cast<const float*>(im.verts.data()), im.verts.size());
            if (im.flat) {
                st.particleArenaBytes = im.verts.size() * sizeof(pica::FlatVertex);
            } else {
                im.runs.clear();
                built = 0;
            }
        } else {
            built = 0;
        }
        im.builtParticles = built;
        st.particleQuads = built;
    }
    lastParticles_ = im.builtParticles;
    if (im.runs.empty() || !im.flat) return;

    pica::FxEyeParams eye;
    eye.view = params.view;
    eye.projection = params.projection;
    eye.fogColour = params.fogColour;
    eye.fogStart = params.fogStart;
    eye.fogEnd = params.fogEnd;
    st.particleDraws = pica::drawFlatRuns(im.flat, im.runs, eye);
}

} // namespace as3d
