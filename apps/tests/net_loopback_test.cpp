// The loopback transport pair (as3d/net_transport.h makeLoopbackPair) and two lockstep peers
// over it (docs/spec/issues/180-online-coop.md 4 and 13 T1): 3,000 frames of random input at 0,
// 50 and 150 ms each way and 0, 10 and 30 % loss on the inputs channel, both sides must take
// the same (host, guest) records every frame; no stall at 0 % loss with the computed delay, few
// at 30 %; a hash mismatch injected on one side is flagged on both. Everything runs on a test
// clock, so a run is the same every time and takes no wall time.
#include <algorithm>
#include <memory>

#include "doctest.h"

#include "as3d/core.h"
#include "as3d/net_lockstep.h"
#include "as3d/net_protocol.h"
#include "as3d/net_signal.h"
#include "as3d/net_transport.h"

using namespace as3d;
using namespace as3d::net;

namespace {

struct TestClock {
    double now = 1000.0;
};

using State = Transport::State;

// A connected pair on the test clock, through the real signalling calls (and the codes).
void connectPair(std::unique_ptr<Transport>& a, std::unique_ptr<Transport>& b, std::shared_ptr<TestClock> clock,
                 int latencyMs, int lossPercent, u32 seed) {
    LoopbackOptions o;
    o.latencyMs = latencyMs;
    o.lossPercent = lossPercent;
    o.seed = seed;
    o.nowSeconds = [clock] { return clock->now; };
    makeLoopbackPair(a, b, o);
    REQUIRE(a);
    REQUIRE(b);
    a->hostOffer();
    a->update();
    SignalInfo invite, reply, viaCode;
    REQUIRE(a->state() == State::SignalReady);
    REQUIRE(a->localSignal(invite));
    REQUIRE(decodeSignalCode(encodeSignalCode(invite), viaCode));
    b->joinOffer(viaCode);
    b->update();
    REQUIRE(b->localSignal(reply));
    REQUIRE(decodeSignalCode(encodeSignalCode(reply), viaCode));
    a->acceptAnswer(viaCode);
    for (int i = 0; i < 100 && (a->state() != State::Connected || b->state() != State::Connected); i++) {
        clock->now += 1.0 / 60.0;
        a->update();
        b->update();
    }
    REQUIRE(a->state() == State::Connected);
    REQUIRE(b->state() == State::Connected);
}

// One peer of the run: what the game side (NetPlay, 6.1) does per display frame, with random
// input.
struct Peer {
    std::unique_ptr<Transport> transport;
    Lockstep lockstep;
    Rng rng;
    std::vector<InputRecord> mine;  // the record set for each frame (empty for 0..delay-1)
    std::vector<InputRecord> hostTaken, guestTaken;
    int throttles = 0;
    u32 faultFrame = kNoFrame;      // from this frame on, this side's hashes are wrong
    u64 runningHash = 1469598103934665603ull;
    int protocolErrors = 0;

    Peer(bool host, u32 seed) : lockstep(host), rng(seed) {}

    InputRecord randomRecord() {
        InputRecord r;
        r.held = static_cast<u16>(rng.next() & 0x3FF);
        r.flags = static_cast<u8>(rng.next() & 7);
        if (r.flags & InputRecord::kSteer) r.steer = static_cast<u8>(rng.next());
        return r;
    }

