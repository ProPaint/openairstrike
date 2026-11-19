// as3d_net_tool: two rtc transports (as3d/net_transport.h) connected without the game, then
// measured (docs/spec/issues/180-online-coop.md 13 "T3").
//
//   as3d_net_tool pair [options]           host and guest in this process, no stdin: the smoke test
//   as3d_net_tool host [options]           prints the invite, reads the reply from stdin
//   as3d_net_tool join [options] <invite>  prints the reply, waits for the host
//
// Options:
//   --stun none|URL[,URL...]  STUN servers (default: AS3D_STUN, else the two public ones)
//   --raw-sdp                 pass whole session descriptions ("sdp:" + base64) instead of the
//                             codes (also chosen by itself when this build's code conversion is
//                             not there yet)
//   --packets N               Inputs-sized packets each way (default 1000)
//   --pings N                 ctl pings each way (default 100)
//   --rate HZ                 packets per second (default 60; pair: 250)
//   --verbose                 AS3D_NET lines for every state change
//
// Once connected both sides send N packets of 62 bytes on the unreliable "in" channel (the size
// of an Inputs packet with 12 records) and the pings on the reliable "ctl" channel, answer the
// other side's pings, and print what arrived, the loss and the round trip times. Exit code 0
// when every ping was answered and at least 95% of the packets arrived.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "as3d/net_signal.h"
#include "as3d/net_transport.h"
#include "transport_rtc_raw.h"

using namespace as3d;
using namespace as3d::net;

namespace {

constexpr size_t kPacketSize = 62;
constexpr u8 kTypeInputs = 0x30, kTypePing = 0x03, kTypePong = 0x04;

double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void putU32(u8* p, u32 v) {
    for (int i = 0; i < 4; ++i) p[i] = u8(v >> (8 * i));
}
u32 getU32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }

// ---- base64 for the raw SDP path ----
const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string b64Encode(const std::string& in) {
    std::string out;
    u32 acc = 0;
    int bits = 0;
    for (unsigned char c : in) {
        acc = (acc << 8) | c;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out += kB64[(acc >> bits) & 63];
        }
    }
    if (bits > 0) out += kB64[(acc << (6 - bits)) & 63];
    while (out.size() % 4) out += '=';
    return out;
}
std::string b64Decode(const std::string& in) {
    std::string out;
    u32 acc = 0;
    int bits = 0;
    for (char c : in) {
        const char* p = std::strchr(kB64, c);
        if (!p || c == '\0') continue; // '=', whitespace
        acc = (acc << 6) | u32(p - kB64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += char((acc >> bits) & 255);
        }
    }
    return out;
}

