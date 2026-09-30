// `as3d_viewer screen <name> [--touch] [--size WxH] [--state k=v,...] [--pointer x,y]
//  [--no-texts] [--level L --scroll S] --out f.png` and `as3d_viewer screen --list`: renders
// one front-end screen (docs/spec/frontend.md 3; the sequels' as2/frontend.md 3) headless over
// a plain background, or over level L seen at scroll S (the attract level behind the menus:
// `--level intro1`). The first game's 3D banner is not drawn; the sequels' helicopter preview
// is. --state takes the keys of Frontend::debugSet (mt=seconds of menu time, players=2,
// unlock=1, page=N, capture=row, name=S, hint=S, kills, enemies, stars, startotal, score,
// maxscore, cheat, heli, heli2, mission, ingame=1; the sequels also dialogue=start|end, dpage,
// typed, accept=1, checkpoint=N, shown, and for the name `comic`: comic=1..4, t=seconds).
#include <GLES3/gl3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "as3d/defs.h"
#include "as3d/frontend.h"
#include "as3d/image.h"
#include "as3d/ui.h"
#include "common.h"
#include "level_render.h"
#include "registry.h"
#include "viewer_scene.h"

namespace {

using namespace as3d::ui;

// A game that does nothing but draw the sequels' model views: the viewer only shows screens.
struct NullHost : GameHost {
    viewer::ViewerContext* ctx = nullptr;
    int fbW = 800, fbH = 600;
    void startMission(const MissionStart&) override {}
    void loadAttract() override {}
    void setPaused(bool) override {}
    void clearPlayerActions() override {}
    void settingsChanged(const as3d::Settings&) override {}
    void saveProfile(const as3d::Profile&) override {}
    void quit() override {}
    // As GameView::drawModel (apps/game/game_view.cpp).
    void drawModel(const ModelView& v) override {
        if (!ctx) return;
        viewer::SceneDesc scene;
        std::string err;
        if (!viewer::buildObjectScene(*ctx, v.object, false, scene, nullptr, err)) return;
        const Mapping m = computeMapping(fbW, fbH);
        const int x0 = static_cast<int>(std::lround(m.toFbX(v.viewport.x)));
        const int x1 = static_cast<int>(std::lround(m.toFbX(v.viewport.x + v.viewport.w)));
        const int y0 = static_cast<int>(std::lround(m.toFbY(v.viewport.y)));
        const int y1 = static_cast<int>(std::lround(m.toFbY(v.viewport.y + v.viewport.h)));
        const int vw = x1 - x0, vh = y1 - y0;
        if (vw <= 0 || vh <= 0) return;
        glViewport(x0, fbH - y1, vw, vh);
        glEnable(GL_SCISSOR_TEST);
        glScissor(x0, fbH - y1, vw, vh);
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        as3d::Camera cam;
        cam.eye = as3d::Vec3{0, 0, 0};
        cam.target = as3d::Vec3{0, 0, -1};
        cam.worldUp = as3d::Vec3{0, 1, 0};
        cam.fovYDegrees = v.fovY;
        cam.aspect = static_cast<float>(vw) / static_cast<float>(vh);
        cam.nearPlane = v.nearPlane;
        cam.farPlane = v.farPlane;
        const float kDeg = 3.14159265f / 180.0f;
        const float ax = v.angles[0] * kDeg, ay = v.angles[1] * kDeg, az = v.angles[2] * kDeg;
        const float sa = std::sin(ax), ca = std::cos(ax), sb = std::sin(ay), cb = std::cos(ay), sc = std::sin(az),
                    cc = std::cos(az);
        as3d::Mat4 model;
        const float rows[3][3] = {{cb * cc, cb * sc, sb},
                                  {sa * sb * cc - ca * sc, sa * sb * sc + ca * cc, -sa * cb},
                                  {-sa * sc - ca * sb * cc, sa * cc - ca * sb * sc, ca * cb}};
        for (int r = 0; r < 3; r++) {
            for (int k = 0; k < 3; k++) model.at(r, k) = rows[r][k];
            model.at(r, 3) = 0;
        }
        for (int k = 0; k < 3; k++) model.at(3, k) = v.origin[k];
        model.at(3, 3) = 1;
        ctx->renderer.begin(cam, as3d::SceneLighting());
        for (const viewer::DrawPart& p : scene.parts) ctx->renderer.submit(*p.mesh, p.material, model * p.model, p.colour);
        ctx->renderer.end();
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, fbW, fbH);
    }
};

bool readFile(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        out.append(buf, n);
        if (out.size() > (8u << 20)) break;
    }
    std::fclose(f);
    return true;
}

