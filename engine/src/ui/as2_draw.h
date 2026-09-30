// Drawing of the sequels' front end (docs/spec/as2/frontend.md 2.5, 2.9, 3.1): the colours,
// the pieces of gfx\ui\interface.tga, the panel with its static-noise opening, the title logo,
// the text button, and the widgets of a MenuSystem in the sequels' style. Internal to
// engine/src/ui.
#pragma once

#include <string_view>

#include "as3d/hud_layout.h"
#include "as3d/menu.h"
#include "as3d/ui.h"

namespace as3d::ui {

// Draws one item of a menu in the sequels' style (called by MenuSystem::drawItems).
void drawSequelItem(MenuDrawContext& c, MenuItem& it, bool focused);

namespace as2 {

// Colours of as2/frontend.md (conventions).
inline Color green() { return packed(0xFF00B000u); }
inline Color disabledGrey() { return grey(0x40 / 255.0f); }
inline Color listGrey() { return grey(0x80 / 255.0f); }
inline Color darkGreenBox() { return packed(0x80006000u); }
inline Color greenOutline() { return packed(0x5000FF00u); }
inline Color red() { return packed(0xFF0000FFu); }

// Atlas pieces (as2/frontend.md 3.1, texel rectangles of interface.tga).
struct Piece {
    float x, y, w, h;
};
constexpr Piece kButtonLeft{18, 79, 23, 30}, kButtonBody{41, 79, 40, 30}, kButtonRight{80, 79, 23, 30};
constexpr Piece kRivetLow{0, 230, 26, 15}, kRivetHigh{0, 215, 26, 15};
constexpr Piece kSliderBar{107, 89, 128, 11}, kSliderKnob{239, 87, 6, 15};
constexpr Piece kArrowLeft{110, 113, 16, 40}, kArrowRight{130, 113, 16, 40};
constexpr Piece kScrollUp{153, 113, 15, 15}, kScrollThumb{153, 130, 15, 6}, kScrollTrack{153, 138, 15, 15},
                kScrollDown{153, 154, 15, 15};

// gfx\ui\interface.tga and gfx\ui\snow.tga (repeat wrapping), or null without data.
const Texture2D* interfaceAtlas(const UiAssets& a);
const Texture2D* snow(const UiAssets& a);
// Spec UVs of a piece of a texture.
SpecUv pieceUv(const Texture2D& t, const Piece& p);

// A piece of the atlas at its texel size (R_Add2DFrame), `w` wide at most `p.w` (cut).
void piece(Renderer2D& r, const Texture2D& t, float x, float y, const Piece& p, Color c = Color{},
           Blend b = Blend::Alpha, float w = -1, float h = -1);
// A piece repeated along x over `len`, the last one cut.
void pieceRunX(Renderer2D& r, const Texture2D& t, float x, float y, float len, const Piece& p, Color c = Color{});
// A whole picture at its natural size with the UVs inset half a texel (R_Add2DPic).
void pic(Renderer2D& r, const Texture2D& t, float x, float y, Color c, Blend b);
// The same stretched over (x, y, w, h).
void picStretched(Renderer2D& r, const Texture2D& t, float x, float y, float w, float h, Color c, Blend b);

// UI_DrawPanel(x, y, w, h, f) titled `title` ("" = none), as2/frontend.md 3.1.
void panel(Renderer2D& r, const UiAssets& a, float x, float y, float w, float h, float f, std::string_view title);
// UI_DrawMenuHeader at logo clock T (as2/frontend.md 3.1).
void titleLogo(Renderer2D& r, const UiAssets& a, float T);
// A text button's frame, caption and rivets with its left edge at `left` and the frame's top
// at `yDrawn`; W = the caption width part (as2/frontend.md 2.5).
void textButton(Renderer2D& r, const UiAssets& a, float left, float yDrawn, float W, std::string_view caption,
                Color captionColor);

// Additive text (the green text routine), `markup` switches braces to white.
void text(Renderer2D& r, const UiAssets& a, float x, float y, std::string_view s, Color c, Align al = Align::Left,
          bool markup = false);

// The front end's own random source for the panel noise (issue 240 item 2), 0..1.
float noiseRandom();

} // namespace as2
} // namespace as3d::ui
