#include "viewer_scene.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common.h"

namespace viewer {

using namespace as3d;

namespace {

bool parseWxH(const std::string& s, int& w, int& h) {
    size_t x = s.find('x');
    if (x == std::string::npos) x = s.find('X');
    if (x == std::string::npos) return false;
    w = std::atoi(s.substr(0, x).c_str());
    h = std::atoi(s.substr(x + 1).c_str());
    return w > 0 && h > 0;
}

const char* kLineVs = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uViewProj;
out vec3 vColor;
void main() { vColor = aColor; gl_Position = uViewProj * vec4(aPos, 1.0); }
)";
const char* kLineFs = R"(#version 300 es
precision mediump float;
in vec3 vColor;
out vec4 fragColor;
void main() { fragColor = vec4(vColor, 1.0); }
)";

struct LineVertex {
    float x, y, z, r, g, b;
};

void addLine(std::vector<LineVertex>& v, Vec3 a, Vec3 b, Vec3 c) {
    v.push_back({a.x, a.y, a.z, c.x, c.y, c.z});
    v.push_back({b.x, b.y, b.z, c.x, c.y, c.z});
}

void drawLines(const std::vector<LineVertex>& verts, const Mat4& viewProj) {
    if (verts.empty()) return;
    static ShaderProgram* prog = nullptr;
    if (!prog) {
        prog = new ShaderProgram();
        std::string err;
        if (!prog->compile(kLineVs, kLineFs, &err)) std::fprintf(stderr, "line shader: %s\n", err.c_str());
    }
    VertexBuffer vbo;
    vbo.upload(verts.data(), verts.size() * sizeof(LineVertex), true);
    VertexLayout layout;
    layout.strideBytes = sizeof(LineVertex);
    layout.attribs = {{0, 3, 0, false}, {1, 3, 3 * sizeof(float), false}};
    VertexArray vao;
    vao.create(vbo, layout);
    prog->use();
    prog->setMat4("uViewProj", viewProj);
    glDisable(GL_BLEND);
    setCull(CullMode::Off);
    setDepth(true, true);
    vao.bind();
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size()));
}

void growBounds(SceneDesc& s, const Mat4& m, const Vec3& lo, const Vec3& hi, bool& first) {
    for (int i = 0; i < 8; i++) {
        Vec3 p{(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z};
        Vec3 w = transformPoint(m, p);
        if (first) {
            s.boundsMin = s.boundsMax = w;
            first = false;
        } else {
            s.boundsMin = {std::min(s.boundsMin.x, w.x), std::min(s.boundsMin.y, w.y), std::min(s.boundsMin.z, w.z)};
            s.boundsMax = {std::max(s.boundsMax.x, w.x), std::max(s.boundsMax.y, w.y), std::max(s.boundsMax.z, w.z)};
        }
    }
}

void finishBounds(SceneDesc& s) {
    bool first = true;
    for (const DrawPart& p : s.parts) {
        const ModelData& m = p.mesh->model;
        growBounds(s, p.model, m.boundsMin, m.boundsMax, first);
    }
    for (const MarkerPoint& m : s.psMarkers) {
        Mat4 t = translation(m.pos);
        growBounds(s, t, Vec3{0, 0, 0}, Vec3{0, 0, 0}, first);
    }
    s.valid = !first;
}

} // namespace

bool parseViewOpts(int argc, char** argv, ViewOpts& o, std::vector<std::string>& positional) {
    for (int i = 0; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %s needs a value\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        const char* v = nullptr;
        if (a == "--out") { if (!(v = next("--out"))) return false; o.out = v; }
        else if (a == "--skin") { if (!(v = next("--skin"))) return false; o.skin = v; }
        else if (a == "--size") {
            if (!(v = next("--size"))) return false;
            if (!parseWxH(v, o.width, o.height)) { std::fprintf(stderr, "error: bad --size '%s'\n", v); return false; }
        } else if (a == "--cell") {
            if (!(v = next("--cell"))) return false;
            if (!parseWxH(v, o.cellW, o.cellH)) { std::fprintf(stderr, "error: bad --cell '%s'\n", v); return false; }
        } else if (a == "--yaw") { if (!(v = next("--yaw"))) return false; o.yaw = static_cast<float>(std::atof(v)); }
        else if (a == "--pitch") { if (!(v = next("--pitch"))) return false; o.pitch = static_cast<float>(std::atof(v)); }
        else if (a == "--dist") { if (!(v = next("--dist"))) return false; o.dist = static_cast<float>(std::atof(v)); }
        else if (a == "--cols") { if (!(v = next("--cols"))) return false; o.cols = std::atoi(v); }
        else if (a == "--tilt") { if (!(v = next("--tilt"))) return false; o.topTiltDeg = static_cast<float>(std::atof(v)); }
        else if (a == "--height") { if (!(v = next("--height"))) return false; o.topHeightFactor = static_cast<float>(std::atof(v)); }
        else if (a == "--cull") {
            if (!(v = next("--cull"))) return false;
            std::string m = v;
            setCullDebugOverride(m == "off" ? 0 : m == "back" ? 1 : m == "front" ? 2 : -1);
        }
        else if (a == "--wire") o.wire = true;
        else if (a == "--tags") o.tags = true;
        else if (a == "--night") o.night = true;
        else if (a == "--list") o.list = true;
        else if (a == "--markers") o.markers = true;
        else positional.push_back(a);
    }
    return true;
}

