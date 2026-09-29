// Spawn-order bookkeeping (docs/spec/hmap.md "Spawn order", "Spawning").
#include <algorithm>

#include "as3d/terrain.h"

namespace as3d {

void SpawnCursor::build(const std::vector<Placement>& placements) {
    sorted_.clear();
    sorted_.reserve(placements.size());
    for (const Placement& p : placements) sorted_.push_back(&p);
    // Stable sort by y (the original bubble-sorts, swapping only on strictly greater).
    std::stable_sort(sorted_.begin(), sorted_.end(),
                     [](const Placement* a, const Placement* b) { return a->y < b->y; });
    cursor_ = 0;
}

void SpawnCursor::reset() { cursor_ = 0; }

SpawnCursor::Result SpawnCursor::update(float mapPos) {
    Result r;
    float nearRow = (mapPos - 64.0f) / kHmapCellSize;
    float farRow = (mapPos + 1000.0f) / kHmapCellSize;
    while (cursor_ < sorted_.size()) {
        const Placement* p = sorted_[cursor_];
        float y = static_cast<float>(p->y);
        if (y > farRow) break;
        if (y < nearRow) r.dropped.push_back(p);
        else r.toSpawn.push_back(p);
        cursor_++;
    }
    return r;
}

} // namespace as3d
