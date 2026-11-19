// The rtc backend of as3d/net_transport.h (docs/spec/issues/180-online-coop.md 2): WebRTC data
// channels through libdatachannel (desktop, Android) or datachannel-wasm (the web), one source
// for both (the two share the rtc:: C++ API used here).
//
// One peer connection, two data channels opened by the host before its offer:
//   "in"   unordered, maxRetransmits = 0: the Inputs packets (sendInputs / pollInputs);
//   "ctl"  reliable and ordered: every other message (sendCtl / pollCtl).
// They are announced in band (DCEP) and the guest takes them from onDataChannel by label:
// datachannel-wasm has no pre-negotiated channels (DataChannelInit there is the reliability
// only), and both sides must do the same for a desktop, Android and web peer to meet.
//
// Codes: no trickle ICE. Each side waits for candidate gathering to complete (or
// kGatherTimeoutSeconds) and turns its local description, plus every candidate reported, into a
// SignalInfo through signalFromSdp / candidateFromSdpLine / selectCandidates; the remote one
// becomes SDP again through sdpFromSignal. This file never parses SDP itself.
//
// DTLS roles: the invite says setup:active (the host will be the DTLS client), so the guest's
// answer is setup:passive and the guest only waits for the host's handshake. The guest can then
// sit with its reply code for as long as the players need to pass it on (until the offer expiry):
// had the guest been the DTLS client, its handshake would start as soon as its ICE checks
// reached the host (the host answers early checks) and time out while the reply is still in a
// chat window.
//
// Threads: libdatachannel calls back on its own threads (datachannel-wasm on the main thread,
// which is also the game thread). The callbacks only write into Shared under its mutex; they hold
// a weak_ptr to it, so a late callback after the transport is gone does nothing. Every Transport
// member runs on the game thread; update() turns what the callbacks recorded into states.
#include <rtc/rtc.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>

#include "as3d/net_transport.h"
#include "transport_rtc_raw.h"

