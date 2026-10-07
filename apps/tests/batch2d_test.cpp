// The neutral 2D batch (as3d/batch2d.h): run merging, vertex generation per blend mode, the
// second texture, CPU tessellation of circles and rings, and an image comparison of the
// tessellated shapes with the shader's anti-aliasing (headless GLES; skips loudly without it).
#include "doctest.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "as3d/batch2d.h"
#include "as3d/gfx.h"
#include "as3d/image.h"
#include "as3d/ui.h"
#include "ui_test_util.h"

using namespace as3d;
using namespace as3d::ui;

namespace {

Quad rectQuad(float x, float y, float w, float h, Color c, Blend b = Blend::Alpha) {
    Quad q;
    q.x = x; q.y = y; q.w = w; q.h = h;
    q.color = c;
    q.blend = b;
    return q;
}

Quad discQuad(float cx, float cy, float r, Color c, Blend b = Blend::Alpha, float inner = 0, float feather = 0) {
    Quad q = rectQuad(cx - r, cy - r, 2 * r, 2 * r, c, b);
    q.shape = QuadShape::Ellipse;
    q.inner = inner;
    q.feather = feather;
    return q;
}

} // namespace

TEST_CASE("batch2d: consecutive quads of equal state merge into one run") {
    const Mapping m = computeMapping(800, 600);
    Texture2D ta, tb;
    std::vector<Quad> qs;
    for (int i = 0; i < 3; i++) qs.push_back(rectQuad(float(i * 10), 0, 8, 8, {1, 1, 1, 1}));
    qs.push_back(rectQuad(0, 20, 8, 8, {1, 1, 1, 1}, Blend::Add)); // blend change
    Quad t1 = rectQuad(0, 40, 8, 8, {1, 1, 1, 1});
    t1.texture = &ta;
    qs.push_back(t1);
    qs.push_back(t1);                                                // same texture: merges
    Quad t2 = t1;
    t2.texture = &tb;
    qs.push_back(t2);                                                // texture change
    std::vector<Vertex2D> v;
    std::vector<Run2D> r;
    buildBatch2D(qs, m, v, r);
    REQUIRE(r.size() == 4u);
    CHECK(v.size() == qs.size() * 6);
    CHECK(r[0].first == 0);   CHECK(r[0].count == 18);
    CHECK(r[1].first == 18);  CHECK(r[1].count == 6);  CHECK(r[1].blend == Blend::Add);
    CHECK(r[2].first == 24);  CHECK(r[2].count == 12); CHECK(r[2].tex == &ta);
    CHECK(r[3].first == 36);  CHECK(r[3].count == 6);  CHECK(r[3].tex == &tb);
    int total = 0;
    for (const Run2D& x : r) total += x.count;
    CHECK(total == static_cast<int>(v.size()));
}

