// Online co-op in the front end (docs/spec/issues/180-online-coop.md 8 and 9; T5): "Online" in
// Start Game of the three front-end styles (the first game's, the sequels' with AirStrike 2's and
// Gulf Thunder's looks, the plain list), the screens Online, OnlineHostCode, OnlineJoinCode,
// OnlineLobby and OnlineNotice against a FakeOnlineHost, the hint box, the in-game menu and the
// end screens online. CPU only: no game data, no GLES context.
#include "doctest.h"

#include <string>
#include <vector>

#include "as3d/frontend.h"
#include "as3d/game_data.h"
#include "as3d/online.h"

using namespace as3d;
using namespace as3d::ui;

namespace {

// Records every call, lets a test set the phase, the codes and the clipboard.
struct FakeOnlineHost : OnlineHost {
    OnlineStatus st;
    std::vector<std::string> calls;
    OnlineLobby hosted;
    std::string joined, replied, clipboard, copied;
    bool page = false;

    const OnlineStatus& status() const override { return st; }
    void hostGame(const OnlineLobby& initial) override {
        calls.push_back("host");
        hosted = initial;
        st.host = true;
        st.phase = OnlinePhase::Gathering;
        st.lobby = initial;
    }
    void joinGame(const std::string& code) override {
        calls.push_back("join");
        joined = code;
        st.host = false;
        st.phase = OnlinePhase::Gathering;
    }
    void acceptReply(const std::string& code) override {
        calls.push_back("reply");
        replied = code;
        st.phase = OnlinePhase::Connecting;
    }
    void cancel() override {
        calls.push_back("cancel");
        st.phase = OnlinePhase::Idle;
    }
    void setLobby(const OnlineLobby& l) override {
        calls.push_back("lobby");
        st.lobby = l;
    }
    void setMyHelicopter(int h) override {
        calls.push_back("heli");
        st.lobby.heli[1] = h;
    }
    void setReady(bool r) override {
        calls.push_back(r ? "ready" : "notready");
        st.lobby.guestReady = r;
    }
    void startGame() override {
        calls.push_back("start");
        st.phase = OnlinePhase::Playing;
    }
    void chooseFlow(OnlineFlowChoice c) override {
        static const char* names[] = {"next", "restart", "continue", "quit", "campaign"};
        calls.push_back(std::string("flow:") + names[static_cast<int>(c)]);
    }
    void requestConfirm() override { calls.push_back("confirm"); }
    std::string clipboardText() override { return clipboard; }
    void setClipboardText(const std::string& s) override {
        calls.push_back("copy");
        copied = s;
    }
    bool pageDialog() const override { return page; }
    void acknowledgeMessage() override {
        calls.push_back("ack");
        st.phase = OnlinePhase::Idle;
        st.message.clear();
    }
    int count(const std::string& c) const {
        int n = 0;
        for (const std::string& s : calls) n += s == c;
        return n;
    }
};

struct FakeGame : GameHost {
    std::vector<MissionStart> starts;
    int attracts = 0;
    bool paused = false;
    void startMission(const MissionStart& s) override { starts.push_back(s); paused = false; }
    void loadAttract() override { attracts++; }
    void setPaused(bool p) override { paused = p; }
    void clearPlayerActions() override {}
    void settingsChanged(const Settings&) override {}
    void saveProfile(const Profile&) override {}
    void quit() override {}
};

enum class Look { First, Sequel, Gulf, Plain };
const Look kLooks[] = {Look::First, Look::Sequel, Look::Gulf, Look::Plain};

const GameProfile& plainCopy() {
    static GameProfile copy = [] {
        GameProfile g = gameProfile(GameId::AirStrike2);
        g.frontend = FrontendStyle::PlainList;
        return g;
    }();
    return copy;
}

const GameProfile* profileOf(Look l) {
    switch (l) {
        case Look::First: return nullptr;
        case Look::Sequel: return &gameProfile(GameId::AirStrike2);
        case Look::Gulf: return &gameProfile(GameId::GulfThunder);
        case Look::Plain: return &plainCopy();
    }
    return nullptr;
}

FrontendContent contentOf(Look l) {
    FrontendContent c;
    const GameProfile* g = profileOf(l);
    c.game = g;
    c.videoOptions = false;
    c.twoPlayerMode = false;
    const int missions = g ? g->rules.missionCount : kMissionCount;
    for (int i = 0; i < missions; i++) c.missionNames[i] = "Mission " + std::to_string(i + 1);
    return c;
}

Profile profileFor(Look l) {
    Profile p;
    if (const GameProfile* g = profileOf(l)) p.progress = Progress::defaults(g->rules, Progress::defaultHelicopters(g));
    p.settings.showLogo = false;
    return p;
}

struct Rig {
    Look look;
    FakeGame game;
    FakeOnlineHost host;
    Profile profile;
    Frontend fe;
    explicit Rig(Look l, bool withHost = true, bool touch = false)
        : look(l), profile(profileFor(l)), fe(game, profile, contentOf(l), Texts{}) {
        fe.setTouchMode(touch);
        fe.menus().setPointer(-50, -50);
        fe.boot();
        fe.update(0.1f, {});
        if (withHost) fe.setOnlineHost(&host);
        fe.open(Screen::MainMenu);
    }
    // Start Game's game mode spinner (id 5; the sequels' 4) and the button that goes on (id 2).
    int modeId() const { return look == Look::Sequel || look == Look::Gulf ? 4 : 5; }
    void frame(const UiInput& in = {}) { fe.update(1.0f / 60.0f, in); }
    void wait(float s) {
        for (float t = 0; t < s; t += 1.0f / 30.0f) fe.update(1.0f / 30.0f, {});
    }
    void key(int k) { frame(UiInput().key(k)); }
    Screen top() const { return fe.topScreen(); }
    MenuItem* item(int id) {
        Menu* m = fe.menus().top();
        return m ? m->find(id) : nullptr;
    }
    bool shown(int id) {
        MenuItem* it = item(id);
        return it && !it->hidden();
    }
    bool tapItem(int id) {
        MenuItem* it = item(id);
        if (!it || it->disabled() || it->hidden()) return false;
        frame(UiInput().tap(it->hit.x + it->hit.w * 0.5f, it->hit.y + it->hit.h * 0.5f));
        return true;
    }
    // Start Game with "Online" chosen and Start / Next: the Online screen.
    void toOnline() {
        fe.open(Screen::StartGame);
        wait(0.3f);
        MenuItem* mode = item(modeId());
        REQUIRE(mode);
        REQUIRE_FALSE(mode->hidden());
        for (int i = 0; i < 4 && mode->values[static_cast<size_t>(mode->index)] != "Online"; i++) REQUIRE(tapItem(modeId()));
        REQUIRE(mode->values[static_cast<size_t>(mode->index)] == "Online");
        REQUIRE(tapItem(2));
        REQUIRE(top() == Screen::Online);
    }
    // ... and Host a game: the host's code screen.
    void toHost() {
        toOnline();
        REQUIRE(tapItem(2));
        REQUIRE(top() == Screen::OnlineHostCode);
    }
    void toJoin() {
        toOnline();
        REQUIRE(tapItem(3));
        REQUIRE(top() == Screen::OnlineJoinCode);
    }
    // A running session: the lobby's campaign started, the phase Playing.
    void playing(bool asHost) {
        host.st.host = asHost;
        host.st.phase = OnlinePhase::Lobby;
        host.st.lobby.guestReady = true;
        host.st.lobby.heli[0] = 0;
        host.st.lobby.heli[1] = 0;
        fe.onlineStartCampaign(host.st.lobby);
        host.st.phase = OnlinePhase::Playing;
        REQUIRE(fe.state() == FrontendState::Playing);
        REQUIRE(fe.online());
    }
};

std::string longCode(size_t n) {
    static const char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    std::string s;
    for (size_t i = 0; s.size() < n; i++) {
        if (i && i % 5 == 0) s += '-';
        s += alphabet[(i * 7 + i / 3) % 32];
    }
    return s;
}

void drawAll(Frontend& fe) {
    UiAssets none;
    Renderer2D r;
    r.begin(800, 600);
    fe.draw(r, none);
    CHECK(r.dropped() == 0);
}

} // namespace