    void displayFrame() {
        transport->update();
        std::vector<u8> msg;
        while (transport->pollInputs(msg)) {
            InputsPacket p;
            if (!decodeInputs(msg.data(), msg.size(), p)) {
                protocolErrors++;
                continue;
            }
            if (p.token == lockstep.token()) lockstep.addRemote(p);
        }
        while (transport->pollCtl(msg)) {
            HashMsg h;
            if (!decodeHash(msg.data(), msg.size(), h)) {
                protocolErrors++;
                continue;
            }
            if (h.token == lockstep.token()) lockstep.checkRemoteHash(h.frame, h.hash);
        }
        if (lockstep.shouldThrottle()) {
            throttles++;
        } else {
            InputRecord r = randomRecord();
            u32 target = lockstep.frame() + static_cast<u32>(lockstep.delay());
            if (lockstep.setLocal(r)) {
                if (mine.size() <= target) mine.resize(target + 1);
                mine[target] = r;
            }
            if (lockstep.canStep()) {
                InputRecord h, g;
                lockstep.takeInputs(h, g);
                hostTaken.push_back(h);
                guestTaken.push_back(g);
                // The "world": a running FNV-1a over what was simulated, hashed every 20 frames.
                for (u8 byte : {static_cast<u8>(h.held), static_cast<u8>(h.held >> 8), h.flags, h.steer,
                                static_cast<u8>(g.held), static_cast<u8>(g.held >> 8), g.flags, g.steer})
                    runningHash = (runningHash ^ byte) * 1099511628211ull;
                u32 f = lockstep.frame() - 1;
                if (f % kHashEvery == 0) {
                    u64 hash = runningHash ^ (f >= faultFrame ? 1 : 0);
                    lockstep.noteLocalHash(f, hash);
                    std::vector<u8> out;
                    encodeHash(HashMsg{lockstep.token(), f, hash}, out);
                    transport->sendCtl(out.data(), out.size());
                }
            } else {
                lockstep.noteStall();
            }
        }
        std::vector<u8> packet;
        lockstep.buildInputsPacket(packet);
        if (!packet.empty()) transport->sendInputs(packet.data(), packet.size());
    }
};

struct RunResult {
    u32 stallsHost = 0, stallsGuest = 0;
    int throttles = 0;
    size_t frames = 0;
};

// Host and guest from the same level start; each display frame the host goes first (the order
// within one frame is arbitrary in reality; the test clock makes it fixed).
RunResult runPair(int latencyMs, int lossPercent, u32 seed, int frames, u32 guestFault = kNoFrame,
                  Peer** keepHost = nullptr, Peer** keepGuest = nullptr) {
    auto clock = std::make_shared<TestClock>();
    auto host = std::make_unique<Peer>(true, seed * 3 + 1);
    auto guest = std::make_unique<Peer>(false, seed * 5 + 2);
    connectPair(host->transport, guest->transport, clock, latencyMs, lossPercent, seed);
    int delay = inputDelayForRtt(2.0 * latencyMs);
    host->lockstep.start(77, delay);
    guest->lockstep.start(77, delay);
    guest->faultFrame = guestFault;
    double t0 = clock->now;
    for (int tick = 0; tick < frames * 4; tick++) {
        clock->now = t0 + tick / 60.0;
        host->displayFrame();
        guest->displayFrame();
        if (host->hostTaken.size() >= static_cast<size_t>(frames) && guest->hostTaken.size() >= static_cast<size_t>(frames))
            break;
    }
    RunResult r;
    r.stallsHost = host->lockstep.stalls();
    r.stallsGuest = guest->lockstep.stalls();
    r.throttles = host->throttles + guest->throttles;
    r.frames = std::min(host->hostTaken.size(), guest->hostTaken.size());

    CHECK(host->protocolErrors == 0);
    CHECK(guest->protocolErrors == 0);
    CHECK(r.frames >= static_cast<size_t>(frames));
    // Every frame: both sides took the same pair, and each pair is what the two sides set.
    bool same = true, faithful = true;
    for (size_t f = 0; f < r.frames; f++) {
        if (host->hostTaken[f] != guest->hostTaken[f] || host->guestTaken[f] != guest->guestTaken[f]) same = false;
        InputRecord wantHost = f < static_cast<size_t>(delay) ? InputRecord() : host->mine[f];
        InputRecord wantGuest = f < static_cast<size_t>(delay) ? InputRecord() : guest->mine[f];
        if (host->hostTaken[f] != wantHost || guest->guestTaken[f] != wantGuest) faithful = false;
    }
    CHECK(same);
    CHECK(faithful);
    if (keepHost) *keepHost = host.release();
    if (keepGuest) *keepGuest = guest.release();
    return r;
}

} // namespace