TEST_CASE("batch2d: shape, texture2 and combine mode split runs") {
    const Mapping m = computeMapping(800, 600);
    Texture2D ta, tb;
    std::vector<Quad> qs;
    qs.push_back(rectQuad(0, 0, 8, 8, {1, 1, 1, 1}));
    qs.push_back(discQuad(50, 50, 10, {1, 1, 1, 1}));
    qs.push_back(discQuad(90, 50, 10, {1, 1, 1, 1}, Blend::Alpha, 0.5f)); // same state: merges with the previous disc
    qs.push_back(rectQuad(0, 0, 8, 8, {1, 1, 1, 1}));
    Quad c1 = rectQuad(0, 0, 8, 8, {1, 1, 1, 1});
    c1.texture2 = &ta;
    c1.combine2 = 2;
    qs.push_back(c1);
    Quad c2 = c1;
    c2.texture2 = &tb;
    qs.push_back(c2); // another second texture
    Quad c3 = c2;
    c3.combine2 = 3;
    qs.push_back(c3); // another combine mode
    Quad c4 = c3;
    c4.combine2 = 0;
    qs.push_back(c4); // second texture without a combine mode: plain quad, merges with nothing before
    qs.push_back(rectQuad(0, 0, 8, 8, {1, 1, 1, 1})); // ... but with the next plain quad
    std::vector<Vertex2D> v;
    std::vector<Run2D> r;
    buildBatch2D(qs, m, v, r);
    REQUIRE(r.size() == 7u);
    CHECK_FALSE(r[0].shape);
    CHECK(r[1].shape);
    CHECK(r[1].count == 12);
    CHECK_FALSE(r[2].shape);
    CHECK(r[3].combine2 == CombineMode::Add);
    CHECK(r[3].tex2 == &ta);
    CHECK(r[4].tex2 == &tb);
    CHECK(r[5].combine2 == CombineMode::Modulate);
    CHECK(r[6].combine2 == CombineMode::None);
    CHECK(r[6].tex2 == nullptr);
    CHECK(r[6].count == 12);
    // Out-of-range combine modes replace.
    Quad c5 = c1;
    c5.combine2 = 9;
    buildBatch2D({c5}, m, v, r);
    REQUIRE(r.size() == 1u);
    CHECK(r[0].combine2 == CombineMode::Replace);
}

TEST_CASE("batch2d: kMaxQuads quads of one state are a single run") {
    Renderer2D r2;
    r2.begin(800, 600);
    for (int i = 0; i < kMaxQuads; i++) CHECK(r2.rect(float(i % 100), float(i / 100), 1, 1, {1, 0, 0, 1}));
    CHECK_FALSE(r2.rect(0, 0, 1, 1, {1, 0, 0, 1}));
    CHECK(r2.dropped() == 1);
    std::vector<Vertex2D> v;
    std::vector<Run2D> r;
    buildBatch2D(r2.quads(), r2.mapping(), v, r);
    CHECK(v.size() == static_cast<size_t>(kMaxQuads) * 6);
    REQUIRE(r.size() == 1u);
    CHECK(r[0].first == 0);
    CHECK(r[0].count == kMaxQuads * 6);
    // Alternating states: one run per quad.
    std::vector<Quad> alt;
    for (int i = 0; i < kMaxQuads; i++) alt.push_back(rectQuad(0, 0, 1, 1, {1, 1, 1, 1}, i % 2 ? Blend::Add : Blend::Alpha));
    buildBatch2D(alt, r2.mapping(), v, r);
    CHECK(r.size() == static_cast<size_t>(kMaxQuads));
}

