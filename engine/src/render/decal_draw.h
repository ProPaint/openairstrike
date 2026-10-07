// Shared decal drawing for ground marks and shadows (private to the render module). DecalBatch
// and DecalLook live in as3d/ground_marks.h; the neutral part (batches -> DrawRun) is
// buildDecalRuns in engine/src/render/decal_build.cpp.
#pragma once

#include <string>
#include <vector>

#include "as3d/gfx.h"
#include "as3d/ground_marks.h"

namespace as3d {

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
    std::vector<DrawRun> runs_;
};

} // namespace as3d
