// Loading of the 2D textures. See as3d/ui.h.
#include "as3d/hud_layout.h"
#include "as3d/image.h"
#include "as3d/ui.h"

namespace as3d::ui {

namespace {

bool loadOne(Vfs& vfs, const std::string& path, Texture2D& out, std::string* error, Wrap wrap = Wrap::ClampToEdge) {
    Blob blob;
    Image image;
    if (!vfs.read(path, blob) || !decodeTga(blob.data(), blob.size(), image)) {
        if (error && error->empty()) *error = "cannot load " + path;
        return false;
    }
    TextureOptions o;
    o.minFilter = o.magFilter = Filter::Linear;
    o.wrapS = o.wrapT = wrap;
    out.create(image, o);
    return true;
}

} // namespace

bool UiAssets::load(Vfs& vfs, std::string* error, GameId g) {
    vfs_ = &vfs;
    game = g;
    missing = 0;
    fontLoaded = loadOne(vfs, "gfx\\ui\\font.tga", font, error);
    if (!fontLoaded) return false;
    // The HUD's atlases as the game's layout names them (a layout without one: not loaded).
    const HudLayout& L = hudLayout();
    struct Item { const char* path; Texture2D* tex; Wrap wrap; };
    const Item items_[] = {
        {"gfx\\ui\\font_alpha.tga", &fontAlpha, Wrap::ClampToEdge},
        {L.barAtlas, &mainbar, Wrap::ClampToEdge},
        {L.lifeTexture, &life, Wrap::ClampToEdge},
        {L.weaponsAtlas, &weapons, Wrap::ClampToEdge},
        {L.missilesAtlas, &missiles, Wrap::ClampToEdge},
        {L.itemsAtlas, &items, Wrap::ClampToEdge},
        {"menu\\cursor_1.tga", &cursor1, Wrap::ClampToEdge},
        {"menu\\cursor_2.tga", &cursor2, Wrap::ClampToEdge},
        {L.mouseCursor, &mcCursor, Wrap::ClampToEdge},
        {L.hint == HintStyle::SequelPanel ? L.panelAtlas : nullptr, &panel, Wrap::ClampToEdge},
        // The panel's static fill samples it at random offsets over more than one width.
        {L.hint == HintStyle::SequelPanel ? L.panelNoise : nullptr, &panelNoise, Wrap::Repeat},
    };
    for (const Item& it : items_)
        if (it.path && !loadOne(vfs, it.path, *it.tex, error, it.wrap)) missing++;
    return true;
}

const HudLayout& UiAssets::hudLayout() const { return ui::hudLayout(game); }

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