TEST_CASE("batch2d: vertex positions, UVs and premultiplied colours of one quad") {
    // 1600 x 1200: scale 2, no offset, so the framebuffer position is twice the virtual one.
    const Mapping m = computeMapping(1600, 1200);
    Quad q = rectQuad(10, 20, 30, 40, {0.5f, 0.25f, 1.0f, 0.5f});
    q.s0 = 0.1f; q.t0 = 0.2f; q.s1 = 0.9f; q.t1 = 0.8f;
    std::vector<Vertex2D> v;
    std::vector<Run2D> r;
    buildBatch2D({q}, m, v, r);
    REQUIRE(v.size() == 6u);
    // Corners TL, BL, BR, TL, BR, TR.
    const float ex[6] = {20, 20, 80, 20, 80, 80}, ey[6] = {40, 120, 120, 40, 120, 40};
    const float es[6] = {0.1f, 0.1f, 0.9f, 0.1f, 0.9f, 0.9f}, et[6] = {0.2f, 0.8f, 0.8f, 0.2f, 0.8f, 0.2f};
    for (int i = 0; i < 6; i++) {
        CHECK(v[i].x == doctest::Approx(ex[i]));
        CHECK(v[i].y == doctest::Approx(ey[i]));
        CHECK(v[i].u == doctest::Approx(es[i]));
        CHECK(v[i].v == doctest::Approx(et[i]));
    }

    struct Case { Blend b; Color in; int out[4]; } cases[] = {
        {Blend::Alpha, {0.5f, 0.25f, 1.0f, 0.5f}, {128, 64, 255, 128}},
        {Blend::Add, {0.5f, 0.25f, 1.0f, 0.5f}, {64, 32, 128, 255}},     // rgb * alpha, alpha 1
        {Blend::Opaque, {0.5f, 0.25f, 1.0f, 0.5f}, {128, 64, 255, 255}}, // alpha forced to 1
        {Blend::Filter, {0.0f, 0.5f, 1.0f, 0.5f}, {128, 191, 255, 255}}, // 1 + (c - 1) * a
    };
    for (const Case& c : cases) {
        Quad cq = q;
        cq.blend = c.b;
        cq.color = c.in;
        buildBatch2D({cq}, m, v, r);
        REQUIRE(v.size() == 6u);
        CHECK(r[0].blend == c.b);
        for (int i = 0; i < 6; i++)
            for (int k = 0; k < 4; k++) CHECK(int(v[i].rgba[k]) == c.out[k]);
        CHECK(v[0].color[0] == doctest::Approx(effectiveColor(cq).r));
        CHECK(v[0].color[3] == doctest::Approx(effectiveColor(cq).a));
    }

    // A line is a one-pixel-thick quad around the segment, sampling the texture centre.
    Quad ln;
    ln.x = 0; ln.y = 0; ln.w = 10; ln.h = 0;
    ln.line = true;
    buildBatch2D({ln}, computeMapping(800, 600), v, r);
    REQUIRE(v.size() == 6u);
    CHECK(v[0].y == doctest::Approx(0.5f));
    CHECK(v[1].y == doctest::Approx(-0.5f));
    CHECK(v[0].u == doctest::Approx(0.5f));

    // Rotation about the centre and the half-pixel shift.
    Quad rot = rectQuad(100, 100, 20, 20, {1, 1, 1, 1});
    rot.rotation = 90;
    const Mapping m1 = computeMapping(800, 600);
    buildBatch2D({rot}, m1, v, r);
    CHECK(v[0].x == doctest::Approx(120)); // top-left goes to the top-right
    CHECK(v[0].y == doctest::Approx(100));
    Batch2DOptions o;
    o.halfPixelShift = true;
    buildBatch2D({rectQuad(100, 100, 20, 20, {1, 1, 1, 1})}, m1, v, r, o);
    CHECK(v[0].x == doctest::Approx(99.5f));
    CHECK(v[0].y == doctest::Approx(99.5f));
}

TEST_CASE("batch2d: second texture coordinates and the Renderer2D corners agree with the batch") {
    Texture2D t2;
    Quad q = rectQuad(0, 0, 10, 10, {1, 1, 1, 1});
    q.texture2 = &t2;
    q.combine2 = 1;
    q.s0b = 0.25f; q.t0b = 0.5f; q.s1b = 0.75f; q.t1b = 1.0f;
    Renderer2D r2;
    r2.begin(800, 600);
    r2.setHalfPixelShift(true);
    r2.add(q);
    Batch2DOptions o;
    o.halfPixelShift = true;
    std::vector<Vertex2D> v;
    std::vector<Run2D> r;
    buildBatch2D(r2.quads(), r2.mapping(), v, r, o);
    REQUIRE(v.size() == 6u);
    CHECK(r[0].tex2 == &t2);
    CHECK(r[0].combine2 == CombineMode::Blend);
    const float eu[6] = {0.25f, 0.25f, 0.75f, 0.25f, 0.75f, 0.75f}, ev[6] = {0.5f, 1.0f, 1.0f, 0.5f, 1.0f, 0.5f};
    for (int i = 0; i < 6; i++) {
        CHECK(v[i].u2 == doctest::Approx(eu[i]));
        CHECK(v[i].v2 == doctest::Approx(ev[i]));
    }
    float px[4], py[4];
    r2.quadCorners(q, px, py);
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) {
        CHECK(v[i].x == doctest::Approx(px[idx[i]]));
        CHECK(v[i].y == doctest::Approx(py[idx[i]]));
    }
}

