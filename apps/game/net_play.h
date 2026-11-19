// Online two-player co-op on the game side (docs/spec/issues/180-online-coop.md 5, 6): owns the
// Transport, the Lockstep, the data fingerprint, the lobby state and the pings; implements the
// front end's OnlineHost (as3d/online.h) and the hooks GameFlow calls while a session is active
// (6.3). Not compiled on the 3DS nor with AS3D_WITH_NET=OFF (apps/game/CMakeLists.txt); code that
// includes this header elsewhere guards it with #if AS3D_WITH_NET.
//
// Per display frame the window loop (game_loop.cpp) calls update(); per simulation step, while
// playing():
//     flow.resolveLocalInput(local);
//     if (!net.prepareStep(local, merged)) -> stall (no step this display frame)
//     flow.step(merged, /*fromNet=*/true); net.afterStep();
// and throttle() may ask it to skip one step (pacing, 4.1).
//
// Log markers (AS3D_INFO, with --markers as the others): AS3D_NET state=S, AS3D_NET code
// kind=invite|reply chars=N, AS3D_NET connected rtt_ms=R platform=P build=B, AS3D_NET fingerprint=F,
// AS3D_NET start token=T seed=S delay=D mission=M, AS3D_NET ready token=T, AS3D_NET stall ms=M,
// AS3D_NET hash frame=F h=H, AS3D_DESYNC token=T frame=F mine=H yours=H, AS3D_NET end reason=R.
//
// The command line (main.cpp --online host|join, the headless integration run of section 14)
// starts a session by itself once the main menu is up (Options::autoRole) and, with
// Options::autoLobby (--bot), plays the lobby and the host's end-screen choices too.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/input.h"
#include "as3d/net_lockstep.h"
#include "as3d/net_protocol.h"
#include "as3d/net_transport.h"
#include "as3d/online.h"
#include "as3d/frontend.h"
#include "game_flow.h"

namespace as3d_game {

class GameSession;
class GameFlow;

class NetPlay : public as3d::ui::OnlineHost {
public:
    struct Options {
        std::vector<std::string> stunServers;      // empty: the defaults (kDefaultStunServers); {"none"}: none
        bool anyBuild = false;                     // AS3D_NET_ANY_BUILD / --net-any-build: a build id mismatch only warns
        bool hashFull = false;                     // --net-hash-full: stateHashFull every hashed frame (tests)
        int hashEvery = as3d::net::kHashEvery;     // --net-hash-every N
        as3d::u32 seed = 0;                        // --seed: the host's level seed (0: std::random_device)
        long faultFrame = -1;                      // --net-fault-frame (section 14 item 2): flip a bit of player 2's health there
        // Tests and the headless integration run (section 14): codes through files.
        std::string codeOutPath;                   // --code-out: write the invite / reply code once known
        std::string inviteInPath;                  // --invite-in: read the invite from a file (it may appear later)
        std::string replyInPath;                   // --reply-in: read the reply from a file (it may appear later)
        std::string invite;                        // --invite / --join CODE (the web's ?join=)
        bool autoLobby = false;                    // --headless --bot: host sets the lobby and starts, guest readies, Next is automatic
        // --online host|join: host a game / join one (with `invite` or `inviteInPath`) once the
        // main menu is up; the lobby's initial values for the host.
        enum class Role { None, Host, Join };
        Role autoRole = Role::None;
        as3d::ui::OnlineLobby autoInitial;
        std::string desyncDir;                     // where desync-*.json go ("": gameDataDir of the game)
        // The platform's clipboard (SDL on desktop and Android) and, on the web, the page's
        // dialog for the codes (8.4): showCode(kind, code) with kind "invite" or "reply",
        // askCode(kind) opens the paste field for that kind.
        std::function<std::string()> clipboardGet;
        std::function<void(const std::string&)> clipboardSet;
        bool pageDialog = false;
        std::function<void(const std::string& kind, const std::string& code)> showCode;
        std::function<void(const std::string& kind)> askCode;
    };

    NetPlay(GameSession& session, GameFlow& flow, const Options& o);
    ~NetPlay() override;
    NetPlay(const NetPlay&) = delete;
    NetPlay& operator=(const NetPlay&) = delete;

    // Tests: the transport to use instead of makeRtcTransport (a side of makeLoopbackPair).
    void setTransportFactory(std::function<std::unique_ptr<as3d::net::Transport>(std::string*)> f);

    void update(double nowSeconds);          // per display frame: transport, ctl messages, timeouts
    bool active() const;                     // a session is on (lobby or playing)
    bool playing() const;                    // a level is running under the lockstep
    // The loop's step gate: true when frame() can be simulated; `merged` is the FrameInput to
    // step (4.2). False: stall.
    bool prepareStep(const as3d::FrameInput& localResolved, as3d::FrameInput& merged);
    void afterStep();                        // hashes, the next Inputs packet
    bool throttle();                         // skip one step this display frame (Lockstep::shouldThrottle)
    bool stalled() const;
    double stalledSeconds() const;

    // GameFlow hooks (6.3, 6.4).
    void localPause(bool on);
    // Host: sends StartMission (seed, the delay from the ping, the host's camera and god mode)
    // and fills `use`, true (load it now). Guest: true once the host's StartMission has arrived
    // (`use` is the host's; `ms` is only logged if it differs), false meanwhile (GameFlow holds
    // the start pending and asks again).
    bool interceptStartMission(const as3d::ui::MissionStart& ms, NetLevelStart& use);
    void levelLoaded();                      // sends Ready(token)
    // Quit, change game, window closed: Bye and the end of the session.
    void endSession(as3d::net::ByeReason reason);

