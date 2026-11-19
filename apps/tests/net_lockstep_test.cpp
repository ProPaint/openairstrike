// The lockstep core (as3d/net_lockstep.h, docs/spec/issues/180-online-coop.md 4 and 13 T1) on
// its own, two instances joined by hand (the loopback runs are in net_loopback_test.cpp): the
// delay rule, the steer byte, the pre-filled frames, first-call-wins, the packet's redundancy and
// ack, the hash check, and the pacing.
#include <cmath>

#include "doctest.h"

#include "as3d/core.h"
#include "as3d/net_lockstep.h"
#include "as3d/net_protocol.h"

using namespace as3d;
using namespace as3d::net;

namespace {

InputRecord rec(u16 held, u8 flags = 0, u8 steer = 0) { return InputRecord{held, flags, steer}; }

InputsPacket packetOf(const Lockstep& l) {
    std::vector<u8> bytes;
    l.buildInputsPacket(bytes);
    InputsPacket p;
    REQUIRE(decodeInputs(bytes.data(), bytes.size(), p));
    return p;
}

void deliver(const Lockstep& from, Lockstep& to) { to.addRemote(packetOf(from)); }

} // namespace

TEST_CASE("net lockstep: input delay from the round trip") {
    CHECK(inputDelayForRtt(0) == 2);
    CHECK(inputDelayForRtt(-5) == 2);
    CHECK(inputDelayForRtt(std::nan("")) == 2);
    CHECK(inputDelayForRtt(30) == 2);   // 4.1: 30 ms -> 2 frames
    CHECK(inputDelayForRtt(34) == 3);
    CHECK(inputDelayForRtt(100) == 4);  // 100 ms -> 4
    CHECK(inputDelayForRtt(250) == 9);  // 250 ms -> 9
    CHECK(inputDelayForRtt(300) == 10);
    CHECK(inputDelayForRtt(5000) == 10);
}

TEST_CASE("net lockstep: the steer byte and the table agree") {
    CHECK(steerByte(1, 0) == 0);
    CHECK(steerByte(0, 1) == 64);
    CHECK(steerByte(-1, 0) == 128);
    CHECK(steerByte(0, -1) == 192);
    CHECK(steerByte(0, 0) == 0);
    CHECK(steerByte(1, -0.001f) == 0); // just below the axis rounds back to 0, not 256
    for (int i = 0; i < 256; i++) {
        CAPTURE(i);
        CHECK(steerByte(kSteerTable[i].x, kSteerTable[i].y) == i);
        CHECK(steerByte(kSteerTable[i].x * 37.5f, kSteerTable[i].y * 37.5f) == i); // only the direction counts
        CHECK(std::fabs(kSteerTable[i].x * kSteerTable[i].x + kSteerTable[i].y * kSteerTable[i].y - 1.0f) < 1e-6f);
    }
}

TEST_CASE("net lockstep: frames 0..delay-1 are empty, setLocal fills frame + delay once") {
    Lockstep l(true);
    CHECK(!l.canStep()); // not started
    std::vector<u8> bytes;
    l.buildInputsPacket(bytes);
    CHECK(bytes.empty());
    CHECK(!l.setLocal(rec(1)));

    l.start(42, 1); // clamped to 2
    CHECK(l.delay() == kMinInputDelay);
    l.start(42, 99);
    CHECK(l.delay() == kMaxInputDelay);
    l.start(42, 3);
    CHECK(l.token() == 42);
    CHECK(l.frame() == 0);
    CHECK(l.localAck() == 2);   // the pre-filled remote frames
    CHECK(l.remoteAck() == kNoFrame);
    for (int f = 0; f < 3; f++) {
        REQUIRE(l.canStep());
        CHECK(l.setLocal(rec(static_cast<u16>(100 + f))));
        CHECK(!l.setLocal(rec(999))); // first call wins
        InputRecord h, g;
        l.takeInputs(h, g);
        CHECK(h == InputRecord());
        CHECK(g == InputRecord());
    }
    CHECK(l.frame() == 3);
    CHECK(!l.canStep()); // the remote's frame 3 is missing
    InputRecord h, g;
    l.takeInputs(h, g); // ignored without canStep
    CHECK(l.frame() == 3);

    // The remote's frames 3..5 arrive: frame 3 steps with our first real record (sampled at 0).
    InputRecord remote[3] = {rec(7), rec(8), rec(9)};
    l.addRemote(3, remote, 3, 4);
    CHECK(l.localAck() == 5);
    CHECK(l.remoteAck() == 4);
    REQUIRE(l.canStep());
    l.takeInputs(h, g);
    CHECK(h == rec(100));
    CHECK(g == rec(7));

    // As the guest, the local record is player 2's.
    Lockstep gst(false);
    gst.start(1, 2);
    gst.takeInputs(h, g);
    gst.setLocal(rec(5));
    gst.takeInputs(h, g);
    gst.setLocal(rec(6));
    InputRecord r2[1] = {rec(77)};
    gst.addRemote(2, r2, 1, kNoFrame);
    REQUIRE(gst.canStep());
    gst.takeInputs(h, g);
    CHECK(h == rec(77));
    CHECK(g == rec(5));
}

