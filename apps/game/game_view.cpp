#include "game_view.h"

#include <algorithm>
#include <cmath>

#include "as3d/defs.h"
#include "as3d/frontend.h"
#include "as3d/load_stats.h"
#include "as3d/object_tree.h"
#include "as3d/render_state.h"
#include "as3d/scene.h"
#include "model_view.h"

namespace as3d_game {

using namespace as3d;

// The banner object (objects\banner.obj: additive, unlit, no depth test) and its own mesh
// renderer; it does not belong to any level.
struct BannerMesh::Impl {
    std::unique_ptr<ResourceCache> cache;
    MeshRenderer mesh;
    Material material;
    const GpuMesh* gpu = nullptr;
    float scale = 1.0f;
};

// The sequels' model view: an object definition with its attachment hierarchy (the preview
// entity is built without its script, as2/frontend.md 3.18), rebuilt when the object changes.
struct GameView::Preview {
    std::unique_ptr<ResourceCache> cache;
    MeshRenderer mesh;
    std::string object;
    std::vector<ModelViewPart> parts;
};


BannerMesh::BannerMesh() = default;
BannerMesh::~BannerMesh() = default;

bool BannerMesh::init(Vfs& vfs, const DefDatabase& db, std::string* error) {
    impl_.reset();
    const ObjectDef* def = db.findObject("banner");
    if (!def) return false; // the sequels have none
    std::unique_ptr<Impl> b(new Impl());
    b->cache.reset(new ResourceCache(vfs));
    std::string meshError;
    if (!def->model.empty() && b->mesh.init(&meshError)) {
        const GpuMesh& gpu = b->cache->mesh(def->model);
        if (gpu.valid) {
            b->gpu = &gpu;
            b->material = Material::fromObjectDef(*def, *b->cache);
            b->scale = def->scale > 0.001f ? def->scale : 1.0f;
            impl_ = std::move(b);
        }
    }
    if (!impl_ && error) *error = meshError.empty() ? "no model" : meshError;
    return impl_ != nullptr;
}

GameView::GameView() = default;
GameView::~GameView() { logLazyLoads(); }

std::unique_ptr<FlowView> makeGameView() { return std::unique_ptr<FlowView>(new GameView()); }

bool GameView::init(GameSession& session, std::string* error, bool level) {
    if (!renderer_.init(session.vfs(), session.db(), error)) return false;
    // The HUD is optional: without its textures the game still runs (a warning is logged).
    std::string hudError;
    const GameId game = session.game() ? session.game()->id : GameId::AirStrike3D;
    hudReady_ = r2d_.init(&hudError) && assets_.load(session.vfs(), &hudError, game);
    if (!hudReady_) AS3D_WARN("HUD unavailable: %s", hudError.c_str());
    // The main menu's banner; optional like the HUD.
    std::string bannerError;
    if (!banner_.init(session.vfs(), session.db(), &bannerError) && !bannerError.empty())
        AS3D_WARN("banner unavailable %s", bannerError.c_str());
    return level ? beginLevel(session, error) : true;
}

bool GameView::beginLevel(GameSession& session, std::string* error) {
    logLazyLoads(); // the level that ended
    if (!renderer_.beginLevel(session.world(), error)) return false;
    if (session.options().precache && session.hasLevel()) precacheLevel(session);
    return true;
}

void GameView::precacheLevel(GameSession& session) {
    World& world = session.world();
    PrecacheReport r = world.precacheScripts();
    r += renderer_.precache(world, session.db());
    const LoadCounts before = LoadStats::lazyLoads();
    if (hudReady_) assets_.precachePlay();
    if (precacheSounds && world.precacheSet()) precacheSounds(*world.precacheSet());
    const LoadCounts d = LoadStats::lazyLoads() - before;
    r.textures += static_cast<int>(d.of(LoadKind::Texture));
    r.sounds += static_cast<int>(d.of(LoadKind::Sound));
    r.bytes += d.bytes;
    lastPrecache_ = r;
    precachedMission_ = session.mission();
    AS3D_INFO("AS3D_PRECACHE mission=%d %s", precachedMission_, r.str().c_str());
    // From here on every load is a lazy one (logged by logLazyLoads at the level's end).
    LoadStats::reset();
}

void GameView::logLazyLoads() {
    if (precachedMission_ < 0) return;
    AS3D_INFO("AS3D_LAZY_LOADS mission=%d %s", precachedMission_, LoadStats::lazyLoads().str().c_str());
    precachedMission_ = -1;
}

void GameView::step(const GameSession& session) {
    const World& w = session.world();
    if (!w.paused()) renderer_.step(w, w.config().dt);
}

ViewRect GameView::worldRect(const ViewRect& frame) const {
    if (screenMode == kScreen4x3) {
        int x, y, w, h;
        ui::computeMapping(frame.width, frame.height).fieldPixels(x, y, w, h);
        if (w > 0 && h > 0 && (w != frame.width || h != frame.height))
            return ViewRect{frame.x + x, frame.y + frame.height - y - h, w, h};
    }
    return frame;
}

void GameView::prepareWorld(const GameSession& session, const ViewRect& rect, float maxEyeOffset, float minFocalDistance) {
    Viewport vp;
    vp.x = rect.x;
    vp.y = rect.y;
    vp.width = rect.width;
    vp.height = rect.height;
    vp.maxEyeOffset = maxEyeOffset;
    vp.minFocalDistance = minFocalDistance;
    renderer_.prepare(session.world(), vp, worldOptions);
}

void GameView::drawWorld(const GameSession&, const ViewRect& rect, const EyeView& eye) {
    renderer_.draw(eye, rect.x, rect.y);
    setViewport(rect.x, rect.y, rect.width, rect.height);
}

void GameView::renderWorld(const GameSession& session, const ViewRect& frame) {
    if (stereoDebugEyeOffset != 0.0f) {
        // --stereo-debug: one prepare, then each eye in its half of the field.
        ViewRect left = worldRect(frame);
        left.width /= 2;
        ViewRect right = left;
        right.x += left.width;
        prepareWorld(session, left, std::fabs(stereoDebugEyeOffset), stereoDebugFocal);
        drawWorld(session, left, EyeView{-stereoDebugEyeOffset, stereoDebugFocal});
        drawWorld(session, right, EyeView{stereoDebugEyeOffset, stereoDebugFocal});
        setViewport(frame.x, frame.y, frame.width, frame.height);
        return;
    }
    const ViewRect wr = worldRect(frame);
    prepareWorld(session, wr);
    drawWorld(session, wr, EyeView{});
    setViewport(frame.x, frame.y, frame.width, frame.height);
}

void GameView::clearBars(const ViewRect& frame) {
    if (screenMode != kScreen4x3) return;
    const int width = frame.width, height = frame.height;
    int x, y, w, h;
    ui::computeMapping(width, height).fieldPixels(x, y, w, h);
    if (w == width && h == height) return;
    // The rectangles around the field, GL convention (y from the bottom) inside the frame.
    const int rects[4][4] = {
        {0, 0, x, height},                              // left
        {x + w, 0, width - x - w, height},              // right
        {x, height - y, w, y},                          // top
        {x, 0, w, height - y - h},                      // bottom
    };
    for (const auto& r : rects) {
        if (r[2] <= 0 || r[3] <= 0) continue;
        as3d::setScissor(true, frame.x + r[0], frame.y + r[1], r[2], r[3]);
        as3d::clearColor(Vec4{0, 0, 0, 1});
    }
    as3d::setScissor(false);
}

void GameView::drawHud(const ui::HudState& state, const ViewRect& rect, HudTarget target) {
    if (!hudReady_) return;
    assets_.hudTarget = target;
    r2d_.begin(rect.width, rect.height, rect.x, rect.y);
    ui::drawHud(r2d_, assets_, state);
    r2d_.flush();
    assets_.hudTarget = HudTarget::Main;
}

void GameView::drawHud(const GameSession& session, const ViewRect& rect, HudTarget target) {
    const World& w = session.world();
    // No HUD on intermission levels or while it is hidden (level end, game over).
    if (w.intermission() || w.levelComplete() || w.gameOver()) return;
    drawHud(hudStateOf(session), rect, target);
}

void GameView::drawFront(const GameSession& session, const ViewRect& rect, ui::Frontend* frontend, bool hintBox,
                         HudTarget target) {
    frameRect_ = rect;
    if (!hudReady_) return;
    assets_.hudTarget = target;
    r2d_.begin(rect.width, rect.height, rect.x, rect.y);
    // The hint box is drawn over everything (render-pipeline.md 1.1 pass 13).
    if (hintBox && session.hasLevel() && session.world().hintShowing()) ui::drawHint(r2d_, assets_, session.world().hintText());
    if (ui::Frontend* fe = frontend) {
        // frontend.md 4.1: the menus after the HUD; the banner goes between the menu frame and
        // the menu items (3.3 steps 4 to 6).
        fe->drawUnder(r2d_, assets_);
        if (fe->bannerVisible()) {
            r2d_.flush();
            drawBanner(fe->menus().menuTime(), rect);
            r2d_.begin(rect.width, rect.height, rect.x, rect.y);
        }
        if (fe->modelViewVisible()) {
            // The sequels' helicopter preview: over the menu's backdrop, under its items.
            r2d_.flush();
            fe->drawModelViews();
            r2d_.begin(rect.width, rect.height, rect.x, rect.y);
        }
        fe->drawOver(r2d_, assets_);
    }
    r2d_.flush();
    assets_.hudTarget = HudTarget::Main;
}

void GameView::drawBrightness(const ViewRect& rect, float b) {
    renderer_.drawBrightness(rect.x, rect.y, rect.width, rect.height, b);
}

void GameView::drawBrightness(const ViewRect& rect) { drawBrightness(rect, brightness); }

DualScreenRects dualScreenRects(int frameWidth, int frameHeight) {
    DualScreenRects d;
    const float sx = static_cast<float>(frameWidth) / kDualFrameWidth, sy = static_cast<float>(frameHeight) / kDualFrameHeight;
    const float s = std::max(std::min(sx, sy), 0.01f);
    d.scale = s;
    const int tw = static_cast<int>(std::lround(kDualTopWidth * s)), bw = static_cast<int>(std::lround(kDualBottomWidth * s));
    const int sh = static_cast<int>(std::lround(kDualScreenHeight * s));
    const int x0 = (frameWidth - tw) / 2;
    const int yTop = (frameHeight - 2 * sh) / 2; // from the top edge
    d.top = ViewRect{x0, frameHeight - yTop - sh, tw, sh};
    d.bottom = ViewRect{x0 + (tw - bw) / 2, frameHeight - yTop - 2 * sh, bw, sh};
    return d;
}

void GameView::drawDual(const GameSession& session, const ViewRect& frame, const FrameLayers& layers, bool sessionHud,
                        bool hintBox) {
    const DualScreenRects d = dualScreenRects(frame.width, frame.height);
    frameRect_ = frame;
    setViewport(frame.x, frame.y, frame.width, frame.height);
    as3d::clear(Vec4{0, 0, 0, 1});
    if (layers.world && session.hasLevel()) {
        prepareWorld(session, d.top);
        drawWorld(session, d.top, EyeView{});
        drawBrightness(d.top, layers.brightness);
    }
    if (sessionHud) drawHud(session, d.bottom, HudTarget::Bottom320x240);
    else if (layers.hud) drawHud(layers.hudState, d.bottom, HudTarget::Bottom320x240);
    drawFront(session, d.bottom, layers.frontend, hintBox, HudTarget::Bottom320x240);
    setViewport(frame.x, frame.y, frame.width, frame.height);
}

void GameView::draw(const GameSession& session, int width, int height) {
    const ViewRect frame{0, 0, width, height};
    if (dualScreen) {
        FrameLayers layers;
        layers.brightness = brightness;
        drawDual(session, frame, layers, true, true);
        return;
    }
    renderWorld(session, frame);
    if (worldOnly) return;
    // Pass 13: the 2D layer (HUD, then the hint box), pass 14: brightness.
    drawHud(session, frame, HudTarget::Main);
    drawFront(session, frame, nullptr, true);
    drawBrightness(frame);
    clearBars(frame);
}

void GameView::drawFrame(const GameSession& session, int width, int height, const FrameLayers& layers) {
    const ViewRect frame{0, 0, width, height};
    frameRect_ = frame;
    if (dualScreen) {
        drawDual(session, frame, layers, false, false);
        return;
    }
    if (layers.world && session.hasLevel()) {
        renderWorld(session, frame);
    } else {
        setViewport(0, 0, width, height);
        as3d::clear(Vec4{0, 0, 0, 1});
    }
    if (layers.hud) drawHud(layers.hudState, frame, HudTarget::Main);
    if (layers.frontend) drawFront(session, frame, layers.frontend, false);
    drawBrightness(frame, layers.brightness);
    clearBars(frame);
}

void GameView::drawModel(GameSession& session, const ui::ModelView& v) {
    if (!preview_) {
        std::unique_ptr<Preview> p(new Preview());
        std::string err;
        if (!p->mesh.init(&err)) {
            AS3D_WARN("model view unavailable: %s", err.c_str());
            return;
        }
        p->cache.reset(new ResourceCache(session.vfs()));
        preview_ = std::move(p);
    }
    Preview& p = *preview_;
    if (p.object != v.object) {
        p.object = v.object;
        p.parts = modelViewParts(session.db(), session.vfs(), *p.cache, v.object);
    }
    if (p.parts.empty()) return;
    // The viewport over the virtual rectangle, scaled to the frame, depth cleared inside it.
    const ui::Mapping m = ui::computeMapping(frameRect_.width, frameRect_.height);
    const int x0 = static_cast<int>(std::lround(m.toFbX(v.viewport.x)));
    const int x1 = static_cast<int>(std::lround(m.toFbX(v.viewport.x + v.viewport.w)));
    const int y0 = static_cast<int>(std::lround(m.toFbY(v.viewport.y)));
    const int y1 = static_cast<int>(std::lround(m.toFbY(v.viewport.y + v.viewport.h)));
    const int vw = x1 - x0, vh = y1 - y0;
    if (vw <= 0 || vh <= 0) return;
    const int gx = frameRect_.x + x0, gy = frameRect_.y + frameRect_.height - y1;
    as3d::setViewport(gx, gy, vw, vh);
    as3d::setScissor(true, gx, gy, vw, vh);
    as3d::clearDepth();
    Camera cam;
    cam.eye = Vec3{0, 0, 0};
    cam.target = Vec3{0, 0, -1};
    cam.worldUp = Vec3{0, 1, 0};
    cam.fovYDegrees = v.fovY;
    cam.aspect = static_cast<float>(vw) / static_cast<float>(vh);
    cam.nearPlane = v.nearPlane;
    cam.farPlane = v.farPlane;
    const Mat4 model = entityMatrix(v.angles, v.origin);
    p.mesh.begin(cam, SceneLighting());
    for (const ModelViewPart& part : p.parts) p.mesh.submit(*part.gpu, part.material, model * part.local, part.colour);
    p.mesh.end();
    as3d::setScissor(false);
    as3d::setViewport(frameRect_.x, frameRect_.y, frameRect_.width, frameRect_.height);
}

void GameView::drawBanner(float mt, int width, int height) { drawBanner(mt, ViewRect{0, 0, width, height}); }

void GameView::drawBanner(float mt, const ViewRect& rect) {
    if (!banner_.valid()) return;
    // Viewport over the virtual rectangle (0, 0, 800, 200) of the rect.
    const ui::Mapping m = ui::computeMapping(rect.width, rect.height);
    const int x0 = static_cast<int>(std::lround(m.toFbX(0))), x1 = static_cast<int>(std::lround(m.toFbX(800)));
    const int y0 = static_cast<int>(std::lround(m.toFbY(0))), y1 = static_cast<int>(std::lround(m.toFbY(200)));
    // BannerMesh::draw flips y against the height it is given: the framebuffer row of the rect's top.
    banner_.draw(mt, rect.x + rect.width, rect.y + rect.height, rect.x + x0, y0, x1 - x0, y1 - y0);
}

void BannerMesh::draw(float mt, int width, int height, int x0, int y0, int vw, int vh, const int* clip, bool loopFit) {
    if (!impl_ || vw <= 0 || vh <= 0) return;
    Impl& banner_ = *impl_;
    // Depth cleared first (0x401b30), inside the viewport.
    as3d::setViewport(x0, height - (y0 + vh), vw, vh);
    as3d::setScissor(true, x0, height - (y0 + vh), vw, vh);
    as3d::clearDepth();
    if (clip) as3d::setScissor(true, clip[0], height - (clip[1] + clip[3]), clip[2], clip[3]);
    else as3d::setScissor(false);
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
    const float ax = (95.0f + 7.0f * std::sin(mt)) * kDeg,
                ay = 3.0f * std::sin((loopFit ? 1.0f : 0.7f) * mt + 0.5f) * kDeg, az = 0.0f;
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
    if (banner_.scale != 1.0f) model = model * scale(Vec3{banner_.scale, banner_.scale, banner_.scale});
    banner_.mesh.begin(cam, SceneLighting());
    banner_.mesh.submit(*banner_.gpu, banner_.material, model);
    banner_.mesh.end();
    as3d::setScissor(false);
    as3d::setViewport(0, 0, width, height);
}

} // namespace as3d_game
