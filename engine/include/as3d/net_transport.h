// The transport of online play (docs/spec/issues/180-online-coop.md 2.2, 2.4): one peer
// connection with two data channels, "in" (id 0, unordered, no retransmits: the Inputs packets)
// and "ctl" (id 1, reliable and ordered: every other message), both negotiated by the host.
// No sockets and no third-party types in this header.
//
// Backends (engine/src/net/<AS3D_NET_BACKEND>/):
//   rtc   libdatachannel on desktop and Android, datachannel-wasm on the web (T3);
//   null  the 3DS and AS3D_WITH_NET=OFF: makeRtcTransport returns null with "online play is
//         not in this build".
// The loopback pair (engine/src/net/loopback.cpp) is always built: the tests join two
// transports in process with simulated latency and loss.
//
// Threads: the rtc backend's callbacks run on libdatachannel's threads (on the web, the main
// thread) and only push into mutex-guarded queues; every member below is called on the game
// thread (update() once per display frame).
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/net_signal.h"

namespace as3d::net {

constexpr size_t kMaxCtlMessage = 1024, kMaxInputsMessage = 128;
// Timeouts of 2.3: candidate gathering is cut after this (the code holds what was found), ICE
// that is not connected after kConnectTimeoutSeconds fails ("can't connect"), and an offer
// waiting for its reply longer than kOfferExpirySeconds is closed ("expired").
constexpr double kGatherTimeoutSeconds = 5.0;
constexpr double kConnectTimeoutSeconds = 20.0;
constexpr double kOfferExpirySeconds = 600.0;
// The public STUN servers given to the agent when nothing overrides them (AS3D_STUN, ?stun=).
constexpr const char* kDefaultStunServers[] = {"stun:stun.l.google.com:19302", "stun:stun.cloudflare.com:3478"};

struct TransportConfig {
    std::vector<std::string> stunServers; // "stun:host:port"; empty = none
    bool verboseLog = false;              // AS3D_NET lines for every state change
};

class Transport {
public:
    enum class State { New, Gathering, SignalReady, Connecting, Connected, Failed, Closed };
    // Why a transport ended in Failed (the screen picks its text from it; error() has the detail).
    enum class Failure { None, CantConnect, Expired, Closed, Other };

    virtual ~Transport() = default;
    virtual void hostOffer() = 0;                           // host: start gathering an offer
    virtual void joinOffer(const SignalInfo& invite) = 0;   // guest: set the offer, gather an answer
    virtual void acceptAnswer(const SignalInfo& reply) = 0; // host: set the answer
    virtual bool localSignal(SignalInfo& out) const = 0;    // true once gathering completed
    virtual void sendCtl(const u8* data, size_t n) = 0;     // reliable, ordered; at most kMaxCtlMessage bytes
    virtual void sendInputs(const u8* data, size_t n) = 0;  // unreliable, unordered; at most kMaxInputsMessage bytes
    virtual bool pollCtl(std::vector<u8>& out) = 0;         // one message, in order; false when none
    virtual bool pollInputs(std::vector<u8>& out) = 0;      // one packet; false when none
    virtual void update() = 0;                              // once per display frame on the game thread
    virtual State state() const = 0;
    virtual Failure failure() const { return state() == State::Failed ? Failure::Other : Failure::None; }
    virtual std::string error() const = 0;                  // why Failed / Closed
    virtual void close() = 0;
};

const char* transportStateName(Transport::State s); // "new", "gathering", ... for AS3D_NET state=

// The rtc backend. The null backend returns nullptr and sets *error to "online play is not in
// this build" (texts: online.err.not_built).
std::unique_ptr<Transport> makeRtcTransport(const TransportConfig& config, std::string* error);
// Whether makeRtcTransport can return a transport in this build (false with the null backend):
// the window offers "Online" only then.
bool rtcTransportAvailable();

// Two transports joined in process, for tests: `latencyMs` each way, `lossPercent` on the inputs
// channel only (ctl never loses), both deterministic from `seed`. The codes still go through
// hostOffer / localSignal / joinOffer / acceptAnswer (fake SignalInfo), so the lobby code path is
// the real one. `a` is the host's side, `b` the guest's. Time: `nowSeconds` (null = the steady
// clock), so that a test can run the latency on a clock of its own.
struct LoopbackOptions {
    int latencyMs = 0;
    int lossPercent = 0;
    u32 seed = 1;
    std::function<double()> nowSeconds;
};
void makeLoopbackPair(std::unique_ptr<Transport>& a, std::unique_ptr<Transport>& b, const LoopbackOptions& o);

} // namespace as3d::net
