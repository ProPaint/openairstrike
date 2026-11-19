// Online co-op on the game side (net_play.h, docs/spec/issues/180-online-coop.md 5, 6): the
// session's life cycle (codes, handshake, lobby, level starts behind the barrier, the level flow
// the host chooses, the end), the lockstep's records and their merge into the FrameInput both
// peers step, the hashes, and the command line's automatic sessions.
#include "net_play.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>

#include "as3d/game_profile.h"
#include "as3d/platform.h"
#include "as3d/sim_fingerprint.h"
#include "game_flow.h"
#include "game_session.h"

namespace as3d_game {

using namespace as3d;
using namespace as3d::net;
using ui::OnlineFlowChoice;
using ui::OnlinePhase;
using ui::Screen;

static_assert(as3d::net::kWireWeaponSlots == as3d::kMaxWeaponSlots, "StartMission carries every weapon slot");

namespace {

const char* phaseName(OnlinePhase p) {
    switch (p) {
        case OnlinePhase::Idle: return "idle";
        case OnlinePhase::Checking: return "checking";
        case OnlinePhase::Gathering: return "gathering";
        case OnlinePhase::InviteReady: return "invite_ready";
        case OnlinePhase::WaitingReply: return "waiting_reply";
        case OnlinePhase::ReplyReady: return "reply_ready";
        case OnlinePhase::Connecting: return "connecting";
        case OnlinePhase::Handshake: return "handshake";
        case OnlinePhase::Lobby: return "lobby";
        case OnlinePhase::Loading: return "loading";
        case OnlinePhase::Barrier: return "barrier";
        case OnlinePhase::Playing: return "playing";
        case OnlinePhase::Failed: return "failed";
        case OnlinePhase::Ended: return "ended";
    }
    return "?";
}

// The texts' %s / %d in order (the texts may come from a translation: never a printf format).
std::string subst(const std::string& fmt, std::initializer_list<std::string> args) {
    std::string out;
    auto it = args.begin();
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '%' && i + 1 < fmt.size() && (fmt[i + 1] == 's' || fmt[i + 1] == 'd')) {
            if (it != args.end()) out += *it++;
            ++i;
            continue;
        }
        out += fmt[i];
    }
    return out;
}

std::string hex(const u8* b, size_t n) {
    static const char kHex[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s += kHex[b[i] >> 4];
        s += kHex[b[i] & 15];
    }
    return s;
}

bool readWholeFile(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && out.size() < 65536) out.append(buf, n);
    std::fclose(f);
    return true;
}

// Written beside and renamed, so that a reader never sees half a code.
bool writeFileAtomic(const std::string& path, const std::string& text) {
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fclose(f) != 0 || !ok) return false;
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

u8 clampU8(long long v) { return static_cast<u8>(std::min(std::max(v, 0LL), 255LL)); }

WireMissionStart toWire(const ui::MissionStart& ms) {
    WireMissionStart w;
    w.mission = clampU8(ms.mission);
    w.difficulty = clampU8(ms.difficulty);
    w.players = clampU8(std::min(std::max(ms.players, 1), 2));
    for (int p = 0; p < 2; ++p) {
        w.heli[p] = clampU8(ms.helicopter[p]);
        w.lives[p] = clampU8(ms.lives[p]);
        w.banked[p] = ms.banked[p];
        for (int k = 0; k < kWireWeaponSlots; ++k) w.upgrades[p][k] = clampU8(ms.upgrades[p][k]);
        w.weapon[p] = clampU8(ms.weapon[p]);
        w.rankAccumulator[p] = ms.rankAccumulator[p];
    }
    w.restart = ms.restart ? 1 : 0;
    w.carryUpgrades = ms.carryUpgrades ? 1 : 0;
    return w;
}

ui::MissionStart fromWire(const WireMissionStart& w) {
    ui::MissionStart ms;
    ms.mission = w.mission;
    ms.difficulty = w.difficulty;
    ms.players = w.players;
    for (int p = 0; p < 2; ++p) {
        ms.helicopter[p] = w.heli[p];
        ms.lives[p] = w.lives[p];
        ms.banked[p] = w.banked[p];
        for (int k = 0; k < kWireWeaponSlots; ++k) ms.upgrades[p][k] = w.upgrades[p][k];
        ms.weapon[p] = w.weapon[p];
        ms.rankAccumulator[p] = w.rankAccumulator[p];
    }
    ms.restart = w.restart != 0;
    ms.carryUpgrades = w.carryUpgrades != 0;
    return ms;
}

} // namespace

NetPlay::NetPlay(GameSession& session, GameFlow& flow, const Options& o)
    : session_(session), flow_(flow), options_(o) {
    if (options_.hashEvery <= 0) options_.hashEvery = kHashEvery;
}

NetPlay::~NetPlay() {
    if (transport_) transport_->close();
    if (lingering_) lingering_->close();
}

void NetPlay::setTransportFactory(std::function<std::unique_ptr<Transport>(std::string*)> f) { factory_ = std::move(f); }

// ---------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------

bool NetPlay::active() const {
    return status_.phase >= OnlinePhase::Gathering && status_.phase <= OnlinePhase::Playing;
}

bool NetPlay::playing() const { return status_.phase == OnlinePhase::Playing && lockstepLive_; }

bool NetPlay::inPlay() const {
    return status_.phase == OnlinePhase::Loading || status_.phase == OnlinePhase::Barrier || status_.phase == OnlinePhase::Playing;
}

bool NetPlay::stalled() const { return stalling_; }
double NetPlay::stalledSeconds() const { return stalling_ ? now_ - stallStart_ : 0.0; }

