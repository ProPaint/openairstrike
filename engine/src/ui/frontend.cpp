// Front-end state machine (frontend.md 1), intro pages (3.2), loading screen (3.16), pause and
// hotkeys during play, and the flow helpers the screens call.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "as3d/frontend.h"

namespace as3d::ui {

struct Frontend::IntroRun {
    std::vector<IntroPage> pages;
    size_t index = 0;
    float clock = -0.5f;
    float speed = 1.0f;
};

namespace {

const char* kScreenNames[] = {"main", "exit", "start", "scores", "name", "options", "controls", "info",
                              "ingame", "hint", "gameover", "complete", "gamecomplete"};

// Touch mode: the button that stands for Esc during play (docs/spec/issues/090).
constexpr RectF kTouchMenuButton{360, 6, 80, 22};

} // namespace

const char* screenName(Screen s) { return kScreenNames[static_cast<int>(s)]; }

bool screenFromName(std::string_view name, Screen& out) {
    for (Screen s : kAllScreens)
        if (name == screenName(s)) { out = s; return true; }
    return false;
}

Frontend::Frontend(GameHost& host, Profile& profile, FrontendContent content, Texts texts)
    : host_(host), profile_(profile), content_(std::move(content)), texts_(std::move(texts)) {
    menus_.showHints = profile_.settings.showHints;
    menus_.drawCursor = !profile_.settings.useSystemMouse;
    pending_ = profile_.settings;
    refreshLocks();
}

Frontend::~Frontend() = default;

void Frontend::setTouchMode(bool on) {
    touch_ = on;
    menus_.touchMode = on;
}

void Frontend::refreshLocks() {
    for (int i = 0; i < kHelicopterCount; i++) heliLocked_[i] = !profile_.progress.helicopterUnlocked[i];
}

void Frontend::boot() {
    heli_[0] = 1;
    heli_[1] = 0;
    heliAlternator_ = 0;
    paused_ = hudHidden_ = false;
    campaign_ = Campaign{};
    if (profile_.settings.showLogo && !content_.intros.empty()) {
        intro_ = std::make_unique<IntroRun>();
        intro_->pages = content_.intros;
        state_ = FrontendState::Intro;
        menus_.clear();
        return;
    }
    state_ = FrontendState::Attract;
    host_.loadAttract();
    showMainMenu();
}

float Frontend::brightness() const { return state_ == FrontendState::Intro ? 0.5f : profile_.settings.brightness; }

bool Frontend::bannerVisible() const { return !menus_.empty() && topScreen() == Screen::MainMenu; }

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
    switch (s) {
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

void Frontend::startLevel(bool restart) {
    MissionStart ms;
    ms.mission = campaign_.mission;
    ms.difficulty = campaign_.difficulty;
    ms.players = campaign_.players;
    ms.restart = restart;
    for (int i = 0; i < 2; i++) {
        ms.helicopter[i] = heli_[i];
        ms.lives[i] = campaign_.p[i].livesAtStart;
        ms.banked[i] = campaign_.p[i].banked;
    }
    paused_ = false;
    hudHidden_ = false;
    state_ = FrontendState::Playing;
    host_.startMission(ms);
}

void Frontend::continueCampaign() {
    menus_.clear();
    campaign_.bank(report_.players, report_.totals);
    campaign_.mission = campaign_.nextMission();
    startLevel(false);
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
    report_ = report;
    setPausedFlag(true);
    hudHidden_ = true;
    const int mission = std::clamp(campaign_.mission, 0, kMissionCount - 1);
    profile_.progress.unlockAfterMission(mission, content_.enableHelic[mission]);
    refreshLocks();
    save(); // ours: right away, not only at exit (frontend.md 6.1 recommendation)
    typedChars_ = 0;
    open(mission == kMissionCount - 1 ? Screen::GameComplete : Screen::MissionComplete);
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
                if (touch_ && content_.touchMenuButton && kTouchMenuButton.contains(menus_.pointerX(), menus_.pointerY())) {
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
        for (const UiEvent& e : input.events)
            if (e.type == UiEvent::Type::Press) intro_->speed *= 4.0f;
        intro_->clock += dt * intro_->speed;
        const IntroPage& page = intro_->pages[intro_->index];
        if (intro_->clock >= (page.divoGames ? 8.0f : 6.0f)) {
            intro_->index++;
            intro_->clock = -0.5f;
            intro_->speed = 1.0f;
            if (intro_->index >= intro_->pages.size()) {
                intro_.reset();
                state_ = FrontendState::Attract;
                host_.loadAttract();
                showMainMenu();
            }
        }
        return true;
    }
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
    Menu dummy;
    MenuDrawContext c{r, a, dummy, 0, true, 0};
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
    r.fullscreen({0, 0, 0, 1}, Blend::Opaque);
    if (!intermission)
        if (const Texture2D* t = a.texture("menu\\loading.tga")) r.pic(272, 172, *t, Color{}, Blend::Alpha);
    r.rect(10, 595, std::clamp(progress, 0.0f, 1.0f) * 780, 1, {0.314f, 0, 0, 1}, Blend::Opaque);
}

// ---------------------------------------------------------------------------
// Viewer and test hooks
// ---------------------------------------------------------------------------
bool Frontend::debugSet(std::string_view key, std::string_view value) {
    const std::string v(value);
    const double num = std::atof(v.c_str());
    const int n = static_cast<int>(num);
    if (key == "mt") {
        // Advance the menu time (and whatever it drives) in small steps.
        for (double t = 0; t < num; t += 0.05) update(0.05f, {});
        return true;
    }
    if (key == "players") { twoPlayers_ = n >= 2; campaign_.players = twoPlayers_ ? 2 : 1; return true; }
    if (key == "mission") { campaign_.mission = std::clamp(n - 1, 0, kMissionCount - 1); return true; }
    if (key == "unlock") {
        for (bool& b : profile_.progress.helicopterUnlocked) b = true;
        for (bool& b : profile_.progress.missionUnlocked) b = true;
        refreshLocks();
        return true;
    }
    if (key == "heli") { heli_[0] = std::clamp(n, 0, kHelicopterCount - 1); return true; }
    if (key == "heli2") { heli_[1] = std::clamp(n, 0, kHelicopterCount - 1); return true; }
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
