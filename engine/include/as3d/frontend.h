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
    int comic = 0;           // the sequels: comic page 1..4 appended after the logo pages (as2/frontend.md 3.2)
};

struct LogoImage {
    std::string path;
    float x = 0, y = 0;
    bool invertX = false, invertY = false;
};

struct FrontendContent {
    const GameProfile* game = nullptr;       // nullptr = the first game (AirStrike 3D)
    std::string missionNames[kMaxMissions];  // levels.txt `name` of the missions, table order
    int enableHelic[kMaxMissions];           // levels.txt enableHelic, -1 = none
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
    // Ours (docs/spec/issues/140), in the Options screen's free rows: "Screen" (Wide / 4:3)
    // and in touch mode "Controls" (Right / Left).
    bool screenOption = false;
    bool handOption = false;
    // Ours (docs/spec/issues/163): more than one playable game is present, the main menu offers
    // "Change game" (GameHost::changeGame). Off: the main menu is exactly as before.
    bool changeGame = false;

    // Ours, for the plain front end (docs/spec/as2/issues/260): what the object definitions
    // named by GameRules::heliObjects say about each helicopter, for its list entry.
    struct HeliInfo {
        bool known = false;
        int health = 0;
        bool hasSpeed = false;
        float speed = 0;
    };
    HeliInfo heli[kMaxHelicopters];

    FrontendContent() {
        for (int& e : enableHelic) e = -1;
    }
};

// Reads <Info>, <Intros> and <Logotypes> of the original Settings.xml (frontend.md 3.2, 3.3).
bool parseSettingsXml(std::string_view xml, FrontendContent& out);

// Ours: removes what the 2010 re-release added to the main menu, the GameTonic.com portal's
// name in the copyright line and its logo picture (`logo2*.tga`), and any intro page showing
// that logo. The DivoGames intro page and the rest of the copyright line stay.
void removeRereleaseBranding(FrontendContent& content);

// ---------------------------------------------------------------------------
// Interface to the game
// ---------------------------------------------------------------------------
struct MissionStart {
    int mission = 0;        // 0..missionCount-1
    int difficulty = kDefaultDifficulty;
    int players = 1;
    int helicopter[2] = {1, 0};
    int lives[2] = {kCampaignStartLives, kCampaignStartLives}; // p_lives at level start
    std::int64_t banked[2] = {0, 0}; // for the HUD score: ftol(p_scores) + banked
    bool restart = false;   // same mission again (Restart)
    // GameRules::upgradesCarryToNextMission: "Next" starts the following mission with the
    // upgrades the player had at the end of the last (as2 engine-behaviour.delta.md 8.2); the
    // host applies `upgrades` and `weapon` in place of the mission's loadout. False on a new
    // game and on Restart (the game applies its loadout table) and always for the first game.
    bool carryUpgrades = false;
    int upgrades[2][kMaxWeaponSlots] = {};
    int weapon[2] = {0, 0};
    // The campaign's rank accumulator at the start (Campaign's, profile.h), so that the
    // checkpoint EndLevel writes (MissionReport::checkpointRank, as2 10.3) holds the whole
    // campaign's rank as the original's does. 0 = only this mission's part.
    double rankAccumulator[2] = {0.0, 0.0};
};

// What the game reports at EndLevel and at game over (frontend.md 5.3 to 5.5).
struct MissionReport {
    LevelPlayerResult players[2];
    LevelTotals totals;
    bool cheatUsed = false; // the session's "cheat used" flag
    // Upgrade levels and current weapon at the end (a host that does not report them leaves
    // hasUpgrades false and nothing is carried).
    bool hasUpgrades = false;
    int upgrades[2][kMaxWeaponSlots] = {};
    int weapon[2] = {0, 0};
    // The campaign checkpoint EndLevel wrote (GameRules::campaignCheckpoint, as2
    // engine-behaviour.delta.md 10.3): the mission it resumes (0-based, the one after the
    // completed mission, as MissionStart::mission; the mission count after the last one,
    // which no start matches) and per player the lives, total score
    // (banked + this mission's) and rank accumulator to start it with. Starting that mission
    // again ("Continue") takes them; any other start begins afresh. False for a game without
    // the checkpoint and at game over.
    bool hasCheckpoint = false;
    int checkpointMission = -1;
    int checkpointLives[2] = {0, 0};
    std::int64_t checkpointScore[2] = {0, 0};
    float checkpointRank[2] = {0.0f, 0.0f};
};