TEST_CASE("net loopback: the signalling calls, latency, ordering, loss and close") {
    auto clock = std::make_shared<TestClock>();
    std::unique_ptr<Transport> a, b;
    LoopbackOptions o;
    o.latencyMs = 50;
    o.lossPercent = 30;
    o.seed = 9;
    o.nowSeconds = [clock] { return clock->now; };
    makeLoopbackPair(a, b, o);
    CHECK(a->state() == State::New);
    SignalInfo invite, reply;
    CHECK(!a->localSignal(invite));
    a->hostOffer();
    CHECK(a->state() == State::Gathering);
    a->update();
    REQUIRE(a->localSignal(invite));
    CHECK(invite.kind == SignalKind::Invite);
    CHECK(!encodeSignalCode(invite).empty());

    // Not this pair's invite: the guest fails.
    {
        std::unique_ptr<Transport> c, d;
        makeLoopbackPair(c, d, o);
        SignalInfo wrong = invite;
        wrong.ufrag = "zzzz";
        d->joinOffer(wrong);
        CHECK(d->state() == State::Failed);
        CHECK(!d->error().empty());
    }

    b->joinOffer(invite);
    b->update();
    CHECK(b->state() == State::SignalReady);
    REQUIRE(b->localSignal(reply));
    CHECK(reply.kind == SignalKind::Reply);
    const u8 early[] = {1, 2, 3};
    b->sendCtl(early, 3); // not connected yet: dropped
    a->acceptAnswer(reply);
    CHECK(a->state() == State::Connecting);
    b->update();
    CHECK(b->state() == State::Connecting);
    clock->now += 0.049;
    a->update();
    CHECK(a->state() == State::Connecting);
    clock->now += 0.002;
    a->update();
    b->update();
    CHECK(a->state() == State::Connected);
    CHECK(b->state() == State::Connected);

    // ctl: in order, never lost, delivered after the latency; oversize messages are not sent.
    std::vector<u8> msg;
    CHECK(!a->pollCtl(msg));
    for (u8 i = 0; i < 100; i++) {
        u8 m[2] = {i, static_cast<u8>(i * 3)};
        b->sendCtl(m, 2);
    }
    std::vector<u8> big(kMaxCtlMessage + 1, 7);
    b->sendCtl(big.data(), big.size());
    CHECK(!a->pollCtl(msg));
    clock->now += 0.0499;
    CHECK(!a->pollCtl(msg));
    clock->now += 0.0002;
    for (u8 i = 0; i < 100; i++) {
        REQUIRE(a->pollCtl(msg));
        CHECK(msg == std::vector<u8>{i, static_cast<u8>(i * 3)});
    }
    CHECK(!a->pollCtl(msg));

    // inputs: about 30 % lost, deterministic from the seed.
    for (int i = 0; i < 1000; i++) {
        u8 m[1] = {static_cast<u8>(i)};
        a->sendInputs(m, 1);
    }
    clock->now += 0.1;
    int got = 0;
    while (b->pollInputs(msg)) got++;
    MESSAGE("inputs delivered at 30 % loss: " << got << " of 1000");
    CHECK(got > 620);
    CHECK(got < 780);

    // close: this side Closed, the other Failed (Closed) once the latency has passed.
    a->close();
    CHECK(a->state() == State::Closed);
    b->update();
    CHECK(b->state() == State::Connected);
    clock->now += 0.06;
    b->update();
    CHECK(b->state() == State::Failed);
    CHECK(b->failure() == Transport::Failure::Closed);
    CHECK(!b->error().empty());
}

TEST_CASE("net loopback: two lockstep peers agree on every frame at 0-150 ms and 0-30 % loss") {
    const int latencies[] = {0, 50, 150};
    const int losses[] = {0, 10, 30};
    for (int latency : latencies) {
        for (int loss : losses) {
            CAPTURE(latency);
            CAPTURE(loss);
            RunResult r = runPair(latency, loss, static_cast<u32>(latency * 100 + loss + 1), 3000);
            MESSAGE("latency " << latency << " ms, loss " << loss << " %: delay " << inputDelayForRtt(2.0 * latency)
                               << ", stalls host " << r.stallsHost << " guest " << r.stallsGuest << ", throttles "
                               << r.throttles);
            if (loss == 0) {
                CHECK(r.stallsHost == 0);
                CHECK(r.stallsGuest == 0);
                CHECK(r.throttles == 0);
            } else {
                // With the delay chosen for the latency the margin is one or two packets: a stall
                // needs two (or more) packets in a row lost, about 9 % of frames at 30 % loss
                // (measured 7 to 12 %), up to 2 % at 10 %. Redundancy keeps every stall to a frame or
                // two; the bound is per side.
                const u32 bound = loss >= 30 ? 450u : 120u;
                CHECK(r.stallsHost < bound);
                CHECK(r.stallsGuest < bound);
            }
        }
    }
}

TEST_CASE("net loopback: the same seed gives the same run") {
    RunResult a = runPair(50, 30, 5, 1000);
    RunResult b = runPair(50, 30, 5, 1000);
    CHECK(a.stallsHost == b.stallsHost);
    CHECK(a.stallsGuest == b.stallsGuest);
    CHECK(a.throttles == b.throttles);
}

TEST_CASE("net loopback: a hash mismatch on one side is flagged on both") {
    Peer* host = nullptr;
    Peer* guest = nullptr;
    runPair(50, 10, 11, 1200, 900, &host, &guest);
    std::unique_ptr<Peer> h(host), g(guest);
    u32 frame = 0;
    u64 mine = 0, yours = 0;
    REQUIRE(h->lockstep.desynced(&frame, &mine, &yours));
    CHECK(frame == 900);
    CHECK((mine ^ yours) == 1);
    u32 gframe = 0;
    u64 gmine = 0, gyours = 0;
    REQUIRE(g->lockstep.desynced(&gframe, &gmine, &gyours));
    CHECK(gframe == 900);
    CHECK(gmine == yours);
    CHECK(gyours == mine);

    // Without the fault, nothing is flagged.
    Peer* h2 = nullptr;
    Peer* g2 = nullptr;
    runPair(50, 10, 11, 1200, kNoFrame, &h2, &g2);
    std::unique_ptr<Peer> hh(h2), gg(g2);
    CHECK(!hh->lockstep.desynced());
    CHECK(!gg->lockstep.desynced());
}
