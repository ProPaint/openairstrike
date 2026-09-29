// Ground decals (docs/spec/render-pipeline.md 3.4, 5.3): a rectangle on the ground is cut out of
// the terrain triangles and drawn as a textured decal that follows the terrain height. This
// header holds the geometry shared by ground marks (craters, scorch marks, light pools) and by
// shadows (as3d/shadow_render.h), and the mark renderer. Plain data: a Terrain, a rectangle
// and a texture; no game world. Needs a current GLES 3.0 context for the renderer.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "as3d/gfx.h"
#include "as3d/math.h"
#include "as3d/terrain.h"

namespace as3d {

// A rectangle in world XY, given by its minimum corner, two unit axes and its extents. Mark
// rectangles are axis aligned; planar shadows rotate with the entity's yaw.
struct GroundRect {
    Vec2 origin;          // corner where s = t = 0
    Vec2 axisX{1.0f, 0.0f};
    Vec2 axisY{0.0f, 1.0f};
    float sizeX = 0.0f;
    float sizeY = 0.0f;

    // The world rectangle [x0, x1] x [y0, y1].
    static GroundRect axisAligned(float x0, float y0, float x1, float y1);
    // The local rectangle [xmin, xmax] x [ymin, ymax] carried by an entity at `pivot` turned
    // by `yawDegrees` about Z (local +X to world (cos, sin)).
    static GroundRect rotated(const Vec2& pivot, float yawDegrees, float xmin, float ymin, float xmax, float ymax);

    // Rectangle coordinates (s, t) in [0, 1] inside the rectangle, t counted along axisY.
    Vec2 coords(float x, float y) const;
};

struct DecalVertex {
    float x, y, z;
    float u, v; // engine texture convention (v = 0 at the image top): v = 1 - t
};

// Appends the terrain triangles inside `rect`, clipped to it (Sutherland-Hodgman on the four
// edges, terrain heights kept), as a triangle list with texture coordinates u = s and
// v = 1 - t. The cells use the terrain renderer's triangulation, so the decal lies exactly on
// the drawn ground (draw it with a polygon offset).
void buildTerrainDecal(const Terrain& terrain, const GroundRect& rect, std::vector<DecalVertex>& out);

enum class DecalBlend { None, Alpha, Add, Filter };

struct DecalViewParams {
    Mat4 view;
    Mat4 projection;
    Vec3 fogColour;
    float fogStart = 1.0e9f; // linear fog; set fogEnd <= fogStart to disable
    float fogEnd = 1.0e9f;
};

struct GroundMarkDesc {
    Vec3 origin;      // entity origin at spawn; the mark never moves afterwards
    // The object's min/max rectangle relative to the origin (x and y of `min`/`max`).
    float minX = -50.0f, minY = -50.0f, maxX = 50.0f, maxY = 50.0f;
    const Texture2D* texture = nullptr; // null draws nothing
    DecalBlend blend = DecalBlend::Filter;
    Vec3 colour{1.0f, 1.0f, 1.0f};      // entity RGB, alpha is always 1
};

// TYPE_MARK draw pass (pass 4): depth test on, depth write off, polygon offset (-1, -1).
class GroundMarkRenderer {
public:
    // The mark list of the original holds 128 records.
    static constexpr size_t kMaxMarks = 128;

    GroundMarkRenderer();
    ~GroundMarkRenderer();
    GroundMarkRenderer(const GroundMarkRenderer&) = delete;
    GroundMarkRenderer& operator=(const GroundMarkRenderer&) = delete;

    bool init(std::string* error);

    // Builds the mark's geometry now (like the original's display list at spawn). Returns the
    // mark's index, or -1 when the list is full or the rectangle touches no terrain.
    int addMark(const Terrain& terrain, const GroundMarkDesc& desc);
    void clear();
    size_t size() const;
    // Triangle count of one mark (for tests); 0 for a bad index.
    size_t triangleCount(int index) const;

    void draw(const DecalViewParams& params);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace as3d