TEST_CASE("net lockstep: records are normalised; stray remote frames are ignored") {
    Lockstep l(true);
    l.start(1, 2);
    l.setLocal(rec(3, 0xFF, 9));                 // unknown flags dropped, steer kept with kSteer
    InputRecord h0, g0;
    l.takeInputs(h0, g0);
    InputsPacket p = packetOf(l);
    REQUIRE(p.count >= 1);
    CHECK(p.records[p.count - 1] == rec(3, InputRecord::kConfirm | InputRecord::kPause | InputRecord::kSteer, 9));

    Lockstep m(true);
    m.start(1, 2);
    m.setLocal(rec(3, InputRecord::kPause, 9)); // a steer byte without kSteer is cleared
    p = packetOf(m);
    CHECK(p.records[p.count - 1] == rec(3, InputRecord::kPause, 0));

    // Remote frames below frame() and kWindow ahead are ignored; an ack beyond what we set is held.
    InputRecord r[1] = {rec(1)};
    m.addRemote(1000, r, 1, 5000);
    CHECK(m.remoteAck() == 2); // our last record is frame 2
    m.addRemote(2, r, 1, 1);
    CHECK(m.remoteAck() == 2);  // never goes back
    CHECK(m.localAck() == 2);
    InputRecord h, g;
    m.takeInputs(h, g);
    m.takeInputs(h, g);
    REQUIRE(m.canStep());
    m.takeInputs(h, g);
    CHECK(g == rec(1));
    InputRecord late[1] = {rec(50)};
    m.addRemote(2, late, 1, kNoFrame); // frame 2 was taken: no effect
    CHECK(m.frame() == 3);
}

TEST_CASE("net lockstep: packets carry the last 12 records from after the remote's ack") {
    Lockstep a(true), b(false);
    a.start(9, 2);
    b.start(9, 2);
    InputRecord h, g;
    // a runs 20 frames on its own; b holds still (b's records are faked into a).
    for (int f = 0; f < 20; f++) {
        a.setLocal(rec(static_cast<u16>(f)));
        InputRecord r[1] = {rec(0)};
        a.addRemote(static_cast<u32>(f + 2), r, 1, kNoFrame);
        a.takeInputs(h, g);
    }
    InputsPacket p = packetOf(a);
    CHECK(p.token == 9);
    CHECK(p.count == kRedundancy);
    CHECK(p.firstFrame == 21 - (kRedundancy - 1));
    CHECK(p.records[kRedundancy - 1] == rec(19)); // frame 21 = sampled at frame 19
    CHECK(p.ack == 21);
    // Once b acks frame 18, only 19..21 go out.
    InputRecord none[1] = {rec(0)};
    a.addRemote(22, none, 1, 18);
    p = packetOf(a);
    CHECK(p.firstFrame == 19);
    CHECK(p.count == 3);
    // All acked: still one record, carrying the ack.
    a.addRemote(23, none, 1, 21);
    p = packetOf(a);
    CHECK(p.firstFrame == 21);
    CHECK(p.count == 1);
    CHECK(p.ack == 23);

    // A duplicate or reordered delivery changes nothing.
    b.addRemote(packetOf(a));
    b.addRemote(packetOf(a));
    CHECK(b.localAck() == 1); // b holds a's 0, 1 (pre-filled), not 2..18 (never sent to b)
}