namespace as3d::net {

namespace {

constexpr size_t kMaxQueuedInputs = 512;     // ~8 s of packets: the game thread polls every frame
constexpr size_t kMaxQueuedCtl = 16384;      // more means the game thread stopped polling
constexpr size_t kMaxInputsBuffered = 16384; // bytes queued in SCTP: drop inputs beyond this

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void initRtcLogger() {
#ifndef __EMSCRIPTEN__
    static std::once_flag once;
    std::call_once(once, [] {
        rtc::InitLogger(rtc::LogLevel::Warning, [](rtc::LogLevel, std::string message) {
            AS3D_WARN("AS3D_NET rtc: %s", message.c_str());
        });
    });
#endif
}

// What the callbacks record; read and cleared by the game thread.
struct Shared {
    std::mutex mutex;
    bool gatheringComplete = false;
    std::vector<std::string> localCandidates; // "candidate:..." lines (onLocalCandidate)
    bool pcFailed = false, pcClosed = false;
    rtc::PeerConnection::State pcState = rtc::PeerConnection::State::New;
    std::shared_ptr<rtc::DataChannel> in, ctl; // the guest's arrive through onDataChannel
    bool inOpen = false, ctlOpen = false;
    bool channelClosed = false;
    std::string channelError;
    std::deque<std::vector<u8>> inQueue, ctlQueue;
    bool ctlOverflow = false;
    size_t droppedInputs = 0;
};

void pushMessage(Shared& s, bool ctl, rtc::message_variant data) {
    const auto* bin = std::get_if<rtc::binary>(&data);
    if (!bin) return; // text messages are not part of the protocol
    const size_t n = bin->size();
    if (n == 0 || n > (ctl ? kMaxCtlMessage : kMaxInputsMessage)) return;
    std::vector<u8> msg(n);
    std::memcpy(msg.data(), bin->data(), n);
    std::lock_guard<std::mutex> lock(s.mutex);
    if (ctl) {
        if (s.ctlQueue.size() >= kMaxQueuedCtl) {
            s.ctlOverflow = true;
            return;
        }
        s.ctlQueue.push_back(std::move(msg));
    } else {
        if (s.inQueue.size() >= kMaxQueuedInputs) {
            s.inQueue.pop_front(); // the oldest is the least useful (redundancy covers it)
            ++s.droppedInputs;
        }
        s.inQueue.push_back(std::move(msg));
    }
}

void wireChannel(const std::shared_ptr<rtc::DataChannel>& dc, bool ctl, const std::weak_ptr<Shared>& weak) {
    dc->onOpen([weak, ctl] {
        if (auto s = weak.lock()) {
            std::lock_guard<std::mutex> lock(s->mutex);
            (ctl ? s->ctlOpen : s->inOpen) = true;
        }
    });
    dc->onClosed([weak] {
        if (auto s = weak.lock()) {
            std::lock_guard<std::mutex> lock(s->mutex);
            s->channelClosed = true;
        }
    });
    dc->onError([weak](std::string error) {
        if (auto s = weak.lock()) {
            std::lock_guard<std::mutex> lock(s->mutex);
            if (s->channelError.empty()) s->channelError = error;
        }
    });
    dc->onMessage([weak, ctl](rtc::message_variant data) {
        if (auto s = weak.lock()) pushMessage(*s, ctl, std::move(data));
    });
    // A channel handed over by onDataChannel may already be open (libdatachannel opens it before
    // the callback); onOpen does not fire again then.
    if (dc->isOpen()) {
        if (auto s = weak.lock()) {
            std::lock_guard<std::mutex> lock(s->mutex);
            (ctl ? s->ctlOpen : s->inOpen) = true;
        }
    }
}

void unwireChannel(const std::shared_ptr<rtc::DataChannel>& dc) {
    if (!dc) return;
    dc->onOpen(nullptr);
    dc->onClosed(nullptr);
    dc->onError(nullptr);
    dc->onMessage(nullptr);
}

class RtcTransport final : public Transport {
public:
    explicit RtcTransport(const TransportConfig& config) : config_(config), shared_(std::make_shared<Shared>()) {
        initRtcLogger();
    }
    ~RtcTransport() override { shutdown(); }

    void hostOffer() override {
        if (state_ != State::New) return;
        host_ = true;
        if (!createPeer()) return;
        try {
            rtc::DataChannelInit inInit;
            inInit.reliability.unordered = true;
            inInit.reliability.maxRetransmits = 0u;
            auto in = pc_->createDataChannel("in", inInit);
            auto ctl = pc_->createDataChannel("ctl");
            {
                std::lock_guard<std::mutex> lock(shared_->mutex);
                shared_->in = in;
                shared_->ctl = ctl;
            }
            wireChannel(in, false, shared_);
            wireChannel(ctl, true, shared_);
            // Creating the first channel negotiates (libdatachannel's automatic negotiation, the
            // browser's negotiationneeded): the offer is set and gathering starts.
        } catch (const std::exception& e) {
            fail(Failure::Other, std::string("could not open the data channels: ") + e.what());
            return;
        }
        setState(State::Gathering);
        gatherStart_ = nowSeconds();
    }

    void joinOffer(const SignalInfo& invite) override {
        if (state_ != State::New) return;
        if (invite.kind != SignalKind::Invite) {
            fail(Failure::Other, "not an invite code");
            return;
        }
        std::string sdp = sdpFromSignal(invite);
        if (sdp.empty()) {
            fail(Failure::Other, "the invite code could not be turned into a session description");
            return;
        }
        joinWithSdp(sdp);
    }

    void acceptAnswer(const SignalInfo& reply) override {
        if (!host_ || state_ != State::SignalReady) return;
        if (reply.kind != SignalKind::Reply) {
            fail(Failure::Other, "not a reply code");
            return;
        }
        std::string sdp = sdpFromSignal(reply);
        if (sdp.empty()) {
            fail(Failure::Other, "the reply code could not be turned into a session description");
            return;
        }
        acceptWithSdp(sdp);
    }

    bool localSignal(SignalInfo& out) const override {
        if (!haveSignal_) return false;
        out = signal_;
        return true;
    }

