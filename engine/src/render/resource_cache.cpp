// Implements as3d::ResourceCache (as3d/scene.h): caches textures and GPU meshes by
// normalized game path, with visible/invisible placeholders on failure per the work
// package ("Missing or broken files yield a visible placeholder ... and one warning,
// never a crash").
#include "as3d/scene.h"

#include <cstddef>

#include "as3d/image.h"
#include "as3d/vfs.h"

namespace as3d {

namespace {

// A small, unmistakably-synthetic magenta/black checker -- the classic "missing
// texture" signal, distinct from any real game asset so it is easy to spot (and to
// grep for in the smoke test: "no magenta placeholder pixels except ...").
Image makeMagentaChecker() {
    Image img;
    img.width = 16;
    img.height = 16;
    img.hasAlpha = false;
    img.rgba.resize(static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * 4);
    for (int y = 0; y < img.height; y++) {
        for (int x = 0; x < img.width; x++) {
            bool a = ((x / 4) + (y / 4)) % 2 == 0;
            size_t idx = (static_cast<size_t>(y) * img.width + x) * 4;
            img.rgba[idx + 0] = a ? 255 : 0;
            img.rgba[idx + 1] = 0;
            img.rgba[idx + 2] = a ? 255 : 0;
            img.rgba[idx + 3] = 255;
        }
    }
    return img;
}

} // namespace

ResourceCache::ResourceCache(Vfs& vfs) : vfs_(vfs) {}

void ResourceCache::warnOnce(const std::string& key, const std::string& message) {
    if (warnedKeys_.insert(key).second) {
        warnings_.push_back(message);
        AS3D_WARN("%s", message.c_str());
    }
}

const Texture2D& ResourceCache::placeholderTexture() {
    if (!placeholderTexture_) {
        placeholderTexture_ = std::make_unique<Texture2D>();
        TextureOptions opts;
        opts.mipmaps = true;
        placeholderTexture_->create(makeMagentaChecker(), opts);
    }
    return *placeholderTexture_;
}

const Texture2D& ResourceCache::whiteTexture() {
    if (!whiteTexture_) {
        Image px;
        px.width = px.height = 1;
        px.rgba = {255, 255, 255, 255};
        whiteTexture_ = std::make_unique<Texture2D>();
        whiteTexture_->create(px, TextureOptions());
    }
    return *whiteTexture_;
}

bool ResourceCache::isPlaceholder(const Texture2D* texture) const {
    return texture && (texture == placeholderTexture_.get() || texture == whiteTexture_.get());
}

ResourceCache::LoadedTexture ResourceCache::texture(const std::string& gamePath, bool mipmaps) {
    if (gamePath.empty()) {
        // No skin/texture was ever declared (e.g. a light/mark/anchor ObjectDef with no
        // "skin" statement, see docs/spec/obj.md "type") -- not a load failure, so no
        // warning, just the placeholder.
        LoadedTexture loaded;
        loaded.texture = missingWhite_ ? &whiteTexture() : &placeholderTexture();
        loaded.hasAlpha = false;
        return loaded;
    }
    std::string key = normalizePath(gamePath);
    auto it = textures_.find(key);
    if (it != textures_.end()) {
        LoadedTexture loaded;
        loaded.texture = it->second.get();
        auto alphaIt = textureAlpha_.find(key);
        loaded.hasAlpha = alphaIt != textureAlpha_.end() && alphaIt->second;
        return loaded;
    }

    Blob blob;
    Image image;
    bool ok = vfs_.read(gamePath, blob) && decodeTga(blob.data(), blob.size(), image);
    if (!ok) {
        warnOnce(key, "ResourceCache: texture '" + gamePath + "' could not be loaded, using placeholder");
        LoadedTexture loaded;
        loaded.texture = missingWhite_ ? &whiteTexture() : &placeholderTexture();
        loaded.hasAlpha = false;
        return loaded;
    }

    auto tex = std::make_unique<Texture2D>();
    TextureOptions opts;
    opts.mipmaps = mipmaps; // trilinear (Linear min+mag with mipmaps) and repeat wrap
                             // are TextureOptions{}'s own defaults already.
    tex->create(image, opts);
    LoadedTexture loaded;
    loaded.texture = tex.get();
    loaded.hasAlpha = image.hasAlpha;
    textures_.emplace(key, std::move(tex));
    textureAlpha_.emplace(key, image.hasAlpha);
    return loaded;
}

const GpuMesh& ResourceCache::mesh(const std::string& gamePath) {
    std::string key = normalizePath(gamePath);
    auto it = meshes_.find(key);
    if (it != meshes_.end()) return *it->second;

    auto gpu = std::make_unique<GpuMesh>();

    Blob blob;
    std::string error;
    bool ok = vfs_.read(gamePath, blob) && loadModel(blob.data(), blob.size(), gpu->model, &error);
    if (ok) {
        std::vector<RenderVertex> verts;
        std::vector<u16> indices;
        ok = buildRenderMesh(gpu->model, verts, indices) && !indices.empty();
        if (ok) {
            gpu->vbo.upload(verts.data(), verts.size() * sizeof(RenderVertex));
            gpu->ibo.upload(indices.data(), indices.size() * sizeof(u16), IndexType::U16);
            VertexLayout layout;
            layout.strideBytes = sizeof(RenderVertex);
            layout.attribs = {
                {0, 3, offsetof(RenderVertex, position), false},
                {1, 3, offsetof(RenderVertex, normal), false},
                {2, 2, offsetof(RenderVertex, uv), false},
            };
            gpu->vao.create(gpu->vbo, layout, &gpu->ibo);
            gpu->indexCount = indices.size();
            gpu->valid = true;
        }
    }
    if (!ok) {
        warnOnce(key, "ResourceCache: mesh '" + gamePath + "' could not be loaded, using an empty placeholder");
        gpu->model = ModelData{};
        gpu->indexCount = 0;
        gpu->valid = false;
    }

    auto res = meshes_.emplace(key, std::move(gpu));
    return *res.first->second;
}

} // namespace as3d
