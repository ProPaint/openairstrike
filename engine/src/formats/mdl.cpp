// Static model (.mdl) loader. See docs/spec/mdl.md for the format and the reverse-
// engineering evidence behind every choice below. Reference implementation:
// tools/ref/mdl.py, which every shipped .mdl is cross-checked against (see
// apps/tests/mdl_test.cpp).
//
// No exceptions, no RTTI (this module is built with -fno-exceptions -fno-rtti):
// malformed input is reported by returning false / setting *error, never by throwing
// or asserting, and every buffer access is bounds-checked before it happens. The 2
// shipped files that don't fit the format exactly (docs/spec/mdl.md, "Files that do
// not fit") are handled by loading whatever whole array elements actually fit and
// dropping anything built on top of missing data, rather than failing outright.
#include "as3d/model.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "as3d/core.h"

namespace as3d {

namespace {

constexpr size_t kHeaderSize = 0x78;          // header + bounding box; arrays start here
constexpr size_t kTexturePathOffset = 0x0C;
constexpr size_t kTexturePathSize = 0x40;     // 64 bytes, NUL-terminated when it fits
constexpr size_t kCountsOffset = 0x4C;        // 5x u32: verts, uvs, faces, normals, tags
constexpr size_t kBBoxOffset = 0x60;          // 6x f32: min xyz, max xyz

constexpr size_t kVertexSize = 12;   // 3x f32
constexpr size_t kUvSize = 8;        // 2x f32
constexpr size_t kFaceSize = 12;     // 3x u16 position index + 3x u16 uv index
constexpr size_t kNormalSize = 12;   // 3x f32
constexpr size_t kTagSize = 56;      // 32-byte name + 3x f32 position + 3x f32 direction
constexpr size_t kTagNameSize = 32;

// See docs/spec/mdl.md, "Texture V orientation": v1.70 uploads TGA pixel rows in file
// order (unflipped) and submits a model's stored UVs to glTexCoord2f unmodified, so
// v=0 samples the *bottom* of the picture there (VERIFIED-CODE, WP-14b). This engine's
// decodeTga always produces a top-down as3d::Image (v=0 = the TOP of the picture;
// see engine/include/as3d/image.h), so this flip is required for a model to look the
// same here as it did in the original. Only ever change this if that image convention
// changes.
constexpr bool kFlipV = true;

// See docs/spec/mdl.md, "Normals": v1.70 recomputes smooth (per-vertex) normals at
// load time as a plain, unnormalized sum of adjacent face cross products, and never
// renormalizes them before lighting (GL_NORMALIZE is disabled at startup and never
// re-enabled anywhere in the executable -- VERIFIED-CODE, WP-14b). This engine
// reproduces the same recompute (so direction matches bit-for-bit, modulo float
// rounding) but then normalizes the result, because nothing downstream of this loader
// should have to know a "normal" might legitimately be well under unit length. Set to
// false to reproduce v1.70's raw output exactly instead.
constexpr bool kNormalizeRecomputedNormals = true;

u32 readU32LE(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

u16 readU16LE(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }

float readF32LE(const u8* p) {
    u32 v = readU32LE(p);
    float f;
    std::memcpy(&f, &v, sizeof f);
    return f;
}

// Reads a NUL-terminated string out of a fixed-size field. If the field is completely
// full (no NUL found), returns the whole field -- matching the 11/442 shipped files
// whose texture path fills all 64 bytes with no room for a terminator.
std::string readFixedField(const u8* data, size_t fieldSize) {
    size_t n = 0;
    while (n < fieldSize && data[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(data), n);
}

Vec3 vsub(const Vec3& a, const Vec3& b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 vadd(const Vec3& a, const Vec3& b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 vcross(const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float vlength(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 vnormalized(const Vec3& v) {
    float l = vlength(v);
    if (!(l > 1e-12f)) return Vec3{0.0f, 0.0f, 0.0f};
    return Vec3{v.x / l, v.y / l, v.z / l};
}

void appendNote(std::string* error, const char* msg) {
    if (!error) return;
    if (!error->empty()) *error += "; ";
    *error += msg;
}

bool asciiEqualCI(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return i == a.size() && b[i] == '\0';
}

} // namespace

const ModelTag* ModelData::findTag(const char* name) const {
    if (!name) return nullptr;
    for (const ModelTag& t : tags) {
        if (asciiEqualCI(t.name, name)) return &t;
    }
    return nullptr;
}

bool loadModel(const u8* data, size_t size, ModelData& out, std::string* error) {
    out = ModelData{};
    if (error) error->clear();

    if (!data || size < kHeaderSize) {
        appendNote(error, "file too small for header");
        return false;
    }
    if (std::memcmp(data, "MDL!", 4) != 0) {
        appendNote(error, "bad magic");
        return false;
    }

    u32 version = readU32LE(data + 4);
    u32 smoothFlag = readU32LE(data + 8);
    if (version != 2) {
        // Version 3 is accepted by the original loader but this spec has not confirmed
        // its on-disk tag layout (docs/spec/mdl.md, "Open questions"): v1.70's own
        // R_LoadModel reads a version-3 tag array as a single flat block matching the
        // *runtime* 80-byte-per-tag shape, which does not match the 56-byte on-disk
        // shape this loader implements for version 2. Refuse rather than guess; no
        // shipped file is version 3.
        appendNote(error, "unsupported version (only 2 is implemented)");
        return false;
    }

    out.version = version;
    out.smoothNormals = (smoothFlag != 0);
    out.normalsPerFace = !out.smoothNormals;
    out.sourceTexturePath = readFixedField(data + kTexturePathOffset, kTexturePathSize);

    u32 nverts = readU32LE(data + kCountsOffset + 0);
    u32 nuvs = readU32LE(data + kCountsOffset + 4);
    u32 nfaces = readU32LE(data + kCountsOffset + 8);
    u32 nnormals = readU32LE(data + kCountsOffset + 12);
    u32 ntags = readU32LE(data + kCountsOffset + 16);

    out.boundsMin = Vec3{readF32LE(data + kBBoxOffset + 0), readF32LE(data + kBBoxOffset + 4),
                          readF32LE(data + kBBoxOffset + 8)};
    out.boundsMax = Vec3{readF32LE(data + kBBoxOffset + 12), readF32LE(data + kBBoxOffset + 16),
                          readF32LE(data + kBBoxOffset + 20)};

    // Clamp every array to however many *whole* elements actually fit in the file, in
    // on-disk order. As soon as one array comes up short, every array after it is
    // treated as absent (its on-disk offset, computed from the original declared
    // counts, cannot be trusted once the file has proven shorter than expected) -- see
    // docs/spec/mdl.md, "Files that do not fit", for the 2 shipped files this matters
    // for. A well-formed file (440 of 442 non-empty shipped files) never triggers this.
    size_t offset = kHeaderSize;
    size_t avail = size - kHeaderSize;
    bool truncated = false;
    bool anyClamped = false;

    auto clampCount = [&](u32 declared, size_t elemSize) -> u32 {
        if (truncated) return 0;
        size_t canFit = avail / elemSize;
        u32 got = declared;
        if (static_cast<size_t>(declared) > canFit) {
            got = static_cast<u32>(canFit);
            truncated = true;
            anyClamped = true;
        }
        offset += static_cast<size_t>(got) * elemSize;
        avail -= static_cast<size_t>(got) * elemSize;
        return got;
    };

    size_t vertsOffset = offset;
    u32 nvertsActual = clampCount(nverts, kVertexSize);
    size_t uvsOffset = offset;
    u32 nuvsActual = clampCount(nuvs, kUvSize);
    size_t facesOffset = offset;
    u32 nfacesActual = clampCount(nfaces, kFaceSize);
    size_t normalsOffset = offset;
    u32 nnormalsActual = clampCount(nnormals, kNormalSize);
    size_t tagsOffset = offset;
    u32 ntagsActual = clampCount(ntags, kTagSize);

    out.positions.reserve(nvertsActual);
    for (u32 i = 0; i < nvertsActual; ++i) {
        const u8* p = data + vertsOffset + static_cast<size_t>(i) * kVertexSize;
        out.positions.push_back(Vec3{readF32LE(p), readF32LE(p + 4), readF32LE(p + 8)});
    }

    out.uvs.reserve(nuvsActual);
    for (u32 i = 0; i < nuvsActual; ++i) {
        const u8* p = data + uvsOffset + static_cast<size_t>(i) * kUvSize;
        float u = readF32LE(p);
        float v = readF32LE(p + 4);
        if (kFlipV) v = 1.0f - v;
        out.uvs.push_back(Vec2{u, v});
    }

    // For flat shading, a face's normal is its own index into the on-disk normal
    // array -- there is no explicit per-face normal index field, it's purely
    // positional (docs/spec/mdl.md, "Normals"). So the on-disk normal array is read
    // here, indexed by *original* (pre-drop) face position, before any invalid faces
    // are dropped below; that keeps `rawNormals[i]` correct for surviving face `i`
    // regardless of drops or of the normal array itself having been clamped short.
    std::vector<Vec3> rawNormals;
    if (!out.smoothNormals) {
        rawNormals.reserve(nnormalsActual);
        for (u32 i = 0; i < nnormalsActual; ++i) {
            const u8* p = data + normalsOffset + static_cast<size_t>(i) * kNormalSize;
            rawNormals.push_back(Vec3{readF32LE(p), readF32LE(p + 4), readF32LE(p + 8)});
        }
    }

    // Faces with an out-of-range position or UV index are dropped (docs/spec/mdl.md
    // says this can only happen on the 2 known-corrupt files, but this loader stays
    // safe for arbitrary/fuzzed input too). Faces are otherwise kept in file order;
    // for flat shading, `out.normals` is built in lockstep so `out.normals[j]` stays
    // the correct original normal for `out.faces[j]` even though `j` no longer equals
    // that face's original on-disk index once earlier faces have been dropped.
    out.faces.reserve(nfacesActual);
    if (!out.smoothNormals) out.normals.reserve(nfacesActual);
    u32 droppedFaces = 0;
    for (u32 i = 0; i < nfacesActual; ++i) {
        const u8* p = data + facesOffset + static_cast<size_t>(i) * kFaceSize;
        Face f;
        f.v[0] = readU16LE(p + 0);
        f.v[1] = readU16LE(p + 2);
        f.v[2] = readU16LE(p + 4);
        f.uv[0] = readU16LE(p + 6);
        f.uv[1] = readU16LE(p + 8);
        f.uv[2] = readU16LE(p + 10);
        bool valid = f.v[0] < out.positions.size() && f.v[1] < out.positions.size() &&
                     f.v[2] < out.positions.size() && f.uv[0] < out.uvs.size() &&
                     f.uv[1] < out.uvs.size() && f.uv[2] < out.uvs.size();
        if (!valid) {
            ++droppedFaces;
            continue;
        }
        out.faces.push_back(f);
        if (!out.smoothNormals) {
            out.normals.push_back(i < rawNormals.size() ? rawNormals[i] : Vec3{0.0f, 0.0f, 0.0f});
        }
    }
    if (droppedFaces > 0) {
        appendNote(error, "dropped face(s) with an out-of-range index");
    }
    if (!out.smoothNormals && !rawNormals.empty() && rawNormals.size() != nfacesActual) {
        appendNote(error, "flat normal count did not match declared face count");
    }

    if (out.smoothNormals) {
        // v1.70 itself discards whatever is on disk here and recomputes from geometry
        // (docs/spec/mdl.md, "Normals" -- exact formula below), so this loader doesn't
        // even need the on-disk bytes to be intact; only positions/faces matter. The
        // declared on-disk normal array was still skipped over above so that the tag
        // array (if any) is found at the right offset for a well-formed file.
        std::vector<Vec3> accum(out.positions.size(), Vec3{0.0f, 0.0f, 0.0f});
        for (const Face& f : out.faces) {
            const Vec3& v0 = out.positions[f.v[0]];
            const Vec3& v1 = out.positions[f.v[1]];
            const Vec3& v2 = out.positions[f.v[2]];
            Vec3 fn = vcross(vsub(v1, v0), vsub(v2, v0));
            accum[f.v[0]] = vadd(accum[f.v[0]], fn);
            accum[f.v[1]] = vadd(accum[f.v[1]], fn);
            accum[f.v[2]] = vadd(accum[f.v[2]], fn);
        }
        if (kNormalizeRecomputedNormals) {
            for (Vec3& n : accum) n = vnormalized(n);
        }
        out.normals = std::move(accum);
        out.normalsPerFace = false;
    } else {
        out.normalsPerFace = true;
    }

    out.tags.reserve(ntagsActual);
    for (u32 i = 0; i < ntagsActual; ++i) {
        const u8* p = data + tagsOffset + static_cast<size_t>(i) * kTagSize;
        ModelTag tag;
        tag.name = readFixedField(p, kTagNameSize);
        tag.position = Vec3{readF32LE(p + kTagNameSize), readF32LE(p + kTagNameSize + 4),
                             readF32LE(p + kTagNameSize + 8)};
        tag.direction = Vec3{readF32LE(p + kTagNameSize + 12), readF32LE(p + kTagNameSize + 16),
                              readF32LE(p + kTagNameSize + 20)};
        out.tags.push_back(std::move(tag));
    }

    if (anyClamped) {
        appendNote(error, "file shorter than its own header counts claim; some data was dropped");
    }
    return true;
}

namespace {

struct CornerKey {
    u32 posIndex;
    u32 uvIndex;
    u32 normalIndex; // vertex index (smooth) or face index (flat)

    bool operator==(const CornerKey& o) const {
        return posIndex == o.posIndex && uvIndex == o.uvIndex && normalIndex == o.normalIndex;
    }
};

struct CornerKeyHash {
    size_t operator()(const CornerKey& k) const {
        std::uint64_t h = k.posIndex;
        h = h * 1099511628211ull ^ k.uvIndex;
        h = h * 1099511628211ull ^ k.normalIndex;
        return static_cast<size_t>(h);
    }
};

} // namespace

bool buildRenderMesh(const ModelData& model, std::vector<RenderVertex>& vertices,
                      std::vector<u16>& indices) {
    vertices.clear();
    indices.clear();

    std::unordered_map<CornerKey, u16, CornerKeyHash> seen;
    seen.reserve(model.faces.size() * 3);

    for (size_t fi = 0; fi < model.faces.size(); ++fi) {
        const Face& f = model.faces[fi];
        for (int corner = 0; corner < 3; ++corner) {
            u32 posIndex = f.v[corner];
            u32 uvIndex = f.uv[corner];
            u32 normalIndex = model.normalsPerFace ? static_cast<u32>(fi) : posIndex;
            CornerKey key{posIndex, uvIndex, normalIndex};

            auto it = seen.find(key);
            u16 idx;
            if (it != seen.end()) {
                idx = it->second;
            } else {
                if (vertices.size() >= 65535) {
                    vertices.clear();
                    indices.clear();
                    return false;
                }
                RenderVertex rv;
                rv.position = (posIndex < model.positions.size()) ? model.positions[posIndex] : Vec3{};
                rv.uv = (uvIndex < model.uvs.size()) ? model.uvs[uvIndex] : Vec2{};
                rv.normal = (normalIndex < model.normals.size()) ? model.normals[normalIndex] : Vec3{};
                idx = static_cast<u16>(vertices.size());
                vertices.push_back(rv);
                seen.emplace(key, idx);
            }
            indices.push_back(idx);
        }
    }
    return true;
}

} // namespace as3d
