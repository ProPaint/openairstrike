// Online co-op end to end in one process (docs/spec/issues/180-online-coop.md 6, 13 "T4"): two
// games (session, front end, NetPlay), headless, joined by the loopback transport pair on a test
// clock. The lobby goes through the real Frontend screens by taps (Start Game -> Online -> Host /
// Join, Copy and Paste through a shared clipboard, Connect, Ready, Start); the mission runs under
// the lockstep with the two-pilot bot and a full state hash compared every frame; a hint box is
// confirmed by the guest alone; each side pauses through its in-game menu; Mission Complete, the
// host's Next followed by the guest; the guest's Quit ends the session with the host's notice. A
// second case flips a bit of the guest's world and expects the desync end on both sides with the
// two dump files. Skips loudly without the first game's data.
//
// The flow and the session are compiled into as3d_tests by game_flow_test.cpp and
// game_integration_test.cpp, NetPlay by apps/CMakeLists.txt: this file includes headers only.
#include "doctest.h"

#if AS3D_WITH_NET

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

#include "../game/game_flow.h"
#include "../game/game_session.h"
#include "../game/net_play.h"
#include "as3d/net_transport.h"
#include "test_data.h"

using namespace as3d;
using namespace as3d_game;
using ui::OnlinePhase;
using ui::Screen;
namespace keys = as3d::ui::keys;

namespace {

constexpr float kDt = 1.0f / 60.0f;

bool haveFirstGame() {
    if (testdata::game().id != GameId::AirStrike3D) {
        std::fprintf(stderr, "SKIPPED (game '%s': the online flow test drives the first game's menus): %s\n",
                     testdata::gameKey().c_str(), __FILE__);
        return false;
    }
    if (testdata::available() && testdata::playable()) return true;
    std::fprintf(stderr, "SKIPPED (no game data): %s\n", __FILE__);
    return false;
}

struct Clock {
    double now = 100.0;
};

struct Peer {
    GameSession session;
    AudioBridge audio;
    InputMapper keys;
    std::unique_ptr<GameFlow> flow;
    std::unique_ptr<NetPlay> net;
    std::unique_ptr<net::Transport> transport; // handed to NetPlay by its factory
    ui::UiInput ui;                            // the next frame's UI events
    bool host = false;
    bool botConfirms = true;                   // the bot's hint-box OK goes into this side's records
    int stalls = 0;
    u32 endAt = ~0u;

    bool init(bool asHost, std::shared_ptr<Clock> clock, std::shared_ptr<std::string> clipboard, long faultFrame) {
        host = asHost;
        GameOptions o;
        o.dataRoot = testdata::root();
        o.startLevel = false;
        o.levelFlow = false;
        std::string err;
        if (!session.init(o, &err)) {
            FAIL_CHECK(err);
            return false;
        }
        audio.init(session.vfs(), true);
        flow.reset(new GameFlow(session, audio));
        FlowConfig c;
        c.profilePath = "";
        c.attract = 1;
        c.showLogo = false;
        c.textsPath = testdata::extractedDir() + "/texts_v170.txt";
        c.settingsXml = testdata::installDir() + "/data/Settings.xml";
        if (!flow->init(c, &err)) {
            FAIL_CHECK(err);
            return false;
        }
        flow->setInputMapper(&keys);
        NetPlay::Options no;
        no.hashFull = true; // --net-hash-full, every frame
        no.hashEvery = 1;
        no.seed = 7;
        no.faultFrame = faultFrame;
        no.desyncDir = std::string(AS3D_REPO_ROOT) + "/build/";
        no.clipboardGet = [clipboard]() { return *clipboard; };
        no.clipboardSet = [clipboard](const std::string& s) { *clipboard = s; };
        net.reset(new NetPlay(session, *flow, no));
        net->setTransportFactory([this](std::string*) { return std::move(transport); });
        flow->setNetPlay(net.get());
        flow->frontend().setOnlineHost(net.get());
        flow->boot();
        (void)clock;
        return true;
    }
    ui::Frontend& fe() { return flow->frontend(); }
    World& world() { return session.world(); }
    bool menu(Screen s) { return fe().menuOpen() && fe().topScreen() == s; }
    OnlinePhase phase() const { return net->status().phase; }