bool ViewerContext::init(int w, int h, std::string& error) {
    if (!mountGameData(vfs)) { error = "no game data (set AS3D_DATA_ROOT)"; return false; }
    if (!db.load(vfs)) { error = "could not load object definitions"; return false; }
    GraphicsConfig cfg;
    cfg.width = w;
    cfg.height = h;
    cfg.headless = true;
    gl = createGraphicsContext(cfg);
    if (!gl) { error = "could not create a headless graphics context"; return false; }
    gl->makeCurrent();
    if (!renderer.init(&error)) return false;
    cache = std::make_unique<ResourceCache>(vfs);
    lighting = SceneLighting::fromMission1(db);
    return true;
}

bool buildModelScene(ViewerContext& ctx, const std::string& mdlPath, const std::string& skin, SceneDesc& scene,
                     std::string& error) {
    const GpuMesh& mesh = ctx.cache->mesh(mdlPath);
    if (!mesh.valid) { error = "model '" + mdlPath + "' could not be loaded"; return false; }
    DrawPart part;
    part.mesh = &mesh;
    const ObjectDef* def = nullptr;
    std::string key = normalizePath(mdlPath);
    for (const ObjectDef& o : ctx.db.objects()) {
        if (!o.model.empty() && normalizePath(o.model) == key) { def = &o; break; }
    }
    if (def) part.material = Material::fromObjectDef(*def, *ctx.cache);
    if (!skin.empty()) {
        ResourceCache::LoadedTexture t = ctx.cache->texture(skin);
        part.material.texture = t.texture;
        part.material.textureHasAlpha = t.hasAlpha;
    } else if (!part.material.texture) {
        part.material = Material::fromObjectDef(ObjectDef{}, *ctx.cache);
    }
    scene.parts.push_back(part);
    for (const ModelTag& t : mesh.model.tags) scene.tagMarkers.push_back({t.position, {1, 1, 0}, 1.0f});
    std::printf("model %s: %zu vertices, %zu faces, bounds (%.2f %.2f %.2f) - (%.2f %.2f %.2f)%s\n", mdlPath.c_str(),
                mesh.model.positions.size(), mesh.model.faces.size(), mesh.model.boundsMin.x, mesh.model.boundsMin.y,
                mesh.model.boundsMin.z, mesh.model.boundsMax.x, mesh.model.boundsMax.y, mesh.model.boundsMax.z,
                def ? "" : " (no object uses this model: placeholder skin)");
    for (const ModelTag& t : mesh.model.tags)
        std::printf("  tag %-20s (%9.3f %9.3f %9.3f)\n", t.name.c_str(), t.position.x, t.position.y, t.position.z);
    finishBounds(scene);
    return true;
}

bool buildObjectScene(ViewerContext& ctx, const std::string& objectName, bool night, SceneDesc& scene,
                      ObjectTree* treeOut, std::string& error) {
    ObjectTree::BuildOptions bo;
    bo.night = night;
    ObjectTree tree = ObjectTree::build(ctx.db, ctx.vfs, objectName, bo);
    if (tree.nodes().empty()) { error = tree.warnings().empty() ? "object not found" : tree.warnings()[0]; return false; }
    std::vector<char> hidden(tree.nodes().size(), 0);
    for (size_t i = 0; i < tree.nodes().size(); i++) {
        const ObjectNode& n = tree.nodes()[i];
        if (!n.active || (n.parent >= 0 && hidden[static_cast<size_t>(n.parent)])) hidden[i] = 1;
        if (hidden[i]) continue;
        if (n.kind == NodeKind::ParticleSystem) {
            scene.psMarkers.push_back({n.worldPos, {1, 0.4f, 0.1f}, 1.0f});
            continue;
        }
        if (!n.def || n.def->model.empty() || (n.def->flags & FL_NODRAW)) continue;
        const GpuMesh& mesh = ctx.cache->mesh(n.def->model);
        if (!mesh.valid) continue;
        DrawPart part;
        part.mesh = &mesh;
        part.material = Material::fromObjectDef(*n.def, *ctx.cache);
        part.model = n.world;
        part.colour = n.colour;
        scene.parts.push_back(part);
        for (const ModelTag& t : mesh.model.tags)
            scene.tagMarkers.push_back({transformPoint(n.world, t.position), {1, 1, 0}, 1.0f});
    }
    if (scene.parts.empty()) { error = "object '" + objectName + "' has nothing to draw (no valid model)"; return false; }
    finishBounds(scene);
    if (treeOut) *treeOut = std::move(tree);
    return true;
}