TEST_CASE("online front end: Start Game offers Online only with a host, in every style") {
    for (Look look : kLooks) {
        INFO("look " << static_cast<int>(look));
        {
            Rig rig(look, false);
            rig.fe.open(Screen::StartGame);
            rig.wait(0.3f);
            if (MenuItem* mode = rig.item(rig.modeId()))
                for (const std::string& v : mode->values) CHECK(v != "Online");
        }
        Rig rig(look);
        rig.toOnline();
        CHECK(rig.host.calls.empty()); // nothing happens before a choice
        CHECK(rig.fe.state() != FrontendState::Playing);
        CHECK(rig.game.starts.empty()); // the Players screen and the campaign are bypassed
        CHECK_FALSE(rig.fe.twoPlayers());
    }
}

TEST_CASE("online front end: every online screen builds and draws, with and without a host") {
    for (Look look : kLooks)
        for (bool withHost : {false, true}) {
            INFO("look " << static_cast<int>(look) << " host " << withHost);
            Rig rig(look, withHost);
            for (Screen s : {Screen::Online, Screen::OnlineHostCode, Screen::OnlineJoinCode, Screen::OnlineLobby,
                             Screen::OnlineNotice}) {
                rig.fe.open(s);
                rig.wait(0.5f);
                REQUIRE(rig.top() == s);
                drawAll(rig.fe);
                CHECK(std::string(screenName(s)).size() > 0);
                Screen back;
                CHECK(screenFromName(screenName(s), back));
                CHECK(back == s);
            }
        }
}