void NetPlay::setPhase(OnlinePhase p) {
    if (status_.phase == p) return;
    status_.phase = p;
    AS3D_INFO("AS3D_NET state=%s", phaseName(p));
}

void NetPlay::reset() {
    if (transport_) {
        transport_->close();
        transport_.reset();
    }
    role_ = Role::None;
    status_ = ui::OnlineStatus();
    helloSent_ = helloOk_ = false;
    codeShown_ = joinAsked_ = false;
    pingSeq_ = 0;
    nextPing_ = 0.0;
    rtts_.clear();
    pongs_ = 0;
    token_ = 0;
    haveStart_ = false;
    readySent_ = false;
    remoteReady_ = 0;
    lockstepLive_ = false;
    confirmRequested_ = localPause_ = false;
    stalling_ = false;
    lastHashed_ = kNoFrame;
    finishing_ = false;
    remoteHashAny_ = false;
    autoChosen_ = false;
    autoScreenSince_ = -1.0;
}

std::string NetPlay::text(const char* key) const { return flow_.frontend().texts().get(key); }

bool NetPlay::computeFingerprint() {
    if (fingerprintDone_) return true;
    simDataFingerprintBytes(session_.vfs(), fingerprint_);
    fingerprintDone_ = true;
    AS3D_INFO("AS3D_NET fingerprint=%s", hex(fingerprint_, 32).c_str());
    return true;
}

bool NetPlay::makeTransport() {
    std::string err;
    if (factory_) {
        transport_ = factory_(&err);
    } else {
        TransportConfig c;
        if (options_.stunServers.empty())
            for (const char* s : kDefaultStunServers) c.stunServers.push_back(s);
        else
            for (const std::string& s : options_.stunServers)
                if (!s.empty() && s != "none") c.stunServers.push_back(s);
        transport_ = makeRtcTransport(c, &err);
    }
    if (transport_) return true;
    AS3D_WARN("AS3D_NET no transport: %s", err.c_str());
    status_.message = text("online.err.not_built");
    setPhase(OnlinePhase::Failed);
    return false;
}

