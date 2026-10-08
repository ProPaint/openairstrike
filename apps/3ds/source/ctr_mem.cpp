#include "ctr_mem.h"

#include <3ds.h>
#include <malloc.h>

#include <algorithm>
#include <cstddef>

#include "as3d/core.h"
#include "gpu_mesh_pica.h"
#include "terrain_pica.h"
#include "texture_budget.h"
#include "water_renderer_pica.h"

namespace ctrapp {
namespace {
size_t g_heapHigh = 0;

size_t heapUsed() {
    struct mallinfo mi = mallinfo();
    return static_cast<size_t>(mi.uordblks);
}
} // namespace

void heapSample() { g_heapHigh = std::max(g_heapHigh, heapUsed()); }

unsigned heapHighKb() { return static_cast<unsigned>(g_heapHigh / 1024); }

void logMem(const char* when, const char* game, int mission) {
    using namespace as3d;
    heapSample();
    const pica::TextureStats ts = pica::textureStats();
    const pica::MeshMemoryStats ms = pica::meshMemoryStats();
    const pica::TerrainPicaStats& tr = pica::terrainPicaStats();
    const pica::WaterPicaStats& wa = pica::waterPicaStats();
    const size_t geom = ms.bytes + tr.vertexBytes + tr.indexBytes + wa.staticBytes + wa.indexBytes;
    AS3D_INFO("AS3D_MEM heap_kb=%zu heap_hw_kb=%u linear_free_kb=%zu linear_tex_kb=%zu linear_geom_kb=%zu vram_kb=%zu "
              "vram_fallbacks=%d when=%s game=%s mission=%d",
              heapUsed() / 1024, heapHighKb(), static_cast<size_t>(linearSpaceFree()) / 1024, ts.linearBytes / 1024,
              geom / 1024, ts.vramBytes / 1024, ts.vramFallbacks, when, game ? game : "", mission);
}

} // namespace ctrapp
