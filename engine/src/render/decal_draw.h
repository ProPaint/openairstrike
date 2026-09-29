// Shared decal drawing for ground marks and shadows (private to the render module).
#pragma once

#include <string>
#include <vector>

#include "as3d/ground_marks.h"

namespace as3d {

struct DecalBatch {
    size_t first = 0; // first vertex
    size_t count = 0;
    const Texture2D* texture = nullptr;
    DecalBlend blend = DecalBlend::Alpha;
    Vec3 colour{1.0f, 1.0f, 1.0f};
};

class DecalDrawer {
public:
    bool init(std::string* error);
    bool valid() const { return program_.valid(); }
    // shadowMode: fragments output (0, 0, 0, texture alpha), the shadow look of section 5.3;
    // otherwise texture * colour (marks).
    void draw(const DecalViewParams& params, const std::vector<DecalVertex>& vertices,
              const std::vector<DecalBatch>& batches, bool shadowMode);

private:
    ShaderProgram program_;
    VertexBuffer vbo_;
    VertexArray vao_;
    size_t capacity_ = 0;
};

} // namespace as3d