double NetPlay::rttMs() const {
    if (rtts_.empty()) return -1.0;
    std::vector<double> v = rtts_;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// 4.1's delay from the median of the last five round trips, plus one frame when they spread over
// more than a frame (a jittery link: the median alone leaves the slow packets no margin).
int NetPlay::chooseDelay() const {
    const double rtt = rttMs();
    int d = inputDelayForRtt(rtt < 0 ? 100.0 : rtt);
    if (rtts_.size() >= 2) {
        const auto mm = std::minmax_element(rtts_.begin(), rtts_.end());
        if (*mm.second - *mm.first > 1000.0 / 60.0) ++d;
    }
    return std::min(std::max(d, kMinInputDelay), kMaxInputDelay);
}

// ---------------------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------------------

void NetPlay::sendCtl(const std::vector<u8>& m) {
    if (transport_ && !m.empty()) transport_->sendCtl(m.data(), m.size());
}

void NetPlay::sendHello() {
    HelloMsg h;
    h.protocolVersion = kNetProtocolVersion;
    h.buildId = netBuildId();
    h.platform = static_cast<u8>(localPlatform());
    h.gameKey = profileGameKey(session_.game());
    std::copy(fingerprint_, fingerprint_ + 32, h.simFingerprint);
    h.role = static_cast<u8>(role_ == Role::Host ? PeerRole::Host : PeerRole::Guest);
    encodeHello(h, buf_);
    sendCtl(buf_);
    helloSent_ = true;
}

void NetPlay::sendLobby() {
    if (role_ != Role::Host) return;
    LobbyMsg m;
    m.mission = clampU8(status_.lobby.mission);
    m.difficulty = clampU8(status_.lobby.difficulty);
    m.hostHeli = clampU8(status_.lobby.heli[0]);
    m.guestHeli = clampU8(status_.lobby.heli[1]);
    m.godMode = status_.lobby.godMode ? 1 : 0;
    encodeLobby(m, buf_);
    sendCtl(buf_);
}

void NetPlay::sendLobbyGuest() {
    if (role_ != Role::Guest) return;
    LobbyGuestMsg m;
    m.heli = clampU8(status_.lobby.heli[1]);
    m.ready = status_.lobby.guestReady ? 1 : 0;
    encodeLobbyGuest(m, buf_);
    sendCtl(buf_);
}

void NetPlay::sendPacket() {
    if (!transport_ || !lockstepLive_) return;
    lockstep_.buildInputsPacket(buf_);
    if (!buf_.empty()) transport_->sendInputs(buf_.data(), buf_.size());
}

void NetPlay::sendHash(u32 frame, u64 hash) {
    HashMsg m;
    m.token = token_;
    m.frame = frame;
    m.hash = hash;
    encodeHash(m, buf_);
    sendCtl(buf_);
    lastHashed_ = frame;
    AS3D_INFO("AS3D_NET hash frame=%u h=%016llx", frame, static_cast<unsigned long long>(hash));
}

// ---------------------------------------------------------------------------------------
// The end
// ---------------------------------------------------------------------------------------

void NetPlay::endWith(const std::string& message, bool bye, ByeReason reason, const char* why) {
    if (!active()) return;
    const bool wasInPlay = inPlay();
    if (transport_) {
        if (bye) {
            ByeMsg b;
            b.reason = static_cast<u8>(reason);
            b.text = why ? why : "";
            encodeBye(b, buf_);
            sendCtl(buf_);
        }
        // Kept a moment so that the Bye or Desync leaves before the connection closes.
        if (lingering_) lingering_->close();
        lingering_ = std::move(transport_);
        lingerUntil_ = now_ + 1.5;
    }
    AS3D_INFO("AS3D_NET end reason=%s frames=%ld", why ? why : "?", steppedFrames_);
    const bool finishing = finishing_;
    lockstepLive_ = false;
    stalling_ = false;
    ended_ = true;
    status_.stalled = status_.remotePaused = status_.losing = false;
    if (message.empty() || finishing) {
        // Ours (Quit, Back, the end of a --frames run): no notice.
        reset();
        AS3D_INFO("AS3D_NET state=%s", phaseName(OnlinePhase::Idle));
        return;
    }
    status_.message = message;
    // Before the lobby a failure (the code screens go to the notice and back); after, the end.
    setPhase(helloOk_ ? OnlinePhase::Ended : OnlinePhase::Failed);
    // A session that was playing: the front end goes to the main menu with the notice (6.4).
    if (wasInPlay) flow_.frontend().onlineEnded(message);
}

void NetPlay::protocolError(const char* what) {
    AS3D_WARN("AS3D_NET protocol error: %s", what);
    endWith(text("online.err.protocol"), true, ByeReason::ProtocolError, "protocol");
}

void NetPlay::endSession(ByeReason reason) {
    const char* why = reason == ByeReason::Quit ? "quit" : reason == ByeReason::LobbyLeft ? "lobby_left" : "other";
    endWith(std::string(), true, reason, why);
}

void NetPlay::cancel() {
    if (active()) {
        endWith(std::string(), helloSent_, ByeReason::LobbyLeft, "cancel");
        return;
    }
    reset();
}

void NetPlay::acknowledgeMessage() {
    if (status_.phase == OnlinePhase::Failed || status_.phase == OnlinePhase::Ended) reset();
    status_.phase = OnlinePhase::Idle;
    status_.message.clear();
}

// ---------------------------------------------------------------------------------------
// Codes
// ---------------------------------------------------------------------------------------

void NetPlay::hostGame(const ui::OnlineLobby& initial) {
    if (active()) return;
    reset();
    desyncSeen_ = false;
    steppedFrames_ = 0;
    computeFingerprint();
    role_ = Role::Host;
    status_.host = true;
    status_.lobby = initial;
    const GameRules& r = session_.rules();
    status_.lobby.mission = std::min(std::max(status_.lobby.mission, 0), std::max(r.missionCount - 1, 0));
    status_.lobby.difficulty = std::min(std::max(status_.lobby.difficulty, 0), std::max(r.difficultyCount - 1, 0));
    for (int p = 0; p < 2; ++p) status_.lobby.heli[p] = std::min(std::max(status_.lobby.heli[p], 0), std::max(r.helicopterCount - 1, 0));
    status_.lobby.guestReady = false;
    status_.lobby.godMode = status_.lobby.godMode || session_.options().world.godMode;
    status_.lobby.pingMs = -1;
    status_.lobby.inputDelay = 0;
    if (!makeTransport()) return;
    setPhase(OnlinePhase::Gathering);
    transport_->hostOffer();
}

void NetPlay::joinGame(const std::string& inviteCode) {
    if (active()) return;
    reset();
    SignalInfo info;
    if (!decodeSignalCode(inviteCode, info)) {
        status_.message = text("online.err.bad_code");
        setPhase(OnlinePhase::Failed);
        return;
    }
    if (info.kind != SignalKind::Invite) {
        status_.message = text("online.err.wrong_kind.reply");
        setPhase(OnlinePhase::Failed);
        return;
    }
    computeFingerprint();
    desyncSeen_ = false;
    steppedFrames_ = 0;
    role_ = Role::Guest;
    status_.host = false;
    status_.lobby = ui::OnlineLobby();
    status_.lobby.heli[1] = 0;
    if (!makeTransport()) return;
    setPhase(OnlinePhase::Gathering);
    transport_->joinOffer(info);
}

void NetPlay::acceptReply(const std::string& replyCode) {
    if (role_ != Role::Host || (status_.phase != OnlinePhase::InviteReady && status_.phase != OnlinePhase::WaitingReply)) return;
    SignalInfo info;
    if (!decodeSignalCode(replyCode, info)) {
        status_.message = text("online.err.bad_code");
        return;
    }
    if (info.kind != SignalKind::Reply) {
        status_.message = text("online.err.wrong_kind");
        return;
    }
    status_.message.clear();
    transport_->acceptAnswer(info);
    setPhase(OnlinePhase::Connecting);
}

void NetPlay::pageCode(const std::string& code) {
    if (role_ == Role::Host) acceptReply(code);
    else if (!active() && status_.phase == OnlinePhase::Idle) joinGame(code);
}

void NetPlay::pageDismissed() {
    ui::Frontend& fe = flow_.frontend();
    if (status_.phase == OnlinePhase::Idle && fe.menuOpen() && fe.topScreen() == Screen::OnlineJoinCode)
        fe.update(0.0f, ui::UiInput().key(ui::keys::Escape));
}

// The local code is known: shown, written for the command line, handed to the page.
void NetPlay::codeReady() {
    SignalInfo info;
    if (!transport_ || !transport_->localSignal(info)) return;
    info.kind = role_ == Role::Host ? SignalKind::Invite : SignalKind::Reply;
    status_.code = encodeSignalCode(info);
    const char* kind = role_ == Role::Host ? "invite" : "reply";
    AS3D_INFO("AS3D_NET code kind=%s chars=%zu", kind, status_.code.size());
    setPhase(role_ == Role::Host ? OnlinePhase::InviteReady : OnlinePhase::ReplyReady);
    if (!options_.codeOutPath.empty() && !writeFileAtomic(options_.codeOutPath, status_.code + "\n"))
        AS3D_WARN("AS3D_NET cannot write %s", options_.codeOutPath.c_str());
    if (options_.showCode) options_.showCode(kind, status_.code);
    if (role_ == Role::Host && options_.askCode) options_.askCode("reply");
    codeShown_ = true;
}

std::string NetPlay::clipboardText() { return options_.clipboardGet ? options_.clipboardGet() : std::string(); }

void NetPlay::setClipboardText(const std::string& s) {
    if (options_.clipboardSet) options_.clipboardSet(s);
    if (options_.pageDialog && options_.showCode && !s.empty()) options_.showCode(role_ == Role::Host ? "invite" : "reply", s);
}

// ---------------------------------------------------------------------------------------
// Lobby
// ---------------------------------------------------------------------------------------

void NetPlay::setLobby(const ui::OnlineLobby& l) {
    if (role_ != Role::Host || status_.phase != OnlinePhase::Lobby) return;
    const GameRules& r = session_.rules();
    if (l.mission >= 0 && l.mission < r.missionCount) status_.lobby.mission = l.mission;
    if (l.difficulty >= 0 && l.difficulty < r.difficultyCount) status_.lobby.difficulty = l.difficulty;
    if (l.heli[0] >= 0 && l.heli[0] < r.helicopterCount) status_.lobby.heli[0] = l.heli[0];
    status_.lobby.godMode = l.godMode || session_.options().world.godMode;
    sendLobby();
}

void NetPlay::setMyHelicopter(int heli) {
    if (heli < 0 || heli >= session_.rules().helicopterCount || status_.phase != OnlinePhase::Lobby) return;
    if (role_ == Role::Host) {
        status_.lobby.heli[0] = heli;
        sendLobby();
    } else if (role_ == Role::Guest) {
        status_.lobby.heli[1] = heli;
        sendLobbyGuest();
    }
}

void NetPlay::setReady(bool ready) {
    if (role_ != Role::Guest || status_.phase != OnlinePhase::Lobby) return;
    status_.lobby.guestReady = ready;
    sendLobbyGuest();
}

void NetPlay::startGame() {
    if (role_ != Role::Host || status_.phase != OnlinePhase::Lobby || !status_.lobby.guestReady) return;
    FlowChoiceMsg m;
    m.choice = static_cast<u8>(FlowCode::StartCampaign);
    encodeFlowChoice(m, buf_);
    sendCtl(buf_);
    // The front end's onlineStartCampaign follows (the lobby's Start): its startMission is the
    // StartMission (interceptStartMission).
}

void NetPlay::chooseFlow(OnlineFlowChoice c) {
    if (!active()) return;
    if (c == OnlineFlowChoice::Quit) {
        endSession(ByeReason::Quit);
        return;
    }
    if (role_ != Role::Host || !inPlay()) return;
    FlowChoiceMsg m;
    m.choice = static_cast<u8>(c);
    encodeFlowChoice(m, buf_);
    sendCtl(buf_);
    AS3D_INFO("AS3D_NET flow choice=%d", static_cast<int>(c));
    // The old level stops here; the front end's startMission that follows is the next start.
    sendPacket();
    lockstepLive_ = false;
    setPhase(OnlinePhase::Loading);
}

void NetPlay::requestConfirm() { confirmRequested_ = true; }

void NetPlay::localPause(bool on) { localPause_ = on; }

// ---------------------------------------------------------------------------------------
// Level starts
// ---------------------------------------------------------------------------------------

bool NetPlay::interceptStartMission(const ui::MissionStart& ms, NetLevelStart& use) {
    use = NetLevelStart();
    use.start = ms;
    if (role_ == Role::Host) {
        StartMissionMsg m;
        m.token = ++nextToken_;
        u32 seed = options_.seed;
        if (seed == 0) {
            std::random_device rd;
            seed = static_cast<u32>(rd());
            if (seed == 0) seed = 1;
        }
        m.seed = seed;
        m.inputDelay = static_cast<u8>(chooseDelay());
        m.camera = clampU8(std::min(std::max(flow_.profile().settings.camera, 0), 3));
        m.godMode = status_.lobby.godMode ? 1 : 0;
        m.start = toWire(ms);
        encodeStartMission(m, buf_);
        sendCtl(buf_);
        start_ = m;
        haveStart_ = false;
    } else if (role_ == Role::Guest) {
        if (!haveStart_) return false;
        haveStart_ = false;
        const ui::MissionStart theirs = fromWire(start_.start);
        if (theirs.mission != ms.mission || theirs.difficulty != ms.difficulty || theirs.restart != ms.restart)
            AS3D_INFO("AS3D_NET start differs: local mission=%d difficulty=%d, the host's mission=%d difficulty=%d", ms.mission,
                      ms.difficulty, theirs.mission, theirs.difficulty);
    } else {
        return true;
    }
    use.start = fromWire(start_.start);
    use.seed = start_.seed;
    use.camera = start_.camera;
    use.godMode = start_.godMode;
    token_ = start_.token;
    delay_ = start_.inputDelay;
    readySent_ = false;
    lockstepLive_ = false;
    stalling_ = false;
    lastHashed_ = kNoFrame;
    status_.lobby.inputDelay = delay_;
    // A new level: nobody holds a pause or a confirm from the last one (the sequels' start
    // dialogue asks for its pause after this).
    confirmRequested_ = false;
    localPause_ = false;
    setPhase(OnlinePhase::Loading);
    AS3D_INFO("AS3D_NET start token=%u seed=%u delay=%d mission=%d", token_, start_.seed, delay_, use.start.mission);
    return true;
}

void NetPlay::levelLoaded() {
    if (status_.phase != OnlinePhase::Loading || token_ == 0) return;
    lockstep_.setHost(role_ == Role::Host);
    lockstep_.start(token_, delay_);
    lockstepLive_ = true;
    ReadyMsg r;
    r.token = token_;
    encodeReady(r, buf_);
    sendCtl(buf_);
    readySent_ = true;
    // A long load is not the other side's silence.
    lastHeard_ = std::max(lastHeard_, now_);
    setPhase(OnlinePhase::Barrier);
    checkBarrier();
}

void NetPlay::checkBarrier() {
    if (status_.phase != OnlinePhase::Barrier || !readySent_ || remoteReady_ != token_) return;
    setPhase(OnlinePhase::Playing);
    AS3D_INFO("AS3D_NET ready token=%u", token_);
    sendPacket();
}

// ---------------------------------------------------------------------------------------
// Steps
// ---------------------------------------------------------------------------------------

bool NetPlay::throttle() {
    if (!playing() || finishing_) return false;
    if (!lockstep_.shouldThrottle()) return false;
    sendPacket();
    return true;
}

bool NetPlay::prepareStep(const FrameInput& local, FrameInput& merged) {
    merged = FrameInput();
    if (!playing() || finishing_) return false;
    InputRecord r;
    r.held = static_cast<u16>(local.held[0] & 0xFFFFu);
    if (confirmRequested_ || local.confirm) r.flags |= InputRecord::kConfirm;
    if (localPause_) r.flags |= InputRecord::kPause;
    if (local.mouseSteer && (local.mouseDx != 0.0f || local.mouseDy != 0.0f)) {
        r.flags |= InputRecord::kSteer;
        r.steer = steerByte(local.mouseDx, local.mouseDy);
    }
    if (lockstep_.setLocal(r)) confirmRequested_ = false; // held for the one frame it went out on
    if (!lockstep_.canStep()) {
        if (!stalling_) {
            stalling_ = true;
            stallStart_ = now_;
        }
        lockstep_.noteStall();
        sendPacket();
        return false;
    }
    if (stalling_) {
        stalling_ = false;
        const double ms = (now_ - stallStart_) * 1000.0;
        if (ms > 100.0) AS3D_INFO("AS3D_NET stall ms=%.0f frame=%u", ms, lockstep_.frame());
    }
    InputRecord h, g;
    lockstep_.takeInputs(h, g);
    // The merge of 4.2.
    merged.held[0] = h.held;
    merged.held[1] = g.held;
    merged.confirm = ((h.flags | g.flags) & InputRecord::kConfirm) != 0;
    const bool pause = ((h.flags | g.flags) & InputRecord::kPause) != 0;
    const World& w = session_.world();
    if (!w.hintShowing() && !w.levelComplete() && !w.gameOver()) merged.pausePressed = pause != w.paused();
    if (h.flags & InputRecord::kSteer) {
        merged.mouseSteer = true;
        merged.mouseDx = kSteerTable[h.steer].x;
        merged.mouseDy = kSteerTable[h.steer].y;
    }
    const InputRecord& theirs = role_ == Role::Host ? g : h;
    status_.remotePaused = (theirs.flags & InputRecord::kPause) != 0 && !localPause_;
    return true;
}

void NetPlay::afterStep() {
    if (!lockstepLive_) return;
    ++steppedFrames_;
    const u32 f = lockstep_.frame() - 1;
    // --net-fault-frame (section 14 item 2): one bit of player 2's health flipped on this side.
    if (options_.faultFrame >= 0 && steppedFrames_ - 1 == options_.faultFrame && session_.hasLevel()) {
        World& w = session_.world();
        const int pi = w.playerEntityIndex(1) >= 0 ? w.playerEntityIndex(1) : w.playerEntityIndex(0);
        if (pi >= 0) {
            w.entity(pi).fields[F_HEALTH] ^= 1u; // the float's lowest mantissa bit
            AS3D_INFO("AS3D_NET fault frame=%u entity=%d", f, pi);
        }
    }
    if (f % static_cast<u32>(options_.hashEvery) == 0) {
        const u64 h = options_.hashFull ? session_.stateHashFull() : session_.stateHash();
        lockstep_.noteLocalHash(f, h);
        sendHash(f, h);
    }
    sendPacket();
    checkDesync();
}

void NetPlay::finish() {
    if (finishing_) return;
    finishing_ = true;
    finishStart_ = now_;
    finishToken_ = token_;
    finishFrame_ = lockstep_.frame() > 0 ? lockstep_.frame() - 1 : 0;
    if (lockstepLive_ && lastHashed_ != finishFrame_ && lockstep_.frame() > 0) {
        const u64 h = options_.hashFull ? session_.stateHashFull() : session_.stateHash();
        lockstep_.noteLocalHash(finishFrame_, h);
        sendHash(finishFrame_, h);
    }
    AS3D_INFO("AS3D_NET finish token=%u frame=%u frames=%ld", finishToken_, finishFrame_, steppedFrames_);
}

bool NetPlay::finished() const {
    if (!finishing_) return !active();
    if (!active()) return true;
    if (remoteHashAny_ && remoteHashToken_ == finishToken_ && remoteHashFrame_ >= finishFrame_) return true;
    return now_ - finishStart_ > 15.0;
}

// ---------------------------------------------------------------------------------------
// Desync (4.4)
// ---------------------------------------------------------------------------------------

void NetPlay::writeDesyncDump(u32 frame) {
    std::string dir = options_.desyncDir;
    if (dir.empty()) dir = gameDataDir(profileGameKey(session_.game()));
    if (!dir.empty() && dir.back() != '/') dir += '/';
    char name[96];
    std::snprintf(name, sizeof name, "desync-%u-%u-%s.json", token_, frame, role_ == Role::Host ? "host" : "guest");
    const std::string path = dir + name;
    if (!session_.hasLevel() || !writeFileAtomic(path, session_.world().dumpStateJson())) AS3D_WARN("AS3D_NET cannot write %s", path.c_str());
    else AS3D_INFO("AS3D_NET desync dump %s", path.c_str());
}

void NetPlay::checkDesync() {
    u32 frame = 0;
    u64 mine = 0, yours = 0;
    if (desyncSeen_ || !lockstep_.desynced(&frame, &mine, &yours)) return;
    desyncSeen_ = true;
    AS3D_INFO("AS3D_DESYNC token=%u frame=%u mine=%016llx yours=%016llx", token_, frame, static_cast<unsigned long long>(mine),
              static_cast<unsigned long long>(yours));
    writeDesyncDump(frame);
    DesyncMsg d;
    d.token = token_;
    d.frame = frame;
    d.mine = mine;
    d.yours = yours;
    encodeDesync(d, buf_);
    sendCtl(buf_);
    finishing_ = false; // a --frames run that desyncs ends with the message, not quietly
    endWith(subst(text("online.err.desync"), {std::to_string(frame), std::to_string(token_) + "-" + std::to_string(frame)}), false, ByeReason::Desync, "desync");
}

// ---------------------------------------------------------------------------------------
// Receiving
// ---------------------------------------------------------------------------------------

void NetPlay::onHello(const HelloMsg& h) {
    if (helloOk_) return;
    const std::string mine = netBuildId();
    if (h.protocolVersion != kNetProtocolVersion) {
        endWith(subst(text("online.err.version"), {h.buildId, mine}), true, ByeReason::Quit, "version");
        return;
    }
    if (h.buildId != mine) {
        if (!options_.anyBuild) {
            endWith(subst(text("online.err.version"), {h.buildId, mine}), true, ByeReason::Quit, "build");
            return;
        }
        AS3D_WARN("AS3D_NET the other build is %s, this one %s (allowed)", h.buildId.c_str(), mine.c_str());
    }
    if (h.gameKey != profileGameKey(session_.game())) {
        endWith(subst(text("online.err.game"), {h.gameKey}), true, ByeReason::Quit, "game");
        return;
    }
    if (!std::equal(fingerprint_, fingerprint_ + 32, h.simFingerprint)) {
        AS3D_INFO("AS3D_NET fingerprint differs: theirs=%s", hex(h.simFingerprint, 32).c_str());
        endWith(subst(text("online.err.files"), {session_.game() ? session_.game()->title : "AirStrike 3D"}), true, ByeReason::Quit, "files");
        return;
    }
    if (h.role == static_cast<u8>(role_ == Role::Host ? PeerRole::Host : PeerRole::Guest)) {
        protocolError("both sides have the same role");
        return;
    }
    helloOk_ = true;
    status_.lobby.peerPlatform = peerPlatformName(h.platform);
    const double rtt = rttMs();
    AS3D_INFO("AS3D_NET connected rtt_ms=%.0f platform=%s build=%s", rtt, status_.lobby.peerPlatform.c_str(), h.buildId.c_str());
    setPhase(OnlinePhase::Lobby);
    if (role_ == Role::Host) sendLobby();
    else sendLobbyGuest();
}

void NetPlay::handleCtl(const std::vector<u8>& m) {
    const u8* d = m.data();
    const size_t n = m.size();
    lastHeard_ = now_;
    switch (static_cast<MsgType>(messageType(d, n))) {
        case MsgType::Hello: {
            HelloMsg h;
            if (!decodeHello(d, n, h)) return protocolError("Hello");
            onHello(h);
            return;
        }
        case MsgType::Bye: {
            ByeMsg b;
            if (!decodeBye(d, n, b)) return protocolError("Bye");
            AS3D_INFO("AS3D_NET bye reason=%d", b.reason);
            const bool proto = b.reason == static_cast<u8>(ByeReason::ProtocolError);
            endWith(text(proto ? "online.err.protocol" : "online.err.left"), false, ByeReason::Quit, "bye");
            return;
        }
        case MsgType::Ping: {
            PingMsg p;
            if (!decodePing(d, n, p)) return protocolError("Ping");
            encodePong(p, buf_);
            sendCtl(buf_);
            return;
        }
        case MsgType::Pong: {
            PingMsg p;
            if (!decodePong(d, n, p)) return protocolError("Pong");
            const double rtt = static_cast<double>(static_cast<u32>(nowMs() - p.sentMs));
            if (rtt >= 0 && rtt < 60000) {
                rtts_.push_back(rtt);
                if (rtts_.size() > 5) rtts_.erase(rtts_.begin());
                ++pongs_;
                status_.lobby.pingMs = static_cast<int>(rttMs() + 0.5);
                if (!inPlay()) status_.lobby.inputDelay = chooseDelay();
            }
            return;
        }
        case MsgType::Lobby: {
            LobbyMsg l;
            if (!decodeLobby(d, n, l)) return protocolError("Lobby");
            if (role_ != Role::Guest) return;
            const GameRules& r = session_.rules();
            if (l.mission >= r.missionCount || l.difficulty >= r.difficultyCount || l.hostHeli >= r.helicopterCount)
                return protocolError("Lobby values");
            status_.lobby.mission = l.mission;
            status_.lobby.difficulty = l.difficulty;
            status_.lobby.heli[0] = l.hostHeli;
            status_.lobby.godMode = l.godMode != 0;
            return;
        }
        case MsgType::LobbyGuest: {
            LobbyGuestMsg l;
            if (!decodeLobbyGuest(d, n, l)) return protocolError("LobbyGuest");
            if (role_ != Role::Host) return;
            if (l.heli >= session_.rules().helicopterCount) return protocolError("LobbyGuest values");
            status_.lobby.heli[1] = l.heli;
            status_.lobby.guestReady = l.ready != 0;
            sendLobby();
            return;
        }
        case MsgType::StartMission: {
            StartMissionMsg s;
            if (!decodeStartMission(d, n, s)) return protocolError("StartMission");
            if (role_ != Role::Guest) return;
            const GameRules& r = session_.rules();
            if (s.start.mission >= r.missionCount || s.start.difficulty >= r.difficultyCount || s.start.heli[0] >= r.helicopterCount ||
                s.start.heli[1] >= r.helicopterCount || s.camera > 3 || s.token <= token_)
                return protocolError("StartMission values");
            start_ = s;
            haveStart_ = true;
            flow_.pollNetStart();
            return;
        }
        case MsgType::Ready: {
            ReadyMsg r;
            if (!decodeReady(d, n, r)) return protocolError("Ready");
            remoteReady_ = r.token;
            checkBarrier();
            return;
        }
        case MsgType::FlowChoice: {
            FlowChoiceMsg f;
            if (!decodeFlowChoice(d, n, f)) return protocolError("FlowChoice");
            if (role_ != Role::Guest) return;
            const OnlineFlowChoice c = static_cast<OnlineFlowChoice>(f.choice);
            AS3D_INFO("AS3D_NET flow choice=%d", static_cast<int>(f.choice));
            if (c == OnlineFlowChoice::Quit) return; // the Bye follows
            if (c == OnlineFlowChoice::StartCampaign) {
                if (status_.phase != OnlinePhase::Lobby) return;
            } else {
                if (!inPlay()) return;
                lockstepLive_ = false;
                stalling_ = false;
                setPhase(OnlinePhase::Loading);
            }
            // The host's choice on this side's front end; its startMission waits for the host's
            // StartMission, which follows on the same channel.
            flow_.frontend().onlineApplyFlow(c);
            return;
        }
        case MsgType::Hash: {
            HashMsg h;
            if (!decodeHash(d, n, h)) return protocolError("Hash");
            if (h.token != token_ || !lockstepLive_) return;
            lockstep_.checkRemoteHash(h.frame, h.hash);
            if (!remoteHashAny_ || remoteHashToken_ != h.token || h.frame > remoteHashFrame_) {
                remoteHashAny_ = true;
                remoteHashToken_ = h.token;
                remoteHashFrame_ = h.frame;
            }
            checkDesync();
            return;
        }
        case MsgType::Desync: {
            DesyncMsg x;
            if (!decodeDesync(d, n, x)) return protocolError("Desync");
            if (desyncSeen_) return;
            desyncSeen_ = true;
            AS3D_INFO("AS3D_DESYNC token=%u frame=%u mine=%016llx yours=%016llx (reported by the other side)", x.token, x.frame,
                      static_cast<unsigned long long>(x.yours), static_cast<unsigned long long>(x.mine));
            writeDesyncDump(x.frame);
            finishing_ = false;
            endWith(subst(text("online.err.desync"), {std::to_string(x.frame), std::to_string(x.token) + "-" + std::to_string(x.frame)}), false, ByeReason::Desync,
                    "desync");
            return;
        }
        default: return; // unknown types are ignored (forward compatibility)
    }
}

void NetPlay::handleInputs(const std::vector<u8>& m) {
    InputsPacket p;
    if (!decodeInputs(m.data(), m.size(), p)) return; // the unreliable channel: a bad packet is dropped
    lastHeard_ = now_;
    if (!lockstepLive_ || p.token != token_) return;   // another level's (the barrier, 4.3)
    lockstep_.addRemote(p);
}

// ---------------------------------------------------------------------------------------
// Per display frame
// ---------------------------------------------------------------------------------------

void NetPlay::update(double nowSeconds) {
    now_ = nowSeconds;
    if (lingering_) {
        lingering_->update();
        std::vector<u8> drop;
        while (lingering_ && lingering_->pollCtl(drop)) {}
        while (lingering_ && lingering_->pollInputs(drop)) {}
        if (now_ >= lingerUntil_) {
            lingering_->close();
            lingering_.reset();
        }
    }
    ui::Frontend& fe = flow_.frontend();
    // The data fingerprint when the Online screen first opens ("Checking game files...": one
    // frame drawn with the text, then the work).
    if (status_.phase == OnlinePhase::Checking) {
        computeFingerprint();
        setPhase(OnlinePhase::Idle);
    } else if (!fingerprintDone_ && status_.phase == OnlinePhase::Idle && fe.menuOpen() && fe.topScreen() == Screen::Online) {
        setPhase(OnlinePhase::Checking);
        return;
    }
    // The web page asks for the invite while the join screen waits for one.
    if (options_.pageDialog && options_.askCode && status_.phase == OnlinePhase::Idle && fe.menuOpen() &&
        fe.topScreen() == Screen::OnlineJoinCode) {
        if (!joinAsked_) options_.askCode("invite");
        joinAsked_ = true;
    } else if (status_.phase == OnlinePhase::Idle) {
        joinAsked_ = false;
    }
    autoStep();
    if (!transport_) return;
    transport_->update();
    const Transport::State st = transport_->state();
    if (st == Transport::State::Failed || st == Transport::State::Closed) {
        AS3D_INFO("AS3D_NET transport %s: %s", transportStateName(st), transport_->error().c_str());
        const char* key = "online.err.lost";
        if (!helloOk_) {
            const Transport::Failure f = transport_->failure();
            key = f == Transport::Failure::Expired ? "online.err.expired"
                  : f == Transport::Failure::Closed && helloSent_ ? "online.err.left"
                                                                  : "online.err.cant_connect";
        }
        endWith(text(key), false, ByeReason::Timeout, "transport");
        return;
    }
    if (status_.phase == OnlinePhase::Gathering) codeReady();
    if (st == Transport::State::Connected && !helloSent_) {
        sendHello();
        lastHeard_ = now_;
        nextPing_ = now_;
        setPhase(OnlinePhase::Handshake);
    }
    if (!helloSent_) return;
    while (transport_ && transport_->pollCtl(buf_)) {
        const std::vector<u8> m = buf_;
        handleCtl(m);
    }
    std::vector<u8> in;
    while (transport_ && transport_->pollInputs(in)) handleInputs(in);
    if (!transport_) return;
    if (now_ >= nextPing_) {
        PingMsg p;
        p.seq = ++pingSeq_;
        p.sentMs = nowMs();
        encodePing(p, buf_);
        sendCtl(buf_);
        nextPing_ = now_ + kPingIntervalSeconds;
    }
    // Silence (6.4): no Inputs and no Pong for kSilenceTimeoutSeconds ends it; the last seconds
    // before that show "Connection lost...".
    const double silent = now_ - lastHeard_;
    status_.losing = status_.phase == OnlinePhase::Playing && silent > 3.0;
    if (silent > kSilenceTimeoutSeconds) {
        endWith(text("online.err.lost"), true, ByeReason::Timeout, "silence");
        return;
    }
    status_.stalled = stalling_ && now_ - stallStart_ >= kStallNoteSeconds;
    // Keep the other side fed while nothing steps here (a stall, the menus over a paused
    // level, the end of a --frames run).
    if (playing()) sendPacket();
}

// ---------------------------------------------------------------------------------------
// The command line and the bot's lobby
// ---------------------------------------------------------------------------------------

void NetPlay::pollCodeFiles() {
    if (now_ < nextFilePoll_) return;
    nextFilePoll_ = now_ + 0.2;
    std::string code;
    SignalInfo info;
    if (role_ == Role::Host && (status_.phase == OnlinePhase::InviteReady || status_.phase == OnlinePhase::WaitingReply) &&
        !options_.replyInPath.empty() && readWholeFile(options_.replyInPath, code) && decodeSignalCode(code, info))
        acceptReply(code);
}

void NetPlay::autoStep() {
    ui::Frontend& fe = flow_.frontend();
    if (options_.autoRole != Options::Role::None && !autoStarted_ && fe.state() != ui::FrontendState::Intro && fe.menuOpen() &&
        fe.topScreen() == Screen::MainMenu && status_.phase == OnlinePhase::Idle) {
        // The screens a player would have opened, so that the session shows as usual (the code
        // screens give way to the lobby, the notice comes up on a failure).
        if (options_.autoRole == Options::Role::Host) {
            autoStarted_ = true;
            hostGame(options_.autoInitial);
            fe.open(Screen::Online);
            fe.open(Screen::OnlineHostCode);
        } else if (now_ >= nextFilePoll_) {
            nextFilePoll_ = now_ + 0.2;
            std::string code = options_.invite;
            SignalInfo info;
            if (code.empty() && !options_.inviteInPath.empty()) readWholeFile(options_.inviteInPath, code);
            if (!code.empty()) {
                if (!options_.invite.empty() || decodeSignalCode(code, info)) {
                    autoStarted_ = true;
                    joinGame(code); // a bad ?join= code fails with the notice
                    fe.open(Screen::Online);
                    fe.open(Screen::OnlineJoinCode);
                }
            }
        }
    }
    if (options_.autoRole != Options::Role::None) pollCodeFiles();
    if (!options_.autoLobby) return;
    if (status_.phase == OnlinePhase::Lobby) {
        if (role_ == Role::Guest && !status_.lobby.guestReady) setReady(true);
        // The host starts once the ping is known (three round trips) and the guest is ready.
        if (role_ == Role::Host && status_.lobby.guestReady && pongs_ >= 3) {
            const ui::OnlineLobby l = status_.lobby;
            startGame();
            fe.onlineStartCampaign(l);
        }
        return;
    }
    if (role_ != Role::Host || status_.phase != OnlinePhase::Playing) return;
    // The host's end-screen choice, two seconds after the screen came up: Next after a mission,
    // Restart after a game over, Quit after the last mission.
    const Screen top = fe.menuOpen() ? fe.topScreen() : Screen::MainMenu;
    const bool end = top == Screen::MissionComplete || top == Screen::GameOver || top == Screen::GameComplete;
    if (!end) {
        autoScreenSince_ = -1.0;
        return;
    }
    if (autoScreenSince_ < 0 || top != autoScreen_) {
        autoScreen_ = top;
        autoScreenSince_ = now_;
        return;
    }
    if (now_ - autoScreenSince_ < 2.0) return;
    autoScreenSince_ = -1.0;
    if (top == Screen::GameComplete) {
        chooseFlow(OnlineFlowChoice::Quit);
        fe.onlineApplyFlow(OnlineFlowChoice::Quit);
        return;
    }
    const OnlineFlowChoice c = top == Screen::MissionComplete ? OnlineFlowChoice::Next : OnlineFlowChoice::Restart;
    chooseFlow(c);
    fe.onlineApplyFlow(c);
}

} // namespace as3d_game
