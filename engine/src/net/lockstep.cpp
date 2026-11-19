// Delay-based lockstep (as3d/net_lockstep.h, docs/spec/issues/180-online-coop.md 4).
//
// Each side keeps two rings of kWindow slots keyed by frame number: its own records (set kDelay
// frames ahead, kept back to the last kRedundancy for resending) and the remote's (frame() up to
// what has arrived). A slot is valid when its `frame` equals the frame asked for, so nothing is
// ever cleared between frames, only at start().
//
// Pacing (4.1, refined here): each Inputs packet tells the receiver how far ahead the sender
// was. The last record of a packet is the sender's frame + delay - 1 (it set frame + delay, then
// stepped), its ack the last of our records it held, which we set at our frame ack - delay. So
// the sender's lead over us as it saw it is `last - ack`, and ours over it, measured the same way
// at the moment we are about to step, is `frame() + delay() - remoteHigh`. On a balanced link both
// leads are the one-way latency in frames; when one side's clock runs faster its lead grows and
// the other's shrinks, the difference being twice the time it is ahead. shouldThrottle fires at a
// difference of kThrottleGap (about a frame and a half ahead), which is before the stall (a lead
// of `delay` frames) on the links the delay was chosen for, so the faster side gives back a step
// at a time instead of running into the stall. (The spec's first rule, frame - remoteAck >
// delay + 2, cannot fire while the remote keeps acking and fires too late when it does: the
// stall comes first.)
#include "as3d/net_lockstep.h"

#include <cmath>