    // OnlineHost.
    const as3d::ui::OnlineStatus& status() const override { return status_; }
    void hostGame(const as3d::ui::OnlineLobby& initial) override;
    void joinGame(const std::string& inviteCode) override;
    void acceptReply(const std::string& replyCode) override;
    void cancel() override;
    void setLobby(const as3d::ui::OnlineLobby& l) override;
    void setMyHelicopter(int heli) override;
    void setReady(bool ready) override;
    void startGame() override;
    void chooseFlow(as3d::ui::OnlineFlowChoice c) override;
    void requestConfirm() override;
    std::string clipboardText() override;
    void setClipboardText(const std::string& s) override;
    bool pageDialog() const override { return options_.pageDialog; }
    void acknowledgeMessage() override;

    // The code pasted into the web page's dialog (as3d_web_online_code): joinGame or acceptReply
    // as the phase wants.
    void pageCode(const std::string& code);
    // The page's dialog was closed without a code (as3d_web_online_dismiss): the guest's join
    // screen goes back as its Back would; nothing otherwise.
    void pageDismissed();

    // Frames simulated under the lockstep since the session started (all levels): the
    // command line's --frames counts these online.
    long steppedFrames() const { return steppedFrames_; }
    // The end of a --frames run: no further step; this side's hash of its last frame goes out
    // and finished() turns true once the other side's hash of the same frame came back (both
    // stepped exactly as far), the session ended, or 15 s passed. The caller then writes its
    // dump and calls endSession.
    void finish();
    bool finished() const;
    // A session ended (Bye, lost, desync, failure) since the last call: the headless run stops.
    bool takeEnded() {
        const bool e = ended_;
        ended_ = false;
        return e;
    }
    // A session ended by a desync (the headless run's exit code stays 0: a clean end).
    bool desynced() const { return desyncSeen_; }

private:
    enum class Role { None, Host, Guest };
    void reset();
    bool computeFingerprint();
    bool makeTransport();
    void setPhase(as3d::ui::OnlinePhase p);
    void handleCtl(const std::vector<as3d::u8>& m);
    void handleInputs(const std::vector<as3d::u8>& m);
    void onHello(const as3d::net::HelloMsg& h);
    void sendCtl(const std::vector<as3d::u8>& m);
    void sendHello();
    void sendLobby();
    void sendLobbyGuest();
    void sendPacket();
    void sendHash(as3d::u32 frame, as3d::u64 hash);
    void checkBarrier();
    void checkDesync();
    void writeDesyncDump(as3d::u32 frame);
    // The session ends with `message` (the notice's text; "" for a quiet end). `bye`: tell the
    // other side first.
    void endWith(const std::string& message, bool bye, as3d::net::ByeReason reason, const char* why);
    void protocolError(const char* what);
    void codeReady();
    void autoStep();
    void pollCodeFiles();
    bool inPlay() const;
    std::string text(const char* key) const;
    double rttMs() const;
    int chooseDelay() const;
    as3d::u32 nowMs() const { return static_cast<as3d::u32>(static_cast<as3d::i64>(now_ * 1000.0)); }

    GameSession& session_;
    GameFlow& flow_;
    Options options_;
    as3d::ui::OnlineStatus status_;
    std::function<std::unique_ptr<as3d::net::Transport>(std::string*)> factory_;
    std::unique_ptr<as3d::net::Transport> transport_;
    // A transport kept alive for a moment after the end, so that its last Bye / Desync leaves.
    std::unique_ptr<as3d::net::Transport> lingering_;
    double lingerUntil_ = 0.0;
    as3d::net::Lockstep lockstep_;
    std::vector<as3d::u8> buf_;

    Role role_ = Role::None;
    double now_ = 0.0;
    bool fingerprintDone_ = false;
    as3d::u8 fingerprint_[32] = {};
    bool helloSent_ = false, helloOk_ = false;
    bool codeShown_ = false;
    bool joinAsked_ = false;
    // Pings (section 3): every kPingIntervalSeconds once ctl is open; the last round trips.
    as3d::u32 pingSeq_ = 0;
    double nextPing_ = 0.0;
    std::vector<double> rtts_;
    int pongs_ = 0;
    double lastHeard_ = 0.0;
    // The level under the lockstep: token, delay, the barrier.
    as3d::u32 token_ = 0;          // the current level's (0: none yet)
    as3d::u32 nextToken_ = 0;      // host: the last token handed out
    int delay_ = as3d::net::kMinInputDelay;
    bool haveStart_ = false;       // guest: a StartMission not used yet
    as3d::net::StartMissionMsg start_;
    bool readySent_ = false;
    as3d::u32 remoteReady_ = 0;    // the token of the other side's last Ready
    bool lockstepLive_ = false;    // lockstep_.start ran for token_
    // The local record's flags.
    bool confirmRequested_ = false;
    bool localPause_ = false;
    // Stalls (AS3D_NET stall, the HUD's note).
    bool stalling_ = false;
    double stallStart_ = 0.0;
    long steppedFrames_ = 0;
    as3d::u32 lastHashed_ = as3d::net::kNoFrame;
    bool desyncSeen_ = false;
    bool ended_ = false;
    // --frames: the end of the run.
    bool finishing_ = false;
    double finishStart_ = 0.0;
    as3d::u32 finishToken_ = 0, finishFrame_ = 0;
    as3d::u32 remoteHashToken_ = 0, remoteHashFrame_ = 0;
    bool remoteHashAny_ = false;
    // The command line and the bot's lobby (Options::autoRole, autoLobby).
    bool autoStarted_ = false;
    double nextFilePoll_ = 0.0;
    as3d::ui::Screen autoScreen_ = as3d::ui::Screen::MainMenu;
    double autoScreenSince_ = -1.0;
    bool autoChosen_ = false;
};

} // namespace as3d_game