    // One display frame, as the window loop runs it (game_loop.cpp GameWindow::frame).
    void frame(double now) {
        net->update(now);
        flow->uiFrame(kDt, ui);
        ui = ui::UiInput();
        const OnlinePhase ph = phase();
        const bool hold = net->active() && (ph == OnlinePhase::Loading || ph == OnlinePhase::Barrier ||
                                            (ph == OnlinePhase::Playing && !net->playing()));
        if (hold || !flow->worldRunning()) return;
        if (net->playing()) {
            if (net->throttle()) return;
            FrameInput in;
            const FrameInput b = botInputPlayers(world(), world().frame());
            in.held[0] = b.held[host ? 0 : 1];
            in.confirm = b.confirm && botConfirms;
            flow->resolveLocalInput(in);
            FrameInput merged;
            if (!net->prepareStep(in, merged)) {
                ++stalls;
                return;
            }
            // The level's end at a chosen frame on both sides (as EndLevel would end it).
            if (world().frame() == endAt) world().endLevel();
            flow->step(merged, true);
            net->afterStep();
        } else if (!net->active() || ph <= OnlinePhase::Lobby) {
            flow->step(FrameInput());
        }
        if (session.hasLevel()) audio.drain(world());
        audio.pump(735);
    }
    // The centre of the first visible, enabled item with this id on the top menu, tapped on the
    // next frame.
    bool tapItem(int id) {
        ui::Menu* m = fe().menus().top();
        if (!m) return false;
        for (const ui::MenuItem& it : m->items)
            if (it.id == id && !it.disabled() && !it.hidden()) {
                ui.tap(it.hit.x + it.hit.w * 0.5f, it.hit.y + it.hit.h * 0.5f);
                return true;
            }
        return false;
    }
};

struct Pair {
    std::shared_ptr<Clock> clock = std::make_shared<Clock>();
    std::shared_ptr<std::string> clipboard = std::make_shared<std::string>();
    Peer h, g;

    bool init(long guestFault = -1, int latencyMs = 0) {
        net::LoopbackOptions lo;
        lo.latencyMs = latencyMs;
        auto c = clock;
        lo.nowSeconds = [c]() { return c->now; };
        net::makeLoopbackPair(h.transport, g.transport, lo);
        if (!h.init(true, clock, clipboard, -1) || !g.init(false, clock, clipboard, guestFault)) return false;
        run(10);
        return h.menu(Screen::MainMenu) && g.menu(Screen::MainMenu);
    }
    void run(int frames) {
        for (int i = 0; i < frames; ++i) {
            clock->now += kDt;
            h.frame(clock->now);
            g.frame(clock->now);
        }
    }
    // Runs until `done` or `limit` frames; false at the limit.
    template <class F> bool until(F done, int limit) {
        for (int i = 0; i < limit; ++i) {
            if (done()) return true;
            run(1);
        }
        return done();
    }
    void tap(Peer& p, int id) {
        if (!p.tapItem(id)) {
            std::string items;
            if (ui::Menu* m = p.fe().menus().top())
                for (const ui::MenuItem& it : m->items)
                    items += " " + std::to_string(it.id) + (it.hidden() ? "h" : "") + (it.disabled() ? "d" : "");
            FAIL("item " << id << " on " << std::string(ui::screenName(p.fe().topScreen())) << " phase "
                          << static_cast<int>(p.phase()) << " items" << items);
        }
        run(1);
    }
    // Main menu -> Start Game -> "Online" -> Next: the Online screen, its fingerprint done.
    void toOnline(Peer& p) {
        REQUIRE(p.menu(Screen::MainMenu));
        p.ui.tap(400, 266);
        run(20);
        REQUIRE(p.menu(Screen::StartGame));
        ui::MenuItem* mode = p.fe().menus().top()->find(5);
        REQUIRE(mode);
        for (int i = 0; i < 4 && mode->values[static_cast<size_t>(mode->index)] != "Online"; i++) tap(p, 5);
        REQUIRE(mode->values[static_cast<size_t>(mode->index)] == "Online");
        tap(p, 2);
        REQUIRE(p.menu(Screen::Online));
        run(3); // "Checking game files..." first
        REQUIRE(until([&] { return p.phase() == OnlinePhase::Idle && p.tapItem(2); }, 30));
        p.ui = ui::UiInput();
    }
    // The whole lobby by taps: host, invite through the clipboard, join, reply back, connect,
    // ready, start; both at the first frame of the mission under the lockstep.
    void lobby() {
        toOnline(h);
        tap(h, 2); // Host a game
        REQUIRE(h.menu(Screen::OnlineHostCode));
        REQUIRE(until([&] { return h.phase() == OnlinePhase::InviteReady; }, 60));
        tap(h, 4); // Copy
        REQUIRE(*clipboard == h.net->status().code);
        toOnline(g);
        tap(g, 3); // Join a game
        REQUIRE(g.menu(Screen::OnlineJoinCode));
        tap(g, 5); // Paste
        tap(g, 7); // Next
        REQUIRE(until([&] { return g.phase() == OnlinePhase::ReplyReady; }, 60));
        tap(g, 4); // Copy the reply
        tap(h, 5); // Paste
        tap(h, 6); // Connect
        REQUIRE(until([&] { return h.menu(Screen::OnlineLobby) && g.menu(Screen::OnlineLobby); }, 120));
        CHECK(h.net->status().lobby.peerPlatform == "desktop");
        run(130); // a few pings
        tap(g, 14); // Ready
        REQUIRE(until([&] { return h.net->status().lobby.guestReady; }, 30));
        tap(h, 13); // Start
        REQUIRE(until([&] { return h.net->playing() && g.net->playing(); }, 120));
        CHECK(h.session.mission() == 1);
        CHECK(g.session.mission() == 1);
    }
};

} // namespace

