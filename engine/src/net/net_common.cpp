// The small shared pieces of the net module that belong to no task's file
// (docs/spec/issues/180-online-coop.md 3, 13 "T0 notes"): the build id, the platform and the
// transport state names. AS3D_BUILD_ID is a compile definition of this file only (net/module.cmake),
// so that a new commit recompiles one file.
#include "as3d/net_protocol.h"
#include "as3d/net_transport.h"

#ifndef AS3D_BUILD_ID
#define AS3D_BUILD_ID "unknown"
#endif

namespace as3d::net {

const char* netBuildId() { return AS3D_BUILD_ID; }

const char* peerPlatformName(u8 platform) {
    switch (static_cast<PeerPlatform>(platform)) {
        case PeerPlatform::Desktop: return "desktop";
        case PeerPlatform::Web: return "web";
        case PeerPlatform::Android: return "android";
    }
    return "unknown";
}

PeerPlatform localPlatform() {
#if defined(__EMSCRIPTEN__)
    return PeerPlatform::Web;
#elif defined(__ANDROID__)
    return PeerPlatform::Android;
#else
    return PeerPlatform::Desktop;
#endif
}

const char* transportStateName(Transport::State s) {
    switch (s) {
        case Transport::State::New: return "new";
        case Transport::State::Gathering: return "gathering";
        case Transport::State::SignalReady: return "signal_ready";
        case Transport::State::Connecting: return "connecting";
        case Transport::State::Connected: return "connected";
        case Transport::State::Failed: return "failed";
        case Transport::State::Closed: return "closed";
    }
    return "unknown";
}

} // namespace as3d::net
