// Coordinate conventions of the PICA backend, as plain math (header only, platform neutral,
// desktop-tested in apps/tests/pica_core_test.cpp).
//
// 1. The rotated framebuffer. The 3DS screens are portrait panels: a top render target is
//    created 240 wide x 400 tall (C3D_RenderTargetCreate(240, 400, ...)), the bottom one
//    240 x 320, and citro3d's *Tilt projections rotate the picture a quarter turn so it reads
//    the right way up. In clip space (citro3d mtx_persptilt.c, mtx_orthotilt.c):
//        x_pica = y_gl,  y_pica = -x_gl,  z_pica = (z_gl - w) / 2,  w_pica = w
//    (z: GL's [-w, w] becomes the PICA's [-w, 0], -w at the near plane). picaTiltMatrix() is
//    that map, so tilt * P_gl is the PICA projection of any GL projection P_gl.
//    Framebuffer coordinates follow: fb x (0..240) is the screen's vertical axis, = the GL
//    window y (from the bottom); fb y (0..W) is the horizontal axis reversed, = W - GL window x.
//    A GL rectangle (x, y from the bottom-left, w, h) on a screen W pixels wide is the fb
//    rectangle (y, W - x - w, h, w): picaFramebufferRect. setViewport/setScissor
//    (render_state_pica.cpp) take GL rectangles and convert with it.
//
// 2. The eyes. citro3d's Mtx_PerspStereoTilt(fovy, aspect, n, f, iod, screen, false) is
//        row 0: (0, F, 0, 0)            F = 1 / tan(fovy / 2),  A = F / aspect
//        row 1: (-A, 0, A iod / (2 screen), iod / 2)
//        row 2: (0, 0, n / (n - f), n f / (n - f))
//        row 3: (0, 0, -1, 0)
//    (mtx_perspstereotilt.c). The engine's eye (as3d/world_render.h) is
//        eyeProjection(view, {e, D}) * eyeViewMatrix(view, {e, D})
//          = P(B = -A e / D) * T(-e, 0, 0) * view.view
//    and tilt * P(B) * T(-e) has row 1 = (-A, 0, A e / D, A e). The two are equal for
//        iod = 2 A e,   screen = A D
//    (NOT iod = 2 e, screen = D: citro3d's translation term iod/2 is in clip units, not eye
//    units; with A = 1.04 at 60 degrees on the top screen the naive mapping is 4 % off).
//    citro3d puts the eye translation into the projection, so the PICA projection pairs with
//    the central view.view. focalDistance 0 (parallel eyes) is screen = infinity: shift 0;
//    picaStereoParams passes kParallelScreen. eyeOffset 0 is Mtx_PerspTilt itself.
#pragma once

#include <cmath>

#include "as3d/math.h"
#include "as3d/world_render.h"

namespace as3d::pica {

struct FbRect {
    int x = 0, y = 0, w = 0, h = 0;
};

// A GL window rectangle (origin bottom-left) on a screen `screenWidth` pixels wide (400 top,
// 320 bottom), as the rotated framebuffer's rectangle.
inline FbRect picaFramebufferRect(int screenWidth, int x, int y, int w, int h) {
    return FbRect{y, screenWidth - x - w, h, w};
}

// GL clip space -> PICA clip space of a tilted target (see the header comment).
inline Mat4 picaTiltMatrix() {
    Mat4 t;
    for (float& v : t.m) v = 0.0f;
    t.at(1, 0) = 1.0f;  // x' = y
    t.at(0, 1) = -1.0f; // y' = -x
    t.at(2, 2) = 0.5f;  // z' = (z - w) / 2
    t.at(3, 2) = -0.5f;
    t.at(3, 3) = 1.0f;  // w' = w
    return t;
}

constexpr float kParallelScreen = 1e30f;

// The arguments of Mtx_PerspStereoTilt (or Mtx_PerspTilt when !stereo) for one eye.
struct PicaStereoParams {
    float fovYRadians = 0, aspect = 1, nearPlane = 1, farPlane = 2;
    bool stereo = false;
    float iod = 0, screen = kParallelScreen;
};

inline PicaStereoParams picaStereoParams(const WorldView& view, const EyeView& eye, float aspect) {
    PicaStereoParams p;
    p.fovYRadians = degToRad(view.fovY);
    p.aspect = aspect;
    p.nearPlane = view.nearPlane;
    p.farPlane = view.farPlane;
    if (eye.eyeOffset == 0.0f) return p;
    const float a = 1.0f / (std::tan(p.fovYRadians * 0.5f) * aspect);
    p.stereo = true;
    p.iod = 2.0f * a * eye.eyeOffset;
    p.screen = eye.focalDistance > 0.0f ? a * eye.focalDistance : kParallelScreen;
    return p;
}

// What picaEyeProjection (eye_projection_pica.cpp, citro3d) must return, from the engine's own
// GL eye: tilt * eyeProjection * T(-eyeOffset, 0, 0). Use with view.view.
inline Mat4 picaEyeProjectionReference(const WorldView& view, const EyeView& eye, float aspect) {
    return picaTiltMatrix() * eyeProjection(view, eye, aspect) * translation(Vec3{-eye.eyeOffset, 0.0f, 0.0f});
}

} // namespace as3d::pica
