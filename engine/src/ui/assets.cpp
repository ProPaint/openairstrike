// Loading of the 2D textures. See as3d/ui.h.
#include "as3d/hud_layout.h"
#include "as3d/image.h"
#include "as3d/load_stats.h"
#include "as3d/ui.h"

namespace as3d::ui {

namespace {

// One texture load (as3d/load_stats.h); texture() caches failures, so each path counts once.
// `cls` is the PICA backend's format hint (the GL backend ignores it): the font atlases are
// LA8, the HUD pictures RGBA4, anything loaded by path keeps the automatic choice.
bool loadOne(Vfs& vfs, const std::string& path, Texture2D& out, std::string* error, Wrap wrap = Wrap::ClampToEdge,
             TextureClass cls = TextureClass::Auto) {
    LoadStats::Site site;
    LoadStats::count(LoadKind::Texture, path, "ui_assets");
    Blob blob;
    Image image;
    if (!vfs.read(path, blob) || !decodeTga(blob.data(), blob.size(), image)) {
        if (error && error->empty()) *error = "cannot load " + path;
        return false;
    }
    TextureOptions o;
    o.minFilter = o.magFilter = Filter::Linear;
    o.wrapS = o.wrapT = wrap;
    o.cls = cls;
    out.create(image, o);
    return true;
}

} // namespace

bool UiAssets::load(Vfs& vfs, std::string* error, GameId g) {
    vfs_ = &vfs;
    game = g;
    missing = 0;
    fontLoaded = loadOne(vfs, "gfx\\ui\\font.tga", font, error, Wrap::ClampToEdge, TextureClass::Font);
    if (!fontLoaded) return false;
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
    for (const Item& it : items_)
        if (it.path && !loadOne(vfs, it.path, *it.tex, error, it.wrap, it.cls)) missing++;
    return true;
}

const HudLayout& UiAssets::hudLayout() const { return ui::hudLayout(game, hudTarget); }

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
