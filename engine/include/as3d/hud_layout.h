// The in-game HUD of each game as data: atlases, pieces, colours, blend modes, icon tables and
// the positions of every element for one and two players. drawHud (ui.h) draws from it.
//
// Specs: the first game docs/spec/frontend.md 4 (the tables reproduce the drawing code that
// existed before this file, value for value); AirStrike 2 docs/spec/as2/frontend.md 4 with
// the corrections of docs/spec/as2/issues/280-hud-layout-corrections.md; Gulf Thunder runs the
// same HUD code as AirStrike 2 with its own weapon icon table and level caps and its own hint
// panel (docs/spec/gulf/frontend.delta.md 3.1, 3.15, 4.2, 4.4).
//
// Pieces are given in the specs' UV convention (SpecUv, t = 1 at the top row of the image);
// sizes and positions are virtual 800x600 pixels. "Mirrored" means the original drew
// the quad from its right edge with a negative width: we draw the covering rectangle with s0
// and s1 exchanged.
#pragma once

#include <vector>

#include "as3d/game_profile.h"
#include "as3d/ui.h"

namespace as3d::ui {

// A piece of an atlas and how it is drawn.
struct HudPiece {
    SpecUv uv;
    float w = 0, h = 0; // drawn size in virtual pixels
    Color color;
    Blend blend = Blend::Alpha;
};

// Spec UVs of the texel rectangle (x, y, w, h) of an atlas of aw x ah texels (y down from the
// top row, as an image viewer shows it).
constexpr SpecUv texelUv(float x, float y, float w, float h, float aw, float ah) {
    return SpecUv{x / aw, 1.0f - (y + h) / ah, (x + w) / aw, 1.0f - y / ah};
}

// One icon of an atlas (weapons, missiles, power-ups).
struct HudIcon {
    SpecUv uv;                  // empty = no picture (the frame and count are still drawn)
    Blend blend = Blend::Alpha;
    bool alwaysFull = false;    // not dimmed when unselected (AS2 timer power-ups)
};

// The elements of one player's HUD, in the order of HudSide::order.
enum class HudElement : u8 {
    Lives, HealthFrame, HealthFill, ScoreFrame, Score, WeaponBox, WeaponLevel, WeaponIcon, Missiles, Powerups
};

// A column of boxes (missiles or power-ups), packed in type order: entry n has its frame at
// (frameX, firstY + step n).
struct HudColumn {
    float frameX = 0, firstY = 0, step = 0;
    bool mirror = false;              // the frame (and selection line) mirrored
    bool followsMissiles = false;     // power-ups only: firstY is the missile column's next slot
    float iconX = 0, iconDy = 0;      // icon's top-left: (iconX, F + iconDy)
    float countRight = 0, countDy = 0; // count: x = ftol(countRight - 10.5 digits), y = F + countDy
    float lineX = 0, lineDy = 0;      // selection line's covering rectangle (lineX, F + lineDy)
    bool drawFrames = true;           // the box behind each entry
    bool drawLine = true;             // the selection line (a layout with a line piece)
    bool iconAfterCount = false;      // drawing order frame, line, count, icon (else frame, icon, line, count)
};

// Positions of one player's elements.
struct HudSide {
    std::vector<HudElement> order;
    float livesX = 0, livesStep = 0, livesY = 0; // icon i at (livesX + livesStep i, livesY)
    float barX = 0, barY = 0;         // health frame, covering rectangle's top-left
    bool barMirror = false;
    float fillX = 0, fillY = 0;       // fill's left edge, or its right edge when fillFromRight
    bool fillFromRight = false;       // grows leftwards from fillX, mirrored
    bool scoreFrame = false;
    float scoreFrameX = 0, scoreFrameY = 0;
    bool scoreFrameMirror = false;
    float scoreX = 0, scoreY = 0;     // left edge, or right edge (x = scoreX - 14 digits) when right-aligned
    bool scoreRightAligned = false;
    float weaponBoxX = 0, weaponBoxY = 0;
    bool weaponBoxMirror = false;
    float weaponIconX = 0, weaponIconY = 0;
    float levelX = 0, levelY = 0;     // pips: left end, or right end when levelFromRight
    bool levelFromRight = false;
    HudColumn missiles, powerups;
};

enum class HudFill : u8 {
    Proportional, // the first game: width fillWidth x health / scale, UVs cut to match
    Segmented,    // AS2: a cap and n = ftol(health / maximum x segments) segments, 1 texel = 1 px
};
enum class HudSelection : u8 {
    DoubleFrame,  // the first game: the selected entry's frame is drawn twice
    LineAndAlpha, // AS2: a selection line; unselected icons are dimmed
};
// The look of the sequels' hint panel (HintStyle::SequelPanel): the pieces of panelAtlas and
// the colours.
enum class HudPanelSkin : u8 {
    As2,  // as2/frontend.md 3.1: interface.tga, cables, rivets, green and orange
    Gulf, // gulf/frontend.delta.md 3.1, 2.5, 3.15: interface_gulf.tga, rails, grey and red
};
struct HudLayout {
    GameId game = GameId::AirStrike3D;
    const char* name = "";

