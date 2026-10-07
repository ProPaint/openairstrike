// Tests for as3d/material_key.h: materialKeyFor reproduces the former GL kBlendTable and
// depth/cull lines of applyMaterialState row by row, keys compare and hash, and the GL
// backend's applyMaterialState sets exactly the old GL state.
#include "doctest.h"

#include "as3d/material_key.h"

#include <GLES3/gl3.h>

#include <cstdio>
#include <memory>
#include <unordered_set>

#include "as3d/defs.h"
#include "as3d/platform.h"
#include "as3d/scene.h"

using namespace as3d;

namespace {

// A Material with the flag fields Material::fromObjectDef derives from blend and rflag.
Material materialFor(MaterialBlend blend, u32 rflag) {
    Material m;
    m.blend = blend;
    m.rflag = rflag;
    m.noLighting = (rflag & RF_NOLIGHTING) != 0;
    m.noCulling = (rflag & RF_NOCULLING) != 0;
    m.noDepthTest = (rflag & RF_NODEPTHTEST) != 0;
    m.noDepthWrite = (rflag & RF_NODEPTHWRITE) != 0;
    return m;
}

// The previous kBlendTable (engine/src/render/gles/material_state.cpp before M0.4b), written
// out, with the fog colour MeshRenderer::draw picked from the blend.
struct OldBlendRow {
    MaterialBlend blend;
    bool enable;
    GLenum src, dst;
    RenderBlend expected;
    FogColorMode fog;
};
const OldBlendRow kOldBlendTable[] = {
    {MaterialBlend::Opaque, false, GL_ONE, GL_ZERO, RenderBlend::None, FogColorMode::Level},
    {MaterialBlend::Alpha, true, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, RenderBlend::Alpha, FogColorMode::Level},
    {MaterialBlend::Add, true, GL_ONE, GL_ONE, RenderBlend::Add, FogColorMode::Black},
    {MaterialBlend::Filter, true, GL_DST_COLOR, GL_ZERO, RenderBlend::Filter, FogColorMode::White},
};

const u32 kFlagSets[] = {0,
                         RF_NOLIGHTING,
                         RF_NOCULLING,
                         RF_NODEPTHTEST,
                         RF_NODEPTHWRITE,
                         RF_NODEPTHTEST | RF_NODEPTHWRITE,
                         RF_NOLIGHTING | RF_NOCULLING | RF_NODEPTHWRITE,
                         RF_NOLIGHTING | RF_NOCULLING | RF_NODEPTHTEST | RF_NODEPTHWRITE,
                         RF_NODLIGHT,
                         RF_NODLIGHT | RF_NODEPTHTEST};

GraphicsContext* headlessContext() {
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 64;
        cfg.height = 64;
        cfg.headless = true;
        cfg.title = "as3d_tests (material_key)";
        return createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx.get();
}

GLint glInt(GLenum e) {
    GLint v = 0;
    glGetIntegerv(e, &v);
    return v;
}

} // namespace

TEST_CASE("materialKeyFor: every blend mode and rflag set against the old kBlendTable") {
    for (const OldBlendRow& row : kOldBlendTable) {
        for (u32 flags : kFlagSets) {
            CAPTURE(static_cast<int>(row.blend));
            CAPTURE(flags);
            MaterialKey k = materialKeyFor(materialFor(row.blend, flags));
            CHECK(k.blend == row.expected);
            CHECK(k.depthTest == ((flags & RF_NODEPTHTEST) == 0));
            CHECK(k.depthWrite == ((flags & RF_NODEPTHWRITE) == 0));
            CHECK(k.cull == ((flags & RF_NOCULLING) ? CullMode::Off : CullMode::Back));
            CHECK(k.fog == row.fog);
            CHECK_FALSE(k.polygonOffset);
            CHECK_FALSE(k.alphaTest);
        }
    }
    // Explicit rows, for the reader.
    MaterialKey opaque = materialKeyFor(materialFor(MaterialBlend::Opaque, 0));
    CHECK(opaque.packed() == MaterialKey{}.packed());
    MaterialKey addNoDepth = materialKeyFor(materialFor(MaterialBlend::Add, RF_NODEPTHTEST | RF_NODEPTHWRITE));
    MaterialKey expected;
    expected.blend = RenderBlend::Add;
    expected.depthTest = false;
    expected.depthWrite = false;
    expected.fog = FogColorMode::Black;
    CHECK(addNoDepth == expected);
    // alphaTest is carried through.
    Material cut = materialFor(MaterialBlend::Opaque, 0);
    cut.alphaTest = true;
    CHECK(materialKeyFor(cut).alphaTest);
}