TEST_CASE("online front end: Host a game, the invite code, the pasted reply, the lobby") {
    for (Look look : kLooks) {
        INFO("look " << static_cast<int>(look));
        Rig rig(look);
        rig.fe.open(Screen::StartGame);
        rig.wait(0.3f);
        rig.toOnline();
        // The first visit runs the check: the buttons give way to the line.
        rig.host.st.phase = OnlinePhase::Checking;
        rig.wait(0.1f);
        CHECK_FALSE(rig.shown(2));
        CHECK_FALSE(rig.shown(3));
        rig.host.st.phase = OnlinePhase::Idle;
        rig.wait(0.1f);
        REQUIRE(rig.tapItem(2));
        CHECK(rig.top() == Screen::OnlineHostCode);
        CHECK(rig.host.count("host") == 1);
        CHECK(rig.host.hosted.difficulty == 2);
        // Gathering: no code, Copy and Connect are off.
        rig.wait(0.1f);
        CHECK(rig.item(4)->disabled());
        CHECK(rig.item(6)->disabled());
        // The code arrives: Copy sends it to the clipboard, the 360-character code draws.
        const std::string code = longCode(360);
        rig.host.st.phase = OnlinePhase::InviteReady;
        rig.host.st.code = code;
        rig.wait(0.1f);
        drawAll(rig.fe);
        REQUIRE(rig.tapItem(4));
        CHECK(rig.host.copied == code);
        // Paste: the clipboard's text without its blanks; Connect hands it to the host.
        rig.host.clipboard = "  ABCD-EFGH-JKMN\r\n";
        REQUIRE(rig.tapItem(5));
        CHECK(rig.item(kOnlineFieldItem)->text == "ABCD-EFGH-JKMN");
        drawAll(rig.fe);
        REQUIRE(rig.tapItem(6));
        CHECK(rig.host.replied == "ABCD-EFGH-JKMN");
        CHECK(rig.item(kOnlineFieldItem)->text.empty());
        // Typing (Ctrl+V arrives as characters) and Backspace.
        rig.host.st.phase = OnlinePhase::WaitingReply;
        MenuItem* field = rig.item(kOnlineFieldItem);
        REQUIRE(field);
        REQUIRE(rig.tapItem(kOnlineFieldItem)); // a click focuses it
        CHECK(rig.fe.wantsTextInput());
        rig.frame(UiInput().text("QWER-1234").key(keys::Backspace));
        CHECK(field->text == "QWER-123");
        // The session reaches the lobby: the code screen gives way to it.
        rig.host.st.phase = OnlinePhase::Lobby;
        rig.wait(0.1f);
        CHECK(rig.top() == Screen::OnlineLobby);
        // Back from the lobby: cancel, and the Online screen is below.
        REQUIRE(rig.tapItem(1));
        CHECK(rig.host.count("cancel") == 1);
        CHECK(rig.top() == Screen::Online);
        // Back on the Online screen with nothing running: no cancel.
        REQUIRE(rig.tapItem(1));
        CHECK(rig.host.count("cancel") == 1);
        CHECK(rig.top() == Screen::StartGame);
    }
}