namespace as3d::net {

namespace {

constexpr i64 kThrottleGap = 3;
constexpr u32 kThrottleEvery = 10; // frames between two throttles at most (4.1)

// Only the flags a version-1 peer may send, and the steer byte only with kSteer: what goes out
// must decode on the other side (decodeInputs refuses anything else) and both sides must keep the
// same bits.
InputRecord normalized(const InputRecord& r) {
    InputRecord n = r;
    n.flags &= InputRecord::kConfirm | InputRecord::kPause | InputRecord::kSteer;
    if (!(n.flags & InputRecord::kSteer)) n.steer = 0;
    return n;
}

} // namespace

// clamp(ceil((rtt / 2) / (1000 / 60)) + 1, 2, 10); the epsilon keeps exact multiples (100 ms ->
// 3 frames of one-way latency -> 4) from rounding up on the float division.
int inputDelayForRtt(double rttMs) {
    if (!(rttMs > 0.0)) return kMinInputDelay;
    double oneWayFrames = rttMs * 0.5 * 60.0 / 1000.0;
    double d = std::ceil(oneWayFrames - 1e-9) + 1.0;
    if (d < kMinInputDelay) return kMinInputDelay;
    if (d > kMaxInputDelay) return kMaxInputDelay;
    return static_cast<int>(d);
}

// round(atan2(dy, dx) * 256 / 2 pi) & 255; a zero vector is 0 (the caller sends no kSteer then).
u8 steerByte(float dx, float dy) {
    if (dx == 0.0f && dy == 0.0f) return 0;
    double a = std::atan2(static_cast<double>(dy), static_cast<double>(dx));
    long v = std::lround(a * 256.0 / (2.0 * 3.14159265358979323846));
    return static_cast<u8>(v & 255);
}

void Lockstep::start(u32 token, int delay) {
    token_ = token;
    delay_ = delay < kMinInputDelay ? kMinInputDelay : delay > kMaxInputDelay ? kMaxInputDelay : delay;
    started_ = true;
    frame_ = 0;
    remoteAck_ = kNoFrame;
    stalls_ = 0;
    throttled_ = false;
    lastThrottle_ = 0;
    for (Slot& s : local_) s = Slot();
    for (Slot& s : remote_) s = Slot();
    for (int f = 0; f < delay_; f++) {
        local_[f].frame = static_cast<u32>(f);
        remote_[f].frame = static_cast<u32>(f);
    }
    localHigh_ = delay_ - 1;
    remoteContig_ = delay_ - 1;
    remoteHigh_ = delay_ - 1;
    remoteLead_ = 0;
    remoteLeadValid_ = false;
    for (HashSlot& h : localHash_) h = HashSlot();
    localHashNext_ = 0;
    remoteHashes_.clear();
    desync_ = false;
    desyncFrame_ = 0;
    desyncMine_ = desyncYours_ = 0;
}

const Lockstep::Slot* Lockstep::find(const Slot* ring, i64 frame) const {
    if (frame < 0 || frame >= static_cast<i64>(kNoFrame)) return nullptr;
    const Slot& s = ring[static_cast<u64>(frame) & (kWindow - 1)];
    return s.frame == static_cast<u32>(frame) ? &s : nullptr;
}

// Sets frame() + delay(), and any frame between the last one set and it with the same record
// (only when the caller stepped without setting one: the local ring must stay gap-free, or
// canStep would wait for a record that never comes).
bool Lockstep::setLocal(const InputRecord& r) {
    if (!started_) return false;
    i64 target = static_cast<i64>(frame_) + delay_;
    if (localHigh_ >= target) return false;
    InputRecord n = normalized(r);
    for (i64 f = localHigh_ + 1; f <= target; f++) {
        Slot& s = local_[static_cast<u64>(f) & (kWindow - 1)];
        s.frame = static_cast<u32>(f);
        s.rec = n;
    }
    localHigh_ = target;
    return true;
}

// Frames below frame() were taken already; frames kWindow or more ahead cannot be legitimate (a
// peer is never more than its delay plus ours ahead) and would overwrite live slots.
void Lockstep::addRemote(u32 firstFrame, const InputRecord* records, int n, u32 ack) {
    if (!started_ || !records || n <= 0) return;
    i64 last = static_cast<i64>(firstFrame) + n - 1;
    for (int i = 0; i < n; i++) {
        i64 f = static_cast<i64>(firstFrame) + i;
        if (f < frame_ || f >= static_cast<i64>(frame_) + kWindow) continue;
        Slot& s = remote_[static_cast<u64>(f) & (kWindow - 1)];
        if (s.frame == static_cast<u32>(f)) continue;
        s.frame = static_cast<u32>(f);
        s.rec = normalized(records[i]);
    }
    while (find(remote_, remoteContig_ + 1)) remoteContig_++;
    // An ack beyond what we ever set is a broken peer's: held at our last record.
    if (ack != kNoFrame) {
        i64 a = static_cast<i64>(ack) > localHigh_ ? localHigh_ : static_cast<i64>(ack);
        if (remoteAck_ == kNoFrame || a > static_cast<i64>(remoteAck_)) remoteAck_ = static_cast<u32>(a);
        // Until the remote's ack covers one of our real records (frame delay() or later) its lead
        // is measured against the pre-filled frames and reads too small: no pacing before that.
        if (last >= remoteHigh_ && static_cast<i64>(ack) >= delay_) {
            remoteLead_ = last - static_cast<i64>(ack);
            remoteLeadValid_ = true;
        }
    }
    if (last > remoteHigh_) remoteHigh_ = last;
}

bool Lockstep::canStep() const { return started_ && find(local_, frame_) && find(remote_, frame_); }

void Lockstep::takeInputs(InputRecord& host, InputRecord& guest) {
    if (!canStep()) {
        host = guest = InputRecord();
        return;
    }
    const InputRecord& mine = find(local_, frame_)->rec;
    const InputRecord& theirs = find(remote_, frame_)->rec;
    host = host_ ? mine : theirs;
    guest = host_ ? theirs : mine;
    frame_++;
}

// The last kRedundancy local records, starting after what the remote acknowledged (it holds those
// with no gap), and always at least the last one, which carries our ack.
void Lockstep::buildInputsPacket(std::vector<u8>& out) const {
    out.clear();
    if (!started_ || localHigh_ < 0) return;
    i64 first = localHigh_ - (kRedundancy - 1);
    if (first < 0) first = 0;
    if (remoteAck_ != kNoFrame && static_cast<i64>(remoteAck_) + 1 > first) first = static_cast<i64>(remoteAck_) + 1;
    if (first > localHigh_) first = localHigh_;
    InputsPacket p;
    p.token = token_;
    p.ack = localAck();
    p.firstFrame = static_cast<u32>(first);
    p.count = static_cast<u8>(localHigh_ - first + 1);
    for (int i = 0; i < p.count; i++) {
        const Slot* s = find(local_, first + i);
        if (s) p.records[i] = s->rec;
    }
    encodeInputs(p, out);
}

u32 Lockstep::localAck() const { return remoteContig_ < 0 ? kNoFrame : static_cast<u32>(remoteContig_); }

bool Lockstep::shouldThrottle() {
    if (!canStep() || !remoteLeadValid_) return false; // stalled anyway, or nothing to compare yet
    if (throttled_ && frame_ - lastThrottle_ < kThrottleEvery) return false;
    i64 ourLead = static_cast<i64>(frame_) + delay_ - remoteHigh_;
    if (ourLead - remoteLead_ < kThrottleGap) return false;
    throttled_ = true;
    lastThrottle_ = frame_;
    return true;
}

void Lockstep::compareHash(u32 frame, u64 mine, u64 yours) {
    if (mine == yours || desync_) return;
    desync_ = true;
    desyncFrame_ = frame;
    desyncMine_ = mine;
    desyncYours_ = yours;
}

void Lockstep::noteLocalHash(u32 frame, u64 hash) {
    localHash_[localHashNext_] = HashSlot{frame, hash};
    localHashNext_ = (localHashNext_ + 1) % kHashKeep;
    for (size_t i = 0; i < remoteHashes_.size(); i++) {
        if (remoteHashes_[i].frame != frame) continue;
        compareHash(frame, hash, remoteHashes_[i].hash);
        remoteHashes_.erase(remoteHashes_.begin() + static_cast<std::ptrdiff_t>(i));
        break;
    }
}

// A remote hash of a frame we hashed is compared at once; one of a frame we have not reached is
// kept (at most kHashKeep, the oldest dropped); one of a frame we passed without a hash, or whose
// hash fell out of the ring, is dropped.
void Lockstep::checkRemoteHash(u32 frame, u64 hash) {
    bool passed = false;
    for (const HashSlot& h : localHash_) {
        if (h.frame == kNoFrame) continue;
        if (h.frame == frame) {
            compareHash(frame, h.hash, hash);
            return;
        }
        if (h.frame > frame) passed = true;
    }
    if (passed) return;
    if (remoteHashes_.size() >= static_cast<size_t>(kHashKeep)) remoteHashes_.erase(remoteHashes_.begin());
    remoteHashes_.push_back(HashSlot{frame, hash});
}

bool Lockstep::desynced(u32* frame, u64* mine, u64* yours) const {
    if (!desync_) return false;
    if (frame) *frame = desyncFrame_;
    if (mine) *mine = desyncMine_;
    if (yours) *yours = desyncYours_;
    return true;
}

} // namespace as3d::net
