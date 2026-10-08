// Memory accounting of the 3DS app against the plan's budgets (docs/3ds-port.md M5.4): the
// heap (malloc, mallinfo), the linear memory left, the linear textures, the static geometry
// (meshes, terrain, water) and the VRAM textures.
#pragma once

namespace ctrapp {

// Raises the heap high-water mark (bytes in use by malloc) to the current value. Called by the
// perf record every 32 frames, by logMem and at the summary.
void heapSample();
// The highest malloc'd bytes seen by heapSample since the process started, in KB.
unsigned heapHighKb();

// One log line:
//   AS3D_MEM heap_kb=N heap_hw_kb=N linear_free_kb=N linear_tex_kb=N linear_geom_kb=N vram_kb=N
//            vram_fallbacks=N when=start|end game=KEY mission=N
// linear_geom_kb: the live GpuMesh buffers (vertex + index) plus the terrain and water renderers'
// vertex and index buffers. `game` may be empty, `mission` 0 when unknown.
void logMem(const char* when, const char* game, int mission);

} // namespace ctrapp