// Mission names and helicopter unlocks from levels.txt, Settings.xml for the main menu.
FrontendContent loadContent() {
    FrontendContent c;
    as3d::Vfs vfs;
    viewer::mountGameData(vfs);
    as3d::DefDatabase db;
    db.load(vfs);
    const as3d::GameData* game = viewer::selectedGame();
    const as3d::GameRules& rules = game && game->game ? game->game->rules : as3d::defaultGameRules();
    if (game && game->game) c.game = game->game;
    // Levels with a `name` are the missions, in table order (levels-txt.md).
    int i = 0;
    for (const as3d::LevelDef& d : db.levels()) {
        if (d.name.empty() || i >= rules.missionCount) continue;
        c.missionNames[i] = d.name;
        c.enableHelic[i] = d.enableHelic;
        i++;
    }
    // The plain front end lists the helicopters with what their definitions say.
    for (int h = 0; h < rules.helicopterCount && rules.heliObjects; h++)
        if (const as3d::ObjectDef* o = db.findObject(rules.heliObjects[h])) {
            c.heli[h].known = true;
            c.heli[h].health = o->health;
            c.heli[h].hasSpeed = o->hasSpeed;
            c.heli[h].speed = o->speed;
        }
    std::string xml;
    if (game && !game->settingsXml.empty() && readFile(game->settingsXml, xml)) {
        parseSettingsXml(xml, c);
        removeRereleaseBranding(c);
    }
    return c;
}

