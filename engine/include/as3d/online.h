// The front end's view of an online session (docs/spec/issues/180-online-coop.md 8.1):
// implemented by apps/game/net_play.cpp (NetPlay), by a FakeOnlineHost in the tests. The
// front end (Frontend::setOnlineHost, the Online screens of engine/src/ui/screens_online.cpp)
// reads status() every frame and calls the actions below; it never sees the transport.
#pragma once

#include <string>

namespace as3d::ui {

enum class OnlinePhase {
    Idle,
    Checking,      // the data fingerprint is being computed ("Checking game files...")
    Gathering,     // finding this side's addresses (host: for the invite; guest: for the reply)
    InviteReady,   // host: the invite code is in status().code
    WaitingReply,  // host: the invite was shown, waiting for the pasted reply
    ReplyReady,    // guest: the reply code is in status().code
    Connecting,
    Handshake,     // Hello exchanged and checked
    Lobby,
    Loading,       // StartMission sent / received, the level loads
    Barrier,       // loaded, waiting for the other side's Ready
    Playing,
    Failed,        // status().message says why (texts online.err.*)
    Ended,         // the session ended (Bye, desync, lost); status().message says why
};
enum class OnlineFlowChoice { Next, Restart, Continue, Quit, StartCampaign };

struct OnlineLobby {           // what the lobby screen shows; the host's values are authoritative
    int mission = 0, difficulty = 2, heli[2] = {1, 0}; // heli[0] the host's, heli[1] the guest's (as Frontend::heli_)
    bool guestReady = false, godMode = false;
    int pingMs = -1, inputDelay = 0;
    std::string peerPlatform;  // "desktop", "web", "android"
};

struct OnlineStatus {
    OnlinePhase phase = OnlinePhase::Idle;
    bool host = false;
    std::string code;          // the invite (host) or reply (guest) code to show, with dashes
    std::string message;       // the failure / end message of section 9, or the status line
    OnlineLobby lobby;
    bool stalled = false;      // in play: waiting for the other side (after kStallNoteSeconds)
    bool remotePaused = false; // in play: the other side holds the pause
    bool losing = false;       // in play: silence for the last seconds before the timeout ("Connection lost...")
};

class OnlineHost {
public:
    virtual ~OnlineHost() = default;
    virtual const OnlineStatus& status() const = 0;
    virtual void hostGame(const OnlineLobby& initial) = 0;    // from the Online screen
    virtual void joinGame(const std::string& inviteCode) = 0; // the pasted invite
    virtual void acceptReply(const std::string& replyCode) = 0;
    virtual void cancel() = 0;                                // Back anywhere before play; Bye
    virtual void setLobby(const OnlineLobby& l) = 0;          // host: mission, difficulty, heli[0], godMode
    virtual void setMyHelicopter(int heli) = 0;               // guest (heli[1]); the host may use setLobby
    virtual void setReady(bool ready) = 0;                    // guest
    virtual void startGame() = 0;                             // host, when the guest is ready
    virtual void chooseFlow(OnlineFlowChoice c) = 0;          // host's end-screen buttons; Quit from either
    virtual void requestConfirm() = 0;                        // the hint box's OK
    virtual std::string clipboardText() = 0;                  // Paste (SDL_GetClipboardText; web: the page's dialog)
    virtual void setClipboardText(const std::string& s) = 0;  // Copy
    virtual bool pageDialog() const { return false; }        // web: the page shows / collects the codes (8.4)
    virtual void acknowledgeMessage() = 0;                    // the notice's OK (phase back to Idle)
};

} // namespace as3d::ui