TEST_CASE("online front end: the host's lobby changes the lobby and Start begins the campaign") {
    for (Look look : kLooks) {
        INFO("look " << static_cast<int>(look));
        Rig rig(look);
        rig.toHost();
        rig.host.st.phase = OnlinePhase::Lobby;
        rig.host.st.lobby.pingMs = 42;
        rig.host.st.lobby.inputDelay = 4;
        rig.host.st.lobby.peerPlatform = "web";
        rig.wait(0.2f);
        REQUIRE(rig.top() == Screen::OnlineLobby);
        drawAll(rig.fe);
        MenuItem* start = rig.item(13);
        REQUIRE(start);
        CHECK(start->disabled()); // the guest is not ready
        // The difficulty spinner writes the lobby.
        MenuItem* diff = rig.item(11);
        REQUIRE(diff);
        const int before = diff->index;
        REQUIRE(rig.tapItem(11));
        CHECK(diff->index != before);
        CHECK(rig.host.count("lobby") == 1);
        CHECK(rig.host.st.lobby.difficulty == diff->index);
        // The guest is ready: Start asks the host and starts the campaign with two players.
        rig.host.st.lobby.guestReady = true;
        rig.wait(0.1f);
        CHECK_FALSE(rig.item(13)->disabled());
        REQUIRE(rig.tapItem(13));
        CHECK(rig.host.count("start") == 1);
        REQUIRE(rig.game.starts.size() == 1);
        CHECK(rig.game.starts.back().players == 2);
        CHECK(rig.game.starts.back().difficulty == diff->index);
        CHECK(rig.fe.state() == FrontendState::Playing);
        CHECK(rig.fe.online());
    }
}

TEST_CASE("online front end: Join a game, the reply code, the guest's lobby") {
    for (Look look : kLooks) {
        INFO("look " << static_cast<int>(look));
        Rig rig(look);
        rig.toJoin();
        CHECK(rig.item(7)->disabled()); // Next waits for a code
        // Typed characters reach the field, which has the focus on opening.
        rig.frame(UiInput().text("ABCD EFGH"));
        CHECK(rig.item(kOnlineFieldItem)->text == "ABCDEFGH"); // blanks are not codes
        rig.host.clipboard = "WXYZ-2345";
        REQUIRE(rig.tapItem(5));
        CHECK(rig.item(kOnlineFieldItem)->text == "WXYZ-2345");
        rig.wait(0.1f);
        REQUIRE(rig.tapItem(7));
        CHECK(rig.host.joined == "WXYZ-2345");
        // The reply code step: paste items give way to the code and Copy.
        rig.host.st.code = longCode(300);
        rig.host.st.phase = OnlinePhase::ReplyReady;
        rig.wait(0.1f);
        CHECK_FALSE(rig.shown(kOnlineFieldItem));
        CHECK(rig.shown(4));
        drawAll(rig.fe);
        REQUIRE(rig.tapItem(4));
        CHECK(rig.host.copied == rig.host.st.code);
        // Connected: the guest's lobby; Ready toggles, the helicopter goes to the host.
        rig.host.st.phase = OnlinePhase::Lobby;
        rig.wait(0.1f);
        REQUIRE(rig.top() == Screen::OnlineLobby);
        CHECK_FALSE(rig.shown(10)); // mission and difficulty are the host's: text only
        CHECK_FALSE(rig.shown(11));
        CHECK(rig.shown(12));
        REQUIRE(rig.tapItem(14));
        CHECK(rig.host.count("ready") == 1);
        rig.wait(0.1f);
        REQUIRE(rig.tapItem(14));
        CHECK(rig.host.count("notready") == 1);
        drawAll(rig.fe);
    }
}

