#include "game_flow.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

#include "as3d/defs.h"
#include "as3d/platform.h"

namespace as3d_game {

using namespace as3d;
using ui::FrontendState;
using ui::Screen;

namespace {

// profile.h Action order -> InputAction.
constexpr InputAction kActionOf[kActionCount] = {
    InputAction::Fire,        InputAction::NextWeapon, InputAction::Missile,  InputAction::NextMissile,
    InputAction::PowerUp,     InputAction::NextPowerUp, InputAction::Forward, InputAction::Backward,
    InputAction::Left,        InputAction::Right,
};

std::string textOf(const Blob& b) { return std::string(reinterpret_cast<const char*>(b.data()), b.size()); }

} // namespace

bool playerScreenCentre(const World& w, int player, float& vx, float& vy) {
    int pi = w.playerEntityIndex(player);
    if (pi < 0) return false;
    const ScreenRect& r = w.entity(pi).rect;
    if (!(r.max[0] > r.min[0]) || !(r.max[1] > r.min[1])) return false;
    vx = 0.5f * (r.min[0] + r.max[0]);
    vy = ui::kVirtualHeight - 0.5f * (r.min[1] + r.max[1]); // window y up -> screen y down
    return vx > -200.0f && vx < 1000.0f && vy > -200.0f && vy < 800.0f;
}

std::string defaultProfilePath() {
    std::string dir = userDataDir();
    return dir.empty() ? std::string() : dir + "profile.bin";
}

void applyDesktopPlayer2Keys(Settings& s) {
    // In Action order; the second slot keeps the spec's joystick default.
    static const int kKeys[kActionCount] = {
        'F', // Primary Attack
        'E', // Switch Weapon
        'G', // Missile Attack
        'Q', // Switch Missiles
        'H', // Use Item
        'R', // Switch Item
        'W', 'S', 'A', 'D',
    };
    for (int a = 0; a < kActionCount; ++a) s.keys[1][a][0] = kKeys[a];
}

void applyBindings(const Settings& s, InputMapper& keys) {
    keys.clearBindings();
    for (int p = 0; p < 2; ++p)
        for (int a = 0; a < kActionCount; ++a)
            for (int slot = 0; slot < 2; ++slot) {
                const int code = s.keys[p][a][slot];
                keys.bind(p, kActionOf[a], slot, code > 0 ? vkToBinding(code) : kNoBinding);
            }
}

GameFlow::GameFlow(GameSession& session, AudioBridge& audio) : session_(session), audio_(audio) {}

GameFlow::~GameFlow() = default;

bool GameFlow::init(const FlowConfig& config, std::string* error) {
    config_ = config;
    // Profile: progress and settings (frontend.md 6).
    profile_ = Profile();
    // Counts of the game, which a saved file must match; the plain front end's games start with
    // helicopter 0 only (issue 260).
    const bool plainGame = session_.game() && session_.game()->frontend == FrontendStyle::PlainList;
    const bool sequelGame = session_.game() && session_.game()->frontend == FrontendStyle::SequelMenus;
    profile_.progress = Progress::defaults(session_.rules(), Progress::defaultHelicopters(session_.game()));
    std::string why;
    // The platform's default path is the first game's old location; the save now lives in a
    // directory per game (docs/spec/issues/160). An explicit path (--profile, tests) is used as is.
    std::string legacyPath;
    const char* key = profileGameKey(session_.game());
    if (!config_.profilePath.empty() && config_.profilePath.size() >= 12 &&
        config_.profilePath.compare(config_.profilePath.size() - 12, 12, "/profile.bin") == 0 &&
        config_.profilePath == defaultProfilePath()) {
        legacyPath = config_.profilePath;
        const std::string dir = gameDataDir(key);
        config_.profilePath = dir.empty() ? std::string() : dir + "profile.bin";
    }
    ProfileLoad loaded = ProfileLoad::Fresh;
    if (!config_.profilePath.empty())
        loaded = loadProfileForGame(config_.profilePath, legacyPath, key, profile_, &why);
    if (loaded == ProfileLoad::Unusable)
        AS3D_WARN("profile %s: %s; using the defaults", config_.profilePath.c_str(), why.c_str());
    if (loaded == ProfileLoad::Migrated && config_.profileSaved) config_.profileSaved(); // web: sync the new tree
    if (loaded == ProfileLoad::Fresh) {
        if (config_.twoPlayerMode) applyDesktopPlayer2Keys(profile_.settings);
        if (config_.webKeys) applyWebKeyBindings(profile_.settings);
        // The game's default MouseControl (as2 7.2: on), with the mouse buttons it binds;
        // never in touch mode nor on the web (a captured pointer needs the page's pointer lock,
        // issue as2/272). The first game's default is off, the Settings default.
        if (session_.rules().mouseControlDefault && !config_.touch && !config_.webKeys && !profile_.settings.mouseControl) {
            profile_.settings.mouseControl = true;
            profile_.settings.applyMouseControlBindings();
        }
    }
    profile_.settings.clampToRanges();
    if (!config_.showLogo) profile_.settings.showLogo = false;

    // Content from the data: mission names and unlocks (levels.txt), Settings.xml, texts.
    ui::FrontendContent content;
    content.game = session_.game();
    if (plainGame || sequelGame) {
        // What the helicopters' definitions say: the plain front end's list (issue 260), the
        // sequels' Speed and Armor bars (as2/frontend.md 3.18).
        for (int h = 0; h < session_.rules().helicopterCount && session_.rules().heliObjects; ++h) {
            const ObjectDef* d = session_.db().findObject(session_.rules().heliObjects[h]);
            if (!d) continue;
            content.heli[h].known = true;
            content.heli[h].health = d->health;
            content.heli[h].hasSpeed = d->hasSpeed;
            content.heli[h].speed = d->speed;
        }
        content.twoPlayerMode = false;
    }
    int i = 0;
    for (const LevelDef& d : session_.db().levels()) {
        if (d.name.empty() || i >= session_.rules().missionCount) continue;
        content.missionNames[i] = d.name;
        content.enableHelic[i] = d.enableHelic;
        ++i;
    }
    Blob b;
    if (!config_.settingsXml.empty() && readPlatformFile(config_.settingsXml, b)) {
        if (!ui::parseSettingsXml(textOf(b), content)) AS3D_WARN("cannot parse %s", config_.settingsXml.c_str());
        ui::removeRereleaseBranding(content);
    } else if (!config_.settingsXml.empty()) {
        AS3D_WARN("no %s: no intro pages, version line or logo", config_.settingsXml.c_str());
    }
    ui::Texts texts;
    if (!config_.textsPath.empty() && readPlatformFile(config_.textsPath, b)) texts.parse(textOf(b));
    else AS3D_WARN("no front-end texts (%s): Information pages show a notice", config_.textsPath.c_str());
    // Video modes, refresh rate, colour depth, fullscreen and 3D sound are not offered: the
    // window is sized from the command line and sound is always 2D (issue 130).
    content.videoOptions = false;
    // The sequels' co-operative mode is not done yet: their Start Game offers one player.
    content.twoPlayerMode = config_.twoPlayerMode && !plainGame && !sequelGame;
    content.mouseControlOption = config_.mouseControlOption;
    content.touchMenuButton = config_.touchMenuButton;
    content.screenOption = config_.screenOptionAlways;
    content.handOption = config_.touch;
    content.changeGame = config_.changeGame;
    screenOverride_ = config_.screenOverride == kScreenWide || config_.screenOverride == kScreen4x3 ? config_.screenOverride : -1;
    lastScreenSetting_ = profile_.settings.screenMode;

    fe_.reset(new ui::Frontend(*this, profile_, std::move(content), std::move(texts)));
    fe_->setTouchMode(config_.touch);
    fe_->menus().drawCursor = !config_.touch && !profile_.settings.useSystemMouse;

    if (config_.attract >= 1 && config_.attract <= session_.rules().attractCount) {
        attract_ = config_.attract;
    } else {
        // Chosen once at boot and reused after every Quit (frontend.md 1.3); wall-clock
        // randomness is fine here, the attract level is never part of a recorded game.
        auto t = std::chrono::steady_clock::now().time_since_epoch().count();
        attract_ = 1 + static_cast<int>(static_cast<unsigned long long>(t) % static_cast<unsigned long long>(session_.rules().attractCount));
    }
    applySettings(profile_.settings);
    (void)error;
    return true;
}

void GameFlow::boot() { fe_->boot(); }

void GameFlow::setInputMapper(InputMapper* keys) {
    keys_ = keys;
    if (keys_) applyBindings(profile_.settings, *keys_);
}

bool GameFlow::playing() const {
    return fe_ && fe_->state() == FrontendState::Playing && !fe_->menuOpen() && !fe_->paused() && session_.hasLevel() &&
           !loadPending();
}

void GameFlow::setTouchMode(bool on) {
    config_.touch = on;
    if (!fe_) return;
    fe_->setTouchMode(on);
    fe_->menus().drawCursor = !on && !profile_.settings.useSystemMouse;
}

// ---------------------------------------------------------------------------------------
// GameHost
// ---------------------------------------------------------------------------------------

void GameFlow::levelLoaded(bool intermission) {
    ++levelLoads_;
    std::string err;
    if (view_ && !view_->beginLevel(session_, &err)) AS3D_ERROR("renderer: %s", err.c_str());
    // A level the front end holds paused from its start (the sequels' start dialogue, whose
    // pause may have been asked for before a deferred load ran).
    if (fe_ && fe_->sequel() && fe_->paused() && session_.hasLevel()) session_.world().setPaused(true);
    if (loadingHook) loadingHook(1.0f, intermission);
    if (levelLoadedHook) levelLoadedHook();
    audio_.startLevel(session_.musicPath());
}

void GameFlow::startMission(const ui::MissionStart& ms) {
    if (config_.deferLoads) {
        pending_ = PendingLoad::Mission;
        pendingStart_ = ms;
        return;
    }
    doStartMission(ms);
}

void GameFlow::loadAttract() {
    if (config_.deferLoads) {
        pending_ = PendingLoad::Attract;
        return;
    }
    doLoadAttract();
}

void GameFlow::runPendingLoad() {
    const PendingLoad p = pending_;
    pending_ = PendingLoad::None;
    if (p == PendingLoad::Mission) doStartMission(pendingStart_);
    else if (p == PendingLoad::Attract) doLoadAttract();
}

void GameFlow::doStartMission(const ui::MissionStart& ms) {
    if (loadingHook) loadingHook(0.1f, false);
    LevelSetup s;
    s.mission = ms.mission + 1;
    s.difficulty = ms.difficulty;
    s.players = ms.players;
    for (int p = 0; p < 2; ++p) {
        s.heli[p] = ms.helicopter[p];
        s.lives[p] = ms.lives[p];
        s.banked[p] = ms.banked[p];
    }
    s.camera = profile_.settings.camera;
    // "Next" in a game whose upgrades carry over (as2 engine-behaviour.delta.md 8.2): the
    // session starts the level with them instead of the mission's loadout; a new game and a
    // Restart get the loadout.
    s.carryUpgrades = ms.carryUpgrades;
    for (int p = 0; p < 2; ++p) {
        for (int k = 0; k < kMaxWeaponSlots; ++k) s.upgrades[p][k] = ms.upgrades[p][k];
        s.weapon[p] = ms.weapon[p];
        s.rankAccumulator[p] = ms.rankAccumulator[p];
    }
    std::string err;
    if (!session_.startMission(s, &err)) {
        AS3D_ERROR("cannot start mission %d: %s", s.mission, err.c_str());
        return;
    }
    levelLoaded(false);
}

void GameFlow::doLoadAttract() {
    if (loadingHook) loadingHook(0.1f, true);
    const std::string id = "intro" + std::to_string(attract_);
    std::string err;
    if (!session_.loadAttract(id, &err)) {
        AS3D_ERROR("cannot load the attract level %s: %s", id.c_str(), err.c_str());
        return;
    }
    levelLoaded(true);
}

void GameFlow::setPaused(bool paused) {
    if (!session_.hasLevel()) return;
    World& w = session_.world();
    // Closing the hint box releases the world's hint pause.
    if (!paused && w.hintShowing()) w.dismissHint();
    else w.setPaused(paused);
}

void GameFlow::clearPlayerActions() {
    if (!session_.hasLevel()) return;
    for (int p = 0; p < kMaxPlayers; ++p) session_.world().player(p).action = 0.0f;
}

void GameFlow::applySettings(const Settings& s) {
    audio_.setVolumes(s.sfxVolume, s.musicVolume);
    if (keys_) applyBindings(s, *keys_);
    if (session_.hasLevel()) session_.world().camera().mode = std::min(std::max(s.camera, 0), 3);
}

void GameFlow::settingsChanged(const Settings& s) {
    // Changing the Screen option ends the session's --screen override.
    if (s.screenMode != lastScreenSetting_) screenOverride_ = -1;
    lastScreenSetting_ = s.screenMode;
    applySettings(s);
}

void GameFlow::setScreenSize(int width, int height) {
    if (fe_) fe_->setScreenOptionShown(config_.screenOptionAlways || width * 3 > height * 4);
}

void GameFlow::saveProfile(const Profile& p) {
    if (config_.profilePath.empty()) return;
    std::string why;
    if (!saveProfileFile(config_.profilePath, p, &why, profileGameKey(session_.game()))) AS3D_ERROR("cannot save the profile: %s", why.c_str());
    else if (config_.profileSaved) config_.profileSaved();
}

void GameFlow::saveNow() { saveProfile(profile_); }

ui::MissionReport GameFlow::report() const {
    const World& w = session_.world();
    ui::MissionReport r;
    for (int p = 0; p < 2; ++p) {
        const PlayerRecord& pr = w.player(p);
        r.players[p].score = pr.scores;
        r.players[p].lives = static_cast<int>(pr.lives);
        r.players[p].stars = pr.stars;
        r.players[p].kills = pr.kills;
    }
    r.totals.starTotal = w.starTotal();
    r.totals.maxScore = w.maxLevelScore();
    r.totals.enemyTotal = w.enemiesInLevel();
    r.cheatUsed = false; // no cheat codes yet
    r.hasUpgrades = true;
    for (int p = 0; p < 2; ++p) {
        r.weapon[p] = static_cast<int>(w.player(p).weapon);
        for (int k = 0; k < kMaxWeaponSlots; ++k) r.upgrades[p][k] = w.player(p).upgrades[k];
    }
    fillCheckpoint(w, r);
    return r;
}

void fillCheckpoint(const World& w, ui::MissionReport& r) {
    // EndLevel's checkpoint (as2 engine-behaviour.delta.md 10.3): World::checkpointMission()
    // is the 1-based number of the completed mission, i.e. the 0-based index of the next.
    r.hasCheckpoint = w.rules().campaignCheckpoint && w.levelComplete() && w.checkpointMission() > 0;
    if (!r.hasCheckpoint) return;
    // After the last mission it is the mission count, which no start matches (as the original).
    r.checkpointMission = w.checkpointMission();
    for (int p = 0; p < 2; ++p) {
        const PlayerRecord& pr = w.player(p);
        r.checkpointLives[p] = pr.checkpointLives;
        r.checkpointScore[p] = pr.checkpointScore;
        r.checkpointRank[p] = pr.checkpointRank;
    }
}

// ---------------------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------------------

void GameFlow::playUiSounds() {
    for (const std::string& s : fe_->takeSounds()) audio_.playUi(s);
}

bool GameFlow::uiFrame(float dt, const ui::UiInput& input) {
    for (const ui::UiEvent& e : input.events)
        if (e.type == ui::UiEvent::Type::PointerMove) {
            pointerX_ = e.x;
            pointerY_ = e.y;
        }
    const bool took = fe_->update(dt, input);
    playUiSounds();
    return took;
}

int GameFlow::step(const FrameInput& input) {
    if (!worldRunning()) return 0;
    FrameInput in = input;
    in.pausePressed = false;
    const bool confirm = in.confirm;
    in.confirm = false;
    if (confirm && fe_->menuOpen() && fe_->topScreen() == Screen::Hint) {
        fe_->update(0.0f, ui::UiInput().key(ui::keys::Enter));
        playUiSounds();
    }
    if (!playing() || !relativeMouseActive()) {
        in.mouseSteer = false;
        in.mouseDx = in.mouseDy = 0.0f;
    } else {
        in.mouseSteer = true; // the motion itself comes from the window loop
    }
    if (!playing()) {
        in.held[0] = in.held[1] = 0;
    } else if (mouseControl() && !relativeMouseRules()) {
        // Mouse control (engine-behaviour.md 7.3): player 1's direction bits follow the pointer.
        float hx = 0, hy = 0;
        if (playerScreenCentre(session_.world(), 0, hx, hy))
            in.held[0] = (in.held[0] & ~kDirectionBits) | mouseControlBits(pointerX_, pointerY_, hx, hy);
    }
    const World& w = session_.world();
    const float t0 = w.time();
    const int ev = session_.step(in);
    const float t1 = w.time();
    if (fe_->state() == FrontendState::Playing && !w.intermission() && ui::typewriterTypes(session_.levelName(), t0, t1))
        audio_.playUi("sounds\\type.wav");
    if (ev & GameSession::kHintShown) fe_->showTutorialHint(w.hintText());
    if (ev & GameSession::kLevelComplete) fe_->onEndLevel(report());
    if (ev & GameSession::kGameOver) fe_->onGameOver(report());
    playUiSounds();
    return ev;
}

void GameFlow::drawModel(const ui::ModelView& v) {
    ++modelViews_;
    if (view_) view_->drawModel(session_, v);
}

void GameFlow::draw(int width, int height) {
    if (!view_) return;
    modelViews_ = 0;
    FrameLayers layers;
    layers.world = worldRunning();
    layers.hud = fe_->hudVisible() && session_.hasLevel() && !session_.world().intermission();
    if (layers.hud) {
        layers.hudState = hudStateOf(session_);
        // The mouse-control cursor (frontend.md 2.7) during play, not under a menu.
        // Not in the sequels, which steer by relative motion (as2 7.2, 11.2).
        layers.hudState.mouseCursor = !config_.touch && mouseControl() && !fe_->menuOpen() && !relativeMouseRules();
        layers.hudState.mouseX = pointerX_;
        layers.hudState.mouseY = pointerY_;
    }
    layers.frontend = fe_.get();
    layers.brightness = fe_->brightness();
    view_->screenMode = screenMode();
    view_->drawFrame(session_, width, height, layers);
}

void GameFlow::onBackground() {
    if (fe_->state() == FrontendState::Playing && !fe_->menuOpen()) {
        fe_->update(0.0f, ui::UiInput().key(ui::keys::Escape));
        playUiSounds();
    }
    saveNow();
}

void GameFlow::back() {
    if (fe_->state() == FrontendState::Intro) {
        fe_->update(0.0f, ui::UiInput().key(ui::keys::Escape)); // speeds the page up
        return;
    }
    if (fe_->menuOpen() && fe_->topScreen() == Screen::MainMenu && fe_->menus().depth() == 1) {
        fe_->open(Screen::Exit);
        return;
    }
    fe_->update(0.0f, ui::UiInput().key(ui::keys::Escape));
    playUiSounds();
}

} // namespace as3d_game
