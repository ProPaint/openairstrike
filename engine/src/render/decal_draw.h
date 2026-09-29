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
    float alpha = 1.0f; // marks: multiplies the texture alpha (the sequels' entity alpha)
};

enum class DecalLook {
    Mark,           // texture × (colour, alpha), the batch's blend
    ShadowAlpha,    // (0, 0, 0, texture alpha) alpha-blended: the first game's shadow (5.3)
    ShadowMultiply, // destination × (1 − texture alpha), fog towards white: the sequels' (delta 5.3)
};

class DecalDrawer {
public:
    bool init(std::string* error);
    bool valid() const { return program_.valid(); }
    void draw(const DecalViewParams& params, const std::vector<DecalVertex>& vertices,
              const std::vector<DecalBatch>& batches, DecalLook look);

private:
    ShaderProgram program_;
    VertexBuffer vbo_;
    VertexArray vao_;
    size_t capacity_ = 0;
};

} // namespace as3d