// A 3D view of one object definition in a viewport of the virtual screen, for the sequels'
// helicopter selection (as2/frontend.md 3.18): camera at the origin looking down -z with y up
// (the banner's convention), the object and its attachments at `origin` with the entity
// angles `angles` (degrees, fields 14..16), depth cleared inside the viewport first.
struct ModelView {
    std::string object;   // definition name (objects\*.obj)
    RectF viewport;       // virtual 800x600 pixels
    float fovY = 60.0f;
    float nearPlane = 1.0f, farPlane = 1000.0f;
    float origin[3] = {0.0f, 0.0f, -100.0f};
    float angles[3] = {0.0f, 0.0f, 0.0f};
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
    // Ours (docs/spec/issues/163): the main menu's "Change game" (FrontendContent::changeGame);
    // the profile is saved first. The host leaves this game for the game selector.
    virtual void changeGame() {}
    // The sequels. Draws `view` into the current frame (between drawUnder and drawOver, the
    // 2D list flushed: see Frontend::drawModelViews). Hosts that draw nothing ignore it.
    virtual void drawModel(const ModelView& view) {}
    // Starts a music module ("music\\track02.mo3", the intro comic) or, with "", stops the
    // music and the sounds (after the comic).
    virtual void playMusic(const std::string& path) {}
};

// Id and place of the "Change game" entry of the main menus (docs/spec/issues/163). The first
// game's: a text button in the free band between the Exit picture (ends at y 423) and the
// corner rule (y 487). The plain front end's: the left slot of the bottom bar.
constexpr int kChangeGameItem = 60;
constexpr RectF kChangeGameRect{310, 440, 180, 32};

// ---------------------------------------------------------------------------
// Front end
// ---------------------------------------------------------------------------
enum class Screen {
    MainMenu, Exit, StartGame, TopScores, NameEntry, Options, Controls, Information,
    InGame, Hint, GameOver, MissionComplete, GameComplete,
    // The sequels' own (as2/frontend.md 1.2): helicopter selection (S3b), credits (S8b),
    // portrait dialogue (S9b, S13b).
    HeliSelect, Credits, Dialogue,
};
const char* screenName(Screen s);
bool screenFromName(std::string_view name, Screen& out);
// The first game's screens.
constexpr Screen kAllScreens[] = {
    Screen::MainMenu, Screen::Exit, Screen::StartGame, Screen::TopScores, Screen::NameEntry,
    Screen::Options, Screen::Controls, Screen::Information, Screen::InGame, Screen::Hint,
    Screen::GameOver, Screen::MissionComplete, Screen::GameComplete,
};
// The sequels' screens (FrontendStyle::SequelMenus).
constexpr Screen kSequelScreens[] = {
    Screen::MainMenu, Screen::Exit, Screen::StartGame, Screen::HeliSelect, Screen::TopScores,
    Screen::NameEntry, Screen::Options, Screen::Controls, Screen::Information, Screen::Credits,
    Screen::InGame, Screen::Hint, Screen::Dialogue, Screen::GameOver, Screen::MissionComplete,
    Screen::GameComplete,
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
    // Whether Options offers the Screen row (the host: always on Android, on desktop while
    // the window is wider than 4:3). Takes effect when Options is next opened.
    void setScreenOptionShown(bool on) { content_.screenOption = on; }

    // Starts the program: the intro pages when ShowLogo = 1 and Settings.xml lists some, then the
    // attract level and the main menu.
    void boot();

    // One frame. Returns true when the UI took the input (gameplay must ignore it).
    bool update(float dt, const UiInput& input);

    void drawUnder(Renderer2D& r, const UiAssets& a);
    void drawOver(Renderer2D& r, const UiAssets& a);
    void draw(Renderer2D& r, const UiAssets& a) { drawUnder(r, a); drawOver(r, a); }
    bool bannerVisible() const;
    // The sequels' 3D views (the helicopter selection's preview): when true, the host flushes
    // the 2D list after drawUnder, calls drawModelViews (GameHost::drawModel for each view),
    // and starts a new 2D list for drawOver.
    bool modelViewVisible() const;
    void drawModelViews();
    // The view drawModelViews would ask for, false when none (for tests and the viewer).
    bool modelView(ModelView& out) const;

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
    // startotal, maxscore, cheat. The sequels also: dialogue (start or end of `mission`: opens
    // it), comic (intro page 1..4 at `t`), t (seconds into it), accept (helicopter selection
    // opened from Mission Complete), checkpoint (the saved checkpoint mission, 1-based),
    // typed (dialogue: characters typed), dpage (dialogue page). Returns false for an
    // unknown key.
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
    // The plain front end of a PlainList game (engine/src/ui/screens_plain.cpp). Screens not
    // listed (top scores, name entry, options, controls) are the same builders with plain
    // widgets, see plain().
    Menu buildPlainMain();
    Menu buildPlainExit();
    Menu buildPlainStartGame();
    Menu buildPlainInGame();
    Menu buildPlainHint();
    Menu buildPlainGameOver();
    Menu buildPlainMissionComplete();
    Menu buildPlainGameComplete();
    // Rows of the helicopter list (id kPlainHeliBase + n) with the number, health and speed
    // of each helicopter; a click or Enter on an unlocked row chooses it for player 1.
    void addPlainHeliRows(Menu& m, float x, float y, float w, float rowH);
    std::string missionLabel(int mission) const;

    // Flow helpers (frontend.cpp).
    void showMainMenu();               // replaces the stack
    void quitToMainMenu(bool bank, bool highScoreCheck);
    void startCampaign(int mission, int difficulty, int players);
    void startLevel(bool restart, bool carryUpgrades = false); // G_BeginLevel on campaign_.mission
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
public:
    // True for a game whose front end is FrontendStyle::PlainList: the plain screens, drawn
    // with our own rectangles and text and none of the first game's menu pictures.
    bool plain() const { return content_.game && content_.game->frontend == FrontendStyle::PlainList; }
    // True for a FrontendStyle::SequelMenus game: the sequels' own menus, comics and
    // dialogues (docs/spec/as2/frontend.md; engine/src/ui/screens_as2_*.cpp, as2_*.cpp).
    bool sequel() const { return content_.game && content_.game->frontend == FrontendStyle::SequelMenus; }

private:
    friend struct SequelScreens; // the sequels' screens and flow (engine/src/ui/as2_screens.h)
    struct SequelState;
    std::unique_ptr<SequelState> sq_;

    const GameRules& rules() const { return content_.game ? content_.game->rules : defaultGameRules(); }

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
    bool heliLocked_[kMaxHelicopters] = {};
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
    int carriedUpgrades_[2][kMaxWeaponSlots] = {};
    int carriedWeapon_[2] = {0, 0};
    bool haveCarried_ = false;
    float playPointerX_ = 0, playPointerY_ = 0;
};

