// Loading of the 2D textures. See as3d/ui.h.
#include "as3d/image.h"
#include "as3d/ui.h"

namespace as3d::ui {

namespace {

bool loadOne(Vfs& vfs, const std::string& path, Texture2D& out, std::string* error) {
    Blob blob;
    Image image;
    if (!vfs.read(path, blob) || !decodeTga(blob.data(), blob.size(), image)) {
        if (error && error->empty()) *error = "cannot load " + path;
        return false;
    }
    TextureOptions o;
    o.minFilter = o.magFilter = Filter::Linear;
    o.wrapS = o.wrapT = Wrap::ClampToEdge;
    out.create(image, o);
    return true;
}

} // namespace

bool UiAssets::load(Vfs& vfs, std::string* error) {
    vfs_ = &vfs;
    missing = 0;
    fontLoaded = loadOne(vfs, "gfx\\ui\\font.tga", font, error);
    if (!fontLoaded) return false;
    struct Item { const char* path; Texture2D* tex; };
    const Item items_[] = {
        {"gfx\\ui\\font_alpha.tga", &fontAlpha}, {"gfx\\ui\\mainbar.tga", &mainbar},
        {"gfx\\ui\\life.tga", &life},             {"gfx\\ui\\weapons.tga", &weapons},
        {"gfx\\ui\\missiles.tga", &missiles},     {"gfx\\ui\\items.tga", &items},
        {"menu\\cursor_1.tga", &cursor1},         {"menu\\cursor_2.tga", &cursor2},
        {"gfx\\mc_cur.tga", &mcCursor},
    };
    for (const Item& it : items_)
        if (!loadOne(vfs, it.path, *it.tex, error)) missing++;
    return true;
}

Font UiAssets::uiFont() const {
    Font f;
    f.metrics = &FontMetrics::original();
    f.texture = fontLoaded ? &font : nullptr;
    f.alphaTexture = fontAlpha.valid() ? &fontAlpha : nullptr;
    return f;
}

const Texture2D* UiAssets::texture(std::string_view path) const {
    std::string key = normalizePath(std::string(path));
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second && it->second->valid() ? it->second.get() : nullptr;
    if (!vfs_) return nullptr;
    auto tex = std::make_unique<Texture2D>();
    if (!loadOne(*vfs_, key, *tex, nullptr)) {
        AS3D_WARN("ui: cannot load %s", key.c_str());
        cache_[key] = nullptr;
        return nullptr;
    }
    const Texture2D* p = tex.get();
    cache_[key] = std::move(tex);
    return p;
}

} // namespace as3d::ui