TEST_CASE("materialKeyFor: the cull debug override") {
    Material m = materialFor(MaterialBlend::Opaque, RF_NOCULLING);
    setCullDebugOverride(0);
    CHECK(materialKeyFor(materialFor(MaterialBlend::Opaque, 0)).cull == CullMode::Off);
    setCullDebugOverride(1);
    CHECK(materialKeyFor(m).cull == CullMode::Back);
    setCullDebugOverride(2);
    CHECK(materialKeyFor(m).cull == CullMode::Front);
    setCullDebugOverride(-1);
    CHECK(materialKeyFor(m).cull == CullMode::Off);
}

TEST_CASE("MaterialKey: equality, hash and fog colours") {
    std::unordered_set<MaterialKey, MaterialKeyHash> keys;
    int combos = 0;
    for (int b = 0; b <= static_cast<int>(RenderBlend::Brightness); b++)
        for (int dt = 0; dt < 2; dt++)
            for (int dw = 0; dw < 2; dw++)
                for (int c = 0; c < 3; c++)
                    for (int po = 0; po < 2; po++)
                        for (int f = 0; f < 3; f++)
                            for (int at = 0; at < 2; at++) {
                                MaterialKey k;
                                k.blend = static_cast<RenderBlend>(b);
                                k.depthTest = dt != 0;
                                k.depthWrite = dw != 0;
                                k.cull = static_cast<CullMode>(c);
                                k.polygonOffset = po != 0;
                                k.fog = static_cast<FogColorMode>(f);
                                k.alphaTest = at != 0;
                                MaterialKey copy = k;
                                CHECK(copy == k);
                                keys.insert(k);
                                ++combos;
                            }
    CHECK(keys.size() == static_cast<size_t>(combos)); // all distinct
    MaterialKey a, b;
    b.polygonOffset = true;
    CHECK(a != b);

    CHECK(fogColorModeFor(RenderBlend::None) == FogColorMode::Level);
    CHECK(fogColorModeFor(RenderBlend::Alpha) == FogColorMode::Level);
    CHECK(fogColorModeFor(RenderBlend::Add) == FogColorMode::Black);
    CHECK(fogColorModeFor(RenderBlend::Filter) == FogColorMode::White);
    Vec3 level{0.4f, 0.3f, 0.2f};
    CHECK(fogColorFor(FogColorMode::Level, level).x == 0.4f);
    CHECK(fogColorFor(FogColorMode::Black, level).y == 0.0f);
    CHECK(fogColorFor(FogColorMode::White, level).z == 1.0f);
}

TEST_CASE("applyMaterialState (GL): the GL state of each old kBlendTable row and flag set") {
    if (!headlessContext()) {
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);
        return;
    }
    for (const OldBlendRow& row : kOldBlendTable) {
        for (u32 flags : kFlagSets) {
            CAPTURE(static_cast<int>(row.blend));
            CAPTURE(flags);
            glEnable(GL_POLYGON_OFFSET_FILL); // a key without polygon offset turns it off
            applyMaterialState(materialFor(row.blend, flags));
            CHECK((glIsEnabled(GL_BLEND) == GL_TRUE) == row.enable);
            if (row.enable) {
                CHECK(glInt(GL_BLEND_SRC_RGB) == static_cast<GLint>(row.src));
                CHECK(glInt(GL_BLEND_DST_RGB) == static_cast<GLint>(row.dst));
            }
            CHECK((glIsEnabled(GL_DEPTH_TEST) == GL_TRUE) == ((flags & RF_NODEPTHTEST) == 0));
            GLboolean mask = GL_FALSE;
            glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);
            CHECK((mask == GL_TRUE) == ((flags & RF_NODEPTHWRITE) == 0));
            CHECK((glIsEnabled(GL_CULL_FACE) == GL_TRUE) == ((flags & RF_NOCULLING) == 0));
            if (!(flags & RF_NOCULLING)) CHECK(glInt(GL_CULL_FACE_MODE) == GL_BACK);
            CHECK(glIsEnabled(GL_POLYGON_OFFSET_FILL) == GL_FALSE);
        }
    }
    MaterialKey k;
    k.blend = RenderBlend::Brightness;
    k.polygonOffset = true;
    applyMaterialState(k);
    CHECK(glInt(GL_BLEND_SRC_RGB) == GL_DST_COLOR);
    CHECK(glInt(GL_BLEND_DST_RGB) == GL_SRC_COLOR);
    CHECK(glIsEnabled(GL_POLYGON_OFFSET_FILL) == GL_TRUE);
    GLfloat factor = 0, units = 0;
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &factor);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &units);
    CHECK(factor == -1.0f);
    CHECK(units == -1.0f);
    applyMaterialState(MaterialKey{});
    CHECK(glIsEnabled(GL_POLYGON_OFFSET_FILL) == GL_FALSE);
    setDepth(true, true);
    setCull(CullMode::Back);
    glDisable(GL_BLEND);
}
