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

// Texture memory/format of the probe (docs/3ds-port.md, M1.0). The spike uses LinearRGBA8.
enum class TexMode {
    LinearRGBA8,   // RGBA8 in linear memory (read by the GPU over the bus)
    VramRGBA8,     // RGBA8 in VRAM
    VramRGB565,    // 16-bit texels in VRAM
    VramRGB565Mip, // RGB565 in VRAM with a CPU-generated mip chain, trilinear filtering
};

// What renderFrame draws besides the scene. All off = the spike.
struct FrameOptions {
    int terrainPasses = 1; // the terrain is submitted this many times per eye
    int blendQuads = 0;    // full-screen alpha-blended textured quads over the scene
    float iod = 0.0f;      // stereo eye separation (used when the right target exists)
};

// Creates the top-screen target and the shader. Call after gfxInitDefault and C3D_Init.
bool renderInit(std::string* error);
// Builds the terrain chunks and the helicopter mesh of the world's current mission, and
// uploads the textures. Writes what it did to `report`.
bool renderLoadScene(as3d::Vfs& vfs, const as3d::World& world, std::string* report);
// Re-uploads the scene's textures in `mode` (frees the previous ones). Needs renderLoadScene.
bool renderSetTextures(TexMode mode, std::string* report);
// Adds (or removes) the second top target (GFX_RIGHT, gfxSet3D) and the bottom screen target
// (a 320x240 textured quad). Without the right target the projection is the mono one.
bool renderSetTargets(bool right, bool bottom);
void renderSetOptions(const FrameOptions& opts);
// Draws one frame (between C3D_FrameBegin and C3D_FrameEnd, done by the caller).
DrawStats renderFrame(const as3d::World& world);
void renderShutdown();

} // namespace spike
