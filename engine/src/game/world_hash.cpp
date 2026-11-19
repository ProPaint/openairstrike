// World::stateHash and stateHashFull (as3d/world.h, docs/spec/issues/180-online-coop.md 4.4):
// FNV-1a 64 over the simulation's cheap identifying state in a fixed order (frame, RNG, time,
// map position, level flags, the players, the entities in use, the particles), sent every
// kHashEvery frames by online peers; the full form hashes dumpStateJson() for the tests.
// T0 stub: both 0 (every comparison matches). T2 makes them real.
#include "as3d/world.h"

namespace as3d {

u64 World::stateHash() const { return 0; }
u64 World::stateHashFull() const { return 0; }

} // namespace as3d