TEST_CASE("batch2d: tessellated discs and rings are plain triangle runs with a transparent rim") {
    const Mapping m = computeMapping(800, 600);
    Batch2DOptions o;
    o.tessellateShapes = true;
    o.circleSegments = 32;
    std::vector<Vertex2D> v;
    std::vector<Run2D> r;
    buildBatch2D({rectQuad(0, 0, 5, 5, {1, 1, 1, 1}), discQuad(300, 300, 100, {1, 0, 0, 1}), rectQuad(0, 0, 5, 5, {1, 1, 1, 1})}, m, v, r, o);
    REQUIRE(r.size() == 1u); // no shape flag when tessellated: everything merges
    CHECK_FALSE(r[0].shape);
    CHECK(r[0].count % 3 == 0);
    CHECK(v.size() > 6u + 6u + 32u * 3u);
    bool centre = false;
    float maxR = 0;
    int rimVerts = 0, solidVerts = 0;
    for (size_t i = 6; i + 6 < v.size(); i++) {
        const float d = std::hypot(v[i].x - 300, v[i].y - 300);
        maxR = std::max(maxR, d);
        if (d < 1e-3f) { centre = true; CHECK(v[i].rgba[3] == 255); }
        if (d > 99.9f) { rimVerts++; CHECK(v[i].rgba[3] == 0); }
        else { solidVerts++; CHECK(v[i].rgba[3] == 255); }
    }
    CHECK(centre);
    CHECK(maxR <= 100.01f);
    CHECK(rimVerts > 0);
    CHECK(solidVerts > 0);

    // A ring: no vertex inside the hole's feather, hole edge alpha 0, additive rgb scaled too.
    buildBatch2D({discQuad(300, 300, 100, {1, 1, 1, 1}, Blend::Add, 0.8f)}, m, v, r, o);
    REQUIRE(r.size() == 1u);
    bool sawHoleEdge = false;
    for (const Vertex2D& x : v) {
        const float d = std::hypot(x.x - 300, x.y - 300);
        CHECK(d > 80 - 1.4f - 0.01f);
        if (d < 80 - 1.0f) { sawHoleEdge = true; CHECK(x.rgba[0] == 0); CHECK(x.rgba[3] == 255); }
        if (d > 80 + 1.5f && d < 100 - 1.5f) { CHECK(x.rgba[0] == 255); }
    }
    CHECK(sawHoleEdge);

    // Texture coordinates follow the quad: the centre samples the middle.
    Quad tq = discQuad(300, 300, 50, {1, 1, 1, 1});
    tq.s0 = 0; tq.t0 = 0; tq.s1 = 1; tq.t1 = 1;
    buildBatch2D({tq}, m, v, r, o);
    for (const Vertex2D& x : v) {
        CHECK(x.u == doctest::Approx((x.x - 250) / 100));
        CHECK(x.v == doctest::Approx((x.y - 250) / 100));
    }
}

