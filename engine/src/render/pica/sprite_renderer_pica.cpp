// Sprites (and the enemy health bars, which are two billboard sprites each) on the PICA (3DS).
// The quads and runs come from the neutral buildSpriteBatches (engine/src/render/sprite_build.cpp),
// capped at kMaxSpriteQuadsPica, converted to FlatVertex in the frame arena and drawn through the
// flat shader; the per-eye contract is in fx_pica.h. There is no separate health bar renderer:
// buildHealthBar (health_bar.cpp) produces SpriteInstances that the world appends to the sprite
// list, so the bars pass through here.
#include <3ds.h>
#include <citro3d.h>

#include <vector>

#include "as3d/sprite_render.h"
#include "fx_draw_pica.h"
#include "fx_pica.h"
#include "pica_frame.h"

namespace as3d {

struct SpriteRenderer::Impl {
    std::vector<SpriteVertex> verts;
    std::vector<DrawRun> runs;

    pica::FlatVertex* flat = nullptr;
    unsigned builtFrame = ~0u;
    const SpriteInstance* builtSprites = nullptr;
    size_t builtCount = 0;
    bool builtCull = false;
    Vec3 builtRight, builtUp;
    int builtQuads = 0;
};

SpriteRenderer::SpriteRenderer() : impl_(new Impl) {}
SpriteRenderer::~SpriteRenderer() = default;

bool SpriteRenderer::init(std::string* error) {
    if (!pica::backendReady()) {
        if (error) *error = "SpriteRenderer: the PICA backend is not initialised";
        return false;
    }
    return true;
}

void SpriteRenderer::draw(const SpriteInstance* sprites, size_t count, const SpriteViewParams& params) {
    Impl& im = *impl_;
    pica::FxPicaStats& st = pica::detail::fxStats();
    lastSprites_ = 0;
    st.spriteDraws = 0;
    if (!pica::backendReady() || count == 0) return;

    Vec3 right, up;
    spriteBillboardAxes(params.view, right, up);
    const unsigned frame = pica::frameArena().frames();
    const bool reuse = im.builtFrame == frame && im.builtSprites == sprites && im.builtCount == count &&
                       im.builtCull == params.cullBackFaces && im.builtRight.x == right.x &&
                       im.builtRight.y == right.y && im.builtRight.z == right.z && im.builtUp.x == up.x &&
                       im.builtUp.y == up.y && im.builtUp.z == up.z;
    if (!reuse) {
        im.builtFrame = frame;
        im.builtSprites = sprites;
        im.builtCount = count;
        im.builtCull = params.cullBackFaces;
        im.builtRight = right;
        im.builtUp = up;
        im.flat = nullptr;
        st.spriteQuads = st.spriteQuadsDropped = 0;
        st.spriteArenaBytes = 0;

        im.verts.clear();
        im.runs.clear();
        // Cap before building (the builder keeps its own kMaxSprites = 512).
        const size_t use = count < static_cast<size_t>(pica::kMaxSpriteQuadsPica) ? count
                                                                                  : static_cast<size_t>(pica::kMaxSpriteQuadsPica);
        int built = buildSpriteBatches(sprites, use, params.view, params.cullBackFaces, im.verts, im.runs);
        if (count > use) st.spriteQuadsDropped = static_cast<int>(count - use);
        if (!im.runs.empty()) {
            im.flat = pica::uploadBuilderVertices(reinterpret_cast<const float*>(im.verts.data()), im.verts.size());
            if (im.flat) {
                st.spriteArenaBytes = im.verts.size() * sizeof(pica::FlatVertex);
            } else {
                im.runs.clear();
                built = 0;
            }
        } else {
            built = 0;
        }
        im.builtQuads = built;
        st.spriteQuads = built;
    }
    lastSprites_ = im.builtQuads;
    if (im.runs.empty() || !im.flat) return;

    pica::FxEyeParams eye;
    eye.view = params.view;
    eye.projection = params.projection;
    eye.fogColour = params.fogColour;
    eye.fogStart = params.fogStart;
    eye.fogEnd = params.fogEnd;
    st.spriteDraws = pica::drawFlatRuns(im.flat, im.runs, eye);
}

} // namespace as3d
