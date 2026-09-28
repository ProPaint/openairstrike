// `as3d_viewer triangle --out out.png [--size WxH] [--window]`: the smallest possible
// smoke test for the platform+render foundation -- one shader, one VBO, one triangle.
//
// Layout, matching apps/tests/gfx_test.cpp's pixel checks: RED at the top of clip
// space, GREEN at the bottom-left, BLUE at the bottom-right, over a dark background.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <GLES3/gl3.h>

#include "as3d/gfx.h"
#include "common.h"
#include "registry.h"

namespace {

const char* kVertexSrc = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kFragmentSrc = R"(#version 300 es
precision mediump float;
in vec3 vColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, 1.0);
}
)";

struct TriangleScene {
    as3d::ShaderProgram program;
    as3d::VertexBuffer vbo;
    as3d::VertexArray vao;
};

int run(int argc, char** argv) {
    viewer::SceneArgs args;
    std::vector<std::string> positional;
    if (!viewer::parseSceneArgs(argc, argv, args, positional)) return 1;

    auto scene = std::make_shared<TriangleScene>();

    auto setup = [scene](std::string& error) -> bool {
        if (!scene->program.compile(kVertexSrc, kFragmentSrc, &error)) return false;

        // x, y, r, g, b -- top vertex RED, bottom-left GREEN, bottom-right BLUE.
        const float verts[] = {
            0.0f, 0.7f,  1.0f, 0.0f, 0.0f,
            -0.7f, -0.7f, 0.0f, 1.0f, 0.0f,
            0.7f, -0.7f,  0.0f, 0.0f, 1.0f,
        };
        scene->vbo.upload(verts, sizeof verts);

        as3d::VertexLayout layout;
        layout.strideBytes = 5 * sizeof(float);
        layout.attribs = {
            {0, 2, 0, false},
            {1, 3, 2 * sizeof(float), false},
        };
        scene->vao.create(scene->vbo, layout);
        return true;
    };

    auto draw = [scene](int width, int height, float) {
        as3d::setViewport(0, 0, width, height);
        as3d::setDepth(false, false);
        as3d::setCull(as3d::CullMode::Off);
        as3d::setBlend(as3d::BlendMode::Off);
        as3d::clear({0.05f, 0.05f, 0.08f, 1.0f}, /*depth=*/false);
        scene->program.use();
        scene->vao.bind();
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };

    return viewer::runScene(args, "as3d_viewer triangle", setup, draw);
}

} // namespace

AS3D_VIEWER_COMMAND("triangle", "render a colour-interpolated triangle to a PNG", run);
