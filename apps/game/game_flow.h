// The game behind the front end (docs/spec/frontend.md 1 and 5): implements the front end's
// GameHost on a GameSession, an AudioBridge and (when something is drawn) a GameView, and
// drives one frame of both. Used by the windowed loop (game_loop.cpp, desktop and Android)
// and by headless runs (main.cpp --ui-script, apps/tests/game_flow_test.cpp).
//
// Per display frame the owner calls uiFrame() with the frame's UI events (menu time runs on
// the wall clock), then step() once per fixed simulation step with the gameplay input, then
// draw(). The front end reaches the world only through pause, "clear p_action", the hint
// box's OK and the camera setting, so it never changes a running simulation otherwise.
//
// Choices where the specs are silent are in docs/spec/issues/130.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "as3d/frontend.h"
#include "as3d/input.h"
#include "as3d/profile.h"
#include "audio_bridge.h"
#include "game_session.h"
#include "game_view.h"

namespace as3d_game {

struct FlowConfig {
    bool touch = false;               // front end in touch mode (Android)
    bool twoPlayerMode = true;        // Start Game offers two players (desktop)
    bool mouseControlOption = true;   // Options offers Mouse Control (desktop)
    bool touchMenuButton = true;      // the front end's own MENU button in touch mode
    std::string profilePath;          // where the profile is loaded from and saved; "" = never saved
    std::string settingsXml;          // Settings.xml of the install (readPlatformFile path); "" = none
    std::string textsPath;            // texts imported from the exe (tools/extract_exe_texts.py); "" = none
    int attract = 0;                  // attract level 1..4 (intro1..intro4); 0 = random at boot
    bool showLogo = true;             // intro pages (also needs ShowLogo = 1 in the settings)
};

// Level-start bits the loop reacts to (renderer and audio are handled inside).
enum FlowEvent : int { kFlowLevelLoaded = 1 };

class GameFlow : public as3d::ui::GameHost {
public:
    GameFlow(GameSession& session, AudioBridge& audio);
    ~GameFlow() override;

    // After session.init (the data is mounted). Loads the profile and the front-end content
    // (mission names, Settings.xml, texts). Does not boot yet.
    bool init(const FlowConfig& config, std::string* error);
    // Intro pages or the attract level with the main menu.
    void boot();

    // Optional pieces. The view is needed for drawing; the mapper gets the key bindings of
    // the settings.
    void setView(GameView* view) { view_ = view; }
    void setInputMapper(as3d::InputMapper* keys);
    // Called while a level loads (progress 0..1; `intermission`: attract level) and once it
    // is loaded (after the view's beginLevel), for the window to present the loading screen
    // and warm the renderer up.
    std::function<void(float progress, bool intermission)> loadingHook;
    std::function<void()> levelLoadedHook;

    // One UI frame: menu time advances by dt. Returns true when the UI took the input.
    bool uiFrame(float dt, const as3d::ui::UiInput& input);
    // Whether the world is simulated this frame (a level is loaded).
    bool worldRunning() const { return session_.hasLevel() && fe_ && fe_->state() != as3d::ui::FrontendState::Intro; }
    // One fixed simulation step. Gameplay input is used only while playing with no menu;
    // `confirm` closes an open hint box (the bot's OK); the pause edge is ignored (the front
    // end owns P). Returns GameSession::StepEvent bits.
    int step(const as3d::FrameInput& input);
    // World, HUD, front end, brightness into the bound framebuffer (needs setView).
    void draw(int width, int height);

    // The app goes to the background: the in-game menu opens during play (so the game comes
    // back paused), and the profile is saved.
    void onBackground();
    // Android Back: the front end's "back" (Esc); on the main menu, the exit confirmation.
    void back();
    // Saves the profile now (window closed, app terminating).
    void saveNow();

    bool quitRequested() const { return quit_; }
    // True while the gameplay controls are live (touch controls drawn, keys reach the world).
    bool playing() const;
    bool mouseControl() const { return profile_.settings.mouseControl; }
    as3d::ui::Frontend& frontend() { return *fe_; }
    as3d::Profile& profile() { return profile_; }
    const FlowConfig& config() const { return config_; }
    int attractLevel() const { return attract_; }
    // Loads so far (missions and attract levels), for tests.
    int levelLoads() const { return levelLoads_; }

    // GameHost.
    void startMission(const as3d::ui::MissionStart& start) override;
    void loadAttract() override;
    void setPaused(bool paused) override;
    void clearPlayerActions() override;
    void settingsChanged(const as3d::Settings& settings) override;
    void saveProfile(const as3d::Profile& profile) override;
    void quit() override { quit_ = true; }
    void gameOverMusic() override { audio_.gameOverMusic(); }

private:
    void levelLoaded(bool intermission);
    void applySettings(const as3d::Settings& s);
    as3d::ui::MissionReport report() const;
    void playUiSounds();

    GameSession& session_;
    AudioBridge& audio_;
    GameView* view_ = nullptr;
    as3d::InputMapper* keys_ = nullptr;
    FlowConfig config_;
    as3d::Profile profile_;
    std::unique_ptr<as3d::ui::Frontend> fe_;
    int attract_ = 1;
    bool quit_ = false;
    int levelLoads_ = 0;
    float pointerX_ = 400, pointerY_ = 300;
};

// The player's helicopter centre in the virtual 800x600 screen (y down), from its collision
// rectangle of the last step. False when there is no visible player helicopter.
bool playerScreenCentre(const as3d::World& w, int player, float& vx, float& vy);

// Profile path of the platform: <userDataDir()>/profile.bin, "" if there is none.
std::string defaultProfilePath();

// Fresh-profile key bindings of player 2 on a desktop keyboard (issue 130): the spec's
// built-in defaults bind both players to the same keys (the original expected a joystick for
// player 2), so player 2 gets W/A/S/D and the keys around them, keeping the joystick codes.
void applyDesktopPlayer2Keys(as3d::Settings& s);

// The settings' bindings (the original's codes) into the mapper's (SDL codes); the pause and
// hint-confirm bindings are cleared, the front end handles P, Enter and Esc.
void applyBindings(const as3d::Settings& s, as3d::InputMapper& keys);

} // namespace as3d_game
