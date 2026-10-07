// citro3d part of the 3DS spike (question 3 of docs/3ds-spike.md): the mission's terrain
// around the camera and the player's helicopter mesh, both textured, on the top screen.
// Deliberately minimal: no water, particles, shadows, sprites, lighting or HUD.
#pragma once

#include <string>

namespace as3d {
class Vfs;
class World;
}

namespace spike {

struct DrawStats {
    int terrainChunks = 0; // chunks drawn this frame
    int vertices = 0;      // vertices submitted this frame (indexed)
};

// Creates the top-screen target and the shader. Call after gfxInitDefault and C3D_Init.
bool renderInit(std::string* error);
// Builds the terrain chunks and the helicopter mesh of the world's current mission, and
// uploads the textures. Writes what it did to `report`.
bool renderLoadScene(as3d::Vfs& vfs, const as3d::World& world, std::string* report);
// Draws one frame (between C3D_FrameBegin and C3D_FrameEnd, done by the caller).
DrawStats renderFrame(const as3d::World& world);
void renderShutdown();

} // namespace spike