TEST_CASE("online front end: a failure opens the notice, OK goes back") {
    for (Look look : kLooks) {
        INFO("look " << static_cast<int>(look));
        Rig rig(look);
        // On the host's screen: back to the Online screen.
        rig.toHost();
        rig.host.st.phase = OnlinePhase::Failed;
        rig.host.st.message = rig.fe.texts().get("online.err.cant_connect");
        rig.wait(0.1f);
        REQUIRE(rig.top() == Screen::OnlineNotice);
        drawAll(rig.fe);
        REQUIRE(rig.tapItem(9));
        CHECK(rig.host.count("ack") == 1);
        CHECK(rig.top() == Screen::Online);
        // On the join screen: the join screen again (another code).
        REQUIRE(rig.tapItem(3));
        rig.host.st.phase = OnlinePhase::Failed;
        rig.host.st.message = rig.fe.texts().get("online.err.bad_code");
        rig.wait(0.1f);
        REQUIRE(rig.top() == Screen::OnlineNotice);
        rig.key(keys::Enter);
        CHECK(rig.top() == Screen::OnlineJoinCode);
        CHECK(rig.host.count("ack") == 2);
        // After a session: the main menu with the notice over it.
        rig.playing(true);
        rig.host.st.phase = OnlinePhase::Ended;
        rig.fe.onlineEnded(rig.fe.texts().get("online.err.left"));
        CHECK(rig.top() == Screen::OnlineNotice);
        CHECK(rig.game.attracts >= 1);
        REQUIRE(rig.tapItem(9));
        CHECK(rig.top() == Screen::MainMenu);
    }
}

TEST_CASE("online front end: the hint box confirms through the session") {
    for (Look look : kLooks) {
        INFO("look " << static_cast<int>(look));
        Rig rig(look);
        rig.playing(false);
        rig.fe.showTutorialHint("Press fire to shoot");
        REQUIRE(rig.top() == Screen::Hint);
        rig.wait(0.6f);
        REQUIRE(rig.tapItem(1));
        CHECK(rig.host.count("confirm") == 1);
        CHECK(rig.top() == Screen::Hint); // closes when the merged confirm reaches both
        rig.key(keys::Enter);
        CHECK(rig.host.count("confirm") == 1); // once
        drawAll(rig.fe);
        rig.fe.onlineHintConfirmed();
        CHECK(rig.fe.menus().empty());
        CHECK_FALSE(rig.game.paused);
    }
}

TEST_CASE("online front end: the in-game menu's Quit tells the session, Options hides the camera") {
    for (Look look : kLooks) {
        INFO("look " << static_cast<int>(look));
        Rig rig(look);
        rig.playing(true);
        rig.fe.open(Screen::InGame);
        rig.wait(0.5f);
        REQUIRE(rig.top() == Screen::InGame);
        REQUIRE(rig.tapItem(2)); // Options
        REQUIRE(rig.top() == Screen::Options);
        bool camera = false;
        for (const MenuItem& it : rig.fe.menus().top()->items)
            if (it.type == ItemType::Spinner && it.label == rig.fe.texts().get("opt.camera")) {
                camera = true;
                CHECK(it.disabled());
                CHECK(it.values.size() == 1);
            }
        // Plain and first-game options differ in rows; the camera row is wherever it is.
        (void)camera;
        rig.key(keys::Escape);
        REQUIRE(rig.top() == Screen::InGame);
        REQUIRE(rig.tapItem(3)); // Quit
        CHECK(rig.host.count("flow:quit") == 1);
        CHECK(rig.top() == Screen::MainMenu);
    }
}