struct Options {
    std::vector<std::string> stun;
    bool stunGiven = false;
    bool raw = false;
    int packets = 1000;
    int pings = 100;
    double rate = 0;
    bool verbose = false;
    std::string invite;
};

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(',', start);
        if (end == std::string::npos) end = s.size();
        if (end > start) out.push_back(s.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

// Whether T1's code conversions are in this build (they are stubs before it lands).
bool codesAvailable() {
    SignalInfo s;
    s.ufrag = "abcd";
    s.pwd = "abcdefghijklmnopqrstuv";
    return !encodeSignalCode(s).empty();
}

// One side of the exchange.
struct Peer {
    Peer(const char* n, Transport* tr) : name(n), t(tr) {}
    const char* name;
    Transport* t;
    int sent = 0, pingsSent = 0;
    double nextPacket = 0, nextPing = 0, lastSend = 0;
    std::vector<bool> got;
    int received = 0, duplicates = 0, pongs = 0;
    double rttMin = 1e9, rttMax = 0, rttSum = 0;
};

bool failedOrClosed(const Transport& t) {
    return t.state() == Transport::State::Failed || t.state() == Transport::State::Closed;
}

void printFailure(const char* who, const Transport& t) {
    std::printf("%s: %s (failure %d): %s\n", who, transportStateName(t.state()), int(t.failure()), t.error().c_str());
}

// Sends what is due and reads what arrived. `t0` is the clock origin of the ping times.
void exchangeStep(Peer& p, const Options& o, double t0) {
    const double t = now();
    const double period = 1.0 / o.rate;
    const double pingPeriod = o.pings > 0 ? (o.packets * period) / o.pings : 0;
    while (p.sent < o.packets && t >= p.nextPacket) {
        u8 buf[kPacketSize] = {};
        buf[0] = kTypeInputs;
        putU32(buf + 1, u32(p.sent));
        p.t->sendInputs(buf, sizeof(buf));
        ++p.sent;
        p.nextPacket += period;
        p.lastSend = t;
    }
    while (p.pingsSent < o.pings && t >= p.nextPing) {
        u8 buf[9];
        buf[0] = kTypePing;
        putU32(buf + 1, u32(p.pingsSent));
        putU32(buf + 5, u32((t - t0) * 1000.0));
        p.t->sendCtl(buf, sizeof(buf));
        ++p.pingsSent;
        p.nextPing += pingPeriod;
        p.lastSend = t;
    }
    std::vector<u8> msg;
    while (p.t->pollInputs(msg)) {
        if (msg.size() != kPacketSize || msg[0] != kTypeInputs) continue;
        const u32 seq = getU32(msg.data() + 1);
        if (seq >= p.got.size()) continue;
        if (p.got[seq]) {
            ++p.duplicates;
        } else {
            p.got[seq] = true;
            ++p.received;
        }
    }
    while (p.t->pollCtl(msg)) {
        if (msg.size() != 9) continue;
        if (msg[0] == kTypePing) {
            msg[0] = kTypePong;
            p.t->sendCtl(msg.data(), msg.size());
        } else if (msg[0] == kTypePong) {
            const double rtt = (now() - t0) * 1000.0 - double(getU32(msg.data() + 5));
            ++p.pongs;
            p.rttMin = std::min(p.rttMin, rtt);
            p.rttMax = std::max(p.rttMax, rtt);
            p.rttSum += rtt;
        }
    }
}

bool peerDone(const Peer& p, const Options& o) {
    if (p.sent < o.packets || p.pingsSent < o.pings) return false;
    if (p.received >= o.packets && p.pongs >= o.pings) return true;
    return now() - p.lastSend > 2.0; // what is still missing is lost
}

bool report(const Peer& p, const Options& o) {
    const double loss = o.packets > 0 ? 100.0 * (o.packets - p.received) / o.packets : 0;
    std::printf("%s: %d/%d packets (loss %.1f%%, %d duplicates), %d/%d pongs", p.name, p.received, o.packets, loss,
                p.duplicates, p.pongs, o.pings);
    if (p.pongs > 0) std::printf(", rtt min %.1f avg %.1f max %.1f ms", p.rttMin, p.rttSum / p.pongs, p.rttMax);
    std::printf("\n");
    return p.pongs >= o.pings && p.received * 100 >= o.packets * 95;
}

// Runs the exchange on the given peers (one or two) until done; false on a transport failure.
bool runExchange(std::vector<Peer*> peers, const Options& o) {
    const double t0 = now();
    for (Peer* p : peers) {
        p->got.assign(size_t(o.packets), false);
        p->nextPacket = p->nextPing = p->lastSend = t0;
    }
    const double limit = t0 + o.packets / o.rate + 30.0;
    for (;;) {
        bool all = true;
        for (Peer* p : peers) {
            p->t->update();
            if (failedOrClosed(*p->t)) {
                printFailure(p->name, *p->t);
                return false;
            }
            exchangeStep(*p, o, t0);
            all = all && peerDone(*p, o);
        }
        if (all) break;
        if (now() > limit) {
            std::printf("exchange timed out\n");
            break;
        }
        sleepMs(1);
    }
    // Across two processes the other side may still wait for answers to its last pings: keep
    // answering for a moment before closing (its close then ends this side, which is fine now).
    if (peers.size() == 1) {
        const double end = now() + 2.0;
        while (now() < end && !failedOrClosed(*peers[0]->t)) {
            peers[0]->t->update();
            exchangeStep(*peers[0], o, t0);
            sleepMs(1);
        }
    }
    bool ok = true;
    for (Peer* p : peers) ok = report(*p, o) && ok;
    return ok;
}

// Updates until `pred` holds; false (after printing why) on a failure or after `seconds`.
template <class Pred>
bool waitFor(std::vector<std::pair<const char*, Transport*>> ts, double seconds, Pred pred, const char* what) {
    const double limit = now() + seconds;
    while (!pred()) {
        for (auto& [name, t] : ts) {
            t->update();
            if (failedOrClosed(*t)) {
                printFailure(name, *t);
                return false;
            }
        }
        if (now() > limit) {
            std::printf("timed out waiting for %s\n", what);
            return false;
        }
        sleepMs(2);
    }
    return true;
}

std::unique_ptr<Transport> make(const Options& o) {
    TransportConfig cfg;
    cfg.stunServers = o.stun;
    cfg.verboseLog = o.verbose;
    std::string err;
    auto t = makeRtcTransport(cfg, &err);
    if (!t) std::printf("no transport: %s\n", err.c_str());
    return t;
}

// The local code or raw blob once SignalReady.
std::string localBlob(const Transport& t, bool raw) {
    if (raw) {
        std::string sdp;
        if (!rtcLocalSdp(t, sdp)) return {};
        return "sdp:" + b64Encode(sdp);
    }
    SignalInfo s;
    if (!t.localSignal(s)) return {};
    return encodeSignalCode(s);
}

bool isRawBlob(const std::string& s) { return s.compare(0, 4, "sdp:") == 0; }

// Joins / accepts with a blob of either kind; false when a code does not decode.
bool applyRemote(Transport& t, const std::string& blob, bool join) {
    if (isRawBlob(blob)) {
        const std::string sdp = b64Decode(blob.substr(4));
        if (join)
            rtcJoinOfferSdp(t, sdp);
        else
            rtcAcceptAnswerSdp(t, sdp);
        return true;
    }
    SignalInfo s;
    if (!decodeSignalCode(blob, s)) {
        std::printf("not a valid %s code\n", join ? "invite" : "reply");
        return false;
    }
    if (join)
        t.joinOffer(s);
    else
        t.acceptAnswer(s);
    return true;
}

bool signalReady(const Transport& t) { return t.state() == Transport::State::SignalReady || t.state() == Transport::State::Connected; }
bool connected(const Transport& t) { return t.state() == Transport::State::Connected; }

int runPair(Options o) {
    if (o.rate <= 0) o.rate = 250;
    auto host = make(o), guest = make(o);
    if (!host || !guest) return 1;
    const double t0 = now();
    if (o.raw)
        rtcHostOfferRaw(*host);
    else
        host->hostOffer();
    std::vector<std::pair<const char*, Transport*>> both = {{"host", host.get()}, {"guest", guest.get()}};
    if (!waitFor(both, kGatherTimeoutSeconds + 5, [&] { return signalReady(*host); }, "the invite")) return 1;
    const std::string invite = localBlob(*host, o.raw);
    std::printf("invite: %zu characters\n", invite.size());
    if (invite.empty() || !applyRemote(*guest, invite, true)) return 1;
    if (!waitFor(both, kGatherTimeoutSeconds + 5, [&] { return signalReady(*guest); }, "the reply")) return 1;
    const std::string reply = localBlob(*guest, o.raw);
    std::printf("reply: %zu characters\n", reply.size());
    if (reply.empty() || !applyRemote(*host, reply, false)) return 1;
    if (!waitFor(both, kConnectTimeoutSeconds + 5, [&] { return connected(*host) && connected(*guest); }, "the connection"))
        return 1;
    std::printf("connected in %.0f ms\n", (now() - t0) * 1000.0);
    Peer h("host", host.get()), g("guest", guest.get());
    const bool ok = runExchange({&h, &g}, o);
    host->close();
    guest->close();
    return ok ? 0 : 1;
}

int runHost(Options o) {
    if (o.rate <= 0) o.rate = 60;
    auto host = make(o);
    if (!host) return 1;
    if (o.raw)
        rtcHostOfferRaw(*host);
    else
        host->hostOffer();
    if (!waitFor({{"host", host.get()}}, kGatherTimeoutSeconds + 5, [&] { return signalReady(*host); }, "the invite")) return 1;
    std::printf("INVITE %s\n", localBlob(*host, o.raw).c_str());
    std::printf("paste the reply and press Enter:\n");
    std::fflush(stdout);
    std::string reply;
    if (!std::getline(std::cin, reply)) return 1;
    reply.erase(0, reply.find_first_not_of(" \t"));
    if (reply.compare(0, 6, "REPLY ") == 0) reply = reply.substr(6);
    if (!applyRemote(*host, reply, false)) return 1;
    if (!waitFor({{"host", host.get()}}, kConnectTimeoutSeconds + 5, [&] { return connected(*host); }, "the connection")) return 1;
    std::printf("connected\n");
    Peer h("host", host.get());
    const bool ok = runExchange({&h}, o);
    host->close();
    return ok ? 0 : 1;
}

int runJoin(Options o) {
    if (o.rate <= 0) o.rate = 60;
    if (o.invite.empty()) {
        std::printf("join needs the invite\n");
        return 2;
    }
    auto guest = make(o);
    if (!guest) return 1;
    std::string invite = o.invite;
    if (invite.compare(0, 7, "INVITE ") == 0) invite = invite.substr(7);
    if (!applyRemote(*guest, invite, true)) return 1;
    const bool raw = isRawBlob(invite);
    if (!waitFor({{"guest", guest.get()}}, kGatherTimeoutSeconds + 5, [&] { return signalReady(*guest); }, "the reply")) return 1;
    std::printf("REPLY %s\n", localBlob(*guest, raw).c_str());
    std::fflush(stdout);
    const double t0 = now();
    if (!waitFor({{"guest", guest.get()}}, kOfferExpirySeconds + 5, [&] { return connected(*guest); }, "the host")) return 1;
    std::printf("connected %.1f s after the reply\n", now() - t0);
    Peer g("guest", guest.get());
    const bool ok = runExchange({&g}, o);
    guest->close();
    return ok ? 0 : 1;
}

int usage() {
    std::printf("usage: as3d_net_tool pair|host|join [--stun none|URL,...] [--raw-sdp] [--packets N] [--pings N]\n"
                "                     [--rate HZ] [--verbose] [invite]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string mode = argv[1];
    Options o;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--stun") {
            const std::string v = next();
            o.stunGiven = true;
            o.stun = v == "none" ? std::vector<std::string>{} : splitList(v);
        } else if (a == "--raw-sdp") {
            o.raw = true;
        } else if (a == "--packets") {
            o.packets = std::max(0, std::atoi(next()));
        } else if (a == "--pings") {
            o.pings = std::max(0, std::atoi(next()));
        } else if (a == "--rate") {
            o.rate = std::atof(next());
        } else if (a == "--verbose") {
            o.verbose = true;
        } else if (!a.empty() && a[0] != '-' && o.invite.empty()) {
            o.invite = a;
        } else {
            return usage();
        }
    }
    if (!o.stunGiven) {
        const char* env = std::getenv("AS3D_STUN");
        if (env && *env) {
            o.stun = std::string(env) == "none" ? std::vector<std::string>{} : splitList(env);
        } else {
            for (const char* s : kDefaultStunServers) o.stun.push_back(s);
        }
    }
    if (!o.raw && !codesAvailable()) {
        std::printf("the connection codes are not in this build yet: using raw SDP\n");
        o.raw = true;
    }
    if (!rtcTransportAvailable()) {
        std::printf("online play is not in this build\n");
        return 1;
    }
    if (mode == "pair") return runPair(o);
    if (mode == "host") return runHost(o);
    if (mode == "join") return runJoin(o);
    return usage();
}
