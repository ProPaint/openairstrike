// Drawing of the sequels' front end (docs/spec/as2/frontend.md 2.5, 2.9, 3.1; Gulf Thunder's
// delta gulf/frontend.delta.md): the colours, the pieces of gfx\ui\interface.tga (Gulf Thunder:
// interface_gulf.tga), the panel with its static-noise opening, the title logo or title bar, the
// text button, and the widgets of a MenuSystem in the sequels' style. Internal to engine/src/ui.
#pragma once

#include <string_view>

#include "as3d/hud_layout.h"
#include "as3d/menu.h"
#include "as3d/ui.h"

namespace as3d::ui {

// Draws one item of a menu in the sequels' style (called by MenuSystem::drawItems).
void drawSequelItem(MenuDrawContext& c, MenuItem& it, bool focused);

namespace as2 {

// Atlas pieces (as2/frontend.md 3.1, texel rectangles of interface.tga; gulf/frontend.delta.md
// 3.1 for interface_gulf.tga).
struct Piece {
    float x, y, w, h;
};

// The front end's per-game look: what AirStrike 2 and Gulf Thunder do differently in the shared
// screens (gulf/frontend.delta.md): colours, atlas pieces, the text button. The panel and the
// title bar differ in structure and have a branch each (panel, titleLogo / gulfTitleBar); the
// positions of the screens that differ are tables in screens_as2_*.cpp keyed by `Skin::gulf`.
struct Skin {
    bool gulf;
    const char* atlas; // the atlas's game path
    // Colours, packed 0xAABBGGRR.
    u32 ink;          // text
    u32 accent;       // focus, titles, statistic lines
    u32 disabled;     // a disabled item
    u32 listDisabled; // the version line and the hints
    u32 rowDisabled;  // a disabled list row
    // The list's selected-row bar and its scroll boxes' tint.
    float selBar[4];
    u32 scrollTint, scrollHover;
    // The text button (as2/frontend.md 2.5; gulf/frontend.delta.md 2.5).
    float buttonMargin, buttonHeight;
    float captionDx, captionDy; // caption centre: left + captionDx + W / 2, top + captionDy
    bool rivets;
    // Pieces.
    Piece buttonLeft, buttonBody, buttonRight;
    Piece sliderBar, sliderKnob, scrollUp, scrollThumb, scrollTrack, scrollDown;
    float sliderKnobDx; // knob x = slider x + sliderKnobDx + position
    Piece arrowLeft, arrowRight;
};

// The current look: set by the front end whose screens are being built and drawn (AirStrike 2's
// until a Gulf Thunder front end says otherwise).
const Skin& skin();
void setGulfLook(bool gulf);
inline bool gulfLook() { return skin().gulf; }

// Colours of as2/frontend.md (conventions) in the current look.
inline Color ink() { return packed(skin().ink); }
inline Color accent() { return packed(skin().accent); }
inline Color disabledGrey() { return packed(skin().disabled); }
inline Color listGrey() { return packed(skin().listDisabled); }
inline Color rowDisabled() { return packed(skin().rowDisabled); }
inline Color darkGreenBox() { return packed(0x80006000u); }
inline Color greenOutline() { return packed(0x5000FF00u); }
inline Color red() { return packed(0xFF0000FFu); }

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

// UI_DrawPanel(x, y, w, h, f) titled `title` ("" = none), as2/frontend.md 3.1; in Gulf Thunder's
// look gulf/frontend.delta.md 3.1.
void panel(Renderer2D& r, const UiAssets& a, float x, float y, float w, float h, float f, std::string_view title);
// UI_DrawMenuHeader at logo clock T (as2/frontend.md 3.1).
void titleLogo(Renderer2D& r, const UiAssets& a, float T);
// The same from pictures the caller holds (the game selector's AirStrike 2 card draws the
// title logo of a game whose data is not the running one): the glow, the spinning "2"
// emblem and the logo with the clouds through its letters, drawn as titleLogo does at the
// offset (ox, oy) and scale k (1 = the title screen's), tinted white with `alpha`. `cloudT`
// is the clock of the clouds alone (equal to T on the title screen).
struct TitleLogoPictures {
    const Texture2D *glow = nullptr, *two = nullptr, *logo = nullptr, *clouds = nullptr;
};
void titleLogoAt(Renderer2D& r, const TitleLogoPictures& p, float T, float cloudT, float ox, float oy, float k, float alpha);
// Gulf Thunder's title bar (gulf/frontend.delta.md 3.1): the letterbox bars, the emblem tinted
// `tint` (RGBA) at logo clock T, and with `scanLines` the scan-line texture over the middle band.
void gulfTitleBar(Renderer2D& r, const UiAssets& a, float T, const float tint[4], bool scanLines);
// A text button's frame, caption and rivets with its left edge at `left` and the frame's top
// at `yDrawn`; W = the caption width part (as2/frontend.md 2.5; gulf 2.5).
void textButton(Renderer2D& r, const UiAssets& a, float left, float yDrawn, float W, std::string_view caption,
                Color captionColor);

// Additive text (the green text routine), `markup` switches braces to white.
void text(Renderer2D& r, const UiAssets& a, float x, float y, std::string_view s, Color c, Align al = Align::Left,
          bool markup = false);

// The front end's own random source for the panel noise (issue 240 item 2), 0..1.
float noiseRandom();

} // namespace as2
} // namespace as3d::ui
