// Loading of the 2D textures. See as3d/ui.h.
#include <algorithm>
#include "as3d/hud_layout.h"
#include "as3d/image.h"
#include "as3d/load_stats.h"
#include "as3d/ui.h"
#include "ui_backend.h"

namespace as3d::ui {

namespace {

// One texture load (as3d/load_stats.h); texture() caches failures, so each path counts once.
// `cls` is the PICA backend's format hint (the GL backend ignores it): the font atlases are
// LA8, the HUD pictures RGBA4, anything loaded by path keeps the automatic choice.
bool loadOne(Vfs& vfs, const std::string& path, Texture2D& out, std::string* error, Wrap wrap = Wrap::ClampToEdge,
             TextureClass cls = TextureClass::Auto, Filter filter = Filter::Linear, Image* keep = nullptr) {
    LoadStats::Site site;
    LoadStats::count(LoadKind::Texture, path, "ui_assets");
    Blob blob;
    Image image;
    if (!vfs.read(path, blob) || !decodeTga(blob.data(), blob.size(), image)) {
        if (error && error->empty()) *error = "cannot load " + path;
        return false;
    }
    TextureOptions o;
    o.minFilter = o.magFilter = filter;
    o.wrapS = o.wrapT = wrap;
    o.cls = cls;
    out.create(image, o);
    if (keep) *keep = std::move(image);
    return true;
}

bool g_consoleTextOverride = false, g_consoleTextOverridden = false;

} // namespace

bool UiAssets::consoleTextDefault() { return g_consoleTextOverridden ? g_consoleTextOverride : consoleTextBackendDefault(); }
void UiAssets::setConsoleTextDefault(bool on) {
    g_consoleTextOverride = on;
    g_consoleTextOverridden = true;
}

Image halfSizePicture(const Image& src, int* w, int* h) {
    const int hw = std::max(src.width / 2, 0), hh = std::max(src.height / 2, 0);
    if (w) *w = hw;
    if (h) *h = hh;
    Image out;
    if (hw <= 0 || hh <= 0 || src.rgba.size() < static_cast<size_t>(src.width) * static_cast<size_t>(src.height) * 4) return out;
    auto side = [](int v) {
        int p = 8;
        while (p < v) p *= 2;
        return p;
    };
    out.width = side(hw);
    out.height = side(hh);
    out.hasAlpha = true;
    out.rgba.assign(static_cast<size_t>(out.width) * static_cast<size_t>(out.height) * 4, 0);
    for (int y = 0; y < hh; ++y)
        for (int x = 0; x < hw; ++x) {
            unsigned a = 0, c[3] = {0, 0, 0};
            for (int k = 0; k < 4; ++k) {
                const int sx = 2 * x + (k & 1), sy = 2 * y + (k >> 1);
                const uint8_t* p = &src.rgba[(static_cast<size_t>(sy) * static_cast<size_t>(src.width) + static_cast<size_t>(sx)) * 4];
                a += p[3];
                for (int i = 0; i < 3; ++i) c[i] += static_cast<unsigned>(p[i]) * p[3];
            }
            uint8_t* o = &out.rgba[(static_cast<size_t>(y) * static_cast<size_t>(out.width) + static_cast<size_t>(x)) * 4];
            for (int i = 0; i < 3; ++i) o[i] = static_cast<uint8_t>(a ? (c[i] + a / 2) / a : 0);
            o[3] = static_cast<uint8_t>((a + 2) / 4);
        }
    return out;
}

bool drawHalfPicture(Renderer2D& r, float x, float y, const HalfPicture& p, Color c, Blend blend) {
    if (!p.valid()) return false;
    const Mapping& m = r.mapping();
    return r.quad(x, y, static_cast<float>(p.width) / m.scaleX, static_cast<float>(p.height) / m.scaleY, 0, 0, p.s1, p.t1,
                  p.texture, c, blend);
}

bool UiAssets::load(Vfs& vfs, std::string* error, GameId g) {
    vfs_ = &vfs;
    game = g;
    missing = 0;
    // The console text: the font drawn texel for pixel (nearest), and its S atlas made now, so that
    // it is created with the font, before any level texture (VRAM on the 3DS).
    const Filter fontFilter = consoleText ? Filter::Nearest : Filter::Linear;
    Image fontImage;
    fontLoaded = loadOne(vfs, "gfx\\ui\\font.tga", font, error, Wrap::ClampToEdge, TextureClass::Font, fontFilter,
                         consoleText ? &fontImage : nullptr);
    if (!fontLoaded) return false;
    if (consoleText) {
        TextureOptions o;
        o.minFilter = o.magFilter = Filter::Nearest;
        o.wrapS = o.wrapT = Wrap::ClampToEdge;
        o.cls = TextureClass::Font;
        fontSmall.create(smallFontAtlas(fontImage, FontMetrics::original()), o);
    }
    // The HUD's atlases as the game's layout names them (a layout without one: not loaded).
    const HudLayout& L = hudLayout();
    struct Item { const char* path; Texture2D* tex; Wrap wrap; TextureClass cls; };
    const Item items_[] = {
        {"gfx\\ui\\font_alpha.tga", &fontAlpha, Wrap::ClampToEdge, TextureClass::Font},
        {L.barAtlas, &mainbar, Wrap::ClampToEdge, TextureClass::Alpha},
        {L.lifeTexture, &life, Wrap::ClampToEdge, TextureClass::Alpha},
        {L.weaponsAtlas, &weapons, Wrap::ClampToEdge, TextureClass::Alpha},
        {L.missilesAtlas, &missiles, Wrap::ClampToEdge, TextureClass::Alpha},
        {L.itemsAtlas, &items, Wrap::ClampToEdge, TextureClass::Alpha},
        {"menu\\cursor_1.tga", &cursor1, Wrap::ClampToEdge, TextureClass::Alpha},
        {"menu\\cursor_2.tga", &cursor2, Wrap::ClampToEdge, TextureClass::Alpha},
        {L.mouseCursor, &mcCursor, Wrap::ClampToEdge, TextureClass::Alpha},
        {L.hint == HintStyle::SequelPanel ? L.panelAtlas : nullptr, &panel, Wrap::ClampToEdge, TextureClass::Alpha},
        // The panel's static fill samples it at random offsets over more than one width.
        {L.hint == HintStyle::SequelPanel ? L.panelNoise : nullptr, &panelNoise, Wrap::Repeat, TextureClass::Alpha},
    };
    for (const Item& it : items_) {
        // The font's alpha twin is filtered as the font; the HUD atlases stay bilinear while the
        // HUD is drawn at fractional scales (0.6 to 0.8 of its texels on the 3DS bottom screen).
        const Filter f = it.tex == &fontAlpha ? fontFilter : Filter::Linear;
        if (it.path && !loadOne(vfs, it.path, *it.tex, error, it.wrap, it.cls, f)) missing++;
    }
    noteFontAtlases(*this);
    return true;
}

const HudLayout& UiAssets::hudLayout() const { return ui::hudLayout(game, hudTarget); }

Font UiAssets::uiFont() const {
    Font f;
    f.metrics = &FontMetrics::original();
    f.texture = fontLoaded ? &font : nullptr;
    f.alphaTexture = fontAlpha.valid() ? &fontAlpha : nullptr;
    if (consoleText && fontSmall.valid()) {
        static const FontMetrics small = smallFontMetrics(FontMetrics::original());
        f.consoleSizes = true;
        f.smallTexture = &fontSmall;
        f.smallMetrics = &small;
    }
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

HalfPicture UiAssets::textureHalf(std::string_view path) const {
    const std::string file = normalizePath(std::string(path));
    const std::string key = "\x02half:" + file; // never a game path
    auto it = halfInfo_.find(key);
    if (it != halfInfo_.end()) return it->second;
    HalfPicture hp;
    if (!vfs_) return hp;
    LoadStats::Site site;
    LoadStats::count(LoadKind::Texture, file, "ui_assets");
    Blob blob;
    Image image;
    if (!vfs_->read(file, blob) || !decodeTga(blob.data(), blob.size(), image)) {
        AS3D_WARN("ui: cannot load %s", file.c_str());
        halfInfo_[key] = hp;
        return hp;
    }
    int w = 0, h = 0;
    Image half = halfSizePicture(image, &w, &h);
    if (half.width > 0) {
        auto tex = std::make_unique<Texture2D>();
        TextureOptions o;
        o.minFilter = o.magFilter = Filter::Nearest;
        o.wrapS = o.wrapT = Wrap::ClampToEdge;
        tex->create(half, o);
        if (tex->valid()) {
            hp.texture = tex.get();
            hp.width = w;
            hp.height = h;
            hp.s1 = static_cast<float>(w) / static_cast<float>(half.width);
            hp.t1 = static_cast<float>(h) / static_cast<float>(half.height);
            cache_[key] = std::move(tex);
        }
    }
    halfInfo_[key] = hp;
    return hp;
}

int UiAssets::precachePlay() const {
    LoadStats::PrecacheScope scope;
    if (hudLayout().hint == HintStyle::SequelPanel) return 0;
    // hud.cpp drawHintOk, screens_game.cpp (mission complete, game over).
    static const char* const kPlay[] = {"menu\\apply_ok_1.tga", "menu\\apply_ok_2.tga", "menu\\miscompl_1.tga",
                                        "menu\\miscompl_2.tga", "menu\\gameover_0.tga", "menu\\gameover_3.tga"};
    for (const char* p : kPlay) texture(p);
    return static_cast<int>(sizeof kPlay / sizeof kPlay[0]);
}

int UiAssets::precacheMenus() const {
    if (!vfs_) return 0;
    LoadStats::PrecacheScope scope;
    // engine/src/ui/screens_*.cpp, menu_widgets.cpp, as2_widgets.cpp, screens_as2_*.cpp.
    static const char* const kFirst[] = {
        "menu\\corner.tga", "menu\\mmenu_1.tga", "menu\\mmenu_2.tga", "menu\\yesno_1.tga", "menu\\yesno_2.tga",
        "menu\\back_1.tga", "menu\\back_2.tga", "menu\\start_1.tga", "menu\\start_2.tga", "menu\\starth_0.tga",
        "menu\\starth_1.tga", "menu\\starth_2.tga", "menu\\topscores_0.tga", "menu\\topscores_1.tga",
        "menu\\topscores_2.tga", "menu\\optionsh_0.tga", "menu\\optionsh_1.tga", "menu\\optionsh_2.tga",
        "menu\\controlsh_0.tga", "menu\\controlsh_1.tga", "menu\\controlsh_2.tga", "menu\\scroller_1.tga",
        "menu\\scroller_2.tga", "menu\\slider.tga", "menu\\slidebutt_1.tga", "menu\\slidebutt_2.tga",
        "menu\\icon_border_2.tga", "menu\\icons_1.tga", "menu\\icons_2.tga", "menu\\icons_3.tga",
        "menu\\confkeys_1.tga", "menu\\confkeys_2.tga", "menu\\apply_ok_1.tga", "menu\\apply_ok_2.tga",
        "menu\\restart_1.tga", "menu\\restart_2.tga", "menu\\quit_1.tga", "menu\\quit_2.tga",
        "menu\\continue_1.tga", "menu\\continue_2.tga", "menu\\miscompl_1.tga", "menu\\miscompl_2.tga",
        "menu\\gameover_0.tga", "menu\\gameover_3.tga", "menu\\loading.tga"};
    static const char* const kSequel[] = {
        "gfx\\ui\\interface.tga", "gfx\\ui\\interface_gulf.tga", "gfx\\ui\\grid.tga", "gfx\\ui\\helicna.tga",
        "gfx\\ui\\portraits2.tga", "gfx\\logo\\logo_gulf.tga", "gfx\\logo\\glow.tga", "gfx\\logo\\two3.tga",
        "gfx\\logo\\logo.tga"};
    static const char* const kSequelRepeat[] = {"gfx\\ui\\snow.tga", "gfx\\logo\\clouds.tga", "gfx\\logo\\lines_gulf.tga"};
    int n = 0;
    auto one = [&](const char* p, bool repeat) {
        ++n;
        if (cache_.count(repeat ? "\x01repeat:" + normalizePath(p) : normalizePath(p)) || !vfs_->exists(p)) return;
        if (repeat) textureRepeat(p);
        else texture(p);
    };
    if (game == GameId::AirStrike3D) {
        for (const char* p : kFirst) one(p, false);
    } else {
        for (const char* p : kSequel) one(p, false);
        for (const char* p : kSequelRepeat) one(p, true);
    }
    return n;
}

const std::vector<std::string>& UiAssets::menuSounds() {
    static const std::vector<std::string> kSounds = {"sounds\\menu1.wav", "sounds\\menu2.wav", "sounds\\type.wav"};
    return kSounds;
}

const Texture2D* UiAssets::textureRepeat(std::string_view path) const {
    const std::string file = normalizePath(std::string(path));
    const std::string key = "\x01repeat:" + file; // never a game path
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second && it->second->valid() ? it->second.get() : nullptr;
    if (!vfs_) return nullptr;
    auto tex = std::make_unique<Texture2D>();
    if (!loadOne(*vfs_, file, *tex, nullptr, Wrap::Repeat)) {
        AS3D_WARN("ui: cannot load %s", file.c_str());
        cache_[key] = nullptr;
        return nullptr;
    }
    const Texture2D* p = tex.get();
    cache_[key] = std::move(tex);
    return p;
}

} // namespace as3d::ui
