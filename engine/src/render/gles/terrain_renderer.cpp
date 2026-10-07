// Terrain renderer: generated base textures, chunked mesh, detail pass, tile overlays.
// See docs/spec/hmap.md "Terrain texturing" / "Tile overlays" and docs/graphics.md.
#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "as3d/terrain_render.h"

namespace as3d {

extern const char* const kTerrainVertexSrc;
extern const char* const kTerrainFragmentSrc;

namespace {

using Vertex = TerrainVertex;

Image placeholderImage() {
    Image img;
    img.width = img.height = 4;
    img.rgba.resize(4 * 4 * 4);
    for (int i = 0; i < 16; i++) {
        img.rgba[i * 4 + 0] = 255;
        img.rgba[i * 4 + 1] = 0;
        img.rgba[i * 4 + 2] = 255;
        img.rgba[i * 4 + 3] = 255;
    }
    return img;
}

// Loads a TGA through the vfs; on failure logs a warning and returns a magenta
// placeholder (and bumps *missing).
Image loadTexture(Vfs& vfs, const std::string& path, int* missing) {
    Blob blob;
    Image img;
    if (!vfs.read(path, blob) || !decodeTga(blob.data(), blob.size(), img)) {
        AS3D_WARN("terrain: missing texture '%s'", path.c_str());
        if (missing) (*missing)++;
        return placeholderImage();
    }
    return img;
}

VertexLayout terrainLayout() {
    VertexLayout l;
    l.strideBytes = sizeof(Vertex);
    l.attribs = {{0, 3, offsetof(Vertex, pos), false},
                 {1, 3, offsetof(Vertex, col), false},
                 {2, 2, offsetof(Vertex, uv0), false},
                 {3, 2, offsetof(Vertex, uv1), false},
                 {4, 3, offsetof(Vertex, nrm), false}};
    return l;
}

struct Chunk {
    size_t triOffset = 0, triCount = 0;   // in indices
    size_t wireOffset = 0, wireCount = 0; // in indices
};

struct TileBatch {
    int set = 0;
    Texture2D texture;
    VertexBuffer vbo;
    VertexArray vao;
};

} // namespace

// ---------------------------------------------------------------------------
// TerrainRenderer
// ---------------------------------------------------------------------------

struct TerrainRenderer::Impl {
    const Terrain* terrain = nullptr;
    ShaderProgram program;
    VertexBuffer vbo;
    IndexBuffer triIbo, wireIbo;
    VertexArray triVao, wireVao;
    TerrainGeometry geo;
    std::vector<Chunk> chunks; // GL index ranges, parallel to geo.chunks
    std::vector<Texture2D> baseTextures;
    Texture2D detail;
    std::vector<std::unique_ptr<TileBatch>> tiles;
};

TerrainRenderer::TerrainRenderer() : impl_(new Impl) {}
TerrainRenderer::~TerrainRenderer() = default;

int TerrainRenderer::chunkCount() const { return static_cast<int>(impl_->geo.chunks.size()); }

bool TerrainRenderer::chunkRows(int k, int& rowStart, int& rows) const {
    if (k < 0 || k >= chunkCount()) return false;
    rowStart = impl_->geo.chunks[static_cast<size_t>(k)].firstRow;
    rows = impl_->geo.chunks[static_cast<size_t>(k)].rows;
    return true;
}

bool TerrainRenderer::build(const Terrain& terrain, Vfs& vfs, std::string* error, const RenderRules& rules) {
    Impl& im = *impl_;
    im = Impl{};
    missing_ = 0;
    lastUpdated_ = 0;
    if (!terrain.level() || terrain.positions().empty()) {
        if (error) *error = "terrain not built";
        return false;
    }
    im.terrain = &terrain;
    const LevelData& level = *terrain.level();
    const TerrainStyle& def = terrain.style();
    const int H = terrain.height();

    if (!im.program.compile(kTerrainVertexSrc, kTerrainFragmentSrc, error)) return false;

    // Textures.
    Image src[4];
    for (int i = 0; i < 4; i++) {
        src[i] = loadTexture(vfs, def.texturesDir + "\\texture" + std::to_string(i + 1) + ".tga", &missing_);
    }
    int blocks = (H + kBaseTextureRows - 1) / kBaseTextureRows;
    im.baseTextures.resize(static_cast<size_t>(blocks));
    TextureOptions topts;
    topts.mipmaps = true;
    topts.minFilter = Filter::Linear;
    topts.magFilter = Filter::Linear;
    for (int b = 0; b < blocks; b++) {
        std::vector<u8> rgb;
        generateBaseTextureRgb(level, src, b, rgb);
        Image img;
        img.width = img.height = kBaseTextureSize;
        img.rgba.resize(static_cast<size_t>(kBaseTextureSize) * kBaseTextureSize * 4);
        for (size_t i = 0; i < static_cast<size_t>(kBaseTextureSize) * kBaseTextureSize; i++) {
            img.rgba[i * 4 + 0] = rgb[i * 3 + 0];
            img.rgba[i * 4 + 1] = rgb[i * 3 + 1];
            img.rgba[i * 4 + 2] = rgb[i * 3 + 2];
            img.rgba[i * 4 + 3] = 255;
        }
        im.baseTextures[static_cast<size_t>(b)].create(img, topts);
    }
    Image det = loadTexture(vfs, def.texturesDir + "\\detail.tga", &missing_);
    im.detail.create(det, topts);

    // Tile atlases the level uses (their sizes give the overlay UVs).
    std::map<int, std::pair<int, int>> atlasSizes;
    std::map<int, Image> atlasImages;
    for (int set : terrainTileSets(level)) {
        Image img = loadTexture(vfs, "tiles\\tiles" + std::to_string(set) + ".tga", &missing_);
        atlasSizes[set] = {img.width, img.height};
        atlasImages[set] = std::move(img);
    }

    // Mesh: the neutral builder's chunks (terrain_build.h) concatenated into one vertex buffer
    // and two index buffers.
    im.geo = buildTerrainGeometry(terrain, rules, atlasSizes);
    if (im.geo.vertexCount > 65535) {
        if (error) *error = "terrain mesh too large for 16-bit indices";
        return false;
    }
    std::vector<Vertex> verts;
    std::vector<u16> tri, wire;
    verts.reserve(im.geo.vertexCount);
    im.chunks.resize(im.geo.chunks.size());
    for (size_t k = 0; k < im.geo.chunks.size(); k++) {
        const TerrainChunkGeometry& cg = im.geo.chunks[k];
        Chunk& ch = im.chunks[k];
        const u16 base = static_cast<u16>(cg.baseVertex);
        verts.insert(verts.end(), cg.verts.begin(), cg.verts.end());
        ch.triOffset = tri.size();
        ch.wireOffset = wire.size();
        for (u16 i : cg.tris) tri.push_back(static_cast<u16>(base + i));
        for (u16 i : cg.wire) wire.push_back(static_cast<u16>(base + i));
        ch.triCount = tri.size() - ch.triOffset;
        ch.wireCount = wire.size() - ch.wireOffset;
    }
    im.vbo.upload(verts.data(), verts.size() * sizeof(Vertex));
    im.triIbo.upload(tri.data(), tri.size() * sizeof(u16), IndexType::U16);
    im.wireIbo.upload(wire.data(), wire.size() * sizeof(u16), IndexType::U16);
    im.triVao.create(im.vbo, terrainLayout(), &im.triIbo);
    im.wireVao.create(im.vbo, terrainLayout(), &im.wireIbo);

    // Tile overlays, one batch per atlas.
    for (const TerrainTileGeometry& tg : im.geo.tiles) {
        auto batch = std::make_unique<TileBatch>();
        batch->set = tg.set;
        TextureOptions to;
        to.mipmaps = false;
        to.wrapS = Wrap::Repeat;
        to.wrapT = Wrap::Repeat;
        batch->texture.create(atlasImages[tg.set], to);
        if (!tg.verts.empty()) {
            batch->vbo.upload(tg.verts.data(), tg.verts.size() * sizeof(Vertex));
            batch->vao.create(batch->vbo, terrainLayout());
        }
        im.tiles.push_back(std::move(batch));
    }
    return true;
}

void TerrainRenderer::update(const VertexRect* rects, size_t count, const TerrainGridView& grid) {
    Impl& im = *impl_;
    lastUpdated_ = 0;
    if (!im.terrain) return;
    const std::vector<int> changed = updateChunkHeights(im.geo, rects, count, grid);
    for (int k : changed) {
        const TerrainChunkGeometry& cg = im.geo.chunks[static_cast<size_t>(k)];
        glBindBuffer(GL_ARRAY_BUFFER, im.vbo.id());
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(cg.baseVertex * sizeof(Vertex)),
                        static_cast<GLsizeiptr>(cg.verts.size() * sizeof(Vertex)), cg.verts.data());
        // Its tile overlays.
        for (size_t b = 0; b < im.tiles.size(); ++b) {
            const auto range = im.geo.tiles[b].ranges[static_cast<size_t>(k)];
            if (range.second <= 0 || !im.tiles[b]->vbo.valid()) continue;
            glBindBuffer(GL_ARRAY_BUFFER, im.tiles[b]->vbo.id());
            glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(static_cast<size_t>(range.first) * sizeof(Vertex)),
                            static_cast<GLsizeiptr>(static_cast<size_t>(range.second) * sizeof(Vertex)),
                            &im.geo.tiles[b].verts[static_cast<size_t>(range.first)]);
        }
        ++lastUpdated_;
    }
}