bool ViewerContext::renderToImage(int w, int h, Image& out, const SceneDesc& scene, const ViewOpts& opts, bool top,
                                  std::string& error) {
    if (!scene.valid) { error = "empty scene"; return false; }
    RenderTarget target;
    if (!target.create(w, h, 4)) { error = "could not create render target"; return false; }
    target.bind();
    setViewport(0, 0, w, h);
    clear({0.42f, 0.44f, 0.48f, 1.0f}, true);

    float aspect = static_cast<float>(w) / static_cast<float>(h);
    Vec3 center = (scene.boundsMin + scene.boundsMax) * 0.5f;
    float radius = std::max(0.5f * length(scene.boundsMax - scene.boundsMin), 0.01f);
    Camera cam = top ? Camera::topDown(center, radius * opts.topHeightFactor, opts.topTiltDeg, aspect)
                     : Camera::framing(scene.boundsMin, scene.boundsMax, opts.yaw, opts.pitch, opts.dist, aspect);
    // Keep fog out of the way of the inspection views.
    SceneLighting light = lighting;
    light.fogStart = 1e7f;
    light.fogEnd = 2e7f;

    if (!opts.wire) {
        renderer.begin(cam, light);
        for (const DrawPart& p : scene.parts) renderer.submit(*p.mesh, p.material, p.model, p.colour);
        renderer.end();
    }

    Mat4 vp = cam.viewProjMatrix();
    std::vector<LineVertex> lines;
    // Ground grid under the object.
    float z = scene.boundsMin.z;
    float half = radius * 1.6f;
    float step = std::pow(10.0f, std::floor(std::log10(half / 4.0f)));
    if (step < 0.001f) step = 1.0f;
    for (float g = -std::floor(half / step) * step; g <= half + 0.001f; g += step) {
        Vec3 c = std::fabs(g) < step * 0.5f ? Vec3{0.25f, 0.25f, 0.28f} : Vec3{0.33f, 0.35f, 0.38f};
        addLine(lines, {center.x + g, center.y - half, z}, {center.x + g, center.y + half, z}, c);
        addLine(lines, {center.x - half, center.y + g, z}, {center.x + half, center.y + g, z}, c);
    }
    if (opts.wire) {
        for (const DrawPart& p : scene.parts) {
            const ModelData& m = p.mesh->model;
            for (const Face& f : m.faces) {
                Vec3 a = transformPoint(p.model, m.positions[f.v[0]]);
                Vec3 b = transformPoint(p.model, m.positions[f.v[1]]);
                Vec3 c = transformPoint(p.model, m.positions[f.v[2]]);
                Vec3 col{0.95f, 0.95f, 0.85f};
                addLine(lines, a, b, col);
                addLine(lines, b, c, col);
                addLine(lines, c, a, col);
            }
        }
    }
    float ms = radius * 0.05f;
    if (opts.tags) {
        for (const MarkerPoint& m : scene.tagMarkers) {
            addLine(lines, m.pos, m.pos + Vec3{ms, 0, 0}, {1, 0, 0});
            addLine(lines, m.pos, m.pos + Vec3{0, ms, 0}, {0, 1, 0});
            addLine(lines, m.pos, m.pos + Vec3{0, 0, ms}, {0.2f, 0.4f, 1});
        }
    }
    if (opts.markers) {
        for (const MarkerPoint& m : scene.psMarkers) {
            addLine(lines, m.pos - Vec3{ms, 0, 0}, m.pos + Vec3{ms, 0, 0}, m.colour);
            addLine(lines, m.pos - Vec3{0, ms, 0}, m.pos + Vec3{0, ms, 0}, m.colour);
            addLine(lines, m.pos - Vec3{0, 0, ms}, m.pos + Vec3{0, 0, ms}, m.colour);
        }
    }
    drawLines(lines, vp);

    if (!target.readPixels(out)) { error = "readPixels failed"; return false; }
    return true;
}

