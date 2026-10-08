// The touch buttons and drag target drawn with ui::Renderer2D. Neutral code shared by the game
// loop of the desktop, Android and web (apps/game/touch_overlay.h wraps it) and by the 3DS
// bottom screen. Call between Renderer2D::begin() and flush(); the layout is TouchMapper's
// (framebuffer pixels of the Renderer2D's target).
#pragma once

#include "as3d/game_profile.h"
#include "as3d/input.h"
#include "as3d/ui.h"

namespace as3d::ui {

// What the touch buttons show besides their shape (docs/spec/issues/140).
struct TouchControlsState {
    // The HUD atlases and font: the missile and power-up buttons show the selected type's
    // icon and count, next weapon the current weapon. Null: plain drawn symbols.
    const UiAssets* assets = nullptr;
    const HudPlayer* player = nullptr; // player 1's HUD state; null: no icons, no counts
    float alpha = 1.0f;                // overall opacity (as3d::TouchFade)
    // The rules the next-item previews cycle by (as3d/player_select.h): the game's, so that the
    // sequels' 9 weapon slots and power-up skip mask preview what a press selects. Null: the
    // first game's.
    const GameRules* rules = nullptr;
};

void drawTouchControls(Renderer2D& r, const TouchMapper& touch, const TouchControlsState& state = {});

// Fills a convex polygon given in the unit square of the rectangle (x, y, w, h) with horizontal
// strips (about one per two framebuffer pixels).
void fillConvex(Renderer2D& r, float x, float y, float w, float h, const float* uv, int n, Color c);

} // namespace as3d::ui
