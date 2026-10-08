#include "quality_profile.h"

#include <citro3d.h>

#include "as3d/core.h"
#include "quality_pica.h"

namespace ctrapp {

QualityProfile newProfile() { return QualityProfile(); }

QualityProfile oldProfile() {
    QualityProfile q;
    q.name = "old";
    q.renderHz = 30;
    q.particleCap = 768;
    q.spriteCap = 384;
    q.blobShadowsOnly = true;
    q.terrainDetail = false;
    q.stereoAllowed = true; // the slider decides; two eyes double the GPU, the player can leave it at 0
    q.preferPrerenderedMusic = true;
    return q;
}

QualityProfile selectQuality(QualityMode mode, bool isNew3ds) {
    switch (mode) {
        case QualityMode::New: return newProfile();
        case QualityMode::Old: return oldProfile();
        default: return isNew3ds ? newProfile() : oldProfile();
    }
}

void applyQuality(const QualityProfile& q, bool isNew3ds) {
    as3d::pica::RenderQuality rq;
    rq.particleCap = q.particleCap;
    rq.spriteCap = q.spriteCap;
    rq.blobShadowsOnly = q.blobShadowsOnly;
    rq.terrainDetail = q.terrainDetail;
    as3d::pica::setRenderQuality(rq);
    C3D_FrameRate(static_cast<float>(q.renderHz));
    AS3D_INFO("AS3D_QUALITY profile=%s hardware=%s renderHz=%d particleCap=%d spriteCap=%d blobShadowsOnly=%d "
              "terrainDetail=%d stereoAllowed=%d prerenderedMusic=%d",
              q.name, isNew3ds ? "new" : "old", q.renderHz, q.particleCap, q.spriteCap, q.blobShadowsOnly ? 1 : 0,
              q.terrainDetail ? 1 : 0, q.stereoAllowed ? 1 : 0, q.preferPrerenderedMusic ? 1 : 0);
}

} // namespace ctrapp
