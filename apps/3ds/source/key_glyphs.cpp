#include "key_glyphs.h"

#include <cstring>

#include "as3d/console_menu.h"
#include "as3d/core.h"
#include "as3d/image.h"

namespace ctrapp {

namespace {
// apps/3ds/assets/keys.inc: kKeysW, kKeysH, kKeysPalette (0xRRGGBBAA), kKeysIndex (one byte per
// pixel, rows from the top), made from keys.png by tools/3ds_key_glyphs.py.
#include "keys.inc"
} // namespace

bool loadKeyGlyphs(as3d::Texture2D& tex, as3d::ui::KeyGlyphAtlas& atlas) {
    using namespace as3d;
    atlas = ui::KeyGlyphAtlas();
    static_assert(kKeysW <= 256 && kKeysH <= 32, "keys.png must fit 256 x 32");
    static_assert(sizeof kKeysIndex == static_cast<size_t>(kKeysW) * kKeysH, "keys.inc: one index per pixel");
    // Power-of-two sides (the PICA's texture sizes: no stretch on upload), transparent padding.
    Image img;
    img.width = 256;
    img.height = 32;
    img.hasAlpha = true;
    img.rgba.assign(static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * 4, 0);
    const size_t colours = sizeof kKeysPalette / sizeof kKeysPalette[0];
    for (int y = 0; y < kKeysH; ++y)
        for (int x = 0; x < kKeysW; ++x) {
            const unsigned i = kKeysIndex[y * kKeysW + x];
            const unsigned c = i < colours ? kKeysPalette[i] : 0u;
            u8* p = &img.rgba[(static_cast<size_t>(y) * 256 + static_cast<size_t>(x)) * 4];
            p[0] = static_cast<u8>(c >> 24);
            p[1] = static_cast<u8>(c >> 16);
            p[2] = static_cast<u8>(c >> 8);
            p[3] = static_cast<u8>(c);
        }
    TextureOptions o;
    o.minFilter = o.magFilter = Filter::Nearest;
    o.wrapS = o.wrapT = Wrap::ClampToEdge;
    tex.create(img, o);
    if (!tex.valid()) return false;
    atlas.texture = &tex;
    for (int k = 0; k < static_cast<int>(ui::HintKey::Count); ++k)
        atlas.rects[k] = ui::consoleKeyRect(static_cast<ui::HintKey>(k));
    AS3D_INFO("ui: key pictures %dx%d (set 4) loaded", kKeysW, kKeysH);
    return true;
}

} // namespace ctrapp
