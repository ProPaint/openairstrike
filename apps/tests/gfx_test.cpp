// Graphics tests: create a real headless GLES 3.0 context and render into it. On a
// machine where no headless context can be created at all, every TEST_CASE here prints
// a loud SKIPPED message and passes -- but on the development machine (and in CI) a
// context is expected to be created successfully, and these tests must actually run.
#include "doctest.h"

#include <GLES3/gl3.h>

#include <cstdio>
#include <memory>

#include "as3d/gfx.h"
#include "as3d/image.h"
#include "as3d/platform.h"

// Under -DAS3D_SANITIZE=ON, ASan/LeakSanitizer flags a handful of small leaks (~1.8KB,
// libdbus registering with the session bus) that come from *inside* the closed-source
// NVIDIA EGL driver's device enumeration (eglQueryDevicesEXT/eglInitialize, called from
// engine/src/platform/egl_headless.cpp's headless context creation) on this development
// machine -- not from any engine code, and reproducible with a standalone EGL program
// that only calls eglQueryDevicesEXT + eglInitialize. This is the compiled-in
// equivalent of an LSAN suppressions file (the same syntax, delivered as a string
// instead of a path), which is required here because tools/ci.sh invokes as3d_tests
// directly with no way to pass a suppressions file via the environment. See
// docs/graphics.md for how this was diagnosed.
#if defined(__SANITIZE_ADDRESS__)
extern "C" const char* __lsan_default_suppressions() { return "leak:libdbus\n"; }
#endif

using namespace as3d;

namespace {

// Shared across TEST_CASEs so EGL is only initialized once (and its "which platform"
// log line, from egl_headless.cpp, is only printed once).
GraphicsContext* headlessContext() {
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 256;
        cfg.height = 256;
        cfg.headless = true;
        cfg.title = "as3d_tests";
        return createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx.get();
}

#define AS3D_REQUIRE_HEADLESS_GL(ctxVar)                                                            \
    GraphicsContext* ctxVar = headlessContext();                                                    \
    if (!ctxVar) {                                                                                  \
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);       \
        return;                                                                                     \
    }

const char* kTriVertexSrc = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kTriFragmentSrc = R"(#version 300 es
precision mediump float;
in vec3 vColor;
out vec4 fragColor;
void main() { fragColor = vec4(vColor, 1.0); }
)";

const char* kTexVertexSrc = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kTexFragmentSrc = R"(#version 300 es
precision mediump float;
in vec2 vUv;
uniform sampler2D uTex;
out vec4 fragColor;
void main() { fragColor = texture(uTex, vUv); }
)";

struct Px {
    int r, g, b, a;
};

Px pixelAt(const Image& img, int x, int y) {
    size_t idx = (static_cast<size_t>(y) * img.width + x) * 4;
    return {img.rgba[idx + 0], img.rgba[idx + 1], img.rgba[idx + 2], img.rgba[idx + 3]};
}

} // namespace

TEST_CASE("headless GLES context reports vendor/renderer/version") {
    AS3D_REQUIRE_HEADLESS_GL(ctx);
    std::string desc = ctx->description();
    CHECK(!desc.empty());
    std::fprintf(stderr, "graphics tests running against: %s\n", desc.c_str());
}

TEST_CASE("ShaderProgram reports a compile error instead of crashing") {
    AS3D_REQUIRE_HEADLESS_GL(ctx);
    ShaderProgram prog;
    std::string error;
    bool ok = prog.compile("#version 300 es\nthis is not valid GLSL at all !! {{{", kTriFragmentSrc, &error);
    CHECK(!ok);
    CHECK(!prog.valid());
    CHECK(!error.empty());

    // A program that failed to compile must not linger as a half-built GL object; a
    // subsequent successful compile on the same instance must still work.
    ok = prog.compile(kTriVertexSrc, kTriFragmentSrc, &error);
    CHECK(ok);
    CHECK(prog.valid());
}

