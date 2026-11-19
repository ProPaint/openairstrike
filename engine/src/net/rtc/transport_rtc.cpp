// The rtc backend of as3d/net_transport.h (docs/spec/issues/180-online-coop.md 2): WebRTC data
// channels through libdatachannel (desktop, Android) or datachannel-wasm (the web), one source
// with #ifdef __EMSCRIPTEN__ where the wasm subset differs. T0 stub: no third-party code yet, so
// it behaves as the null backend except that it says the transport is coming (T3 makes it real).
#include "as3d/net_transport.h"

namespace as3d::net {

std::unique_ptr<Transport> makeRtcTransport(const TransportConfig&, std::string* error) {
    if (error) *error = "online play is not in this build";
    return nullptr;
}

// False until T3: the window offers "Online" only once a transport can be made.
bool rtcTransportAvailable() { return false; }

} // namespace as3d::net
