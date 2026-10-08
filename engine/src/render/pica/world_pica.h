// The PICA WorldRenderer's own counters (3DS only, world_renderer_pica.cpp), for the composer's
// logs: how many of the last prepare()'s model records draw from a linear-memory texture.
#pragma once

#include <string>

namespace as3d::pica {

struct WorldPicaStats {
    int records = 0;         // model records of the last prepare()
    int linearRecords = 0;   // of which with a texture in linear memory (over the VRAM budget)
    std::string linearSkins; // their definitions, each once (" name name ...")
};

const WorldPicaStats& worldPicaStats();

} // namespace as3d::pica
