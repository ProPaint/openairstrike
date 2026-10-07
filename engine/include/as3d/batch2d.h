// Neutral vertex generation of the 2D layer (as3d/ui.h): the quad list becomes triangle lists
// grouped in runs of equal draw state. The GLES backend draws them with its shader; the PICA
// backend (3DS) will draw the same data with fixed-function combiners and, since it has no
// fwidth, with the circles and rings tessellated on the CPU (Batch2DOptions::tessellateShapes).
// Pure CPU code, testable without a GL context.
#pragma once

#include <cstdint>
#include <vector>

#include "as3d/ui.h"

namespace as3d::ui {

// How the second texture of a quad (Quad::texture2) is combined with the first stage.
// Values 1..3 are Quad::combine2; any other non-zero Quad::combine2 becomes Replace.
enum class CombineMode : uint8_t { None = 0, Blend = 1, Add = 2, Modulate = 3, Replace = 4 };

// One vertex of a triangle list, in framebuffer pixels (origin top-left, y down).
struct Vertex2D {
    float x = 0, y = 0;            // framebuffer pixels
    float u = 0, v = 0;            // first texture (v = 0 at the top); 0.5/0.5 for lines
    float u2 = 0, v2 = 0;          // second texture, meaningful when the run has tex2
    float lx = 0, ly = 0;          // position in the inscribed ellipse, in radii (-1..1); shape runs
    float inner = 0, feather = 0;  // ellipse hole radius and feather, fractions of the radius
    float color[4] = {1, 1, 1, 1}; // effectiveColor(), full precision (the GLES backend uploads this)
    uint8_t rgba[4] = {255, 255, 255, 255}; // the same rounded to 8 bits, clamped to 0..1
};

// A range of vertices drawn with one state. Consecutive quads with an equal
// (blend, tex, tex2, combine2, shape) are merged into one run.
struct Run2D {
    Blend blend = Blend::Alpha;
    const Texture2D* tex = nullptr;  // null = untextured (the backend binds a white texel)
    const Texture2D* tex2 = nullptr; // null unless combine2 != None
    CombineMode combine2 = CombineMode::None;
    int first = 0, count = 0;        // vertices (triangle list)
    bool shape = false;              // the shader must shape these vertices (lx, ly, inner,
                                     // feather); never set when the shapes are tessellated
};

struct Batch2DOptions {
    bool tessellateShapes = false; // Ellipse quads become a CPU-tessellated fan/ring strip plus a feather band
    int circleSegments = 48;       // segments around the circle when tessellating (>= 8)
    bool halfPixelShift = false;   // Renderer2D::setHalfPixelShift
};

// Colour sent to the GPU: additive quads use alpha as intensity (rgb premultiplied, alpha 1),
// Opaque forces alpha 1, Filter fades towards white (no change); Alpha is unchanged.
Color effectiveColor(const Quad& q);

// Framebuffer corners of a quad: top-left, bottom-left, bottom-right, top-right (a line: the
// segment's two sides, one pixel thick). Rotation about the centre and the optional half-pixel
// shift included. Renderer2D::quadCorners forwards here.
void quadCornersFb(const Quad& q, const Mapping& m, bool halfPixelShift, float px[4], float py[4]);

// Clears `verts` and `runs`, then appends the geometry of `quads` in order. A rectangle is 6
// vertices (triangles 0,1,2 and 0,2,3 of quadCornersFb). Ellipse quads:
//  - default: the same 6 vertices with lx/ly/inner/feather set, run.shape = true;
//  - tessellated: knots at normalised radii (hole edge, hole edge + aa, 1 - aa, 1; a fan from
//    the centre when the first knot is 0), vertex coverage
//      cov(r) = (1 - smoothstep(1-aa, 1, r)) * smoothstep(inner-aa, inner, r),
//      aa = 1.27 px / radius_px + feather
//    (1.27 = mean of the shader's fwidth over a circle). The vertex alpha (for Add, the rgb) is
//    scaled by cov, linear between knots: the edge falls from 1 to 0 over about one pixel where
//    the shader's anti-aliasing does. Texture coordinates are interpolated over the quad. The
//    run has shape = false, so shapes merge with plain quads of the same state.
void buildBatch2D(const std::vector<Quad>& quads, const Mapping& m, std::vector<Vertex2D>& verts,
                  std::vector<Run2D>& runs, const Batch2DOptions& opts = {});

} // namespace as3d::ui
