// `as3d_viewer cube --out out.png [--size WxH] [--window]`: a lit, depth-tested,
// perspective-projected, textured rotating cube -- the milestone's test of the math
// library (perspective/lookAt/rotation/inverse/transpose), depth testing and
// backface culling together. Headless mode writes a single frame (a fixed rotation);
// --window animates it continuously.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <GLES3/gl3.h>

#include "as3d/gfx.h"
#include "as3d/math.h"
#include "common.h"
#include "registry.h"

namespace {

const char* kVertexSrc = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat3 uNormalMat;
out vec3 vNormal;
out vec2 vUv;
void main() {
    vNormal = uNormalMat * aNormal;
    vUv = aUv;
    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);
}
)";

const char* kFragmentSrc = R"(#version 300 es
precision mediump float;
in vec3 vNormal;
in vec2 vUv;
uniform sampler2D uTex;
uniform vec3 uLightDir;
out vec4 fragColor;
void main() {
    vec3 n = normalize(vNormal);
    float diff = max(dot(n, normalize(uLightDir)), 0.0);
    vec3 base = texture(uTex, vUv).rgb;
    vec3 color = base * (0.25 + 0.75 * diff);
    fragColor = vec4(color, 1.0);
}
)";

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

// 6 faces x 4 vertices, wound so cross(v1-v0, v2-v0) points along the outward normal
// (i.e. correct CCW-front-facing winding for glFrontFace(GL_CCW) + back-face culling).
void buildCube(std::vector<Vertex>& verts, std::vector<uint16_t>& indices) {
    struct Face {
        float n[3];
        float p[4][3];
    };
    const float h = 0.5f;
    const Face faces[6] = {
        {{1, 0, 0}, {{h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}}},
        {{-1, 0, 0}, {{-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}}},
        {{0, 1, 0}, {{-h, h, -h}, {-h, h, h}, {h, h, h}, {h, h, -h}}},
        {{0, -1, 0}, {{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}}},
        {{0, 0, 1}, {{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}}},
        {{0, 0, -1}, {{h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}}},
    };
    const float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    for (const Face& f : faces) {
        uint16_t base = static_cast<uint16_t>(verts.size());
        for (int i = 0; i < 4; i++) {
            Vertex v;
            v.px = f.p[i][0];
            v.py = f.p[i][1];
            v.pz = f.p[i][2];
            v.nx = f.n[0];
            v.ny = f.n[1];
            v.nz = f.n[2];
            v.u = uvs[i][0];
            v.v = uvs[i][1];
            verts.push_back(v);
        }
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }
}

struct CubeScene {
    as3d::ShaderProgram program;
    as3d::VertexBuffer vbo;
    as3d::IndexBuffer ibo;
    as3d::VertexArray vao;
    as3d::Texture2D tex;
    size_t indexCount = 0;
};

int run(int argc, char** argv) {
    viewer::SceneArgs args;
    std::vector<std::string> positional;
    if (!viewer::parseSceneArgs(argc, argv, args, positional)) return 1;

    auto scene = std::make_shared<CubeScene>();

    auto setup = [scene](std::string& error) -> bool {
        if (!scene->program.compile(kVertexSrc, kFragmentSrc, &error)) return false;

        std::vector<Vertex> verts;
        std::vector<uint16_t> indices;
        buildCube(verts, indices);
        scene->vbo.upload(verts.data(), verts.size() * sizeof(Vertex));
        scene->ibo.upload(indices.data(), indices.size() * sizeof(uint16_t), as3d::IndexType::U16);
        scene->indexCount = indices.size();

        as3d::VertexLayout layout;
        layout.strideBytes = sizeof(Vertex);
        layout.attribs = {
            {0, 3, offsetof(Vertex, px), false},
            {1, 3, offsetof(Vertex, nx), false},
            {2, 2, offsetof(Vertex, u), false},
        };
        scene->vao.create(scene->vbo, layout, &scene->ibo);

        const as3d::u8 colorA[4] = {230, 140, 40, 255};
        const as3d::u8 colorB[4] = {40, 90, 200, 255};
        as3d::Image tex = viewer::makeCheckerboard(64, 64, 16, colorA, colorB);
        as3d::TextureOptions opts;
        opts.minFilter = as3d::Filter::Nearest;
        opts.magFilter = as3d::Filter::Nearest;
        scene->tex.create(tex, opts);
        return true;
    };

    auto draw = [scene](int width, int height, float timeSeconds) {
        as3d::setViewport(0, 0, width, height);
        as3d::setDepth(true, true);
        as3d::setCull(as3d::CullMode::Back);
        as3d::setBlend(as3d::GlBlend::Off);
        as3d::clear({0.08f, 0.09f, 0.12f, 1.0f}, /*depth=*/true);

        // A fixed, well-chosen rotation in headless mode (one frame of "a rotating
        // cube"); continuous rotation when animated in a window.
        float yawDeg = 35.0f + timeSeconds * 40.0f;
        float pitchDeg = 25.0f + timeSeconds * 25.0f;
        as3d::Mat4 model = as3d::rotationY(yawDeg) * as3d::rotationX(pitchDeg);

        float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        as3d::Mat4 proj = as3d::perspective(60.0f, aspect, 0.1f, 100.0f);
        as3d::Mat4 view = as3d::lookAt({1.3f, 1.1f, 1.7f}, {0, 0, 0}, {0, 1, 0});
        as3d::Mat4 viewProj = proj * view;

        as3d::Mat3 normalMat = as3d::transpose(as3d::inverse(as3d::mat3FromMat4(model)));

        scene->program.use();
        scene->program.setMat4("uModel", model);
        scene->program.setMat4("uViewProj", viewProj);
        scene->program.setMat3("uNormalMat", normalMat);
        scene->program.setVec3("uLightDir", as3d::normalize(as3d::Vec3{0.4f, 0.8f, 0.5f}));
        scene->program.setInt("uTex", 0);
        scene->tex.bind(0);
        scene->vao.bind();
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(scene->indexCount), GL_UNSIGNED_SHORT, nullptr);
    };

    return viewer::runScene(args, "as3d_viewer cube", setup, draw);
}

} // namespace

AS3D_VIEWER_COMMAND("cube", "render a lit, textured, rotating cube to a PNG", run);
