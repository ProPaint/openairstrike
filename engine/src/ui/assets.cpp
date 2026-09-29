// Loading of the 2D textures. See as3d/ui.h.
#include "as3d/ui.h"

namespace as3d::ui {

namespace {

bool loadOne(Vfs& vfs, const char* path, Texture2D& out, std::string* error) {
    Blob blob;
    Image image;
    if (!vfs.read(path, blob) || !decodeTga(blob.data(), blob.size(), image)) {
        if (error && error->empty()) *error = std::string("cannot load ") + path;
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
    missing = 0;
    fontLoaded = loadOne(vfs, "gfx\\ui\\font.tga", font, error);
    if (!fontLoaded) return false;
    struct Item { const char* path; Texture2D* tex; };
    const Item items_[] = {
        {"gfx\\ui\\mainbar.tga", &mainbar},   {"gfx\\ui\\life.tga", &life},
        {"gfx\\ui\\weapons.tga", &weapons},   {"gfx\\ui\\missiles.tga", &missiles},
        {"gfx\\ui\\items.tga", &items},       {"menu\\cursor_1.tga", &cursor1},
        {"menu\\cursor_2.tga", &cursor2},
    };
    for (const Item& it : items_)
        if (!loadOne(vfs, it.path, *it.tex, error)) missing++;
    return true;
}

Font UiAssets::uiFont() const {
    Font f;
    f.metrics = &FontMetrics::original();
    f.texture = fontLoaded ? &font : nullptr;
    return f;
}

} // namespace as3d::ui
