// Static models (`.mdl`). See docs/spec/mdl.md. Owned by the orchestrator.
#pragma once

#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/vec.h"

namespace as3d {

// A named attachment point. `direction` is read from the file as-is but, per
// docs/spec/mdl.md ("The trailing 12 bytes"), v1.70 never actually uses it: every
// tag's rotation is hardcoded to identity at load time in the original engine. It is
// exposed here as real (if currently unused) authored data, not derived or invented.
struct ModelTag {
    std::string name;
    Vec3 position;
    Vec3 direction;
};

// One triangle: 3 indices into ModelData::positions and 3 indices into ModelData::uvs
// (not interleaved on disk, and not shared with each other -- a corner's position and
// UV are looked up independently, exactly as R_LoadModel's file format stores them).
struct Face {
    u16 v[3] = {0, 0, 0};
    u16 uv[3] = {0, 0, 0};
};

// A fully parsed .mdl file, in this engine's own conventions (see docs/spec/mdl.md):
//  - uvs have already been flipped (v' = 1 - v) to match this engine's top-down image
//    convention (as3d::Image, decodeTga) instead of v1.70's bottom-up one.
//  - normals have already been resolved into the shape the engine should use them in:
//    one entry per vertex (index with a face's own position index) when
//    `normalsPerFace` is false, one entry per face (index with the face's own index in
//    `faces`) when it is true. For smooth models this engine also normalizes the
//    recomputed normals (v1.70 does not -- see docs/spec/mdl.md "Normals"), controlled
//    by a constant in mdl.cpp.
struct ModelData {
    u32 version = 0;
    bool smoothNormals = false;   // header flag @ 0x08; mirrors !normalsPerFace
    std::string sourceTexturePath; // original authoring-machine path, from the header

    Vec3 boundsMin;
    Vec3 boundsMax;

    std::vector<Vec3> positions;
    std::vector<Vec2> uvs;
    std::vector<Face> faces;
    std::vector<Vec3> normals;
    bool normalsPerFace = false;  // see the struct comment above

    std::vector<ModelTag> tags;

    // Case-insensitive (ASCII) lookup by name. Returns nullptr if not found. The name
    // "origin" is never a real tag in the file -- callers that want to support it as an
    // implicit identity attachment point (see docs/spec/mdl.md, attach cross-check) must
    // special-case it themselves, the same way objects/*.obj's `attach` does.
    const ModelTag* findTag(const char* name) const;
};

// Parses a .mdl file already read into memory. Returns false and leaves `out` default-
// constructed (empty) only when nothing usable could be recovered at all (bad magic,
// unsupported version, or too short to even hold the header) -- see docs/spec/mdl.md
// "Files that do not fit" for the 2 shipped files that need the leniency described
// next. Never crashes or reads out of bounds on malformed input.
//
// `error`, if non-null, is always set to a diagnostic string when there is anything to
// report -- including on a *successful* but degraded load (truncated/corrupt input
// where whatever was still recoverable was kept: arrays are clamped to how many whole
// elements the file actually holds, and faces with an out-of-range position or UV
// index are dropped). Check `*error` for a non-empty string even when `loadModel`
// returns true. It is left untouched (not cleared) when there is nothing to report and
// `error` is non-null but was already empty; callers should clear it themselves if
// reusing the same string across calls.
bool loadModel(const u8* data, size_t size, ModelData& out, std::string* error = nullptr);

// One GL-ready vertex: a single index selects a consistent (position, normal, uv)
// corner, unlike the file's separately-indexed arrays.
struct RenderVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

// Builds a single-indexed triangle list from `model`, deduplicating corners that share
// the same position/uv/normal. Returns false (leaving both outputs empty) if more than
// 65535 unique vertices would be needed, since `indices` is 16-bit.
bool buildRenderMesh(const ModelData& model, std::vector<RenderVertex>& vertices,
                      std::vector<u16>& indices);

} // namespace as3d
