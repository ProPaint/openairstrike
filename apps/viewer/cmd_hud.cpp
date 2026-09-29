// `as3d_viewer [--game KEY] hud [--players 2] [--health N] [--max-health N] [--score N]
//  [--lives N] [--weapon N] [--upgrades a,b,...] [--missiles a,b,c,d,e] [--selected N]
//  [--powerups a,b,...] [--pselected N] [--p2-health N] [--p2-max-health N] [--p2-score N]
//  [--p2-lives N] [--p2-weapon N] [--p2-upgrades ...] [--p2-missiles ...] [--p2-powerups ...]
//  [--p2-selected N] [--p2-pselected N] [--name S --time s] [--message S --age s] [--cursor x,y]
//  [--hint S] [--size WxH] [--bg r,g,b] --out f.png`: draws the HUD of the selected game
// headless over a neutral background (frontend.md 4; as2/frontend.md 4). Counts of 0 are not
// shown; power-up counts are per slot 0..15 (the sequels' timer slots 6, 7 and 9 hold the
// seconds left); upgrades are the level per weapon slot.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "as3d/gfx.h"
#include "as3d/image.h"
#include "as3d/platform.h"
#include "as3d/ui.h"
#include "common.h"
#include "registry.h"

namespace viewer {
bool parseTriple(const char* s, float& a, float& b, float& c);
}

