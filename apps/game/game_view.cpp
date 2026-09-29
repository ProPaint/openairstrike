#include "game_view.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

#include "as3d/defs.h"
#include "as3d/frontend.h"
#include "as3d/scene.h"

namespace as3d_game {

using namespace as3d;

// The banner object (objects\banner.obj: additive, unlit, no depth test) and its own mesh
// renderer; it does not belong to any level.
struct GameView::Banner {
    std::unique_ptr<ResourceCache> cache;
    MeshRenderer mesh;
    Material material;
    const GpuMesh* gpu = nullptr;
    float scale = 1.0f;
};

GameView::GameView() = default;
GameView::~GameView() = default;

bool GameView::init(GameSession& session, std::string* error, bool level) {
    if (!renderer_.init(session.vfs(), session.db(), error)) return false;
    // The HUD is optional: without its textures the game still runs (a warning is logged).
    std::string hudError;
    hudReady_ = r2d_.init(&hudError) && assets_.load(session.vfs(), &hudError);
    if (!hudReady_) AS3D_WARN("HUD unavailable: %s", hudError.c_str());
    // The main menu's banner; optional like the HUD.
    banner_.reset();
    if (const ObjectDef* def = session.db().findObject("banner")) {
        std::unique_ptr<Banner> b(new Banner());
        b->cache.reset(new ResourceCache(session.vfs()));
        std::string meshError;
        if (!def->model.empty() && b->mesh.init(&meshError)) {
            const GpuMesh& gpu = b->cache->mesh(def->model);
            if (gpu.valid) {
                b->gpu = &gpu;
                b->material = Material::fromObjectDef(*def, *b->cache);
                b->scale = def->scale > 0.001f ? def->scale : 1.0f;
                banner_ = std::move(b);
            }
        }
        if (!banner_) AS3D_WARN("banner unavailable %s", meshError.c_str());
    }
    return level ? beginLevel(session, error) : true;
}

bool GameView::beginLevel(GameSession& session, std::string* error) {
    return renderer_.beginLevel(session.world(), error);
}

void GameView::step(const GameSession& session) {
    const World& w = session.world();
    if (!w.paused()) renderer_.step(w, w.config().dt);
}

ui::HudState hudStateOf(const GameSession& session) {
    const World& w = session.world();
    ui::HudState hs;
    hs.playerCount = std::min(std::max(w.numPlayers(), 1), 2);
    for (int p = 0; p < hs.playerCount; ++p) {
        const PlayerRecord& pr = w.player(p);
        ui::HudPlayer& hp = hs.players[p];
        int pi = w.playerEntityIndex(p);
        hp.health = pi >= 0 ? std::max(w.entity(pi).f(F_HEALTH), 0.0f) : 0.0f;
        hp.lives = pr.lives > 0.0f ? static_cast<int>(std::min(pr.lives, 99.0f)) : 0;
        hp.score = session.displayScore(p);
        hp.weapon = (pr.weapon >= 0.0f && pr.weapon < 64.0f) ? static_cast<int>(pr.weapon) : 0;
        for (int k = 0; k < ui::kWeaponSlots; ++k) hp.upgrades[k] = std::max(pr.upgrades[k], 0);
        hp.missileSelected = pr.currentMissile;
        for (int t = 0; t < ui::kMissileTypes; ++t) hp.missiles[t] = std::max(pr.missiles[t], 0);
        hp.powerupSelected = pr.currentPowerup;
        for (int k = 0; k < ui::kPowerupSlots; ++k) hp.powerups[k] = std::max(pr.powerups[k], 0);
    }
    // Level-name typewriter (frontend.md 4.5): the world's clock stops while paused, like the
    // original's level clock.
    hs.levelName = session.levelName();
    hs.levelTime = w.time();
    return hs;
}

void GameView::renderWorld(const World& world, int width, int height) {
    if (screenMode == kScreen4x3) {
        int x, y, w, h;
        ui::computeMapping(width, height).fieldPixels(x, y, w, h);
        if (w > 0 && h > 0 && (w != width || h != height)) {
            renderer_.render(world, x, height - y - h, w, h);
            glViewport(0, 0, width, height);
            return;
        }
    }
    renderer_.render(world, width, height);
}

void GameView::clearBars(int width, int height) {
    if (screenMode != kScreen4x3) return;
    int x, y, w, h;
    ui::computeMapping(width, height).fieldPixels(x, y, w, h);
    if (w == width && h == height) return;
    // The rectangles around the field, GL convention (y from the bottom).
    const int rects[4][4] = {
        {0, 0, x, height},                              // left
        {x + w, 0, width - x - w, height},              // right
        {x, height - y, w, y},                          // top
        {x, 0, w, height - y - h},                      // bottom
    };
    glEnable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    for (const auto& r : rects) {
        if (r[2] <= 0 || r[3] <= 0) continue;
        glScissor(r[0], r[1], r[2], r[3]);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glDisable(GL_SCISSOR_TEST);
}

void GameView::draw(const GameSession& session, int width, int height) {
    const World& w = session.world();
    renderWorld(w, width, height);
    // Pass 13: the 2D layer. No HUD on intermission levels or while it is hidden (level end,
    // game over); the hint box is drawn over everything.
    if (hudReady_) {
        r2d_.begin(width, height);
        if (!w.intermission() && !w.levelComplete() && !w.gameOver()) ui::drawHud(r2d_, assets_, hudStateOf(session));
        if (w.hintShowing()) ui::drawHint(r2d_, assets_, w.hintText());
        r2d_.flush();
    }
    // Pass 14: brightness.
    renderer_.drawBrightness(width, height, brightness);
    clearBars(width, height);
}

void GameView::drawFrame(const GameSession& session, int width, int height, const FrameLayers& layers) {
    if (layers.world && session.hasLevel()) {
        renderWorld(session.world(), width, height);
    } else {
        glViewport(0, 0, width, height);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    if (hudReady_) {
        // frontend.md 4.1: HUD, then the menus; the banner goes between the menu frame and the
        // menu items (3.3 steps 4 to 6).
        r2d_.begin(width, height);
        if (layers.hud) ui::drawHud(r2d_, assets_, layers.hudState);
        if (ui::Frontend* fe = layers.frontend) {
            fe->drawUnder(r2d_, assets_);
            if (fe->bannerVisible()) {
                r2d_.flush();
                drawBanner(fe->menus().menuTime(), width, height);
                r2d_.begin(width, height);
            }
            fe->drawOver(r2d_, assets_);
        }
        r2d_.flush();
    }
    renderer_.drawBrightness(width, height, layers.brightness);
    clearBars(width, height);
}

void GameView::drawBanner(float mt, int width, int height) {
    if (!banner_) return;
    // Viewport over the virtual rectangle (0, 0, 800, 200), depth cleared first (0x401b30).
    const ui::Mapping m = ui::computeMapping(width, height);
    const int x0 = static_cast<int>(std::lround(m.toFbX(0))), x1 = static_cast<int>(std::lround(m.toFbX(800)));
    const int y0 = static_cast<int>(std::lround(m.toFbY(0))), y1 = static_cast<int>(std::lround(m.toFbY(200)));
    const int vw = x1 - x0, vh = y1 - y0;
    if (vw <= 0 || vh <= 0) return;
    glViewport(x0, height - y1, vw, vh);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x0, height - y1, vw, vh);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    // Camera at the origin with the engine's view convention and zero angles (identity view:
    // looking down -z, y up), field of view 60.
    Camera cam;
    cam.eye = Vec3{0, 0, 0};
    cam.target = Vec3{0, 0, -1};
    cam.worldUp = Vec3{0, 1, 0};
    cam.fovYDegrees = 60.0f;
    cam.aspect = static_cast<float>(vw) / static_cast<float>(vh);
    cam.nearPlane = 1.0f;
    cam.farPlane = 1000.0f;
    // Entity at (-34, 7, -30), angles (95 + 7 sin mt, 3 sin(0.7 mt + 0.5), 0) degrees; the
    // axis is AnglesToAxis (engine-behaviour.md 4.3) with the angles as fields 14..16.
    const float kDeg = 3.14159265f / 180.0f;
    const float ax = (95.0f + 7.0f * std::sin(mt)) * kDeg, ay = 3.0f * std::sin(0.7f * mt + 0.5f) * kDeg, az = 0.0f;
    const float sa = std::sin(ax), ca = std::cos(ax), sb = std::sin(ay), cb = std::cos(ay), sc = std::sin(az),
                cc = std::cos(az);
    const Vec3 fwd{cb * cc, cb * sc, sb};
    const Vec3 left{sa * sb * cc - ca * sc, sa * sb * sc + ca * cc, -sa * cb};
    const Vec3 up{-sa * sc - ca * sb * cc, sa * cc - ca * sb * sc, ca * cb};
    Mat4 model;
    model.at(0, 0) = fwd.x; model.at(0, 1) = fwd.y; model.at(0, 2) = fwd.z; model.at(0, 3) = 0;
    model.at(1, 0) = left.x; model.at(1, 1) = left.y; model.at(1, 2) = left.z; model.at(1, 3) = 0;
    model.at(2, 0) = up.x; model.at(2, 1) = up.y; model.at(2, 2) = up.z; model.at(2, 3) = 0;
    model.at(3, 0) = -34.0f; model.at(3, 1) = 7.0f; model.at(3, 2) = -30.0f; model.at(3, 3) = 1;
    if (banner_->scale != 1.0f) model = model * scale(Vec3{banner_->scale, banner_->scale, banner_->scale});
    banner_->mesh.begin(cam, SceneLighting());
    banner_->mesh.submit(*banner_->gpu, banner_->material, model);
    banner_->mesh.end();
    glViewport(0, 0, width, height);
}

} // namespace as3d_game