TEST_CASE("online front end: end screens, the host's buttons go to the session, the guest waits") {
    for (Look look : kLooks) {
        INFO("look " << static_cast<int>(look));
        MissionReport r;
        r.players[0].score = 1000;
        r.players[0].lives = 1;
        {
            Rig rig(look);
            rig.playing(true);
            rig.fe.onEndLevel(r);
            rig.wait(2.5f);
            REQUIRE(rig.top() == Screen::MissionComplete);
            drawAll(rig.fe);
            CHECK(rig.shown(3));
            REQUIRE(rig.tapItem(3));
            CHECK(rig.host.count("flow:next") == 1);
            CHECK(rig.game.starts.size() == 2); // the next mission
        }
        {
            Rig rig(look);
            rig.playing(false);
            rig.fe.onEndLevel(r);
            rig.wait(2.5f);
            REQUIRE(rig.top() == Screen::MissionComplete);
            drawAll(rig.fe);
            CHECK_FALSE(rig.shown(3)); // Next and Restart are the host's
            CHECK_FALSE(rig.shown(2));
            CHECK(rig.shown(1));       // Quit stays
            // The host's choice arrives.
            rig.fe.onlineApplyFlow(OnlineFlowChoice::Next);
            CHECK(rig.game.starts.size() == 2);
            CHECK(rig.host.count("flow:next") == 0); // applying is not choosing
        }
        {
            Rig rig(look);
            rig.playing(false);
            rig.fe.onGameOver(r);
            rig.wait(2.5f);
            REQUIRE(rig.top() == Screen::GameOver);
            drawAll(rig.fe);
            CHECK_FALSE(rig.shown(1)); // Restart is the host's
            REQUIRE(rig.tapItem(2));
            CHECK(rig.host.count("flow:quit") == 1);
            CHECK(rig.top() == Screen::MainMenu);
        }
        {
            Rig rig(look);
            rig.playing(false);
            rig.fe.onlineApplyFlow(OnlineFlowChoice::Quit);
            CHECK(rig.top() == Screen::MainMenu);
            CHECK(rig.host.count("flow:quit") == 0);
        }
    }
}

TEST_CASE("online front end: the in-play note follows the session") {
    Rig rig(Look::First);
    rig.playing(true);
    rig.host.st.lobby.pingMs = 45;
    rig.frame();
    CHECK(rig.fe.onlineNote() == "Online 45 ms");
    rig.host.st.remotePaused = true;
    rig.frame();
    CHECK(rig.fe.onlineNote() == "Paused by the other player");
    rig.host.st.stalled = true;
    rig.frame();
    CHECK(rig.fe.onlineNote() == "Waiting for the other player...");
    rig.host.st.losing = true;
    rig.frame();
    CHECK(rig.fe.onlineNote() == "Connection lost...");
    drawAll(rig.fe);
    rig.host.st.phase = OnlinePhase::Idle;
    rig.frame();
    CHECK(rig.fe.onlineNote().empty());
}

TEST_CASE("online front end: every text of section 9 has a default") {
    Texts t;
    for (const char* key :
         {"label.players.online", "online.title", "online.host", "online.join", "online.intro", "online.checking",
          "online.invite_label", "online.reply_label", "online.paste_invite", "online.paste_reply", "online.reply_sent",
          "online.code_warning", "online.copy", "online.paste", "online.connect", "online.next", "online.ready",
          "online.not_ready", "online.start", "online.page_code", "online.gathering", "online.waiting_reply",
          "online.connecting", "online.connected", "online.lobby.mission", "online.lobby.difficulty",
          "online.lobby.your_heli", "online.lobby.other_heli", "online.lobby.delay", "online.waiting_host",
          "online.waiting_start", "online.note.ping", "online.note.stalled", "online.note.remote_paused",
          "online.note.losing", "online.err.bad_code", "online.err.wrong_kind", "online.err.expired",
          "online.err.cant_connect", "online.err.version", "online.err.files", "online.err.game",
          "online.err.protocol", "online.err.left", "online.err.lost", "online.err.desync", "online.err.not_built",
          "online.options.camera_host"}) {
        INFO(key);
        CHECK_FALSE(t.get(key).empty());
    }
}
