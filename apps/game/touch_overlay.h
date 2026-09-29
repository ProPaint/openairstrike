// 2D overlays of the game loop, drawn with ui::Renderer2D: the on-screen touch buttons and
// drag target, the pause screen ("tap to continue") and the loading screen. Call between
// Renderer2D::begin() and flush().
#pragma once

#include "as3d/input.h"
#include "as3d/ui.h"

namespace as3d_game {

// What the touch buttons show besides their shape (docs/spec/issues/140).
struct TouchOverlayState {
    // The HUD atlases and font: the missile and power-up buttons show the selected type's
    // icon and count, next weapon the current weapon. Null: plain drawn symbols.
    const as3d::ui::UiAssets* assets = nullptr;
    const as3d::ui::HudPlayer* player = nullptr; // player 1's HUD state; null: no icons, no counts
    float alpha = 1.0f;                          // overall opacity (as3d::TouchFade)
};

void drawTouchControls(as3d::ui::Renderer2D& r, const as3d::TouchMapper& touch, const TouchOverlayState& state = {});
// Dims the screen and draws a large "play" symbol.
void drawPauseOverlay(as3d::ui::Renderer2D& r, bool touch);
// Black screen with a progress bar, `progress` in 0..1.
void drawLoadingScreen(as3d::ui::Renderer2D& r, float progress);

} // namespace as3d_game