    void sendCtl(const u8* data, size_t n) override { send(true, data, n); }
    void sendInputs(const u8* data, size_t n) override { send(false, data, n); }

    bool pollCtl(std::vector<u8>& out) override {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (shared_->ctlQueue.empty()) return false;
        out = std::move(shared_->ctlQueue.front());
        shared_->ctlQueue.pop_front();
        return true;
    }

    bool pollInputs(std::vector<u8>& out) override {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (shared_->inQueue.empty()) return false;
        out = std::move(shared_->inQueue.front());
        shared_->inQueue.pop_front();
        return true;
    }

    void update() override {
        if (state_ == State::New || state_ == State::Failed || state_ == State::Closed) return;
        const double now = nowSeconds();
        bool gathered, pcFailed, pcClosed, inOpen, ctlOpen, channelClosed, ctlOverflow;
        std::string channelError;
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            gathered = shared_->gatheringComplete;
            pcFailed = shared_->pcFailed;
            pcClosed = shared_->pcClosed;
            inOpen = shared_->inOpen;
            ctlOpen = shared_->ctlOpen;
            channelClosed = shared_->channelClosed;
            ctlOverflow = shared_->ctlOverflow;
            channelError = shared_->channelError;
        }
        if (ctlOverflow) {
            fail(Failure::Other, "control messages were not read");
            return;
        }
        if (state_ == State::Connected) {
            if (pcFailed || pcClosed || channelClosed) {
                fail(Failure::Closed, channelError.empty() ? "the connection was lost" : "the connection was lost: " + channelError);
            }
            return;
        }
        // Before Connected: any failure of the peer connection is "can't connect".
        if (pcFailed || pcClosed || channelClosed) {
            fail(Failure::CantConnect, "the connection could not be established (ICE failed)");
            return;
        }
        if (state_ == State::Gathering) {
            if (gathered || now - gatherStart_ >= kGatherTimeoutSeconds) {
                if (!gathered && config_.verboseLog) AS3D_INFO("AS3D_NET gathering cut after %.0f s", kGatherTimeoutSeconds);
                if (!buildLocalSignal()) return;
                setState(State::SignalReady);
                signalReadyAt_ = now;
            }
            return;
        }
        if (inOpen && ctlOpen) {
            setState(State::Connected);
            return;
        }
        if (state_ == State::SignalReady) {
            // The host waits for the reply, the guest for the host's connection (it holds its
            // reply code until the host pastes it).
            if (now - signalReadyAt_ >= kOfferExpirySeconds) {
                fail(Failure::Expired, host_ ? "the invite code expired" : "the reply code expired");
            }
            return;
        }
        if (state_ == State::Connecting && now - connectStart_ >= kConnectTimeoutSeconds) {
            fail(Failure::CantConnect, "no connection after 20 s");
        }
    }

    State state() const override { return state_; }
    Failure failure() const override { return failure_; }
    std::string error() const override { return error_; }

    void close() override {
        if (state_ == State::Closed) return;
        shutdown();
        if (state_ != State::Failed) setState(State::Closed);
    }

