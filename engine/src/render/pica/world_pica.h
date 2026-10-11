// The PICA WorldRenderer's own counters (3DS only, world_renderer_pica.cpp), for the composer's
// logs: how many of the last prepare()'s model records draw from a linear-memory texture.
#pragma once

#include <string>

namespace as3d::pica {

struct WorldPicaStats {
    int records = 0;         // model records of the last prepare()
    int linearRecords = 0;   // of which with a texture in linear memory (over the VRAM budget)
    std::string linearSkins; // their definitions, each once (" name name ...")
    unsigned culledRecords = 0; // model records outside the prepared cull volume, not submitted (last prepare())
};

const WorldPicaStats& worldPicaStats();

// The depth range of what the central camera sees (stereo_comfort.h), measured by prepare() while
// two eyes are drawn (frameEyes() == 2), for the frame composer's comfort clamp (docs/3ds-port.md
// "Stereo comfort and frame polish (round 2, P5)"). The terrain probe refreshes one row of its ray
// grid per prepare (all six on the first after a level start or a mono stretch).
struct SceneDepth {
    bool valid = false;        // measured by the last prepare (two eyes)
    bool terrainHit = false;   // the probe's rays hit the terrain or the water
    float terrainNear = 0.0f;  // the nearest terrain / water depth seen (0: none)
    float terrainFar = 0.0f;   // the farthest (within the far plane and the fog's end)
    float modelNear = 0.0f;    // the nearest point of an opaque or transparent model's box in the view (0: none)
    float nearPlane = 0.0f, farLimit = 0.0f;
    unsigned probes = 0;       // terrain probes since the level start
    long long probeSteps = 0;  // ray steps of those probes
    unsigned long long probeTicks = 0, modelTicks = 0; // CPU ticks of the probes and of the model scans
    unsigned modelScans = 0;
};
const SceneDepth& sceneDepth();

} // namespace as3d::pica