namespace {

template <size_t N>
bool parseList(const char* s, int (&dst)[N]) {
    size_t n = 0;
    while (*s && n < N) {
        char* end = nullptr;
        dst[n++] = static_cast<int>(std::strtol(s, &end, 10));
        if (end == s) return false;
        s = *end == ',' ? end + 1 : end;
    }
    return true;
}

// As cmd_text.cpp's renderUiHeadless, with the UI assets of the selected game.
template <class Draw>
bool renderHud(int w, int h, as3d::ui::Color bg, const Draw& draw, as3d::Image& out) {
    as3d::Vfs vfs;
    const as3d::GameData* game = viewer::selectedGame();
    if (game)
        if (auto dir = as3d::makeDirSource(game->dataDir)) vfs.mount(std::move(dir));
    if (!viewer::mountGameData(vfs)) { std::fprintf(stderr, "error: no game data (set AS3D_DATA_ROOT)\n"); return false; }
    const as3d::GameId id = game && game->game ? game->game->id : as3d::GameId::AirStrike3D;
    as3d::GraphicsConfig cfg;
    cfg.headless = true;
    auto gl = as3d::createGraphicsContext(cfg);
    if (!gl) { std::fprintf(stderr, "error: could not create a headless graphics context\n"); return false; }
    gl->makeCurrent();
    std::string err;
    as3d::ui::UiAssets assets;
    if (!assets.load(vfs, &err, id)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return false; }
    if (assets.missing) std::fprintf(stderr, "warning: %d UI texture(s) missing: %s\n", assets.missing, err.c_str());
    as3d::ui::Renderer2D r2;
    if (!r2.init(&err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return false; }
    as3d::RenderTarget target;
    if (!target.create(w, h, 4)) { std::fprintf(stderr, "error: cannot create %dx%d render target\n", w, h); return false; }
    target.bind();
    as3d::clear({bg.r, bg.g, bg.b, 1.0f}, true);
    r2.begin(w, h);
    draw(r2, assets);
    int quads = static_cast<int>(r2.quads().size());
    int calls = r2.flush();
    std::printf("quads=%d draw calls=%d dropped=%d\n", quads, calls, r2.dropped());
    if (!target.readPixels(out)) { std::fprintf(stderr, "error: readback failed\n"); return false; }
    return true;
}

int run(int argc, char** argv) {
    using namespace as3d::ui;
    HudState st;
    std::string out, hint;
    int w = 800, h = 600;
    Color bg{0.16f, 0.22f, 0.16f, 1};
    HudPlayer* p2 = &st.players[1];
    HudPlayer* p1 = &st.players[0];
    bool p2Set = false;
    // Demo defaults so a bare `hud` shows every element.
    *p1 = HudPlayer{};
    p1->health = 300; p1->lives = 4; p1->score = 12345; p1->weapon = 1;
    p1->upgrades[0] = 2; p1->upgrades[1] = 3;
    p1->missileSelected = 1;
    p1->powerupSelected = 1;
    int demoM[kMissileTypes] = {12, 8, 3, 0, 5};
    int demoP[4] = {1, 2, 0, 1};
    std::memcpy(p1->missiles, demoM, sizeof demoM);
    std::memcpy(p1->powerups, demoP, sizeof demoP);
    for (int i = 0; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        const char* v = nullptr;
        bool ok = true;
        if (a == "--out" && (v = val())) out = v;
        else if (a == "--size" && (v = val())) ok = std::sscanf(v, "%dx%d", &w, &h) == 2 && w >= 16 && h >= 16 && w <= 8192 && h <= 8192;
        else if (a == "--bg" && (v = val())) ok = viewer::parseTriple(v, bg.r, bg.g, bg.b);
        else if (a == "--players" && (v = val())) st.playerCount = std::atoi(v) >= 2 ? 2 : 1;
        else if (a == "--health" && (v = val())) p1->health = static_cast<float>(std::atof(v));
        else if (a == "--max-health" && (v = val())) p1->maxHealth = static_cast<float>(std::atof(v));
        else if (a == "--score" && (v = val())) p1->score = std::atoll(v);
        else if (a == "--lives" && (v = val())) p1->lives = std::atoi(v);
        else if (a == "--weapon" && (v = val())) p1->weapon = std::atoi(v);
        else if (a == "--upgrades" && (v = val())) ok = parseList(v, p1->upgrades);
        else if (a == "--pselected" && (v = val())) p1->powerupSelected = std::atoi(v);
        else if (a == "--cursor" && (v = val())) { st.mouseCursor = std::sscanf(v, "%f,%f", &st.mouseX, &st.mouseY) == 2; ok = st.mouseCursor; }
        else if (a == "--selected" && (v = val())) p1->missileSelected = std::atoi(v);
        else if (a == "--missiles" && (v = val())) ok = parseList(v, p1->missiles);
        else if (a == "--powerups" && (v = val())) ok = parseList(v, p1->powerups);
        else if (a == "--p2-health" && (v = val())) { p2->health = static_cast<float>(std::atof(v)); p2Set = true; }
        else if (a == "--p2-max-health" && (v = val())) { p2->maxHealth = static_cast<float>(std::atof(v)); p2Set = true; }
        else if (a == "--p2-score" && (v = val())) { p2->score = std::atoll(v); p2Set = true; }
        else if (a == "--p2-lives" && (v = val())) { p2->lives = std::atoi(v); p2Set = true; }
        else if (a == "--p2-weapon" && (v = val())) { p2->weapon = std::atoi(v); p2Set = true; }
        else if (a == "--p2-upgrades" && (v = val())) { ok = parseList(v, p2->upgrades); p2Set = true; }
        else if (a == "--p2-missiles" && (v = val())) { ok = parseList(v, p2->missiles); p2Set = true; }
        else if (a == "--p2-powerups" && (v = val())) { ok = parseList(v, p2->powerups); p2Set = true; }
        else if (a == "--p2-selected" && (v = val())) { p2->missileSelected = std::atoi(v); p2Set = true; }
        else if (a == "--p2-pselected" && (v = val())) { p2->powerupSelected = std::atoi(v); p2Set = true; }
        else if (a == "--name" && (v = val())) st.levelName = v;
        else if (a == "--time" && (v = val())) st.levelTime = static_cast<float>(std::atof(v));
        else if (a == "--message" && (v = val())) st.message = v;
        else if (a == "--age" && (v = val())) st.messageAge = static_cast<float>(std::atof(v));
        else if (a == "--hint" && (v = val())) hint = v;
        else { std::fprintf(stderr, "bad or incomplete option %s\n", a.c_str()); ok = false; }
        if (!ok) return 1;
    }
    if (out.empty()) {
        std::fprintf(stderr, "usage: as3d_viewer [--game KEY] hud [--players 2] [--health N] [--max-health N] [--score N]\n"
                             "  [--lives N] [--weapon N] [--upgrades a,b,...] [--missiles a,b,c,d,e] [--selected N]\n"
                             "  [--powerups a,b,...] [--pselected N] [--cursor x,y] [--p2-health N ...]\n"
                             "  [--name S --time s] [--message S --age s] [--hint S] [--size WxH] [--bg r,g,b] --out f.png\n");
        return 1;
    }
    // Sensible defaults so a bare `hud` shows something: two players share the first player's
    // values unless overridden.
    if (st.playerCount == 2 && !p2Set) *p2 = *p1;

    as3d::Image img;
    bool ok = renderHud(w, h, bg, [&](Renderer2D& r, const UiAssets& a) {
        drawHud(r, a, st);
        if (!hint.empty()) drawHint(r, a, hint);
    }, img);
    if (!ok) return 1;
    if (!as3d::writePng(out.c_str(), img)) { std::fprintf(stderr, "error: cannot write %s\n", out.c_str()); return 1; }
    std::printf("wrote %s (%dx%d)\n", out.c_str(), img.width, img.height);
    return 0;
}

} // namespace

AS3D_VIEWER_COMMAND("hud", "draw the in-game HUD of the selected game from command line values (--players, --health, --hint ...)", run);
