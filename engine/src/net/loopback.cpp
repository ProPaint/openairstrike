// Two transports joined in process for the tests (as3d/net_transport.h makeLoopbackPair,
// docs/spec/issues/180-online-coop.md 2.4). T0 stub: no pair (both null). T1 makes it real.
#include "as3d/net_transport.h"

namespace as3d::net {

void makeLoopbackPair(std::unique_ptr<Transport>& a, std::unique_ptr<Transport>& b, const LoopbackOptions&) {
    a.reset();
    b.reset();
}

} // namespace as3d::net