// Loading screen (S9, frontend.md 3.16): black, menu\loading.tga unless an intermission level
// loads, and a progress line. The host calls it at each loading step.
void drawLoadingScreen(Renderer2D& r, const UiAssets& a, float progress, bool intermission);

// ---------------------------------------------------------------------------
// The game selector (ours, docs/spec/issues/163): one card per game whose data is present, in
// the style of the plain front end (black bars, rules, text buttons in the game font, our own
// rectangles). It must work with any single game's data, so it asks the asset cache for no
// texture: the font comes from the UiAssets given to draw() (loaded from any present game), and
// a card shows its game's own logo when the window loaded one (GameCard::logo), else the title
// in large text. Keyboard (Left / Right / Tab choose, Enter plays, Esc exits), mouse and touch
// (a click or tap on a card plays it) through the menu system.
// ---------------------------------------------------------------------------
struct GameCard {
    std::string key;                     // the game's key
    std::string title, version;          // "AirStrike 2", "2.51"
    std::vector<std::string> saveLines;  // the player's save of that game (as3d/launcher.h)
    const Texture2D* logo = nullptr;     // the game's own title picture, or null
};

class GameSelector {
public:
    GameSelector(std::vector<GameCard> cards, int preselected);
    GameSelector(const GameSelector&) = delete;
    GameSelector& operator=(const GameSelector&) = delete;

    void setTouchMode(bool on);
    // The part of the virtual 800x600 screen clear of display cutouts (Mapping::toVirt of the
    // safe insets); cards and buttons stay inside it. Rebuilds the layout when it changes.
    void setSafeArea(float left, float top, float right, float bottom);

    void update(float dt, const UiInput& input);
    // The card played (a click, a tap, Enter or the Play button), -1 until then.
    int chosen() const { return chosen_; }
    bool exitRequested() const { return exit_; }
    // The card that Play would start: the focused card, or the last one focused.
    int current() const { return current_; }
    void draw(Renderer2D& r, const UiAssets& fontAssets);

    const std::vector<GameCard>& cards() const { return cards_; }
    RectF cardRect(int index) const;
    RectF playRect() const { return play_; }
    RectF exitRect() const { return exitButton_; }
    MenuSystem& menus() { return menus_; }

private:
    void build();

    std::vector<GameCard> cards_;
    MenuSystem menus_;
    std::vector<RectF> rects_;
    RectF play_, exitButton_;
    float safeL_ = 0, safeT_ = 0, safeR_ = 800, safeB_ = 600;
    int current_ = 0;
    int chosen_ = -1;
    bool exit_ = false;
};

} // namespace as3d::ui