void TerrainRenderer::render(const TerrainViewParams& params, const TerrainRenderOptions& options) {
    Impl& im = *impl_;
    if (!im.terrain || !im.program.valid()) return;
    const std::vector<int> visible = visibleTerrainChunks(im.geo, params.projection * params.view);
    lastVisible_ = static_cast<int>(visible.size());

    setCull(CullMode::Off);
    setDepth(true, true);
    setBlend(GlBlend::Off);

    im.program.use();
    im.program.setMat4("uView", params.view);
    im.program.setMat4("uProj", params.projection);
    im.program.setVec3("uFogColor", params.fogColour);
    im.program.setFloat("uFogStart", params.fogStart);
    im.program.setFloat("uFogEnd", params.fogEnd);
    im.program.setInt("uTex0", 0);
    im.program.setInt("uTex1", 1);
    im.program.setInt("uDetail", options.detail ? 1 : 0);
    im.program.setInt("uMode", 0);
    // Dynamic lights (spec 2.4): position + radius, colour, spot direction + cosines.
    {
        size_t n = std::min(params.lightCount, static_cast<size_t>(kMaxDynamicLights));
        float pr[kMaxDynamicLights * 4] = {}, colr[kMaxDynamicLights * 3] = {}, dir[kMaxDynamicLights * 4] = {};
        for (size_t i = 0; i < n; i++) {
            const DynamicLight& l = params.lights[i];
            pr[i * 4 + 0] = l.position.x; pr[i * 4 + 1] = l.position.y; pr[i * 4 + 2] = l.position.z;
            pr[i * 4 + 3] = l.radius;
            colr[i * 3 + 0] = l.colour.x; colr[i * 3 + 1] = l.colour.y; colr[i * 3 + 2] = l.colour.z;
            dir[i * 4 + 0] = l.direction.x; dir[i * 4 + 1] = l.direction.y; dir[i * 4 + 2] = l.direction.z;
            dir[i * 4 + 3] = l.spot ? 1.0f : 0.0f;
        }
        float cosv[kMaxDynamicLights * 2] = {};
        for (size_t i = 0; i < n; i++) { cosv[i * 2] = params.lights[i].cosOuter; cosv[i * 2 + 1] = params.lights[i].cosInner; }
        im.program.setInt("uLightCount", static_cast<int>(n));
        if (n > 0) {
            glUniform4fv(im.program.uniformLocation("uLightPosRadius"), static_cast<GLsizei>(n), pr);
            glUniform3fv(im.program.uniformLocation("uLightColour"), static_cast<GLsizei>(n), colr);
            glUniform4fv(im.program.uniformLocation("uLightDirSpot"), static_cast<GLsizei>(n), dir);
            glUniform2fv(im.program.uniformLocation("uLightCos"), static_cast<GLsizei>(n), cosv);
        }
    }
    im.detail.bind(1);

    const VertexArray& vao = options.wireframe ? im.wireVao : im.triVao;
    vao.bind();
    for (int k : visible) {
        const Chunk& ch = im.chunks[static_cast<size_t>(k)];
        im.baseTextures[static_cast<size_t>(im.geo.chunks[static_cast<size_t>(k)].block)].bind(0);
        if (options.wireframe) {
            glDrawElements(GL_LINES, static_cast<GLsizei>(ch.wireCount), GL_UNSIGNED_SHORT,
                           reinterpret_cast<const void*>(ch.wireOffset * sizeof(u16)));
        } else {
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(ch.triCount), GL_UNSIGNED_SHORT,
                           reinterpret_cast<const void*>(ch.triOffset * sizeof(u16)));
        }
    }

    if (options.tiles && !options.wireframe) {
        setBlend(GlBlend::AlphaBlend);
        setDepth(true, false);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -1.0f);
        im.program.setInt("uMode", 1);
        for (size_t b = 0; b < im.tiles.size(); ++b) {
            auto& batch = im.tiles[b];
            if (!batch->vao.valid()) continue;
            batch->texture.bind(0);
            batch->vao.bind();
            for (int k : visible) {
                auto range = im.geo.tiles[b].ranges[static_cast<size_t>(k)];
                if (range.second > 0) glDrawArrays(GL_TRIANGLES, range.first, range.second);
            }
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    setDepth(true, true);
    setBlend(GlBlend::Off);
}

} // namespace as3d