TEST_CASE("net lockstep: the hash check flags the first mismatch, whichever side hashed first") {
    Lockstep l(true);
    l.start(1, 2);
    l.noteLocalHash(20, 0xAAAA);
    l.checkRemoteHash(20, 0xAAAA);
    CHECK(!l.desynced());
    l.checkRemoteHash(40, 0xBBBB); // the remote is ahead: kept
    CHECK(!l.desynced());
    l.noteLocalHash(40, 0xBBBB);
    CHECK(!l.desynced());
    l.checkRemoteHash(60, 0x1234);
    l.noteLocalHash(60, 0x5678);
    u32 frame = 0;
    u64 mine = 0, yours = 0;
    REQUIRE(l.desynced(&frame, &mine, &yours));
    CHECK(frame == 60);
    CHECK(mine == 0x5678);
    CHECK(yours == 0x1234);
    l.noteLocalHash(80, 1);
    l.checkRemoteHash(80, 2); // the first one stays
    l.desynced(&frame, &mine, &yours);
    CHECK(frame == 60);

    l.start(2, 2); // a new level forgets it all
    CHECK(!l.desynced());
    l.noteLocalHash(20, 5);
    l.checkRemoteHash(20, 6);
    REQUIRE(l.desynced(&frame));
    CHECK(frame == 20);

    // A remote hash of a frame we passed without hashing, or one that fell out of the ring, is
    // dropped; the 64 most recent own hashes are kept.
    Lockstep m(true);
    m.start(1, 2);
    for (u32 f = 20; f <= 20 * 70; f += 20) m.noteLocalHash(f, f);
    m.checkRemoteHash(20, 999); // out of the ring
    m.checkRemoteHash(30, 999); // never hashed
    CHECK(!m.desynced());
    m.checkRemoteHash(20 * 70 - 20 * 63, 1); // the oldest kept
    CHECK(m.desynced(&frame));
    CHECK(frame == 20 * 7);
}

// One side steps every display frame, the other every second one (held back: a slow machine or
// a throttled page). The fast side runs into the stall without pacing; shouldThrottle must fire
// for it, and never for the slow side. Zero latency, packets swapped by hand each frame.
TEST_CASE("net lockstep: shouldThrottle fires for the side that runs ahead") {
    Lockstep fast(true), slow(false);
    fast.start(3, 4);
    slow.start(3, 4);
    int fastThrottles = 0, slowThrottles = 0, fastStalls = 0;
    u32 lastThrottleFrame = 0;
    bool spacingOk = true;
    InputRecord h, g;
    for (int tick = 0; tick < 600; tick++) {
        if (fast.shouldThrottle()) {
            if (fastThrottles > 0 && fast.frame() - lastThrottleFrame < 10) spacingOk = false;
            lastThrottleFrame = fast.frame();
            fastThrottles++;
        } else {
            fast.setLocal(rec(1));
            if (fast.canStep()) fast.takeInputs(h, g);
            else fastStalls++;
        }
        deliver(fast, slow);
        if (tick % 2 == 0) {
            if (slow.shouldThrottle()) slowThrottles++;
            slow.setLocal(rec(2));
            if (slow.canStep()) slow.takeInputs(h, g);
            deliver(slow, fast);
        }
    }
    MESSAGE("fast: " << fastThrottles << " throttles, " << fastStalls << " stalls; slow at frame " << slow.frame());
    CHECK(fastThrottles > 0);
    CHECK(slowThrottles == 0);
    CHECK(spacingOk);
    CHECK(fast.frame() <= slow.frame() + 4 + 1);

    // Balanced, no throttle at all.
    Lockstep a(true), b(false);
    a.start(4, 2);
    b.start(4, 2);
    int throttles = 0;
    for (int tick = 0; tick < 600; tick++) {
        if (a.shouldThrottle()) throttles++;
        a.setLocal(rec(1));
        REQUIRE(a.canStep());
        a.takeInputs(h, g);
        deliver(a, b);
        if (b.shouldThrottle()) throttles++;
        b.setLocal(rec(2));
        REQUIRE(b.canStep());
        b.takeInputs(h, g);
        deliver(b, a);
    }
    CHECK(throttles == 0);
}
