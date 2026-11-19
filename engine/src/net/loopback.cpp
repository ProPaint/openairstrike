// Two transports joined in process for the tests (as3d/net_transport.h makeLoopbackPair,
// docs/spec/issues/180-online-coop.md 2.4).
//
// The pair shares one Link: per direction a ctl queue and an inputs queue of messages stamped
// with the time they may be delivered (sent + latencyMs). ctl never loses; each inputs packet is
// dropped with lossPercent from an Rng seeded by `seed`, so a run is the same every time on the
// same clock. With one fixed latency both queues stay in order (the inputs channel of the real
// backend may reorder; the lockstep does not care, its test can shuffle by hand).
//
// The signalling goes through the real calls with fake SignalInfo (valid for encodeSignalCode,
// so a test may also pass them through the codes): hostOffer -> SignalReady with the invite on
// the next update; joinOffer(invite) -> SignalReady with the reply, or Failed when the invite is
// not this pair's; acceptAnswer(reply) -> the host Connecting (the guest stays SignalReady until
// then and follows on its next update), both Connected on an update once latencyMs has passed.
// close() makes this side Closed and the other Failed (Failure::Closed) once the latency has
// passed. Messages sent before Connected are dropped, as they would be on the real channels.
#include <chrono>
#include <deque>
#include <mutex>

#include "as3d/net_transport.h"

namespace as3d::net {

namespace {

struct Message {
    double due = 0.0;
    std::vector<u8> bytes;
};

struct Link {
    LoopbackOptions options;
    Rng loss;
    std::deque<Message> ctl[2], inputs[2]; // [to side]
    SignalInfo signal[2];                  // side 0 the invite, side 1 the reply
    bool offered = false, joined = false, accepted = false;
    double acceptedAt = 0.0;
    bool closed[2] = {false, false};
    double closedAt[2] = {0.0, 0.0};
    explicit Link(const LoopbackOptions& o) : options(o), loss(o.seed ? o.seed : 1) {}

    double now() const {
        if (options.nowSeconds) return options.nowSeconds();
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }
    double latency() const { return options.latencyMs > 0 ? options.latencyMs / 1000.0 : 0.0; }
};

// A deterministic, valid SignalInfo per side: ice-chars of the right lengths, a fingerprint and a
// TEST-NET-1 host candidate, all from the seed.
SignalInfo fakeSignal(u32 seed, int side) {
    static const char kChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    Rng rng(seed * 2654435761u + static_cast<u32>(side) * 40503u + 1u);
    SignalInfo s;
    s.kind = side == 0 ? SignalKind::Invite : SignalKind::Reply;
    s.setup = side == 0 ? DtlsSetup::ActPass : DtlsSetup::Active;
    s.ufrag = side == 0 ? "lbH" : "lbG";
    for (int i = 0; i < 5; i++) s.ufrag += kChars[rng.next() % 64];
    for (int i = 0; i < 24; i++) s.pwd += kChars[rng.next() % 64];
    for (u8& b : s.fingerprint) b = static_cast<u8>(rng.next());
    SignalCandidate c;
    c.type = CandidateType::Host;
    c.priority = 2130706431u;
    c.address[0] = 192;
    c.address[1] = 0;
    c.address[2] = 2;
    c.address[3] = static_cast<u8>(1 + side);
    c.port = static_cast<u16>(50000 + side);
    s.candidates.push_back(c);
    return s;
}

class LoopbackTransport : public Transport {
public:
    LoopbackTransport(std::shared_ptr<Link> link, std::shared_ptr<std::mutex> mutex, int side)
        : link_(std::move(link)), mutex_(std::move(mutex)), side_(side) {}
    ~LoopbackTransport() override { close(); }