    // Atlases (game paths). nullptr: the game has none.
    const char* barAtlas = nullptr;      // frames, fill, boxes, pips, selection line
    float barAtlasW = 256, barAtlasH = 256;
    const char* lifeTexture = nullptr;
    const char* weaponsAtlas = nullptr;
    const char* missilesAtlas = nullptr;
    const char* itemsAtlas = nullptr;
    float iconAtlasW = 256, iconAtlasH = 128; // weapons, missiles and items
    const char* mouseCursor = nullptr;   // drawn when HudState::mouseCursor is set
    const char* panelAtlas = nullptr;    // the hint box's panel pieces (SequelPanel)
    const char* panelNoise = nullptr;    // the panel's static fill (SequelPanel)

    // Pieces of barAtlas.
    HudPiece barFrame, scoreFrame, box;
    HudPiece fill;                       // Proportional: uv.s1 is the span for a full bar
    HudFill fillStyle = HudFill::Proportional;
    float fillWidth = 0;                 // Proportional: drawn width of a full bar
    float fillSpanTexels = 0;            // Proportional: texels of a full bar (s1 = f x span / atlas)
    float fillCap = 0, fillSegment = 0;  // Segmented: texels (= pixels) of the cap and of a segment
    int fillSegments = 0;
    HudPiece levelOn, levelMax;          // weapon level pips (w, h: one pip); none if w == 0
    HudPiece selLine;                    // selection line; none if w == 0
    HudPiece life;                       // uv, size, colour and blend of one life icon
    int lifeIconsMax = 5;

    // Numbers.
    Color scoreColor, countSelected, countOther;
    float countScale = 0.75f;
    int missileCountMin = 1;             // counts shown from this value up
    int powerupCountMin = 1;

    HudSelection selection = HudSelection::DoubleFrame;
    float dimUnselected = 1.0f;          // ALPHA icons: alpha; ADD icons: grey level
    float iconW = 66, iconH = 35;
    std::vector<HudIcon> weapons, missiles, powerups; // indexed by slot / type
    std::vector<int> weaponLevelMax;     // pips per weapon slot (empty: no pips)
    // The filled pips are drawn for the whole level even above the cap, running past the
    // empty ones (Gulf Thunder's laser: level 7, cap 5; gulf/frontend.delta.md 4.2).
    bool levelOverrunsCap = false;

    bool typewriterSkipsBraces = false;  // the level name is centred without '{' and '}'
    HintStyle hint = HintStyle::V170Box;
    HudPanelSkin panelSkin = HudPanelSkin::As2;

    HudSide onePlayer;
    HudSide twoPlayers[2];
};

// The layout of a game. Gulf Thunder's is AirStrike 2's with its own tables and panel.
const HudLayout& hudLayout(GameId game);

} // namespace as3d::ui
