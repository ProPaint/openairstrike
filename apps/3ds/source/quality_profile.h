// The 3DS quality profile (M4.1): what the Old 3DS (268 MHz ARM11, no L2 cache, 120 MB) gives up
// to hold the 60 Hz simulation, against the New 3DS preset that gives up nothing. Chosen at the
// start of the loop from APT_CheckNew3DS, or by run.ini quality=auto|new|old (testing the Old
// profile on a New 3DS or in the emulator). Logged as one AS3D_QUALITY line. docs/3ds-port.md M4.1.
#pragma once

#include "run_config.h"

namespace ctrapp {

struct QualityProfile {
    const char* name = "new";
    int renderHz = 60;               // 60: one drawn frame per vblank; 30: every other (sim stays 60 Hz)
    int particleCap = 1536;          // particle quads per frame (pica::RenderQuality)
    int spriteCap = 768;             // sprite quads per frame
    bool blobShadowsOnly = false;    // no silhouettes generated, every shadow is the blob
    bool terrainDetail = true;       // the terrain's detail texture (2-stage TEV)
    bool stereoAllowed = true;       // the 3D slider may draw two eyes
    bool preferPrerenderedMusic = false; // Ogg before the module (M2.4 already prefers it when present)
    // Stereo at 60 (docs/3ds-port.md "Stereo at 60 (round 2, P3)"): the terrain's far chunks coarse
    // (0 never, 1 while two eyes are drawn, 2 always) beyond terrainLodDistance world units, and
    // the silhouettes in atlas pages.
    int terrainLod = 1;
    float terrainLodDistance = 520.0f;
    float terrainLodBand = 40.0f; // hysteresis: coarse beyond distance + band, fine again under distance - band
    bool shadowAtlas = true;
    // R4.1 (docs/3ds-port.md "Stereo GPU budget (round 4, R4.1)"): the coarse far chunks without
    // the detail texture (run.ini terrain_far_tev; on with gpu_lean=1).
    bool terrainFarTev = false;

    // Simulation steps one drawn frame may catch up (the loops' bounded catch-up).
    int maxCatchUp() const { return renderHz == 30 ? 4 : 2; }
};

QualityProfile newProfile();
QualityProfile oldProfile();
// `mode` is run.ini quality=: Auto picks by `isNew3ds`.
QualityProfile selectQuality(QualityMode mode, bool isNew3ds);
// run.ini terrain_lod=, terrain_lod_dist=, shadow_atlas=, terrain_far_tev= (gpu_lean=) over the profile's values.
void applyRunOverrides(QualityProfile& q, const RunConfig& run);
// Sets pica::RenderQuality and the target frame rate (C3D_FrameRate); call after the graphics
// context exists and before the level starts. Logs the AS3D_QUALITY line.
void applyQuality(const QualityProfile& q, bool isNew3ds);

} // namespace ctrapp
