// The game selector screen with what it needs from the data (docs/spec/issues/163): the font
// (gfx\ui\font.tga and font_alpha.tga, which every game of the family ships) from the first
// game listed, and each game's own title picture when its data has an obvious one
// (gameLogoPath), else the title in text; the save summary of each game (as3d/launcher.h). The
// layout and input are ui::GameSelector's. Used by the window (game_loop.cpp), by
// `as3d_game --headless --selector-shot` and by apps/tests/launcher_test.cpp.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "as3d/frontend.h"
#include "as3d/input.h"
#include "as3d/launcher.h"
#include "as3d/vfs.h"
#include "game_session.h"

namespace as3d_game {

// One game the selector lists.
struct LauncherEntry {
    const as3d::GameProfile* game = nullptr;
    GameOptions files;           // where its data is (mounted briefly for the font and the logo)
    std::string savePath;        // <user data>/<key>/profile.bin ("" = no save shown)
    std::string legacySavePath;  // the first game's save from before issue 160 ("" = none)
};

// The title picture of a game's own data ("" = the game has none): AirStrike 2 and Gulf
// Thunder show theirs on the intro screen; AirStrike 3D's is drawn in text.
const char* gameLogoPath(as3d::GameId id);

class LauncherScreen {
public:
    LauncherScreen() = default;
    LauncherScreen(const LauncherScreen&) = delete;
    LauncherScreen& operator=(const LauncherScreen&) = delete;

    // Needs a current GLES 3.0 context. False when no listed game provides a font.
    bool init(const std::vector<LauncherEntry>& games, int preselected, bool touch, std::string* error);
    void setTouchMode(bool on);
    // The display cutouts (framebuffer pixels) for a framebuffer of this size.
    void setScreen(int fbWidth, int fbHeight, const as3d::SafeInsets& insets);
    void update(float dt, const as3d::ui::UiInput& input);
    // Into the bound framebuffer (begins and flushes `r`).
    void draw(as3d::ui::Renderer2D& r, int fbWidth, int fbHeight);

    // The game played, once one is (nullptr before).
    const as3d::GameProfile* chosen() const;
    bool exitRequested() const { return sel_ && sel_->exitRequested(); }
    int current() const { return sel_ ? sel_->current() : 0; }
    as3d::ui::GameSelector& selector() { return *sel_; }
    const std::vector<LauncherEntry>& games() const { return games_; }
    // "as3d@x,y,w,h as2@..." and the buttons, virtual 800x600 (the AS3D_SELECTOR marker).
    std::string layoutMarker() const;
    const std::string& fontGame() const { return fontGame_; }

private:
    std::vector<LauncherEntry> games_;
    as3d::ui::UiAssets font_;           // the font only; asks the data for nothing else
    std::vector<std::unique_ptr<as3d::Texture2D>> logos_;
    std::unique_ptr<as3d::ui::GameSelector> sel_;
    std::string fontGame_;
};

} // namespace as3d_game
