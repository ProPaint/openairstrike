// The game's scrolling camera (docs/spec/hmap.md "Camera", docs/spec/engine-behaviour.md
// section 9). Produces view/projection matrices for the engine's Z-up world
// (x right, y = scroll direction, z up) using as3d/math.h's Y-up GL conventions.
#pragma once

#include "as3d/math.h"
#include "as3d/vec.h"

namespace as3d {

struct CameraPreset {
    float fovDegrees;
    float pitchDegrees; // measured from straight down, tilted toward +y (negative in the table)
    float height;       // camera z
    float yOffset;      // camera y = mapPos + yOffset
};

// Modes 0..3; mode 1 is the default.
constexpr CameraPreset kGameCameraPresets[4] = {
    {60.0f, -35.0f, 270.0f, 0.0f},
    {60.0f, -45.0f, 270.0f, 0.0f},
    {60.0f, -50.0f, 270.0f, 0.0f},
    {70.0f, -15.0f, 370.0f, 100.0f},
};
constexpr float kGameCameraNear = 4.0f;
constexpr float kGameCameraMinX = 578.0f;
constexpr float kGameCameraMaxX = 702.0f;

struct GameCamera {
    Vec3 position;
    Mat4 view;
    Mat4 projection;
    float nearPlane = kGameCameraNear;
    float farPlane = 2000.0f;
};

// mode is clamped to 0..3, cameraX to [578, 702]. far = max(fogEnd, 1000) with fog, else 2000.
GameCamera computeGameCamera(int mode, float mapPos, float cameraX, float aspect, bool hasFog, float fogEnd);
// The same for a game's own preset and x limits (GameRules); the caller has clamped the mode.
GameCamera computeGameCamera(const CameraPreset& preset, float minX, float maxX, float mapPos, float cameraX,
                             float aspect, bool hasFog, float fogEnd);

} // namespace as3d
