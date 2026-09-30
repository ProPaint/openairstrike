// The sequels' front end (FrontendStyle::SequelMenus, docs/spec/as2/frontend.md): the state
// the Frontend keeps for it and the screen builders and flow steps, which are friends of
// Frontend. Screens: screens_as2_main.cpp (main menu, exit, top scores, name entry,
// information, credits), screens_as2_start.cpp (Start Game, helicopter selection),
// screens_as2_options.cpp (options, controls), screens_as2_game.cpp (in-game menu, hint box,
// game over, mission complete, game complete); dialogues as2_dialogue.cpp; the comic pages
// and the loading comic as2_comics.cpp; the flow (new game and "Continue", end of level,
// unlocks) as2_flow.cpp. Internal to engine/src/ui.
#pragma once

#include <string>
#include <vector>

#include "as2_draw.h"
#include "as3d/frontend.h"

namespace as3d::ui {

// The intro pages being shown (frontend.md 3.2; the sequels' comic pages as2 3.2).
struct Frontend::IntroRun {
    std::vector<IntroPage> pages;
    size_t index = 0;
    float clock = -0.5f;
    float speed = 1.0f;
};

struct Frontend::SequelState {
    float logoClock = 0;    // T of the title logo (as2@0x2219184): 0.5 x dt per frame
    float tint[4] = {1, 1, 1, 1}; // Gulf Thunder's emblem colour, moving towards the screen's (gulf 3.1)
    float heliSpin = 0;     // the preview's spin, degrees, never reset (as2@0x221917c)
    // Helicopter selection.
    bool heliAccept = false;     // opened from Mission Complete ("Accept" mode)
    int heliShown = 0;           // the player whose helicopter is shown
    int heliOpened[2] = {0, 0};  // the choices when it opened (issue 240 item 4)
    int missionChoice = 0;       // Start Game's list selection, kept between openings
    // Portrait dialogue (as2/frontend.md 3.19).
    struct Page {
        std::string text;
        int speaker = 0; // 0 officer, 1 pilot
    };
    std::vector<Page> pages;
    int dialogueMission = 0; // 0-based
    bool dialogueEnd = false;
    size_t page = 0;
    float fade = 0;          // opening and closing value
    bool closing = false;
    int typed = 0;           // characters of the page shown
    float charClock = 0;
    float pageTimer = 0;     // seconds since the page was complete
    // Intro comic.
    bool comicMusic = false;
    // Game Complete typing sounds.
    int congratsTyped = 0;
};

// What differs in where the screens put things (as2/frontend.md 3.3, 3.13, 3.14; gulf/
// frontend.delta.md): AirStrike 2's and Gulf Thunder's tables, chosen by the current look.
struct InfoIcon {
    int kind; // 0 weapon, 1 missile, 2 power-up
    int slot;
    float y;
};
struct Layout {
    float mainY[6];    // the main menu's six buttons
    float mainMinW;
    float inGameY[4];  // Resume, Options, Restart, Quit
    float inGameMinW;
    bool introComic;   // the four comic pages follow the logo pages
    int infoPages;     // pages of the Information spinner
    int infoKey[8];    // the texts' page number (info.N.*) of spinner page 0..infoPages-1
    std::vector<InfoIcon> (*infoIcons)(int key); // the icons of a texts page
};
const Layout& layout();

// A menu of the current look: its text buttons' margin and height (gulf/frontend.delta.md 2.2).
inline Menu newMenu() {
    Menu m;
    m.buttonMargin = as2::skin().buttonMargin;
    m.buttonHeight = as2::skin().buttonHeight;
    return m;
}

// Built-in values where the texts file has none (the caption strings of the executable's
// buttons keep their padding spaces, which set the button widths).
struct SequelScreens {
    // Texts.
    static std::string tr(const Frontend& f, const std::string& key);
    // A caption Gulf Thunder has in two forms (its own key), AirStrike 2 in one (`fallback`).
    static std::string trOr(const Frontend& f, const std::string& key, const std::string& fallback);
    static std::string heliName(const Frontend& f, int heli);
    // Dialogue pages of mission `m` (0-based), start or end; empty without the texts file.
    static std::vector<Frontend::SequelState::Page> dialogue(const Frontend& f, int m, bool end);

    // Screens.
    static Menu build(Frontend& f, Screen s);
    static Menu buildScreen(Frontend& f, Screen s);
    static Menu mainMenu(Frontend& f);
    static Menu exit(Frontend& f);
    static Menu topScores(Frontend& f);
    static Menu nameEntry(Frontend& f);
    static Menu information(Frontend& f);
    static Menu credits(Frontend& f);
    static Menu startGame(Frontend& f);
    static Menu heliSelect(Frontend& f);
    static Menu options(Frontend& f);
    static Menu controls(Frontend& f);
    static Menu inGame(Frontend& f);
    static Menu hint(Frontend& f);
    static Menu gameOver(Frontend& f);
    static Menu missionComplete(Frontend& f);
    static Menu gameComplete(Frontend& f);
    static Menu dialogueScreen(Frontend& f);

    // Selects the look (AirStrike 2's or Gulf Thunder's) of the game the front end runs.
    static void applyLook(const Frontend& f);
    // The operation's name under Gulf Thunder's loading comic.
    static std::string loadingName(const Frontend& f);
    // Whether the game has the four comic pages after its logo pages (AirStrike 2: yes, Gulf Thunder: no).
    static bool hasIntroComic(const Frontend& f);

    // Flow (as2/frontend.md 5).
    static void boot(Frontend& f);
    static void tick(Frontend& f, float dt);
    static void newGame(Frontend& f);                 // G_NewGame: the checkpoint, then the level
    static void afterLevelStart(Frontend& f);         // the start dialogue, paused
    static void onEndLevel(Frontend& f, const MissionReport& report);
    static void missionCompleted(Frontend& f);        // G_MissionComplete: unlocks, S15 or S16
    static void openDialogue(Frontend& f, int mission, bool end); // false: none for that slot
    static bool startWithContinue(const Frontend& f, int mission);
    static bool heliLocked(const Frontend& f, int heli);

    // Intro comic pages and loading comic (as2_comics.cpp).
    static float comicDuration(int page);
    static void drawComicPage(Renderer2D& r, const UiAssets& a, int page, float t);
    static void drawTouchSkip(Renderer2D& r, const UiAssets& a);
    static RectF touchSkipRect();
    static RectF touchMenuRect();
    static void drawLoading(Renderer2D& r, const UiAssets& a, float progress, bool intermission, int mission,
                            const std::string& name = std::string());
    // The title logo (AirStrike 2) or the title bar (Gulf Thunder) of the front screens.
    static void header(MenuDrawContext& c, Frontend& f);};

} // namespace as3d::ui
