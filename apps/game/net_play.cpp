// Online co-op on the game side (net_play.h, docs/spec/issues/180-online-coop.md 5, 6). T0
// skeleton: no session ever starts (status() stays Idle, active() and playing() are false, so the
// loop never routes a step here). T4 implements it.
#include "net_play.h"

#include "as3d/game_profile.h"
#include "game_flow.h"
#include "game_session.h"

namespace as3d_game {

static_assert(as3d::net::kWireWeaponSlots == as3d::kMaxWeaponSlots, "StartMission carries every weapon slot");

NetPlay::NetPlay(GameSession& session, GameFlow& flow, const Options& o)
    : session_(session), flow_(flow), options_(o) {
    (void)session_; // used from T4 on
    (void)flow_;
}

NetPlay::~NetPlay() = default;

void NetPlay::setTransportFactory(std::function<std::unique_ptr<as3d::net::Transport>(std::string*)> f) {
    factory_ = std::move(f);
}

void NetPlay::update(double) {}
bool NetPlay::active() const { return false; }
bool NetPlay::playing() const { return false; }

bool NetPlay::prepareStep(const as3d::FrameInput& localResolved, as3d::FrameInput& merged) {
    merged = localResolved;
    return false;
}

void NetPlay::afterStep() {}
bool NetPlay::throttle() { return false; }
bool NetPlay::stalled() const { return false; }
double NetPlay::stalledSeconds() const { return 0.0; }

void NetPlay::localPause(bool) {}

bool NetPlay::interceptStartMission(const as3d::ui::MissionStart& ms, NetLevelStart& use) {
    use = NetLevelStart();
    use.start = ms;
    return true;
}

void NetPlay::levelLoaded() {}
void NetPlay::endSession(as3d::net::ByeReason) {}

void NetPlay::hostGame(const as3d::ui::OnlineLobby&) {}
void NetPlay::joinGame(const std::string&) {}
void NetPlay::acceptReply(const std::string&) {}
void NetPlay::cancel() {}
void NetPlay::setLobby(const as3d::ui::OnlineLobby&) {}
void NetPlay::setMyHelicopter(int) {}
void NetPlay::setReady(bool) {}
void NetPlay::startGame() {}
void NetPlay::chooseFlow(as3d::ui::OnlineFlowChoice) {}
void NetPlay::requestConfirm() {}

std::string NetPlay::clipboardText() { return options_.clipboardGet ? options_.clipboardGet() : std::string(); }

void NetPlay::setClipboardText(const std::string& s) {
    if (options_.clipboardSet) options_.clipboardSet(s);
}

void NetPlay::acknowledgeMessage() {
    status_.phase = as3d::ui::OnlinePhase::Idle;
    status_.message.clear();
}

void NetPlay::pageCode(const std::string&) {}

} // namespace as3d_game
