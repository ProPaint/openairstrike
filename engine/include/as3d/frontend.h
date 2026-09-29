// Front end: the screen state machine of the original (intro pages, attract level with the main
// menu and its sub-menus, play with the in-game menu, pause, hint box, game over, mission and
// game complete) on top of the menu system. Spec: docs/spec/frontend.md 1 and 3 (authority),
// progression rules in as3d/profile.h.
//
// The front end never touches the game world. It talks to the game through GameHost (plain
// structs and callbacks, implemented by the game loop) and receives the game's events through
// Frontend::onEndLevel / onGameOver / showTutorialHint. A fake host for tests lives in
// apps/tests/frontend_test.cpp.
//
// Per frame, the game loop does:
//     bool uiTookInput = frontend.update(dt, input);   // gameplay ignores the input if true
//     render the level (the attract level or the mission; frozen while frontend.paused())
//     if (frontend.hudVisible()) drawHud(r2d, assets, hud);
//     frontend.drawUnder(r2d, assets);                   // menu frames, intro pages
//     if (frontend.bannerVisible()) { flush 2D; host draws the 3D banner; begin 2D again }
//     frontend.drawOver(r2d, assets);                    // items, tooltip, cursor
//     for (auto& s : frontend.takeSounds()) play s (2D)
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "as3d/menu.h"
#include "as3d/profile.h"
#include "as3d/ui.h"

namespace as3d::ui {

// ---------------------------------------------------------------------------
// Texts compiled into the original executable (issue 080). Loaded from the file written by
// tools/extract_exe_texts.py (format documented there); short generic labels have built-in
// English defaults, so everything works without the file except the Information pages and the
// Game Complete lines, which then say that the texts are not installed.
// ---------------------------------------------------------------------------
class Texts {
public:
    // Parses `key = "value"` lines; returns the number of entries read. Bad lines are skipped.
    int parse(std::string_view content);
    // The loaded value, else the built-in default, else "".
    std::string get(std::string_view key) const;
    bool loaded(std::string_view key) const { return values_.count(std::string(key)) != 0; }
    // True once the Information pages are present.
    bool installed() const { return loaded("info.1.title"); }
    void set(std::string key, std::string value) { values_[std::move(key)] = std::move(value); }

private:
    std::map<std::string, std::string> values_;
};

// ---------------------------------------------------------------------------
// Content the host supplies from the data files
// ---------------------------------------------------------------------------
struct IntroPage {
    bool divoGames = true;   // the built-in DivoGames page, else an image page
    std::string image;       // game path
    Color back{0, 0, 0, 1};  // BackColor
};

struct LogoImage {
    std::string path;
    float x = 0, y = 0;
    bool invertX = false, invertY = false;
};

struct FrontendContent {
    std::string missionNames[kMissionCount]; // levels.txt `name` of the 20 missions, table order
    int enableHelic[kMissionCount];          // levels.txt enableHelic, -1 = none
    std::string version, copyright;          // Settings.xml <Info>
    std::vector<IntroPage> intros;           // Settings.xml <Intros>
    std::vector<LogoImage> logos;            // Settings.xml <Logotypes>
    bool videoOptions = true;                // desktop: resolution, refresh, depth, fullscreen, 3D sound
    int videoModeCount = 9;                  // leading modes of kVideoModeNames the display supports
    std::vector<int> refreshRates;           // extra "N Hz" values of the refresh spinner
    // What the host supports (docs/spec/issues/130): the Start Game "Game mode" spinner (two
    // players), the Options "Mouse Control" spinner, and in touch mode the MENU button during
    // play (false when the host has its own pause control that opens the in-game menu).
    bool twoPlayerMode = true;
    bool mouseControlOption = true;
    bool touchMenuButton = true;

