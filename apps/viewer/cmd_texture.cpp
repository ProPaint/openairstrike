// `as3d_viewer texture <game path> --out out.png [--size WxH] [--window]`: loads a
// .tga through the merged Vfs (see docs/spec/pak.md), draws it on a quad over a
// checkerboard with alpha blending, and writes a PNG. Exercises the texture
// orientation convention documented in as3d/gfx.h end to end: the quad's UV (0,0) is
// its top-left corner, matching Image row 0 = top, so the output must look upright.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <GLES3/gl3.h>

#include "as3d/gfx.h"
#include "as3d/image.h"
#include "as3d/vfs.h"
#include "common.h"
#include "registry.h"

namespace {

const char* kVertexSrc = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kFragmentSrc = R"(#version 300 es
precision mediump float;
in vec2 vUv;
uniform sampler2D uTex;
out vec4 fragColor;
void main() {
    fragColor = texture(uTex, vUv);
}
)";

// Position (clip xy) + UV, UV (0,0) = top-left, matching as3d/gfx.h's convention.
struct QuadVertex {
    float x, y, u, v;
};

void makeQuadVao(as3d::VertexBuffer& vbo, as3d::VertexArray& vao, float half) {
    const QuadVertex verts[6] = {
        {-half, half, 0, 0},  {-half, -half, 0, 1}, {half, -half, 1, 1},
        {-half, half, 0, 0},  {half, -half, 1, 1},  {half, half, 1, 0},
    };
    vbo.upload(verts, sizeof verts);
    as3d::VertexLayout layout;
    layout.strideBytes = sizeof(QuadVertex);
    layout.attribs = {
        {0, 2, 0, false},
        {1, 2, 2 * sizeof(float), false},
    };
    vao.create(vbo, layout);
}

struct TextureScene {
    as3d::ShaderProgram program;
    as3d::VertexBuffer bgVbo, fgVbo;
    as3d::VertexArray bgVao, fgVao;
    as3d::Texture2D checkerTex;
    as3d::Texture2D loadedTex;
};

int run(int argc, char** argv) {
    viewer::SceneArgs args;
    std::vector<std::string> positional;
    if (!viewer::parseSceneArgs(argc, argv, args, positional)) return 1;
    if (positional.empty()) {
        std::fprintf(stderr, "usage: as3d_viewer texture <game path> --out out.png [--size WxH] [--window]\n");
        return 1;
    }
    std::string gamePath = positional[0];

    as3d::Vfs vfs;
    if (!viewer::mountGameData(vfs)) return 1;

    as3d::Blob blob;
    if (!vfs.read(gamePath, blob)) {
        std::fprintf(stderr, "error: '%s' not found in the mounted game data\n", gamePath.c_str());
        return 1;
    }
    as3d::Image loaded;
    if (!as3d::decodeTga(blob.data(), blob.size(), loaded)) {
        std::fprintf(stderr, "error: '%s' is not a valid TGA\n", gamePath.c_str());
        return 1;
    }
    std::printf("loaded %s: %dx%d, hasAlpha=%s\n", gamePath.c_str(), loaded.width, loaded.height,
                loaded.hasAlpha ? "true" : "false");

    auto scene = std::make_shared<TextureScene>();
    auto loadedImage = std::make_shared<as3d::Image>(std::move(loaded));

    auto setup = [scene, loadedImage](std::string& error) -> bool {
        if (!scene->program.compile(kVertexSrc, kFragmentSrc, &error)) return false;

        makeQuadVao(scene->bgVbo, scene->bgVao, 1.0f);
        makeQuadVao(scene->fgVbo, scene->fgVao, 0.75f);

        const as3d::u8 colorA[4] = {60, 60, 70, 255};
        const as3d::u8 colorB[4] = {200, 200, 210, 255};
        as3d::Image checker = viewer::makeCheckerboard(64, 64, 8, colorA, colorB);
        as3d::TextureOptions checkerOpts;
        checkerOpts.minFilter = as3d::Filter::Nearest;
        checkerOpts.magFilter = as3d::Filter::Nearest;
        scene->checkerTex.create(checker, checkerOpts);

        as3d::TextureOptions loadedOpts;
        loadedOpts.minFilter = as3d::Filter::Nearest;
        loadedOpts.magFilter = as3d::Filter::Nearest;
        loadedOpts.wrapS = as3d::Wrap::ClampToEdge;
        loadedOpts.wrapT = as3d::Wrap::ClampToEdge;
        scene->loadedTex.create(*loadedImage, loadedOpts);
        return true;
    };

    auto draw = [scene](int width, int height, float) {
        as3d::setViewport(0, 0, width, height);
        as3d::setDepth(false, false);
        as3d::setCull(as3d::CullMode::Off);
        as3d::clear({0.0f, 0.0f, 0.0f, 1.0f}, /*depth=*/false);

        scene->program.use();
        scene->program.setInt("uTex", 0);

        as3d::setBlend(as3d::GlBlend::Off);
        scene->checkerTex.bind(0);
        scene->bgVao.bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);

        as3d::setBlend(as3d::GlBlend::AlphaBlend);
        scene->loadedTex.bind(0);
        scene->fgVao.bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };

    return viewer::runScene(args, "as3d_viewer texture", setup, draw);
}

} // namespace

AS3D_VIEWER_COMMAND("texture", "draw a game .tga on a quad over a checkerboard", run);
