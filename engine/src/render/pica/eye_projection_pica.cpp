// picaEyeProjection (pica_frame.h): one eye's projection for a tilted target, by citro3d.
// The parameter mapping (iod = 2 A eyeOffset, screen = A focalDistance) and the proof that the
// result equals tilt * eyeProjection * T(-eyeOffset) are in pica_space.h; the desktop test
// apps/tests/pica_core_test.cpp checks the mapping against a transcription of citro3d's
// mtx_perspstereotilt.c, and apps/3ds/pica_test.cpp compares this function's matrices with
// picaEyeProjectionReference on the console and draws markers where the GL math puts them.
#include <citro3d.h>

#include "pica_frame.h"
#include "pica_space.h"
#include "shader_set.h"

namespace as3d::pica {

Mat4 picaEyeProjection(const WorldView& view, const EyeView& eye, float aspect) {
    const PicaStereoParams p = picaStereoParams(view, eye, aspect);
    C3D_Mtx m;
    if (p.stereo) {
        Mtx_PerspStereoTilt(&m, p.fovYRadians, p.aspect, p.nearPlane, p.farPlane, p.iod, p.screen, false);
    } else {
        Mtx_PerspTilt(&m, p.fovYRadians, p.aspect, p.nearPlane, p.farPlane, false);
    }
    return fromC3D(m);
}

} // namespace as3d::pica
