// Per-game render rules (as3d/render_rules.h). Values of the sequels:
// docs/spec/as2/render-pipeline.delta.md, section named on each field of the header.
#include "as3d/render_rules.h"

namespace as3d {

namespace {

RenderRules firstGame() { return RenderRules(); }

RenderRules sequel() {
    RenderRules r;
    r.water = WaterStyle::WaveGrid;
    r.terrainCentralNormals = true;
    r.tileHalfTexelInset = true;
    r.shadowEraseBelowGround = false;
    r.shadowHeightExpCap = 8;
    r.shadowMaxSide = 256;
    r.shadowMipmaps = false;
    r.shadowMultiply = true;
    r.spriteCulling = false;
    r.billboardTrueAxes = true;
    r.envViewNormal = true;
    r.missingTextureWhite = true;
    r.markEntityAlpha = true;
    r.markTextureUnflipped = true;
    r.skidMarkPass = true;
    r.ui2dHalfPixelShift = true;
    return r;
}

const RenderRules kRules[kGameCount] = {firstGame(), sequel(), sequel()};

} // namespace

const RenderRules& renderRules(GameId id) {
    int i = static_cast<int>(id);
    return kRules[(i >= 0 && i < kGameCount) ? i : 0];
}

const RenderRules& renderRulesFor(const GameRules& rules) {
    for (int i = 0; i < kGameCount; ++i) {
        GameId id = static_cast<GameId>(i);
        if (&gameProfile(id).rules == &rules) return renderRules(id);
    }
    return renderRules(GameId::AirStrike3D);
}

} // namespace as3d