// ---------------------------------------------------------------------------
// 5x7 font (own data): rows of 5 bits, MSB = leftmost pixel.
// ---------------------------------------------------------------------------
namespace {
struct Glyph {
    char c;
    const char* rows[7];
};
const Glyph kGlyphs[] = {
    {'A', {"01110", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {'B', {"11110", "10001", "10001", "11110", "10001", "10001", "11110"}},
    {'C', {"01110", "10001", "10000", "10000", "10000", "10001", "01110"}},
    {'D', {"11110", "10001", "10001", "10001", "10001", "10001", "11110"}},
    {'E', {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}},
    {'F', {"11111", "10000", "10000", "11110", "10000", "10000", "10000"}},
    {'G', {"01110", "10001", "10000", "10111", "10001", "10001", "01111"}},
    {'H', {"10001", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {'I', {"01110", "00100", "00100", "00100", "00100", "00100", "01110"}},
    {'J', {"00111", "00010", "00010", "00010", "00010", "10010", "01100"}},
    {'K', {"10001", "10010", "10100", "11000", "10100", "10010", "10001"}},
    {'L', {"10000", "10000", "10000", "10000", "10000", "10000", "11111"}},
    {'M', {"10001", "11011", "10101", "10101", "10001", "10001", "10001"}},
    {'N', {"10001", "11001", "10101", "10011", "10001", "10001", "10001"}},
    {'O', {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}},
    {'P', {"11110", "10001", "10001", "11110", "10000", "10000", "10000"}},
    {'Q', {"01110", "10001", "10001", "10001", "10101", "10010", "01101"}},
    {'R', {"11110", "10001", "10001", "11110", "10100", "10010", "10001"}},
    {'S', {"01111", "10000", "10000", "01110", "00001", "00001", "11110"}},
    {'T', {"11111", "00100", "00100", "00100", "00100", "00100", "00100"}},
    {'U', {"10001", "10001", "10001", "10001", "10001", "10001", "01110"}},
    {'V', {"10001", "10001", "10001", "10001", "10001", "01010", "00100"}},
    {'W', {"10001", "10001", "10001", "10101", "10101", "11011", "10001"}},
    {'X', {"10001", "10001", "01010", "00100", "01010", "10001", "10001"}},
    {'Y', {"10001", "10001", "01010", "00100", "00100", "00100", "00100"}},
    {'Z', {"11111", "00001", "00010", "00100", "01000", "10000", "11111"}},
    {'0', {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}},
    {'1', {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}},
    {'2', {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}},
    {'3', {"11110", "00001", "00001", "01110", "00001", "00001", "11110"}},
    {'4', {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}},
    {'5', {"11111", "10000", "11110", "00001", "00001", "10001", "01110"}},
    {'6', {"00110", "01000", "10000", "11110", "10001", "10001", "01110"}},
    {'7', {"11111", "00001", "00010", "00100", "01000", "01000", "01000"}},
    {'8', {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}},
    {'9', {"01110", "10001", "10001", "01111", "00001", "00010", "01100"}},
    {'_', {"00000", "00000", "00000", "00000", "00000", "00000", "11111"}},
    {'-', {"00000", "00000", "00000", "11111", "00000", "00000", "00000"}},
    {'.', {"00000", "00000", "00000", "00000", "00000", "01100", "01100"}},
    {':', {"00000", "01100", "01100", "00000", "01100", "01100", "00000"}},
    {'/', {"00001", "00001", "00010", "00100", "01000", "10000", "10000"}},
    {'*', {"00000", "10101", "01110", "11111", "01110", "10101", "00000"}},
    {'?', {"01110", "10001", "00001", "00010", "00100", "00000", "00100"}},
};
const Glyph* findGlyph(char c) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const Glyph& g : kGlyphs)
        if (g.c == c) return &g;
    return nullptr;
}
} // namespace

int textWidth(const std::string& text, int scale) { return static_cast<int>(text.size()) * 6 * scale; }

void drawText(Image& img, int x, int y, const std::string& text, const unsigned char rgb[3], int scale) {
    for (char ch : text) {
        const Glyph* g = findGlyph(ch);
        if (g) {
            for (int row = 0; row < 7; row++)
                for (int col = 0; col < 5; col++) {
                    if (g->rows[row][col] != '1') continue;
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++) {
                            int px = x + col * scale + sx, py = y + row * scale + sy;
                            if (px < 0 || py < 0 || px >= img.width || py >= img.height) continue;
                            size_t i = (static_cast<size_t>(py) * img.width + px) * 4;
                            img.rgba[i] = rgb[0];
                            img.rgba[i + 1] = rgb[1];
                            img.rgba[i + 2] = rgb[2];
                            img.rgba[i + 3] = 255;
                        }
                }
        }
        x += 6 * scale;
    }
}

} // namespace viewer
