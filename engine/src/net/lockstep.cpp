// Delay-based lockstep (as3d/net_lockstep.h, docs/spec/issues/180-online-coop.md 4). T0 stub:
// nothing is ever ready to step. T1 makes it real.
#include "as3d/net_lockstep.h"

namespace as3d::net {

int inputDelayForRtt(double) { return kMinInputDelay; }
u8 steerByte(float, float) { return 0; }

void Lockstep::start(u32 token, int delay) {
    token_ = token;
    delay_ = delay < kMinInputDelay ? kMinInputDelay : delay > kMaxInputDelay ? kMaxInputDelay : delay;
    frame_ = 0;
    remoteAck_ = kNoFrame;
    stalls_ = 0;
    desync_ = false;
}
bool Lockstep::setLocal(const InputRecord&) { return false; }
void Lockstep::addRemote(u32, const InputRecord*, int, u32) {}
bool Lockstep::canStep() const { return false; }
void Lockstep::takeInputs(InputRecord& host, InputRecord& guest) {
    host = guest = InputRecord();
}
void Lockstep::buildInputsPacket(std::vector<u8>& out) const { out.clear(); }
u32 Lockstep::localAck() const { return kNoFrame; }
bool Lockstep::shouldThrottle() { return false; }
void Lockstep::noteLocalHash(u32, u64) {}
void Lockstep::checkRemoteHash(u32, u64) {}
bool Lockstep::desynced(u32*, u64*, u64*) const { return false; }

} // namespace as3d::net