    FrontendContent() {
        for (int& e : enableHelic) e = -1;
    }
};

// Reads <Info>, <Intros> and <Logotypes> of the original Settings.xml (frontend.md 3.2, 3.3).
bool parseSettingsXml(std::string_view xml, FrontendContent& out);

// ---------------------------------------------------------------------------
// Interface to the game
// ---------------------------------------------------------------------------
struct MissionStart {
    int mission = 0;        // 0..19
    int difficulty = kDefaultDifficulty;
    int players = 1;
    int helicopter[2] = {1, 0};
    int lives[2] = {kCampaignStartLives, kCampaignStartLives}; // p_lives at level start
    std::int64_t banked[2] = {0, 0}; // for the HUD score: ftol(p_scores) + banked
    bool restart = false;   // same mission again (Restart)
};

// What the game reports at EndLevel and at game over (frontend.md 5.3 to 5.5).
struct MissionReport {
    LevelPlayerResult players[2];
    LevelTotals totals;
    bool cheatUsed = false; // the session's "cheat used" flag
};

class GameHost {
public:
    virtual ~GameHost() = default;
    // Frees the current level, shows the loading screen while the mission loads, then starts
    // play (unpaused, HUD shown) with the level-start resets of frontend.md 5.2.
    virtual void startMission(const MissionStart& start) = 0;
    // Frees the current level and loads the attract level chosen at boot (no players, no HUD).
    virtual void loadAttract() = 0;
    // Freezes or runs the level (frontend.md 1.1); the level is rendered either way.
    virtual void setPaused(bool paused) = 0;
    // p_action = 0 for both players.
    virtual void clearPlayerActions() = 0;
    // Live settings changed (volumes, brightness, camera, mouse control, bindings).
    virtual void settingsChanged(const Settings& settings) = 0;
    // Options -> Apply on desktop: recreate the window with the new video settings.
    virtual void applyVideoSettings(const Settings& settings) {}
    // Writes the profile to the platform's user-data path (atomically, see profile.h).
    virtual void saveProfile(const Profile& profile) = 0;
    virtual void quit() = 0;
    // Game over: the current music module jumps to pattern order 35.
    virtual void gameOverMusic() {}
    // Draws the main menu's 3D banner (objects\banner.obj) in the top 200 virtual pixels,
    // between drawUnder and drawOver (frontend.md 3.3 step 5). `mt` is the menu time.
    virtual void drawBanner(float mt) {}
};

// ---------------------------------------------------------------------------
// Front end
// ---------------------------------------------------------------------------
enum class Screen {
    MainMenu, Exit, StartGame, TopScores, NameEntry, Options, Controls, Information,
    InGame, Hint, GameOver, MissionComplete, GameComplete,
};
const char* screenName(Screen s);
bool screenFromName(std::string_view name, Screen& out);
constexpr Screen kAllScreens[] = {
    Screen::MainMenu, Screen::Exit, Screen::StartGame, Screen::TopScores, Screen::NameEntry,
    Screen::Options, Screen::Controls, Screen::Information, Screen::InGame, Screen::Hint,
    Screen::GameOver, Screen::MissionComplete, Screen::GameComplete,
};

enum class FrontendState { Boot, Intro, Attract, Playing };

class Frontend {
public:
    Frontend(GameHost& host, Profile& profile, FrontendContent content, Texts texts);
    ~Frontend();
    Frontend(const Frontend&) = delete;
    Frontend& operator=(const Frontend&) = delete;

    // Touch mode (frontend.md 7): on-screen equivalents for keys, no drawn cursor. See
    // docs/spec/issues/090-touch-mode-additions.md.
    void setTouchMode(bool on);
    bool touchMode() const { return touch_; }

    // Starts the program: the intro pages when ShowLogo = 1 and Settings.xml lists some, then the
    // attract level and the main menu.
    void boot();

    // One frame. Returns true when the UI took the input (gameplay must ignore it).
    bool update(float dt, const UiInput& input);

    void drawUnder(Renderer2D& r, const UiAssets& a);
    void drawOver(Renderer2D& r, const UiAssets& a);
    void draw(Renderer2D& r, const UiAssets& a) { drawUnder(r, a); drawOver(r, a); }
    bool bannerVisible() const;

    // Game -> front end.
    void onEndLevel(const MissionReport& report); // EndLevel(): unlocks, then S15 or S16
    void onGameOver(const MissionReport& report); // all lives lost: S14
    void showTutorialHint(std::string text);      // ShowTutorialHint: S12