int run(int argc, char** argv) {
    std::string name, out, stateArg;
    int w = 800, h = 600;
    bool touch = false, list = false, noTexts = false;
    float px = -50, py = -50, scroll = 400, brightness = -1.0f; // default: 0.6 over a level, none over the plain background
    std::string level;
    for (int i = 0; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        const char* v = nullptr;
        if (a == "--out" && (v = val())) out = v;
        else if (a == "--size" && (v = val())) {
            if (std::sscanf(v, "%dx%d", &w, &h) != 2 || w < 16 || h < 16 || w > 8192 || h > 8192) return 1;
        } else if (a == "--state" && (v = val())) stateArg = v;
        else if (a == "--pointer" && (v = val())) { if (std::sscanf(v, "%f,%f", &px, &py) != 2) return 1; }
        else if (a == "--level" && (v = val())) level = v;
        else if (a == "--scroll" && (v = val())) scroll = static_cast<float>(std::atof(v));
        else if (a == "--brightness" && (v = val())) brightness = static_cast<float>(std::atof(v));
        else if (a == "--touch") touch = true;
        else if (a == "--list") list = true;
        else if (a == "--no-texts") noTexts = true;
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 1; }
        else name = a;
    }
    if (list) {
        for (Screen s : kSequelScreens) std::printf("%s\n", screenName(s));
        std::printf("intro\ncomic\nloading\nplaying\n");
        return 0;
    }
    Screen screen = Screen::MainMenu;
    const bool special = name == "intro" || name == "loading" || name == "playing" || name == "comic";
    if (out.empty() || (!special && !screenFromName(name, screen))) {
        std::fprintf(stderr, "usage: as3d_viewer screen <name> [--touch] [--size WxH] [--state k=v,...] [--pointer x,y] "
                             "[--no-texts] --out f.png\n       as3d_viewer screen --list\n");
        return 1;
    }

    Texts texts;
    std::string textFile;
    const as3d::GameData* game = viewer::selectedGame();
    if (!noTexts && game && !game->textsFile.empty() && readFile(game->textsFile, textFile)) texts.parse(textFile);
    NullHost host;
    as3d::Profile profile;
    FrontendContent content = loadContent();
    const bool plainGame = content.game && content.game->frontend == as3d::FrontendStyle::PlainList;
    profile.progress = as3d::Progress::defaults(content.game ? content.game->rules : as3d::defaultGameRules(),
                                                as3d::Progress::defaultHelicopters(content.game));
    content.videoOptions = !touch && !plainGame; // a touch device has no video modes to pick; the sequels have none
    if (plainGame) {
        content.screenOption = true; // as the game host offers it on wide windows
        content.handOption = touch;
        content.twoPlayerMode = false;
    }
    Frontend fe(host, profile, content, texts);
    fe.setTouchMode(touch);
    fe.menus().setPointer(px, py);

    // Split --state into keys applied before the screen is built and keys that need it open.
    std::vector<std::pair<std::string, std::string>> before, after;
    for (size_t i = 0; i < stateArg.size();) {
        size_t j = stateArg.find(',', i);
        if (j == std::string::npos) j = stateArg.size();
        std::string kv = stateArg.substr(i, j - i);
        i = j + 1;
        const size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
        const bool late = k == "mt" || k == "name" || k == "capture" || k == "dialogue" || k == "dpage" || k == "typed" ||
                          k == "t";
        (late ? after : before).emplace_back(k, v);
    }
    for (auto& [k, v] : before)
        if (!fe.debugSet(k, v)) { std::fprintf(stderr, "unknown state key %s\n", k.c_str()); return 1; }

    bool loading = false;
    if (name == "intro") {
        fe.boot();
        fe.update(3.0f, {}); // a moment into the DivoGames page
    } else if (name == "loading") {
        loading = true;
    } else if (name == "playing") {
        fe.setState(FrontendState::Playing);
    } else if (name == "comic") {
        // the comic=N and t=T state keys set the page
    } else if (screen == Screen::Dialogue) {
        fe.setState(FrontendState::Playing); // the dialogue=start|end state key opens it
    } else {
        const bool inMission = screen == Screen::InGame || screen == Screen::Hint || screen == Screen::GameOver ||
                               screen == Screen::MissionComplete || screen == Screen::GameComplete;
        fe.setState(inMission ? FrontendState::Playing : FrontendState::Attract);
        if (screen != Screen::MainMenu && !inMission) fe.open(Screen::MainMenu);
        fe.open(screen);
    }
    for (auto& [k, v] : after)
        if (!fe.debugSet(k, v)) { std::fprintf(stderr, "unknown state key %s\n", k.c_str()); return 1; }
    fe.update(0.0f, UiInput().move(px, py));

    // Render: the level behind (--level) or a plain background, the screen's back half, the
    // model views, the front half.
    viewer::ViewerContext ctx;
    std::string err;
    if (!ctx.init(w, h, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    host.ctx = &ctx;
    host.fbW = w;
    host.fbH = h;
    as3d::Texture2D background;
    if (!level.empty()) {
        viewer::LevelRenderOptions lo;
        lo.width = w;
        lo.height = h;
        lo.scroll = scroll;
        lo.brightness = 0.0f; // applied to the whole frame below
        if (game && game->game) lo.game = game->game->id;
        as3d::Image bg;
        if (!viewer::renderLevel(ctx.vfs, ctx.db, *ctx.cache, ctx.renderer, level, lo, bg, nullptr, err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        background.create(bg);
    }
    UiAssets assets;
    if (!assets.load(ctx.vfs, &err, game && game->game ? game->game->id : as3d::GameId::AirStrike3D)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    Renderer2D r;
    if (!r.init(&err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    as3d::RenderTarget target;
    if (!target.create(w, h, 4)) { std::fprintf(stderr, "error: cannot create a %dx%d target\n", w, h); return 1; }
    target.bind();
    as3d::clear({0.18f, 0.24f, 0.30f, 1.0f}, true);
    r.begin(w, h);
    if (background.valid()) {
        // The rendered level covers the whole framebuffer (its image is bottom-up: flipped).
        Quad q;
        const Mapping& m = r.mapping();
        q.x = m.left();
        q.y = m.top();
        q.w = m.right() - m.left();
        q.h = m.bottom() - m.top();
        q.texture = &background;
        q.blend = Blend::Opaque;
        q.t0 = 0;
        q.t1 = 1;
        r.add(q);
    }
    if (loading) {
        drawLoadingScreen(r, assets, 0.6f, false);
    } else {
        fe.drawUnder(r, assets);
        if (fe.modelViewVisible()) {
            r.flush();
            fe.drawModelViews();
            r.begin(w, h);
        }
        fe.drawOver(r, assets);
    }
    r.flush();
    as3d::Image img;
    if (!target.readPixels(img)) { std::fprintf(stderr, "error: readback failed\n"); return 1; }
    // The game's brightness pass after the 2D layer (render-pipeline.md 1.5): x 2b, clamped;
    // so the pictures have the brightness of the game's frames and the originals' screenshots.
    if (brightness < 0) brightness = level.empty() ? 0.0f : 0.6f;
    if (brightness > 0)
        for (size_t i = 0; i < img.rgba.size(); i++)
            if (i % 4 != 3) img.rgba[i] = static_cast<as3d::u8>(std::min(255.0f, img.rgba[i] * 2.0f * brightness));
    if (!as3d::writePng(out.c_str(), img)) { std::fprintf(stderr, "error: cannot write %s\n", out.c_str()); return 1; }
    std::printf("wrote %s (%dx%d)\n", out.c_str(), img.width, img.height);
    return 0;
}

} // namespace

AS3D_VIEWER_COMMAND("screen", "render a front-end screen headless (--list, --touch, --state k=v,...)", run);
