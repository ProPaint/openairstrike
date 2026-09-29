// Values the sequels read from an object definition that the definition loader
// (as3d/defs.h, another package) is being taught at the same time: `speed` and `civilian`
// (as2/engine-behaviour.delta.md 3.1). The world reads them only through these accessors,
// so connecting the loader's new fields is a change of this header's implementation
// (engine/src/game/world.cpp) alone.
#pragma once

namespace as3d {

struct ObjectDef;

// The definition's `speed` (copied into field 23 at spawn in the sequels; 3.1.3). False when
// the definition has none (field 23 then starts at 0 as in the first game).
// TODO(orchestrator): return ObjectDef::speed once defs.h has the field. Until then only the
// player helicopters (class player; the only definitions that carry `speed` in the as2 data)
// are given a temporary 1.0.
bool objectDefSpeed(const ObjectDef& def, float* out);

// Class value (script field 2) of a definition: 1 player, 2 enemy, 3 item, 5 civilian.
// TODO(orchestrator): if the loader keeps `civilian` apart from ObjectDef::kind (a flag, not
// ObjectKind value 5), return 5 for such definitions here.
float objectDefClass(const ObjectDef& def);

} // namespace as3d
