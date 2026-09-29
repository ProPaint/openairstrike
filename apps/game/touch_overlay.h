// 2D overlays of the game loop, drawn with ui::Renderer2D quads and rectangles only: the
// on-screen touch buttons and drag target, the pause screen ("tap to continue") and the
// loading screen. Call between Renderer2D::begin() and flush().
#pragma once

#include "as3d/input.h"
#include "as3d/ui.h"

namespace as3d_game {

void drawTouchControls(as3d::ui::Renderer2D& r, const as3d::TouchMapper& touch);
// Dims the screen and draws a large "play" symbol.
void drawPauseOverlay(as3d::ui::Renderer2D& r, bool touch);
// Black screen with a progress bar, `progress` in 0..1.
void drawLoadingScreen(as3d::ui::Renderer2D& r, float progress);

} // namespace as3d_game