    // ---- the raw-SDP path (transport_rtc_raw.h) ----
    void hostOfferRaw() {
        raw_ = true;
        hostOffer();
    }
    void joinOfferSdp(const std::string& sdp) {
        if (state_ != State::New) return;
        raw_ = true;
        joinWithSdp(sdp);
    }
    void acceptAnswerSdp(const std::string& sdp) {
        if (!host_ || state_ != State::SignalReady) return;
        acceptWithSdp(sdp);
    }
    bool localSdp(std::string& out) const {
        if (state_ == State::New || state_ == State::Gathering || localSdp_.empty()) return false;
        out = localSdp_;
        return true;
    }

private:
    bool createPeer() {
        rtc::Configuration rc;
        for (const std::string& url : config_.stunServers) {
            if (url.empty() || url == "none") continue;
            try {
                rc.iceServers.emplace_back(url);
            } catch (const std::exception& e) {
                AS3D_WARN("AS3D_NET ignoring STUN server '%s': %s", url.c_str(), e.what());
            }
        }
#ifndef __EMSCRIPTEN__
        rc.maxMessageSize = 262144;
#endif
        try {
            pc_ = std::make_shared<rtc::PeerConnection>(rc);
        } catch (const std::exception& e) {
            fail(Failure::Other, std::string("WebRTC is not available: ") + e.what());
            return false;
        }
        std::weak_ptr<Shared> weak = shared_;
        pc_->onStateChange([weak](rtc::PeerConnection::State st) {
            if (auto s = weak.lock()) {
                std::lock_guard<std::mutex> lock(s->mutex);
                s->pcState = st;
                if (st == rtc::PeerConnection::State::Failed) s->pcFailed = true;
                if (st == rtc::PeerConnection::State::Closed) s->pcClosed = true;
            }
        });
        pc_->onGatheringStateChange([weak](rtc::PeerConnection::GatheringState st) {
            if (st != rtc::PeerConnection::GatheringState::Complete) return;
            if (auto s = weak.lock()) {
                std::lock_guard<std::mutex> lock(s->mutex);
                s->gatheringComplete = true;
            }
        });
        pc_->onLocalCandidate([weak](rtc::Candidate c) {
            if (auto s = weak.lock()) {
                std::lock_guard<std::mutex> lock(s->mutex);
                s->localCandidates.push_back(c.candidate());
            }
        });
        // The guest's channels (the host's are created in hostOffer). Labels as the host names them.
        pc_->onDataChannel([weak](std::shared_ptr<rtc::DataChannel> dc) {
            auto s = weak.lock();
            if (!s) return;
            const std::string label = dc->label();
            const bool ctl = label == "ctl";
            if (!ctl && label != "in") return;
            {
                std::lock_guard<std::mutex> lock(s->mutex);
                (ctl ? s->ctl : s->in) = dc;
            }
            wireChannel(dc, ctl, weak);
        });
        return true;
    }

