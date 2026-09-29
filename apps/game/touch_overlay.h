// 2D overlays of the game loop, drawn with ui::Renderer2D: the on-screen touch buttons and
// drag target, the pause screen ("tap to continue") and the loading screen. Call between
// Renderer2D::begin() and flush().
#pragma once

#include "as3d/game_profile.h"
#include "as3d/input.h"
#include "as3d/ui.h"
#include "fps_counter.h"

namespace as3d_game {

// What the touch buttons show besides their shape (docs/spec/issues/140).
struct TouchOverlayState {
    // The HUD atlases and font: the missile and power-up buttons show the selected type's
    // icon and count, next weapon the current weapon. Null: plain drawn symbols.
    const as3d::ui::UiAssets* assets = nullptr;
    const as3d::ui::HudPlayer* player = nullptr; // player 1's HUD state; null: no icons, no counts
    float alpha = 1.0f;                          // overall opacity (as3d::TouchFade)
    // The rules the next-item previews cycle by (as3d/player_select.h): the game's, so that the
    // sequels' 9 weapon slots and power-up skip mask preview what a press selects. Null: the
    // first game's.
    const as3d::GameRules* rules = nullptr;
};

void drawTouchControls(as3d::ui::Renderer2D& r, const as3d::TouchMapper& touch, const TouchOverlayState& state = {});
// The frame counter (Show FPS): "NN FPS" and, smaller, the worst frame time and the dropped
// simulation steps of the last second, in the game font on a dark backing, in a free top
// corner: the one opposite the touch pause button (inside the insets), beside the pause
// button when that sits at the top centre, else the screen's top right corner (wider than
// 4:3) or the top centre between the HUD's bars. `touch` may be null (no touch controls).
void drawFpsCounter(as3d::ui::Renderer2D& r, const as3d::ui::UiAssets& assets, const FpsCounter& fps,
                    const as3d::TouchLayout* touch, const as3d::SafeInsets& insets);
// Dims the screen and draws a large "play" symbol.
void drawPauseOverlay(as3d::ui::Renderer2D& r, bool touch);
// Black screen with a progress bar, `progress` in 0..1.
void drawLoadingScreen(as3d::ui::Renderer2D& r, float progress);

} // namespace as3d_game
