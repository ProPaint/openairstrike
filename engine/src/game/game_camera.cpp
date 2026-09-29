#include "as3d/game_camera.h"

#include <algorithm>
#include <cmath>

namespace as3d {

GameCamera computeGameCamera(int mode, float mapPos, float cameraX, float aspect, bool hasFog, float fogEnd) {
    mode = std::min(std::max(mode, 0), 3);
    const CameraPreset& p = kGameCameraPresets[mode];
    GameCamera cam;
    cameraX = std::min(std::max(cameraX, kGameCameraMinX), kGameCameraMaxX);
    cam.position = {cameraX, mapPos + p.yOffset, p.height};
    cam.farPlane = hasFog ? std::max(fogEnd, 1000.0f) : 2000.0f;
    // Pitch is the angle from straight down, tilted toward +y. Forward = (0, sin t, -cos t);
    // up = right x forward with right = +x, i.e. (0, cos t, sin t). This reproduces the
    // spec facts: screen right = +x, screen up = +y, and the view centre hits z = 0 at
    // height * tan(t) ahead (270 for the default mode).
    float t = degToRad(-p.pitchDegrees);
    Vec3 fwd{0.0f, std::sin(t), -std::cos(t)};
    Vec3 up{0.0f, std::cos(t), std::sin(t)};
    cam.view = lookAt(cam.position, cam.position + fwd, up);
    cam.projection = perspective(p.fovDegrees, aspect, kGameCameraNear, cam.farPlane);
    return cam;
}

} // namespace as3d