namespace {

struct ShapeImages {
    Image shader, tess;
};

// Draws the same set of discs and rings (the shapes of the touch controls: body, rims,
// feathered shadow, additive glow, a thin ring, a small dot) with the shader and tessellated.
bool renderShapes(Renderer2D& r, bool tess, int w, int h, Image& out, const Color& bg) {
    RenderTarget target;
    if (!target.create(w, h, 0)) return false;
    target.bind();
    glViewport(0, 0, w, h);
    glClearColor(bg.r, bg.g, bg.b, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    r.setTessellateShapes(tess);
    r.begin(w, h);
    const float rad = 90;
    r.circle(150, 160, rad * 1.10f, Color{0, 0, 0, 0.30f}, Blend::Alpha, rad * 0.22f);     // shadow
    r.circle(150, 150, rad, Color{0.2f, 0.4f, 0.9f, 0.8f});                                // body
    r.ring(150, 150, rad, 6.8f, Color{1, 1, 1, 0.9f}, Blend::Alpha, 0.5f);                 // rim
    r.ring(150, 150, rad * 0.9f, 2.25f, Color{1, 1, 1, 0.10f});                            // faint ring
    r.ring(400, 150, rad * 1.12f, rad * 0.14f, Color{1.0f, 0.55f, 0.1f, 0.55f}, Blend::Add, rad * 0.10f); // glow
    r.ring(400, 150, 9.0f, 1.6f, Color{1, 1, 1, 0.45f});
    r.circle(400, 150, 1.8f, Color{1, 1, 1, 0.45f});
    r.circle(600, 150, 40, Color{1, 0, 0, 1});
    r.ring(600, 380, 120, 20, Color{0, 1, 0, 1});
    r.ring(250, 400, 60, 3, Color{1, 1, 0, 1});
    r.flush();
    return target.readPixels(out);
}

} // namespace

TEST_CASE("batch2d: tessellated shapes look like the shader's anti-aliasing") {
    AS3D_REQUIRE_GLES();
    Renderer2D r;
    std::string err;
    REQUIRE_MESSAGE(r.init(&err), err);
    const int w = 800, h = 600;

    struct Edge { float cx, cy, r; };
    // Every circle edge (outer and hole edges, including the glow and shadow) in pixels.
    const std::vector<Edge> edges = {
        {150, 160, 99},  {150, 150, 90}, {150, 150, 83.2f}, {150, 150, 81}, {150, 150, 78.75f},
        {400, 150, 100.8f}, {400, 150, 85.2f}, {400, 150, 9}, {400, 150, 7.4f}, {400, 150, 1.8f},
        {600, 150, 40}, {600, 380, 120}, {600, 380, 100}, {250, 400, 60}, {250, 400, 57},
    };
    for (const Color& bg : {Color{0, 0, 0, 1}, Color{0.35f, 0.4f, 0.3f, 1}}) {
        Image a, b;
        REQUIRE(renderShapes(r, false, w, h, a, bg));
        REQUIRE(renderShapes(r, true, w, h, b, bg));
        REQUIRE(a.rgba.size() == b.rgba.size());
        // Metric 1: mean absolute error over all pixels and the three colour channels.
        // Metric 2: the largest per-channel difference among the pixels farther than 2 pixels
        // from every shape edge (distance of the pixel centre to each circle's edge).
        // Metric 3 (reported): the largest difference anywhere.
        double sum = 0;
        int maxOutside = 0, maxAll = 0, bandPixels = 0;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                bool band = false;
                for (const Edge& e : edges)
                    if (std::fabs(std::hypot(x + 0.5f - e.cx, y + 0.5f - e.cy) - e.r) <= 2.0f) { band = true; break; }
                int worst = 0;
                const size_t o = (static_cast<size_t>(y) * w + x) * 4;
                for (int c = 0; c < 3; c++) {
                    const int d = std::abs(int(a.rgba[o + c]) - int(b.rgba[o + c]));
                    sum += d;
                    worst = std::max(worst, d);
                }
                maxAll = std::max(maxAll, worst);
                if (band) bandPixels++;
                else maxOutside = std::max(maxOutside, worst);
            }
        const double mae = sum / (3.0 * w * h);
        std::fprintf(stderr, "batch2d image test (bg %.2f): mean abs error %.4f/255, max outside the 2 px edge bands %d/255, max anywhere %d/255, band pixels %d\n",
                     bg.r, mae, maxOutside, maxAll, bandPixels);
        CHECK(mae <= 6.0);
        CHECK(maxOutside <= 64);
        // Both paths must have drawn something.
        long lit = 0;
        for (size_t i = 0; i < b.rgba.size(); i += 4)
            lit += std::abs(int(b.rgba[i]) - int(255 * bg.r)) + std::abs(int(b.rgba[i + 1]) - int(255 * bg.g)) > 24;
        CHECK(lit > 20000);
    }
}
