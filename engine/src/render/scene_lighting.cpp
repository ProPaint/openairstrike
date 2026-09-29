// Implements as3d::SceneLighting::fromMission1 (as3d/scene.h) from docs/spec/levels-txt.md's
// "sun"/"fog" statements.
#include "as3d/scene.h"

#include "as3d/defs.h"

namespace as3d {

SceneLighting SceneLighting::fromMission1(const DefDatabase& db) {
    SceneLighting lighting; // defaults already match mission 1's own values

    const LevelDef* mission1 = nullptr;
    for (const LevelDef& lv : db.levels()) {
        if (lv.id == "mission1") {
            mission1 = &lv;
            break;
        }
    }
    if (!mission1) return lighting;

    // docs/spec/levels-txt.md "sun": 9 floats, GUESS grouping (diffuse RGB, direction,
    // ambient RGB) -- the only reading consistent with the shipped values (e.g. mission
    // 1's "sun 0.9 0.9 0.9 -1.0 -0.5 1.0 0.3 0.3 0.1").
    lighting.sunColor = Vec3{mission1->sun[0], mission1->sun[1], mission1->sun[2]};
    lighting.sunDirection = Vec3{mission1->sun[3], mission1->sun[4], mission1->sun[5]};
    lighting.ambientColor = Vec3{mission1->sun[6], mission1->sun[7], mission1->sun[8]};

    if (mission1->hasFog) {
        lighting.fogColor = Vec3{mission1->fogColor[0], mission1->fogColor[1], mission1->fogColor[2]};
        lighting.fogStart = mission1->fogNear;
        lighting.fogEnd = mission1->fogFar;
    }
    return lighting;
}

} // namespace as3d
