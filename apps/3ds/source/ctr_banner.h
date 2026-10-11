// The first game's main-menu banner (objects\banner.obj, the flaming title; docs/3ds-port.md
// "Packaging and launch") on the top screen. The desktop draws it in the virtual rectangle
// (0, 0, 800, 200) of the main menu over the attract level (GameView::drawBanner) and in the
// game selector's marquee box (LauncherScreen's banner drawer). The 3DS's menus are on the bottom
// screen, so it goes on the top one: the whole width, a quarter of the height (400 x 100 px, the
// desktop's 4:1), at the top, over the letterbox's black band and its red rule as on the desktop's
// main menu (frontend.md 3.3), so that the additive flames are on black as there. The frame
// composer shows it over the attract level (the main menu, ctr_frame.cpp) and the game selector
// over its AirStrike 3D card (ctr_selector.cpp): the same picture in both places.
//
// Stereo (docs/3ds-port.md "Stereo comfort and frame polish"): the screen plane at the nearest
// vertex of the swaying mesh (the camera is the origin, identity view), so the title sits on the
// screen with its depth behind it, never in front of the band (2D, at the screen plane) it
// overlaps; kBannerFocal when no vertex is in the view. Its depth (the mesh is 20 to 45 units
// away; the world's eye offset made it span 15 px and more) is kept to kBannerDepthPx behind the
// screen at slider 1.
#pragma once

#include <memory>
#include <string>

#include "as3d/math.h"
#include "as3d/scene.h"
#include "ctr_frame.h"
#include "mesh_renderer_pica.h"

namespace as3d {
class DefDatabase;
class Vfs;
} // namespace as3d

namespace ctrapp {

constexpr int kBannerW = 400, kBannerH = 100;
constexpr float kBannerFocal = 40.0f;
constexpr float kBannerDepthPx = 4.0f; // the banner's depth behind the screen at slider 1 (stereo_comfort)
constexpr float kBannerBand = 99.0f * 240.0f / 600.0f; // the letterbox's black band (virtual 0..99)

// BannerMesh::draw's model matrix (apps/game/game_view.cpp): the entity at (-34, 7, -30), angles
// (95 + 7 sin mt, 3 sin(0.7 mt + 0.5), 0) degrees, times the object's scale.
as3d::Mat4 bannerMatrix(float mt, float objectScale);

class ConsoleBanner {
public:
    ConsoleBanner();
    ~ConsoleBanner();

    // The game's "banner" object: its mesh and material from a cache of this banner's own (over
    // `vfs`, which must outlive it). False when the game has none (the sequels: their title stays
    // in text) or its mesh is missing (logged). Logs AS3D_BANNER once per process.
    bool load(as3d::Vfs& vfs, const as3d::DefDatabase& db);
    bool valid() const { return gpu_ != nullptr; }

    // The pass at the menu clock `mt` and the stereo for the slider (`eyes` 2 when the frame draws
    // two eyes): the comfort rules above when `stereoComfort`, else the plain stereoFor.
    void prepare(float mt, float slider, int eyes, bool stereoComfort, float maxEyeOffset, float minFocal);
    // One eye, in the current top target: the band, then the mesh in the top kBannerW x kBannerH
    // pixels (BannerMesh::draw's camera: the origin, identity view, 60 degrees, planes 1..1000),
    // the depth cleared there first.
    void drawEye(int eye);
    // The banner in a box of the current target (pixels from the top left; `targetH` the
    // target's height), mono, as the desktop selector's card marquee: the viewport scaled and
    // placed so that the part of the 4:1 picture the title covers (ui::kBannerContent) fills the
    // box, scissored to the box. `depth`: the target has a depth buffer, cleared in the box first;
    // without one (the bottom screen) the mesh is drawn without the depth test, in its pass order.
    void drawBox(int x, int y, int w, int h, int targetW, int targetH, bool depth);

    float time() const { return time_; }
    const StereoParams& stereo() const { return stereo_; }
    const as3d::pica::MeshDrawStats& lastStats() const { return pass_.lastStats(); } // of the last drawEye

private:
    std::unique_ptr<as3d::ResourceCache> cache_;
    const as3d::GpuMesh* gpu_ = nullptr;
    as3d::Material material_;
    float scale_ = 1.0f;
    float time_ = 0.0f;
    StereoParams stereo_;
    as3d::pica::MeshPass pass_;
};

} // namespace ctrapp