    void joinWithSdp(const std::string& sdp) {
        host_ = false;
        if (!createPeer()) return;
        try {
            // Setting the offer makes the answer and starts gathering (automatic negotiation).
            pc_->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));
        } catch (const std::exception& e) {
            fail(Failure::Other, std::string("the invite was refused: ") + e.what());
            return;
        }
        setState(State::Gathering);
        gatherStart_ = nowSeconds();
    }

    void acceptWithSdp(const std::string& sdp) {
        try {
            pc_->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Answer));
        } catch (const std::exception& e) {
            fail(Failure::Other, std::string("the reply was refused: ") + e.what());
            return;
        }
        setState(State::Connecting);
        connectStart_ = nowSeconds();
    }

    // The local description with every candidate so far, as SignalInfo (or raw SDP).
    bool buildLocalSignal() {
        std::string sdp;
        std::vector<std::string> candidates;
        try {
            auto desc = pc_->localDescription();
            if (desc) sdp = std::string(*desc);
        } catch (const std::exception& e) {
            AS3D_WARN("AS3D_NET local description: %s", e.what());
        }
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            candidates = shared_->localCandidates;
        }
        if (sdp.empty()) {
            fail(Failure::Other, "no local session description");
            return false;
        }
        if (raw_) {
            if (host_) {
                // As the coded path does (see the top of the file): the host will be the DTLS client.
                const std::string from = "a=setup:actpass", to = "a=setup:active";
                for (size_t p = sdp.find(from); p != std::string::npos; p = sdp.find(from, p + to.size()))
                    sdp.replace(p, from.size(), to);
            }
            // Candidates the description may not hold yet (gathering cut by the timeout).
            for (const std::string& c : candidates) {
                std::string line = c.compare(0, 2, "a=") == 0 ? c : "a=" + c;
                if (sdp.find(line) == std::string::npos) sdp += line + "\r\n";
            }
            localSdp_ = sdp;
            return true;
        }
        SignalInfo s;
        if (!signalFromSdp(sdp, s)) {
            fail(Failure::Other, "the local session description could not be turned into a code");
            return false;
        }
        for (const std::string& line : candidates) {
            SignalCandidate c;
            if (!candidateFromSdpLine(line, c)) continue;
            if (std::find(s.candidates.begin(), s.candidates.end(), c) == s.candidates.end()) s.candidates.push_back(c);
        }
        selectCandidates(s.candidates);
        s.kind = host_ ? SignalKind::Invite : SignalKind::Reply;
        if (host_) s.setup = DtlsSetup::Active;
        localSdp_ = sdp;
        signal_ = s;
        haveSignal_ = true;
        if (config_.verboseLog) AS3D_INFO("AS3D_NET local %s: %zu candidate(s)", host_ ? "invite" : "reply", s.candidates.size());
        return true;
    }

    void send(bool ctl, const u8* data, size_t n) {
        if (state_ != State::Connected || n == 0 || n > (ctl ? kMaxCtlMessage : kMaxInputsMessage)) return;
        std::shared_ptr<rtc::DataChannel> dc;
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            dc = ctl ? shared_->ctl : shared_->in;
        }
        if (!dc) return;
        try {
            if (!ctl && dc->bufferedAmount() > kMaxInputsBuffered) return; // congested: the next packet repeats these frames
            dc->send(reinterpret_cast<const rtc::byte*>(data), n);
        } catch (const std::exception& e) {
            // The channel closed under us; update() reports it.
            AS3D_WARN("AS3D_NET send on %s failed: %s", ctl ? "ctl" : "in", e.what());
        }
    }

    void setState(State s) {
        if (s == state_) return;
        state_ = s;
        if (config_.verboseLog) AS3D_INFO("AS3D_NET state=%s", transportStateName(s));
    }

    void fail(Failure f, const std::string& why) {
        if (state_ == State::Failed || state_ == State::Closed) return;
        failure_ = f;
        error_ = why;
        AS3D_WARN("AS3D_NET failed: %s", why.c_str());
        shutdown();
        setState(State::Failed);
    }

    void shutdown() {
        std::shared_ptr<rtc::DataChannel> in, ctl;
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            in = std::move(shared_->in);
            ctl = std::move(shared_->ctl);
            shared_->in.reset();
            shared_->ctl.reset();
        }
        try {
            unwireChannel(in);
            unwireChannel(ctl);
            if (in) in->close();
            if (ctl) ctl->close();
            if (pc_) {
                pc_->onStateChange(nullptr);
                pc_->onGatheringStateChange(nullptr);
                pc_->onLocalCandidate(nullptr);
                pc_->onDataChannel(nullptr);
                pc_->close();
            }
        } catch (const std::exception& e) {
            AS3D_WARN("AS3D_NET close: %s", e.what());
        }
        pc_.reset();
    }

    TransportConfig config_;
    std::shared_ptr<Shared> shared_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    State state_ = State::New;
    Failure failure_ = Failure::None;
    std::string error_;
    bool host_ = false, raw_ = false;
    double gatherStart_ = 0, signalReadyAt_ = 0, connectStart_ = 0;
    SignalInfo signal_;
    bool haveSignal_ = false;
    std::string localSdp_;
};

} // namespace

std::unique_ptr<Transport> makeRtcTransport(const TransportConfig& config, std::string* error) {
    if (error) error->clear();
    return std::make_unique<RtcTransport>(config);
}

bool rtcTransportAvailable() { return true; }

void rtcHostOfferRaw(Transport& t) { static_cast<RtcTransport&>(t).hostOfferRaw(); }
void rtcJoinOfferSdp(Transport& t, const std::string& sdp) { static_cast<RtcTransport&>(t).joinOfferSdp(sdp); }
void rtcAcceptAnswerSdp(Transport& t, const std::string& sdp) { static_cast<RtcTransport&>(t).acceptAnswerSdp(sdp); }
bool rtcLocalSdp(const Transport& t, std::string& out) { return static_cast<const RtcTransport&>(t).localSdp(out); }

} // namespace as3d::net
