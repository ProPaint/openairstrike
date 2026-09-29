// Shared plumbing for the model/object/sheet/top viewer commands (WP-30/33).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "as3d/image.h"
#include "as3d/object_tree.h"
#include "as3d/platform.h"
#include "as3d/scene.h"

#include "as3d/defs.h"

namespace viewer {

struct ViewOpts {
    int width = 640;
    int height = 480;
    float yaw = 35.0f;
    float pitch = 25.0f;
    float dist = 0.75f;   // multiplier on the default framing distance
    bool wire = false;
    bool tags = false;
    bool night = false;
    bool list = false;
    bool markers = false;   // small markers for particle-system nodes
    std::string out;
    std::string skin;
    int cols = 0;
    int cellW = 200;
    int cellH = 180;
    // Top view (placeholder camera; the real one is specified elsewhere).
    float topTiltDeg = 20.0f;
    float topHeightFactor = 2.2f;
};

// Parses the common options; other tokens go to `positional`. False on a bad value.
bool parseViewOpts(int argc, char** argv, ViewOpts& opts, std::vector<std::string>& positional);

struct SceneDesc;

// Headless GL context, game data, definitions, cache and renderer bundled together.
class ViewerContext {
public:
    bool init(int w, int h, std::string& error);
    as3d::Vfs vfs;
    as3d::DefDatabase db;
    std::unique_ptr<as3d::ResourceCache> cache;
    as3d::MeshRenderer renderer;
    as3d::SceneLighting lighting;
    std::unique_ptr<as3d::GraphicsContext> gl;
    // Renders into a w x h offscreen target and reads it back.
    bool renderToImage(int w, int h, as3d::Image& out, const SceneDesc& scene, const ViewOpts& opts,
                       bool top, std::string& error);
};

struct DrawPart {
    const as3d::GpuMesh* mesh = nullptr;
    as3d::Material material;
    as3d::Mat4 model = as3d::Mat4::identity();
    as3d::Vec4 colour{1, 1, 1, 1};
};
struct MarkerPoint {
    as3d::Vec3 pos;
    as3d::Vec3 colour;
    float size = 1.0f;
};
struct SceneDesc {
    std::vector<DrawPart> parts;
    std::vector<MarkerPoint> tagMarkers;   // drawn as axis crosses when --tags
    std::vector<MarkerPoint> psMarkers;    // particle systems, when --markers
    as3d::Vec3 boundsMin, boundsMax;
    bool valid = false;
};

// Builds the parts of one model (with optional skin override).
bool buildModelScene(ViewerContext& ctx, const std::string& mdlPath, const std::string& skin, SceneDesc& scene,
                     std::string& error);
// Builds the full attachment hierarchy of one object definition.
bool buildObjectScene(ViewerContext& ctx, const std::string& objectName, bool night, SceneDesc& scene,
                      as3d::ObjectTree* treeOut, std::string& error);

// 5x7 label drawing into an Image (uppercase ASCII only, own embedded font).
void drawText(as3d::Image& img, int x, int y, const std::string& text, const unsigned char rgb[3], int scale = 1);
int textWidth(const std::string& text, int scale = 1);

} // namespace viewer
