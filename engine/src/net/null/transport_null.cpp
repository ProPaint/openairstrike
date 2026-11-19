// The null backend of as3d/net_transport.h (docs/spec/issues/180-online-coop.md 12): the 3DS
// and AS3D_WITH_NET=OFF. No transport; the front end is never offered "Online" (the window sets
// no OnlineHost), and anything that asks anyway gets the not_built message.
#include "as3d/net_transport.h"

namespace as3d::net {

std::unique_ptr<Transport> makeRtcTransport(const TransportConfig&, std::string* error) {
    if (error) *error = "online play is not in this build";
    return nullptr;
}

bool rtcTransportAvailable() { return false; }

} // namespace as3d::net
