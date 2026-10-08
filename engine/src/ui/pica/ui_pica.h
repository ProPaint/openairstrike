// The PICA backend of the 2D layer (renderer2d_pica.cpp, 3DS only): counters and options.
//
// Renderer2D::flush() on the PICA draws the batch with the flat shaders: a pixel ortho matrix
// of the current frame target (pica::drawOn), per run the blend, texture unit 0 (+ unit 1 with
// tevCombine2 for a second texture, drawn with the Flat2 shader), C3D_DrawArrays. It draws
// into the target selected with pica::drawOn between frameBegin() and frameEnd(); all textures
// must exist already (UiAssets::load, UiAssets::texture) because creating one inside a frame
// would stall the GPU queue. The scissor is the caller's (as3d::setScissor, GL rectangles); the
// flush leaves it alone. It leaves blend off, depth test and write on, culling back, as the
// other PICA renderers do.
//
// Vertex cache: each Renderer2D keeps the converted vertices of its last few lists (6) in linear
// memory of their own; a flush of a list equal to one of them (same quads, mapping, half-pixel
// shift, circle segments, snapping) draws from it without building the batch again (the second
// eye, the touch band, a HUD that did not change). A changed list is converted into a slot no
// draw of the current frame uses (else into the frame arena).
#pragma once

#include <cstddef>

namespace as3d::ui {

struct Ui2dPicaStats {
    int flushes = 0;         // flush() calls that drew something
    int runs = 0;            // runs drawn (= C3D_DrawArrays calls)
    int flat2Runs = 0;       // of which two-texture runs (Flat2 shader)
    int vertices = 0;        // triangle-list vertices drawn
    size_t arenaBytes = 0;   // frame arena bytes taken by the vertex arrays
    int arenaFailures = 0;   // flushes skipped because the frame arena was full
    int missingTextures = 0; // runs whose texture was not loaded (drawn white)
    int cacheHits = 0;       // flushes that reused the vertices of an unchanged list (renderer2d_pica.cpp)
};

// Counters since the last reset (the app resets them once per frame, or reads the totals).
const Ui2dPicaStats& ui2dPicaStats();
void ui2dPicaResetStats();

// Pixel snapping (default on): the vertices of axis-aligned textured quads are rounded to whole
// target pixels, so glyphs and icons (12x6 px per font glyph at the 0.4 scale of a 320x240 or
// 400x240 target) are not smeared by bilinear filtering across fractional positions. Shapes,
// lines, rotated quads and untextured fills keep their exact coordinates.
void ui2dPicaSetSnap(bool on);

} // namespace as3d::ui