TEST_CASE("online flow: lobby by taps, a mission under the lockstep, hint, pauses, Next, Quit") {
    if (!haveFirstGame()) return;
    Pair p;
    REQUIRE(p.init());
    p.h.botConfirms = false; // the hint boxes close by the guest's OK alone
    p.lobby();

    // The tutorial's hint boxes: shown on both at the same frame, closed by the guest.
    int hintFrames = 0;
    for (int i = 0; i < 900; ++i) {
        p.run(1);
        if (p.h.session.world().hintShowing()) ++hintFrames;
        REQUIRE(p.h.net->active());
        REQUIRE(p.g.net->active());
    }
    CHECK(hintFrames > 0);
    CHECK_FALSE(p.h.menu(Screen::Hint));
    CHECK_FALSE(p.g.session.world().hintShowing());

    // A pause by each side: the in-game menu pauses both worlds; Resume releases them.
    for (Peer* who : {&p.h, &p.g}) {
        Peer& other = who == &p.h ? p.g : p.h;
        REQUIRE(p.until([&] { return !who->fe().menuOpen() && !p.h.session.world().hintShowing(); }, 600));
        who->ui.key(keys::Escape);
        REQUIRE(p.until([&] { return other.session.world().paused() && who->session.world().paused(); }, 60));
        REQUIRE(who->menu(Screen::InGame));
        p.run(5);
        CHECK(other.net->status().remotePaused);
        CHECK_FALSE(who->net->status().remotePaused);
        p.run(30);
        CHECK(other.session.world().paused());
        p.tap(*who, 1); // Resume
        REQUIRE(p.until([&] { return !other.session.world().paused() && !who->session.world().paused(); }, 60));
        p.run(60);
    }

    // Mission Complete on both at the same frame, then the host's Next (Continue).
    REQUIRE(p.until([&] { return !p.h.fe().menuOpen() && !p.g.fe().menuOpen(); }, 600));
    p.h.endAt = p.g.endAt = std::max(p.h.session.world().frame(), p.g.session.world().frame()) + 30;
    REQUIRE(p.until([&] { return p.h.menu(Screen::MissionComplete) && p.g.menu(Screen::MissionComplete); }, 60));
    p.run(30);
    const int loads = p.g.flow->levelLoads();
    p.tap(p.h, 3); // Continue
    REQUIRE(p.until([&] { return p.h.net->playing() && p.g.net->playing() && p.g.flow->levelLoads() > loads; }, 300));
    CHECK(p.h.session.mission() == 2);
    CHECK(p.g.session.mission() == 2);
    p.run(300);
    CHECK(p.h.session.world().frame() > 100);
    CHECK_FALSE(p.h.net->desynced());
    CHECK_FALSE(p.g.net->desynced());

    // The guest quits through its in-game menu: the host gets "The other player left".
    p.g.ui.key(keys::Escape);
    p.run(2);
    REQUIRE(p.g.menu(Screen::InGame));
    p.tap(p.g, 3); // Quit
    CHECK_FALSE(p.g.net->active());
    REQUIRE(p.until([&] { return p.h.menu(Screen::OnlineNotice); }, 120));
    CHECK(p.h.net->status().message == p.h.fe().texts().get("online.err.left"));
    CHECK(p.g.menu(Screen::MainMenu));
    p.tap(p.h, 9); // OK
    CHECK(p.h.phase() == OnlinePhase::Idle);
    CHECK(p.h.menu(Screen::MainMenu));
}

TEST_CASE("online flow: a world that differs ends the session on both sides with the dumps") {
    if (!haveFirstGame()) return;
    Pair p;
    REQUIRE(p.init(/*guestFault=*/120, /*latencyMs=*/30));
    p.lobby();
    REQUIRE(p.until([&] { return p.h.net->desynced() && p.g.net->desynced(); }, 600));
    REQUIRE(p.until([&] { return p.h.menu(Screen::OnlineNotice) && p.g.menu(Screen::OnlineNotice); }, 60));
    CHECK(p.h.phase() == OnlinePhase::Ended);
    for (const char* who : {"host", "guest"}) {
        bool found = false;
        for (int f = 118; f <= 125 && !found; ++f) {
            const std::string path = std::string(AS3D_REPO_ROOT) + "/build/desync-1-" + std::to_string(f) + "-" + who + ".json";
            if (std::FILE* file = std::fopen(path.c_str(), "rb")) {
                found = true;
                std::fclose(file);
                std::remove(path.c_str());
            }
        }
        CHECK_MESSAGE(found, who << " wrote no desync dump");
    }
}

#endif // AS3D_WITH_NET
