// The first game's banner on the top screen (ctr_banner.h).
#include "ctr_banner.h"

#include <3ds.h>

#include <algorithm>
#include <cmath>

#include "as3d/core.h"
#include "as3d/defs.h"
#include "as3d/fog_lut.h"
#include "as3d/frontend.h"
#include "as3d/render_state.h"
#include "as3d/vfs.h"
#include "as3d/world_render.h"
#include "ctr_context.h"
#include "flat_fill.h"
#include "model_view.h"
#include "pica_frame.h"
#include "stereo_comfort.h"

namespace ctrapp {

using namespace as3d;

namespace {
constexpr int kTopW = as3d::ctr::kTopWidth, kTopH = as3d::ctr::kTopHeight;
constexpr float kBannerAspect = static_cast<float>(kBannerW) / static_cast<float>(kBannerH);
bool g_logged = false;
} // namespace

Mat4 bannerMatrix(float mt, float objectScale) {
    const float angles[3] = {95.0f + 7.0f * std::sin(mt), 3.0f * std::sin(0.7f * mt + 0.5f), 0.0f};
    const float origin[3] = {-34.0f, 7.0f, -30.0f};
    Mat4 model = as3d_game::entityMatrix(angles, origin);
    if (objectScale != 1.0f) model = model * scale(Vec3{objectScale, objectScale, objectScale});
    return model;
}

ConsoleBanner::ConsoleBanner() = default;
ConsoleBanner::~ConsoleBanner() = default;

bool ConsoleBanner::load(Vfs& vfs, const DefDatabase& db) {
    cache_.reset();
    gpu_ = nullptr;
    const ObjectDef* def = db.findObject("banner");
    if (!def || def->model.empty()) return false;
    const uint64_t t0 = svcGetSystemTick();
    std::unique_ptr<ResourceCache> cache(new ResourceCache(vfs));
    const GpuMesh& gpu = cache->mesh(def->model);
    if (!gpu.valid) {
        AS3D_WARN("banner: %s has no mesh: the title in text", def->model.c_str());
        return false;
    }
    cache_ = std::move(cache);
    gpu_ = &gpu;
    material_ = Material::fromObjectDef(*def, *cache_);
    scale_ = def->scale > 0.001f ? def->scale : 1.0f;
    if (!g_logged)
        AS3D_INFO("AS3D_BANNER model=%s scale=%.2f load_ms=%.1f viewport=(0, 0, %d, %d)", def->model.c_str(),
                  static_cast<double>(scale_), static_cast<double>(svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11, kBannerW,
                  kBannerH);
    g_logged = true;
    return true;
}

void ConsoleBanner::prepare(float mt, float slider, int eyes, bool stereoComfort, float maxEyeOffset, float minFocal) {
    if (!valid()) return;
    time_ = mt;
    pass_.begin(SceneLighting(), nullptr, 0);
    pass_.setEnvironment(Mat4::identity(), 0.0f, false);
    const Mat4 model = bannerMatrix(mt, scale_);
    pass_.submit(*gpu_, material_, model, Vec4{1, 1, 1, 1});
    pass_.finish();
    const float s = eyes == 2 ? slider : 0.0f;
    float nearD = 0.0f, farD = 0.0f;
    if (stereoComfort && pica::meshDepthRange(model, gpu_->model.positions, 60.0f, kBannerAspect, 1.0f, 1000.0f, nearD, farD)) {
        stereo_ = stereoFor(s, nearD, maxEyeOffset, 1.0f);
        const float k = std::min(std::max(slider, 0.0f), 1.0f);
        stereo_.eyeOffset = pica::limitEyeOffsetForDepth(stereo_.eyeOffset, nearD, farD, pica::stereoA(60.0f, kBannerAspect),
                                                         static_cast<float>(kBannerW), kBannerDepthPx * k);
    } else {
        stereo_ = stereoFor(s, kBannerFocal, maxEyeOffset, minFocal);
    }
}

void ConsoleBanner::drawBox(int x, int y, int w, int h, int targetW, int targetH, bool depth) {
    if (!valid() || w <= 0 || h <= 0) return;
    const ui::BannerContent& k = ui::kBannerContent;
    float vw = static_cast<float>(w) / (k.x1 - k.x0);
    vw = std::min(vw, static_cast<float>(h) / (k.y1 - k.y0) * 4.0f);
    // The viewport (wider than the box: the picture's margins) must lie in the target: a box at
    // the screen's edge gets a slightly smaller title, still centred.
    const float cx = static_cast<float>(x) + static_cast<float>(w) * 0.5f, cy = static_cast<float>(y) + static_cast<float>(h) * 0.5f;
    const float kx = (k.x0 + k.x1) * 0.5f, ky = (k.y0 + k.y1) * 0.5f;
    vw = std::min(vw, std::min(cx / kx, (static_cast<float>(targetW) - cx) / (1.0f - kx)));
    vw = std::min(vw, std::min(cy / ky, (static_cast<float>(targetH) - cy) / (1.0f - ky)) * 4.0f);
    const float vh = vw * 0.25f;
    const float vx = cx - kx * vw, vy = cy - ky * vh;
    const int px = std::max(0, static_cast<int>(std::lround(vx))), py = std::max(0, static_cast<int>(std::lround(vy)));
    const int pw = std::min(targetW - px, static_cast<int>(std::lround(vw))), ph = std::min(targetH - py, static_cast<int>(std::lround(vh)));
    if (pw <= 0 || ph <= 0) return;
    setViewport(px, targetH - (py + ph), pw, ph);
    setScissor(true, x, targetH - (y + h), w, h);
    if (depth) clearDepth();
    WorldView wv;
    wv.view = Mat4::identity();
    wv.fovY = 60.0f;
    wv.nearPlane = 1.0f;
    wv.farPlane = 1000.0f;
    pica::setDepthMapping(FogDepthMap::WBuffer, wv.nearPlane);
    pica::disableFog();
    pass_.draw(pica::picaEyeProjection(wv, EyeView(), static_cast<float>(pw) / static_cast<float>(ph)) * wv.view);
    setScissor(false);
    setViewport(0, 0, targetW, targetH);
}

void ConsoleBanner::drawEye(int eye) {
    if (!valid()) return;
    // The desktop's main menu has the banner over the letterbox's black top band, whose red rule
    // is at virtual y 99 (frontend.md 3.3): the same band at the top screen's scale.
    pica::fillRect(0.0f, 0.0f, static_cast<float>(kTopW), kBannerBand, 0, 0, 0);
    pica::fillRect(0.0f, kBannerBand, kTopW * 0.975f, kBannerBand + 1.0f, 211, 0, 0);
    const int glY = kTopH - kBannerH;
    setViewport(0, glY, kBannerW, kBannerH);
    setScissor(true, 0, glY, kBannerW, kBannerH);
    clearDepth();
    WorldView wv;
    wv.view = Mat4::identity();
    wv.fovY = 60.0f;
    wv.nearPlane = 1.0f;
    wv.farPlane = 1000.0f;
    EyeView ev;
    if (stereo_.eyes == 2) {
        ev.eyeOffset = eye == 0 ? -stereo_.eyeOffset : stereo_.eyeOffset;
        ev.focalDistance = stereo_.focal;
    }
    pica::setDepthMapping(FogDepthMap::WBuffer, wv.nearPlane);
    pica::disableFog(); // SceneLighting's fog starts at 650
    pass_.draw(pica::picaEyeProjection(wv, ev, kBannerAspect) * wv.view);
    setScissor(false);
    setViewport(0, 0, kTopW, kTopH);
}

} // namespace ctrapp
