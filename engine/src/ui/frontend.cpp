// Front-end state machine (frontend.md 1), intro pages (3.2), loading screen (3.16), pause and
// hotkeys during play, and the flow helpers the screens call.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "as2_screens.h"
#include "as3d/frontend.h"

namespace as3d::ui {

namespace {

const char* kScreenNames[] = {"main",     "exit",     "start",        "scores", "name",    "options",
                              "controls", "info",     "ingame",       "hint",   "gameover", "complete",
                              "gamecomplete", "heli", "credits", "dialogue"};

// Touch mode: the button that stands for Esc during play (docs/spec/issues/090).
constexpr RectF kTouchMenuButton{360, 6, 80, 22};

// The loading screen is drawn by the hosts through drawLoadingScreen(), which has no front end
// at hand: the plain front end of a PlainList game leaves what it needs here.
struct LoadingState {
    bool plain = false;
    std::string title, mission;
    bool sequel = false;  // the sequels' comic loading screen (as2/frontend.md 3.16)
    int missionIndex = 0; // 0-based, chooses the comic
};
LoadingState& loadingState() {
    static LoadingState s;
    return s;
}

} // namespace

const char* screenName(Screen s) { return kScreenNames[static_cast<int>(s)]; }

bool screenFromName(std::string_view name, Screen& out) {
    for (Screen s : kSequelScreens)
        if (name == screenName(s)) { out = s; return true; }
    return false;
}

Frontend::Frontend(GameHost& host, Profile& profile, FrontendContent content, Texts texts)
    : host_(host), profile_(profile), content_(std::move(content)), texts_(std::move(texts)),
      campaign_(rules()) {
    menus_.showHints = profile_.settings.showHints;
    menus_.drawCursor = !profile_.settings.useSystemMouse;
    menus_.plain = plain();
    pending_ = profile_.settings;
    refreshLocks();
    if (plain()) {
        heli_[0] = heli_[1] = 0; // as2 engine-behaviour.delta.md 7.6: both players start on entry 0
        loadingState().plain = true;
        loadingState().title = content_.game->title;
    }
    if (sequel()) {
        sq_ = std::make_unique<SequelState>();
        menus_.sequel = true;
        heli_[0] = heli_[1] = 0;
        loadingState().sequel = true;
    }
}

Frontend::~Frontend() {
    if (plain() || sequel()) loadingState() = LoadingState{};
}

void Frontend::setTouchMode(bool on) {
    touch_ = on;
    menus_.touchMode = on;
}

void Frontend::refreshLocks() {
    for (int i = 0; i < rules().helicopterCount; i++) heliLocked_[i] = !profile_.progress.helicopterUnlocked[i];
}

void Frontend::boot() {
    heli_[0] = plain() || sequel() ? 0 : 1;
    heli_[1] = 0;
    heliAlternator_ = 0;
    paused_ = hudHidden_ = false;
    campaign_ = Campaign(rules());
    if (profile_.settings.showLogo && (!content_.intros.empty() || sequel())) {
        intro_ = std::make_unique<IntroRun>();
        intro_->pages = content_.intros;
        if (sequel()) SequelScreens::boot(*this); // the four comic pages after the logo pages
        state_ = FrontendState::Intro;
        menus_.clear();
        return;
    }
    state_ = FrontendState::Attract;
    host_.loadAttract();
    showMainMenu();
}

float Frontend::brightness() const { return state_ == FrontendState::Intro ? 0.5f : profile_.settings.brightness; }

bool Frontend::bannerVisible() const {
    return !plain() && !sequel() && !menus_.empty() && topScreen() == Screen::MainMenu;
}

bool Frontend::wantsTextInput() const {
    // Touch mode has its own keyboard on the name-entry screen.
    const Menu* m = menus_.top();
    return !touch_ && m && m->focused >= 0 && m->items[static_cast<size_t>(m->focused)].type == ItemType::Edit;
}

Screen Frontend::topScreen() const {
    const Menu* m = menus_.top();
    return m && m->tag >= 0 ? static_cast<Screen>(m->tag) : Screen::MainMenu;
}

void Frontend::open(Screen s) {
    Menu m;
    if (sequel()) {
        m = SequelScreens::build(*this, s);
    } else if (plain()) {
        switch (s) {
            case Screen::MainMenu: m = buildPlainMain(); break;
            case Screen::Exit: m = buildPlainExit(); break;
            case Screen::StartGame: m = buildPlainStartGame(); break;
            case Screen::InGame: m = buildPlainInGame(); break;
            case Screen::Hint: m = buildPlainHint(); break;
            case Screen::GameOver: m = buildPlainGameOver(); break;
            case Screen::MissionComplete: m = buildPlainMissionComplete(); break;
            case Screen::GameComplete: m = buildPlainGameComplete(); break;
            case Screen::Information: return; // left out: the sequels' texts are not extracted (issue 260)
            case Screen::TopScores: m = buildTopScores(); break;
            case Screen::NameEntry: m = buildNameEntry(); break;
            case Screen::Options: m = buildOptions(); break;
            case Screen::Controls: m = buildControls(); break;
            default: return; // the sequels' own screens
        }
    } else switch (s) {
        case Screen::MainMenu: m = buildMainMenu(); break;
        case Screen::Exit: m = buildExit(); break;
        case Screen::StartGame: m = buildStartGame(); break;
        case Screen::TopScores: m = buildTopScores(); break;
        case Screen::NameEntry: m = buildNameEntry(); break;
        case Screen::Options: m = buildOptions(); break;
        case Screen::Controls: m = buildControls(); break;
        case Screen::Information: m = buildInformation(); break;
        case Screen::InGame: m = buildInGame(); break;
        case Screen::Hint: m = buildHint(); break;
        case Screen::GameOver: m = buildGameOver(); break;
        case Screen::MissionComplete: m = buildMissionComplete(); break;
        case Screen::GameComplete: m = buildGameComplete(); break;
        default: return; // the sequels' own screens
    }
    m.tag = static_cast<int>(s);
    m.name = screenName(s);
    if (s == Screen::MainMenu) menus_.replaceAll(std::move(m));
    else menus_.push(std::move(m));
    // Screens that start with a focused widget (name entry: the edit field).
    if (s == Screen::NameEntry)
        if (Menu* top = menus_.top())
            for (size_t i = 0; i < top->items.size(); i++)
                if (top->items[i].type == ItemType::Edit) menus_.focus(*top, static_cast<int>(i));
}

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------
void Frontend::showMainMenu() { open(Screen::MainMenu); }

void Frontend::setPausedFlag(bool on) {
    paused_ = on;
    host_.setPaused(on);
}

void Frontend::resumePlay() {
    setPausedFlag(false);
    hudHidden_ = false;
    host_.clearPlayerActions();
}

void Frontend::settingsChanged() {
    menus_.showHints = profile_.settings.showHints;
    host_.settingsChanged(profile_.settings);
}

void Frontend::save() { host_.saveProfile(profile_); }

void Frontend::startCampaign(int mission, int difficulty, int players) {
    campaign_.start(mission, difficulty, players);
    twoPlayers_ = campaign_.players == 2;
    menus_.clear();
    startLevel(false);
}

void Frontend::startLevel(bool restart, bool carryUpgrades) {
    MissionStart ms;
    ms.mission = campaign_.mission;
    if (carryUpgrades && haveCarried_) {
        ms.carryUpgrades = true;
        for (int i = 0; i < 2; i++) {
            ms.weapon[i] = carriedWeapon_[i];
            for (int k = 0; k < kMaxWeaponSlots; k++) ms.upgrades[i][k] = carriedUpgrades_[i][k];
        }
    }
    if (plain()) loadingState().mission = missionLabel(campaign_.mission);
    loadingState().missionIndex = campaign_.mission;
    ms.difficulty = campaign_.difficulty;
    ms.players = campaign_.players;
    ms.restart = restart;
    for (int i = 0; i < 2; i++) {
        ms.helicopter[i] = heli_[i];
        ms.lives[i] = campaign_.p[i].livesAtStart;
        ms.banked[i] = campaign_.p[i].banked;
        // The sequels' checkpoint holds the whole campaign's rank (issue as2/271).
        if (sequel()) ms.rankAccumulator[i] = campaign_.p[i].rankAccumulator;
    }
    paused_ = false;
    hudHidden_ = false;
    state_ = FrontendState::Playing;
    host_.startMission(ms);
    if (sequel()) SequelScreens::afterLevelStart(*this); // the start dialogue
}

void Frontend::continueCampaign() {
    menus_.clear();
    campaign_.bank(report_.players, report_.totals);
    campaign_.mission = campaign_.nextMission();
    // "Next" keeps the upgrades collected so far in a game that says so (the first game clears
    // them at every level start and never sets the flag).
    startLevel(false, rules().upgradesCarryToNextMission);
}

void Frontend::quitToMainMenu(bool bank, bool check) {
    if (bank) campaign_.bank(report_.players, report_.totals);
    campaign_.active = false;
    paused_ = false;
    hudHidden_ = false;
    state_ = FrontendState::Attract;
    host_.loadAttract();
    showMainMenu();
    if (check) highScoreCheck();
}

void Frontend::highScoreCheck() {
    if (campaign_.players != 1) return; // two-player scores are never recorded
    if (profile_.progress.qualifyingSlot(campaign_.p[0].banked) >= 0) open(Screen::NameEntry);
}

void Frontend::onEndLevel(const MissionReport& report) {
    if (sequel()) {
        SequelScreens::onEndLevel(*this, report); // checkpoint, end dialogue, then S15 or S16
        return;
    }
    report_ = report;
    haveCarried_ = report.hasUpgrades;
    for (int i = 0; i < 2; i++) {
        carriedWeapon_[i] = report.weapon[i];
        for (int k = 0; k < kMaxWeaponSlots; k++) carriedUpgrades_[i][k] = report.upgrades[i][k];
    }
    setPausedFlag(true);
    hudHidden_ = true;
    const int mission = std::clamp(campaign_.mission, 0, rules().missionCount - 1);
    profile_.progress.unlockAfterMission(mission, content_.enableHelic[mission]);
    refreshLocks();
    save(); // ours: right away, not only at exit (frontend.md 6.1 recommendation)
    typedChars_ = 0;
    open(mission == rules().missionCount - 1 ? Screen::GameComplete : Screen::MissionComplete);
}

void Frontend::onGameOver(const MissionReport& report) {
    report_ = report;
    setPausedFlag(true);
    hudHidden_ = true;
    host_.gameOverMusic();
    open(Screen::GameOver);
}

void Frontend::showTutorialHint(std::string text) {
    hintText_ = std::move(text);
    setPausedFlag(true); // the HUD stays visible behind the box
    open(Screen::Hint);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
bool Frontend::handlePlayingInput(const UiInput& input) {
    bool took = false;
    Settings& s = profile_.settings;
    for (const UiEvent& e : input.events) {
        if (!menus_.empty()) {
            // A key opened a menu earlier in this frame: the rest goes to it.
            UiInput rest;
            rest.events.push_back(e);
            menus_.update(0, rest);
            took = true;
            continue;
        }
        if (e.type == UiEvent::Type::PointerMove) {
            menus_.setPointer(e.x, e.y);
            continue;
        }
        if (e.type != UiEvent::Type::Press) continue;
        switch (e.code) {
            case keys::Escape:
                setPausedFlag(true);
                hudHidden_ = true;
                open(Screen::InGame);
                took = true;
                break;
            case 'P':
            case keys::Pause:
                if (paused_) resumePlay();
                else setPausedFlag(true);
                took = true;
                break;
            case keys::F5: case keys::F6:
                s.sfxVolume = std::clamp(s.sfxVolume + (e.code == keys::F6 ? 0.1f : -0.1f), 0.0f, 1.0f);
                settingsChanged();
                took = true;
                break;
            case keys::F7: case keys::F8:
                s.musicVolume = std::clamp(s.musicVolume + (e.code == keys::F8 ? 0.1f : -0.1f), 0.0f, 1.0f);
                settingsChanged();
                took = true;
                break;
            case keys::F9:
                s.camera = (s.camera + 1) % 4;
                settingsChanged();
                took = true;
                break;
            case keys::Mouse1:
                if (touch_ && content_.touchMenuButton &&
                    (sequel() ? SequelScreens::touchMenuRect() : kTouchMenuButton).contains(menus_.pointerX(), menus_.pointerY())) {
                    menus_.playSound("sounds\\menu1.wav");
                    setPausedFlag(true);
                    hudHidden_ = true;
                    open(Screen::InGame);
                    took = true;
                }
                break;
            default: break;
        }
    }
    return took;
}

bool Frontend::update(float dt, const UiInput& input) {
    menus_.showHints = profile_.settings.showHints;
    if (state_ == FrontendState::Intro && intro_) {
        bool skip = false;
        for (const UiEvent& e : input.events) {
            if (e.type == UiEvent::Type::PointerMove && sequel()) menus_.setPointer(e.x, e.y);
            if (e.type != UiEvent::Type::Press) continue;
            // Ours, the sequels in touch mode: a small Skip button ends the comic at once.
            if (sequel() && touch_ && e.code == keys::Mouse1 &&
                SequelScreens::touchSkipRect().contains(menus_.pointerX(), menus_.pointerY()))
                skip = true;
            else
                intro_->speed *= 4.0f;
        }
        if (sq_ && !sq_->comicMusic && intro_->pages[intro_->index].comic == 1) {
            sq_->comicMusic = true; // page 1 starts music\track02.mo3 from order 0 (as2/frontend.md 3.2)
            host_.playMusic("music\\track02.mo3");
        }
        intro_->clock += dt * intro_->speed;
        const IntroPage& page = intro_->pages[intro_->index];
        const float length = page.comic ? SequelScreens::comicDuration(page.comic) : page.divoGames ? 8.0f : 6.0f;
        if (skip || intro_->clock >= length) {
            intro_->index = skip ? intro_->pages.size() : intro_->index + 1;
            intro_->speed = 1.0f;
            // Comic pages start at 0, the logo pages half a second early (their white lead-in).
            intro_->clock = intro_->index < intro_->pages.size() && intro_->pages[intro_->index].comic ? 0.0f : -0.5f;
            if (intro_->index >= intro_->pages.size()) {
                intro_.reset();
                if (sequel()) host_.playMusic(""); // the comic's music stops
                state_ = FrontendState::Attract;
                host_.loadAttract();
                showMainMenu();
            }
        }
        return true;
    }
    if (sequel()) SequelScreens::tick(*this, dt);
    if (!menus_.empty()) {
        menus_.update(dt, input);
        return true;
    }
    if (state_ == FrontendState::Playing) return handlePlayingInput(input);
    for (const UiEvent& e : input.events)
        if (e.type == UiEvent::Type::PointerMove) menus_.setPointer(e.x, e.y);
    return false;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void Frontend::drawIntro(Renderer2D& r, const UiAssets& a) {
    if (!intro_ || intro_->index >= intro_->pages.size()) return;
    const IntroPage& p = intro_->pages[intro_->index];
    const float t = intro_->clock;
    if (p.comic) {
        SequelScreens::drawComicPage(r, a, p.comic, t);
        if (touch_) SequelScreens::drawTouchSkip(r, a);
        return;
    }
    if (p.divoGames) {
        r.fullscreen({1, 1, 1, 1}, Blend::Opaque);
        const Texture2D* logo = a.texture("gfx\\logo.tga");
        if (t > 0 && logo) {
            const float f = std::min(t / 2.0f, 1.0f);
            r.quadSpec(400 - 52 * f, 166, 104 * f, 268, 0.7f, 0, 1, 1, logo, Color{}, Blend::Opaque);
            if (t >= 2) {
                const float al = t < 4 ? (t - 2) / 2.0f : 1.0f;
                r.quadSpec(322, 180, 164, 52, 0.01f, 0.8f, 0.64f, 1.0f, logo, {1, 1, 1, al}, Blend::Alpha);
                r.quadSpec(318, 232, 164, 204, 0.01f, 0, 0.64f, 0.8f, logo, {1, 1, 1, al}, Blend::Alpha);
            }
        }
        if (t > 7) r.fullscreen({1, 1, 1, std::min(t - 7, 1.0f)}, Blend::Alpha);
        return;
    }
    if (t <= 0) {
        r.fullscreen({1, 1, 1, 1}, Blend::Opaque);
        return;
    }
    r.fullscreen(p.back, Blend::Opaque);
    if (const Texture2D* img = a.texture(p.image))
        r.pic(400 - static_cast<float>(img->width() / 2), 300 - static_cast<float>(img->height() / 2), *img, Color{}, Blend::Alpha);
    if (t < 1) r.fullscreen({1, 1, 1, 1 - t}, Blend::Alpha);
    if (t > 5) r.fullscreen({1, 1, 1, std::min(t - 5, 1.0f)}, Blend::Alpha);
}

void Frontend::drawTouchPlayButtons(Renderer2D& r, const UiAssets& a) {
    if (sequel()) {
        // The sequels: a text button of their style at the top centre.
        const RectF b = SequelScreens::touchMenuRect();
        as2::textButton(r, a, b.x, b.y, b.w - 46, SequelScreens::tr(*this, "button.touch_menu"), as2::green());
        return;
    }
    Menu dummy;
    MenuDrawContext c{r, a, dummy, 0, true, 0, plain()};
    drawTextButton(c, kTouchMenuButton, texts_.get("touch.menu"), false, false);
}

void Frontend::drawUnder(Renderer2D& r, const UiAssets& a) {
    if (state_ == FrontendState::Intro) {
        drawIntro(r, a);
        return;
    }
    menus_.drawBackground(r, a);
}

void Frontend::drawOver(Renderer2D& r, const UiAssets& a) {
    if (state_ == FrontendState::Intro) return;
    if (state_ == FrontendState::Playing && menus_.empty() && touch_ && content_.touchMenuButton) drawTouchPlayButtons(r, a);
    menus_.drawItems(r, a);
}

void drawLoadingScreen(Renderer2D& r, const UiAssets& a, float progress, bool intermission) {
    if (loadingState().sequel) {
        SequelScreens::drawLoading(r, a, progress, intermission, loadingState().missionIndex);
        return;
    }
    r.fullscreen({0, 0, 0, 1}, Blend::Opaque);
    if (loadingState().plain) {
        // Ours: "Loading", the mission's name (none for an attract level) and a progress bar, in
        // the game font on black.
        const float p = std::clamp(progress, 0.0f, 1.0f);
        Menu dummy;
        MenuDrawContext c{r, a, dummy, 0, false, 0, true};
        if (!intermission) {
            widgets::shadowedText(c, 400, 236, "Loading", orange(), Align::Center, 2.0f);
            if (!loadingState().mission.empty())
                widgets::shadowedText(c, 400, 290, loadingState().mission, Color{}, Align::Center);
            r.rect(250, 330, 300, 14, {0.188f, 0, 0, 1}, Blend::Opaque);
            r.rect(252, 332, 296 * p, 10, orange(), Blend::Opaque);
            r.outline(250, 330, 300, 14, rust(), Blend::Opaque);
        } else {
            r.rect(10, 595, p * 780, 1, {0.314f, 0, 0, 1}, Blend::Opaque);
        }
        return;
    }
    if (!intermission)
        if (const Texture2D* t = a.texture("menu\\loading.tga")) r.pic(272, 172, *t, Color{}, Blend::Alpha);
    r.rect(10, 595, std::clamp(progress, 0.0f, 1.0f) * 780, 1, {0.314f, 0, 0, 1}, Blend::Opaque);
}

// ---------------------------------------------------------------------------
// Viewer and test hooks
// ---------------------------------------------------------------------------
bool Frontend::modelView(ModelView& out) const {
    // The helicopter selection's preview of an unlocked helicopter, once the menu is open
    // (as2/frontend.md 3.18 step 2).
    if (!sequel() || menus_.empty() || topScreen() != Screen::HeliSelect) return false;
    const Menu* m = menus_.top();
    if (!m || m->open < 1.0f || !rules().heliObjects) return false;
    const int h = std::clamp(heli_[std::clamp(sq_->heliShown, 0, 1)], 0, rules().helicopterCount - 1);
    if (SequelScreens::heliLocked(*this, h)) return false;
    out = ModelView{};
    out.object = rules().heliObjects[h];
    out.viewport = {170, 160, 460, 270};
    out.fovY = 60.0f;
    // GUESS (as2/frontend.md 10.1 item 8): the two values the view stores are the planes.
    out.nearPlane = 1.0f;
    out.farPlane = 1000.0f;
    out.origin[0] = 0;
    out.origin[1] = 0;
    out.origin[2] = -100.0f;
    out.angles[0] = 100.0f + 5.0f * std::sin(2.0f * menus_.menuTime());
    out.angles[1] = sq_->heliSpin - 120.0f;
    out.angles[2] = 0;
    return true;
}

bool Frontend::modelViewVisible() const {
    ModelView v;
    return modelView(v);
}

void Frontend::drawModelViews() {
    ModelView v;
    if (modelView(v)) host_.drawModel(v);
}

bool Frontend::debugSet(std::string_view key, std::string_view value) {
    const std::string v(value);
    const double num = std::atof(v.c_str());
    const int n = static_cast<int>(num);
    if (sequel()) {
        SequelState& s = *sq_;
        if (key == "dialogue") {
            SequelScreens::openDialogue(*this, campaign_.mission, v == "end");
            return true;
        }
        if (key == "dpage") {
            s.page = static_cast<size_t>(std::max(n, 0));
            if (!s.pages.empty()) s.page = std::min(s.page, s.pages.size() - 1);
            s.fade = 1.0f;
            s.typed = 0;
            return true;
        }
        if (key == "typed") { s.fade = 1.0f; s.typed = n; return true; }
        if (key == "comic") {
            intro_ = std::make_unique<IntroRun>();
            IntroPage p;
            p.divoGames = false;
            p.comic = std::clamp(n, 1, 4);
            intro_->pages.push_back(p);
            intro_->clock = 0;
            state_ = FrontendState::Intro;
            menus_.clear();
            return true;
        }
        if (key == "t") { if (intro_) intro_->clock = static_cast<float>(num); return true; }
        if (key == "accept") { s.heliAccept = n != 0; return true; }
        if (key == "checkpoint") {
            profile_.progress.checkpoint = CampaignCheckpoint{};
            profile_.progress.checkpoint.mission = n - 1;
            return true;
        }
        if (key == "shown") { s.heliShown = std::clamp(n, 0, 1); return true; }
    }
    if (key == "mt") {
        // Advance the menu time (and whatever it drives) in small steps.
        for (double t = 0; t < num; t += 0.05) update(0.05f, {});
        return true;
    }
    if (key == "players") { twoPlayers_ = n >= 2; campaign_.players = twoPlayers_ ? 2 : 1; return true; }
    if (key == "mission") {
        campaign_.mission = std::clamp(n - 1, 0, rules().missionCount - 1);
        if (plain()) loadingState().mission = missionLabel(campaign_.mission);
        loadingState().missionIndex = campaign_.mission;
        return true;
    }
    if (key == "unlock") {
        for (int i = 0; i < rules().helicopterCount; i++) profile_.progress.helicopterUnlocked[i] = true;
        for (int i = 0; i < rules().missionCount; i++) profile_.progress.missionUnlocked[i] = true;
        refreshLocks();
        return true;
    }
    if (key == "heli") { heli_[0] = std::clamp(n, 0, rules().helicopterCount - 1); return true; }
    if (key == "heli2") { heli_[1] = std::clamp(n, 0, rules().helicopterCount - 1); return true; }
    if (key == "page") { infoPage_ = std::clamp(n - 1, 0, 9); return true; }
    if (key == "capture") { captureRow_ = std::clamp(n, 0, kActionCount - 1); return true; }
    if (key == "hint") { hintText_ = v; return true; }
    if (key == "kills") { report_.players[0].kills = n; return true; }
    if (key == "enemies") { report_.totals.enemyTotal = n; return true; }
    if (key == "stars") { report_.players[0].stars = num; return true; }
    if (key == "startotal") { report_.totals.starTotal = n; return true; }
    if (key == "score") { report_.players[0].score = num; return true; }
    if (key == "maxscore") { report_.totals.maxScore = num; return true; }
    if (key == "cheat") { report_.cheatUsed = n != 0; return true; }
    if (key == "ingame") { optionsInGame_ = n != 0; state_ = n ? FrontendState::Playing : FrontendState::Attract; return true; }
    if (key == "name") {
        if (Menu* m = menus_.top())
            for (MenuItem& it : m->items)
                if (it.type == ItemType::Edit) { it.text = v; it.cursor = static_cast<int>(v.size()); }
        return true;
    }
    return false;
}

} // namespace as3d::ui
