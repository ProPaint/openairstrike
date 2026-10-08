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

    // Simulation steps one drawn frame may catch up (the loops' bounded catch-up).
    int maxCatchUp() const { return renderHz == 30 ? 4 : 2; }
};

QualityProfile newProfile();
QualityProfile oldProfile();
// `mode` is run.ini quality=: Auto picks by `isNew3ds`.
QualityProfile selectQuality(QualityMode mode, bool isNew3ds);
// Sets pica::RenderQuality and the target frame rate (C3D_FrameRate); call after the graphics
// context exists and before the level starts. Logs the AS3D_QUALITY line.
void applyQuality(const QualityProfile& q, bool isNew3ds);

} // namespace ctrapp
