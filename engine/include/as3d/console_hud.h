// The first game's in-play screens on the 3DS (docs/3ds-ui-design.md "In play on the console
// (round 4, R4.7)"). The bottom screen is the HUD of HudTarget::Bottom320x240 drawn by drawHud
// in whole pixels (hud_layouts.cpp makeV170Bottom: the tiles are the touch controls, see
// TouchLayoutOptions::hudTiles); these add what is not the HUD's own art:
//   - drawConsoleHudExtras: the pad reminder (the owner's key pictures, S labels) and the pressed
//     look of the tile under the finger, on the bottom screen;
//   - drawConsolePlayTop: the top screen in play: the level name for its typewriter's 4 s (T and
//     L, outlined), the cheat message, PAUSED (T, outlined, the world dimmed 45 %).
// The tutorial hint is a front-end screen (Screen::Hint): ConsoleMenuView draws its box on the top
// screen and its OK on the bottom one (as3d/console_menu.h).
// Coordinates are screen pixels; the drawing converts them through the Renderer2D's mapping (the
// 3DS lists' 0.4 mapping or beginPixels alike).
#pragma once

#include <string>

#include "as3d/ui.h"

namespace as3d::ui {

// The pad reminder's lines (the default pad bindings, as consoleKeyNames): B Missile, X Power-up,
// Y Weapon, R Next missile.
struct ConsoleHudKey {
    HintKey key;
    const char* label;
};
const ConsoleHudKey* consoleHudKeys(int* count);

// Over drawHud's console HUD (the same list, after it): the pad reminder in the layout's keysArea
// and, when `pressed` is not null, the pressed look of that tile (bottom-screen pixels).
void drawConsoleHudExtras(Renderer2D& r, const UiAssets& a, const KeyGlyphAtlas* keys, const RectF* pressed);

// The top screen in play.
struct ConsolePlayTop {
    std::string levelName;
    float levelTime = -1.0f;  // the typewriter's clock (HudState::levelTime)
    std::string message;
    float messageAge = -1.0f; // HudState::messageAge
    bool paused = false;      // PAUSED over the dimmed world (the level name is not drawn then)
};
// Whether drawConsolePlayTop would draw anything.
bool consolePlayTopVisible(const ConsolePlayTop& s);
// The level name split as the top screen shows it: "Mission 1: Tutorial" -> "MISSION 1" (L) over
// "TUTORIAL" (T, or L when T is wider than the screen less 8 px); a name without ':' is all title.
struct ConsoleLevelTitle {
    std::string head, title;
    FontSize titleSize = FontSize::T;
};
ConsoleLevelTitle consoleLevelTitle(const FontMetrics& m, const std::string& name);
// Draws it (400 x 240 pixels of the list's mapping); returns false when nothing was drawn.
bool drawConsolePlayTop(Renderer2D& r, const UiAssets& a, const ConsolePlayTop& s);

} // namespace as3d::ui
