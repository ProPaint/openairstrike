// Same game data for online play (docs/spec/issues/180-online-coop.md 5.3): SHA-256 over the
// mounted files that can reach the simulation, in sorted path order, each as `path, u32 size,
// bytes`: every entry of vfs.list("") except the media types (textures, sounds, music, fonts,
// videos; the constexpr list is in engine/src/game/sim_fingerprint.cpp). Copies that differ only
// in pictures or in the pak layout simulate alike and give the same fingerprint. Reads ~20 to
// 40 MB: computed once per game session (NetPlay caches it) when the Online screen first opens.
#pragma once

#include <string>

#include "as3d/core.h"

namespace as3d {

class Vfs;

// 64 lower-case hex digits (logged as AS3D_NET fingerprint=...).
std::string simDataFingerprint(Vfs& vfs);
// The same digest as 32 bytes (Hello::simFingerprint).
void simDataFingerprintBytes(Vfs& vfs, u8 out[32]);

} // namespace as3d
