// The simulation data fingerprint (as3d/sim_fingerprint.h, docs/spec/issues/180-online-coop.md
// 5.3): SHA-256 of the mounted non-media files in sorted path order. T0 stub: all zeros (every
// pair of peers agrees). T2 makes it real, with the constexpr list of excluded media extensions
// here.
#include <cstring>

#include "as3d/sim_fingerprint.h"
#include "as3d/vfs.h"

namespace as3d {

void simDataFingerprintBytes(Vfs&, u8 out[32]) { std::memset(out, 0, 32); }

std::string simDataFingerprint(Vfs&) { return std::string(64, '0'); }

} // namespace as3d