TEST_CASE("triangle renders with correct colours and orientation, read back top-down") {
    AS3D_REQUIRE_HEADLESS_GL(ctx);

    RenderTarget target;
    REQUIRE(target.create(256, 256, /*msaaSamples=*/0));
    target.bind();

    ShaderProgram program;
    std::string error;
    REQUIRE(program.compile(kTriVertexSrc, kTriFragmentSrc, &error));

    // Same layout as `as3d_viewer triangle`: RED at the top, GREEN bottom-left, BLUE
    // bottom-right.
    const float verts[] = {
        0.0f, 0.7f,  1.0f, 0.0f, 0.0f,
        -0.7f, -0.7f, 0.0f, 1.0f, 0.0f,
        0.7f, -0.7f,  0.0f, 0.0f, 1.0f,
    };
    VertexBuffer vbo;
    vbo.upload(verts, sizeof verts);
    VertexLayout layout;
    layout.strideBytes = 5 * sizeof(float);
    layout.attribs = {{0, 2, 0, false}, {1, 3, 2 * sizeof(float), false}};
    VertexArray vao;
    vao.create(vbo, layout);

    const Vec4 background{0.05f, 0.05f, 0.08f, 1.0f};
    setViewport(0, 0, 256, 256);
    setDepth(false, false);
    setCull(CullMode::Off);
    setBlend(GlBlend::Off);
    clear(background, /*depth=*/false);
    program.use();
    vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);

    Image img;
    REQUIRE(target.readPixels(img));
    CHECK(img.width == 256);
    CHECK(img.height == 256);

    auto isNear = [](int v, int expected, int tol = 40) { return std::abs(v - expected) <= tol; };

    // Corner: background colour.
    Px corner = pixelAt(img, 2, 2);
    CHECK(isNear(corner.r, 12));
    CHECK(isNear(corner.g, 12));
    CHECK(isNear(corner.b, 20));

    // Triangle in (pixel-x, image-row) space (apex at clip y=+0.7, base at y=-0.7):
    // apex ~ (128, 37), base-left ~ (38, 217), base-right ~ (218, 217). All sample
    // points below are comfortably inside those edges.

    // Orientation: the vertex placed at the top of clip space (y = +0.7) must show up
    // in the low-numbered (top) rows of the Image, and it must be dominantly red.
    Px topCenter = pixelAt(img, 128, 60);
    CHECK(topCenter.r > topCenter.g + 40);
    CHECK(topCenter.r > topCenter.b + 40);

    // Near the base-left corner: dominantly green.
    Px bottomLeft = pixelAt(img, 55, 205);
    CHECK(bottomLeft.g > bottomLeft.r + 40);
    CHECK(bottomLeft.g > bottomLeft.b + 40);

    // Near the base-right corner: dominantly blue.
    Px bottomRight = pixelAt(img, 201, 205);
    CHECK(bottomRight.b > bottomRight.r + 40);
    CHECK(bottomRight.b > bottomRight.g + 40);

    // Flip-detector: a point near the base, on the symmetry axis, is a ~50/50 blend of
    // GREEN and BLUE with very little RED (the apex is far away in barycentric terms).
    // If the readback were vertically flipped, this point would instead land near the
    // (all-red) apex and fail both checks below.
    Px bottomCenter = pixelAt(img, 128, 210);
    CHECK(bottomCenter.r < bottomCenter.g - 40);
    CHECK(bottomCenter.r < bottomCenter.b - 40);
}

TEST_CASE("Texture2D orientation: a 2x2 synthetic image lands in the expected on-screen quadrants") {
    AS3D_REQUIRE_HEADLESS_GL(ctx);

    // Image row 0 = top (per as3d::Image): top-left RED, top-right GREEN,
    // bottom-left BLUE, bottom-right YELLOW.
    Image src;
    src.width = 2;
    src.height = 2;
    src.hasAlpha = false;
    src.rgba = {
        255, 0, 0, 255,    0, 255, 0, 255,   // row 0 (top):    TL red,    TR green
        0, 0, 255, 255,    255, 255, 0, 255, // row 1 (bottom): BL blue,   BR yellow
    };

    TextureOptions opts;
    opts.minFilter = Filter::Nearest;
    opts.magFilter = Filter::Nearest;
    opts.wrapS = Wrap::ClampToEdge;
    opts.wrapT = Wrap::ClampToEdge;
    Texture2D tex;
    tex.create(src, opts);

    RenderTarget target;
    REQUIRE(target.create(64, 64, /*msaaSamples=*/0));
    target.bind();

    ShaderProgram program;
    std::string error;
    REQUIRE(program.compile(kTexVertexSrc, kTexFragmentSrc, &error));

    // Full-screen quad; UV (0,0) at the top-left vertex, matching as3d/gfx.h's
    // "v=0 is the top" convention.
    struct V { float x, y, u, v; };
    const V verts[6] = {
        {-1, 1, 0, 0}, {-1, -1, 0, 1}, {1, -1, 1, 1},
        {-1, 1, 0, 0}, {1, -1, 1, 1},  {1, 1, 1, 0},
    };
    VertexBuffer vbo;
    vbo.upload(verts, sizeof verts);
    VertexLayout layout;
    layout.strideBytes = sizeof(V);
    layout.attribs = {{0, 2, 0, false}, {1, 2, 2 * sizeof(float), false}};
    VertexArray vao;
    vao.create(vbo, layout);

    setViewport(0, 0, 64, 64);
    setDepth(false, false);
    setCull(CullMode::Off);
    setBlend(GlBlend::Off);
    clear({0, 0, 0, 1}, false);
    program.use();
    program.setInt("uTex", 0);
    tex.bind(0);
    vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);

    Image img;
    REQUIRE(target.readPixels(img));

    auto isColor = [](Px p, int r, int g, int b) { return p.r == r && p.g == g && p.b == b; };

    CHECK(isColor(pixelAt(img, 16, 16), 255, 0, 0));   // top-left -> red
    CHECK(isColor(pixelAt(img, 48, 16), 0, 255, 0));   // top-right -> green
    CHECK(isColor(pixelAt(img, 16, 48), 0, 0, 255));   // bottom-left -> blue
    CHECK(isColor(pixelAt(img, 48, 48), 255, 255, 0)); // bottom-right -> yellow
}