    void hostOffer() override {
        std::lock_guard<std::mutex> lock(*mutex_);
        if (side_ != 0 || state_ != State::New) return fail("hostOffer: not the host side of a new pair");
        link_->offered = true;
        state_ = State::Gathering;
    }
    void joinOffer(const SignalInfo& invite) override {
        std::lock_guard<std::mutex> lock(*mutex_);
        if (side_ != 1 || state_ != State::New) return fail("joinOffer: not the guest side of a new pair");
        if (!link_->offered || !(invite == link_->signal[0])) return fail("joinOffer: not this pair's invite");
        link_->joined = true;
        state_ = State::Gathering;
    }
    void acceptAnswer(const SignalInfo& reply) override {
        std::lock_guard<std::mutex> lock(*mutex_);
        if (side_ != 0 || state_ != State::SignalReady) return fail("acceptAnswer: no offer waiting");
        if (!link_->joined || !(reply == link_->signal[1])) return fail("acceptAnswer: not this pair's reply");
        link_->accepted = true;
        link_->acceptedAt = link_->now();
        state_ = State::Connecting;
    }
    bool localSignal(SignalInfo& out) const override {
        std::lock_guard<std::mutex> lock(*mutex_);
        if (!gathered_) return false;
        out = link_->signal[side_];
        return true;
    }
    void sendCtl(const u8* data, size_t n) override { send(link_->ctl[1 - side_], data, n, kMaxCtlMessage, false); }
    void sendInputs(const u8* data, size_t n) override {
        send(link_->inputs[1 - side_], data, n, kMaxInputsMessage, true);
    }
    bool pollCtl(std::vector<u8>& out) override { return poll(link_->ctl[side_], out); }
    bool pollInputs(std::vector<u8>& out) override { return poll(link_->inputs[side_], out); }

    void update() override {
        std::lock_guard<std::mutex> lock(*mutex_);
        double now = link_->now();
        if (state_ == State::Gathering) {
            gathered_ = true;
            state_ = State::SignalReady;
        }
        if (side_ == 1 && state_ == State::SignalReady && link_->accepted) state_ = State::Connecting;
        if (state_ == State::Connecting && link_->accepted && now >= link_->acceptedAt + link_->latency() - 1e-9)
            state_ = State::Connected;
        int other = 1 - side_;
        if (link_->closed[other] && (state_ == State::Connected || state_ == State::Connecting) &&
            now >= link_->closedAt[other] + link_->latency() - 1e-9) {
            state_ = State::Failed;
            failure_ = Failure::Closed;
            error_ = "the other side closed the connection";
        }
    }
    State state() const override {
        std::lock_guard<std::mutex> lock(*mutex_);
        return state_;
    }
    Failure failure() const override {
        std::lock_guard<std::mutex> lock(*mutex_);
        return failure_;
    }
    std::string error() const override {
        std::lock_guard<std::mutex> lock(*mutex_);
        return error_;
    }
    void close() override {
        std::lock_guard<std::mutex> lock(*mutex_);
        if (state_ == State::Closed) return;
        state_ = State::Closed;
        if (error_.empty()) error_ = "closed";
        link_->closed[side_] = true;
        link_->closedAt[side_] = link_->now();
    }

private:
    void fail(const char* why) {
        state_ = State::Failed;
        failure_ = Failure::Other;
        error_ = why;
    }
    bool connected() const { return state_ == State::Connected && !link_->closed[1 - side_]; }
    void send(std::deque<Message>& q, const u8* data, size_t n, size_t max, bool lossy) {
        std::lock_guard<std::mutex> lock(*mutex_);
        if (!connected() || !data || n == 0 || n > max) return;
        if (lossy && link_->options.lossPercent > 0 &&
            static_cast<int>(link_->loss.next() % 100u) < link_->options.lossPercent)
            return;
        q.push_back(Message{link_->now() + link_->latency(), std::vector<u8>(data, data + n)});
    }
    bool poll(std::deque<Message>& q, std::vector<u8>& out) {
        std::lock_guard<std::mutex> lock(*mutex_);
        if (state_ != State::Connected || q.empty() || link_->now() < q.front().due - 1e-9) return false;
        out = std::move(q.front().bytes);
        q.pop_front();
        return true;
    }

    std::shared_ptr<Link> link_;
    std::shared_ptr<std::mutex> mutex_;
    int side_;
    State state_ = State::New;
    Failure failure_ = Failure::None;
    std::string error_;
    bool gathered_ = false;
};

} // namespace

void makeLoopbackPair(std::unique_ptr<Transport>& a, std::unique_ptr<Transport>& b, const LoopbackOptions& o) {
    auto link = std::make_shared<Link>(o);
    link->signal[0] = fakeSignal(o.seed, 0);
    link->signal[1] = fakeSignal(o.seed, 1);
    auto mutex = std::make_shared<std::mutex>();
    a = std::make_unique<LoopbackTransport>(link, mutex, 0);
    b = std::make_unique<LoopbackTransport>(link, mutex, 1);
}

} // namespace as3d::net