    // Queries for the game loop.
    FrontendState state() const { return state_; }
    bool paused() const { return paused_; }
    bool hudVisible() const { return state_ == FrontendState::Playing && !hudHidden_; }
    bool menuOpen() const { return !menus_.empty(); }
    bool wantsTextInput() const; // desktop: an edit field is focused (start text input); touch mode draws its own keyboard
    // Brightness the game should use: forced to 0.5 during the intro pages, else the setting.
    float brightness() const;
    std::vector<std::string> takeSounds() { return menus_.takeSounds(); }
    const Campaign& campaign() const { return campaign_; }
    const int* helicopters() const { return heli_; }
    bool twoPlayers() const { return twoPlayers_; }

    // For tests and the viewer.
    MenuSystem& menus() { return menus_; }
    Profile& profile() { return profile_; }
    Texts& texts() { return texts_; }
    // Builds a screen and pushes it (the main menu replaces the stack).
    void open(Screen s);
    Screen topScreen() const; // meaningful only while a menu is open
    // Sets the state the viewer needs to show a screen in context (`--state k=v`): mt, players,
    // mission, unlock (all), page, capture (row), name, hint, kills, stars, score, enemies,
    // startotal, maxscore, cheat. Returns false for an unknown key.
    bool debugSet(std::string_view key, std::string_view value);
    void setState(FrontendState s) { state_ = s; }

private:
    struct IntroRun;

    // Screen builders (engine/src/ui/screens_*.cpp).
    Menu buildMainMenu();
    Menu buildExit();
    Menu buildStartGame();
    Menu buildTopScores();
    Menu buildNameEntry();
    Menu buildOptions();
    Menu buildControls();
    Menu buildInformation();
    Menu buildInGame();
    Menu buildHint();
    Menu buildGameOver();
    Menu buildMissionComplete();
    Menu buildGameComplete();

    // Flow helpers (frontend.cpp).
    void showMainMenu();               // replaces the stack
    void quitToMainMenu(bool bank, bool highScoreCheck);
    void startCampaign(int mission, int difficulty, int players);
    void startLevel(bool restart);     // G_BeginLevel on campaign_.mission
    void continueCampaign();
    void highScoreCheck();
    void setPausedFlag(bool on);
    void resumePlay();                 // in-game menu Resume / hint close
    void settingsChanged();
    void save();
    void drawIntro(Renderer2D& r, const UiAssets& a);
    void drawTouchPlayButtons(Renderer2D& r, const UiAssets& a);
    bool handlePlayingInput(const UiInput& input);
    void drawStats(MenuDrawContext& c, float boxY);
    void refreshLocks();

    GameHost& host_;
    Profile& profile_;
    FrontendContent content_;
    Texts texts_;
    MenuSystem menus_;
    Campaign campaign_;
    MissionReport report_;             // the last EndLevel / game over report
    FrontendState state_ = FrontendState::Boot;
    bool touch_ = false;
    bool paused_ = false;
    bool hudHidden_ = false;
    bool twoPlayers_ = false;
    int heli_[2] = {1, 0};
    int heliAlternator_ = 0;
    bool heliLocked_[kHelicopterCount] = {};
    int difficultyChoice_ = kDefaultDifficulty;
    std::string hintText_;
    std::unique_ptr<IntroRun> intro_;
    // Options: pending values applied only by Apply (video, 3D sound).
    Settings pending_;
    bool optionsInGame_ = false;
    int controlsPlayer_ = 0;
    int captureRow_ = -1;
    int infoPage_ = 0;
    int typedChars_ = 0;               // Game Complete typing sound state
    float playPointerX_ = 0, playPointerY_ = 0;
};

// Loading screen (S9, frontend.md 3.16): black, menu\loading.tga unless an intermission level
// loads, and a progress line. The host calls it at each loading step.
void drawLoadingScreen(Renderer2D& r, const UiAssets& a, float progress, bool intermission);

} // namespace as3d::ui
