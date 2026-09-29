// The HUD layouts of the three games (as3d/hud_layout.h).
#include "as3d/hud_layout.h"

namespace as3d::ui {

namespace {

// Colours shared by the games (frontend.md 4.1, as2/frontend.md 4 "the base's").
const Color kLifeGrey = grey(0xA0 / 255.0f);
const Color kScoreColor{0.753f, 0.188f, 0.0f, 1};
const Color kCountSelected{0.816f, 0.251f, 0.0f, 1};
const Color kCountOther{0.502f, 0.031f, 0.0f, 1};

// Missile icons, table 0x457b80 of v1.70 = as2@0x49e278 (same values).
std::vector<HudIcon> missileIcons() {
    return {
        {{0.0f, 0.727f, 0.258f, 1.0f}, Blend::Alpha, false},
        {{0.516f, 0.727f, 0.774f, 1.0f}, Blend::Alpha, false},
        {{0.258f, 0.727f, 0.516f, 1.0f}, Blend::Alpha, false},
        {{0.774f, 0.727f, 0.998f, 1.0f}, Blend::Alpha, false},
        {{0.0f, 0.501f, 0.258f, 0.727f}, Blend::Alpha, false},
    };
}

// ---------------------------------------------------------------------------
// AirStrike 3D v1.70 (frontend.md 4). These are the constants of the drawing code this file
// replaced; apps/tests/hud_layout_test.cpp keeps the old tables as its expectations.
// ---------------------------------------------------------------------------
HudLayout makeV170() {
    HudLayout L;
    L.game = GameId::AirStrike3D;
    L.name = "as3d";
    L.barAtlas = "gfx\\ui\\mainbar.tga";
    L.barAtlasW = 256; L.barAtlasH = 128;
    L.lifeTexture = "gfx\\ui\\life.tga";
    L.weaponsAtlas = "gfx\\ui\\weapons.tga";
    L.missilesAtlas = "gfx\\ui\\missiles.tga";
    L.itemsAtlas = "gfx\\ui\\items.tga";
    L.mouseCursor = "gfx\\mc_cur.tga";

    const Color frameGrey = grey(0x80 / 255.0f);
    L.barFrame = {{0.0f, 0.8359f, 0.7031f, 1.0f}, 180, 21, frameGrey, Blend::Add};
    L.scoreFrame = L.barFrame; // the score sits in a second health-bar frame
    L.box = {{0.0f, 0.375f, 0.2734f, 0.6719f}, 70, 39, frameGrey, Blend::Add};
    L.fill = {{0.0078f, 0.6719f, 174.0f / 256.0f, 0.8359f}, 172, 21, Color{}, Blend::Add};
    L.fillStyle = HudFill::Proportional;
    L.fillWidth = 172;
    L.fillSpanTexels = 174;
    L.life = {{0, 0, 1, 1}, 32, 32, kLifeGrey, Blend::Add};
    L.lifeIconsMax = 5;

    L.scoreColor = kScoreColor;
    L.countSelected = kCountSelected;
    L.countOther = kCountOther;
    L.missileCountMin = 1;
    L.powerupCountMin = 2; // power-up counts only above 1
    L.selection = HudSelection::DoubleFrame;
    L.dimUnselected = 1.0f;

    // Weapon table 0x457ae0: 20 entries, 10..19 zero (no picture).
    L.weapons = {
        {{0.0f, 0.727f, 0.258f, 1.0f}, Blend::Add, false},     {{0.258f, 0.727f, 0.516f, 1.0f}, Blend::Add, false},
        {{0.516f, 0.727f, 0.774f, 1.0f}, Blend::Add, false},   {{0.774f, 0.727f, 0.998f, 1.0f}, Blend::Add, false},
        {{0.0f, 0.18f, 0.258f, 0.453f}, Blend::Add, false},    {{0.516f, 0.453f, 0.774f, 0.727f}, Blend::Add, false},
        {{0.774f, 0.453f, 0.998f, 0.727f}, Blend::Add, false}, {{0.0f, 0.453f, 0.258f, 0.727f}, Blend::Add, false},
        {{0.258f, 0.453f, 0.516f, 0.727f}, Blend::Add, false}, {{0.258f, 0.18f, 0.516f, 0.453f}, Blend::Add, false},
    };
    L.missiles = missileIcons();
    // Power-up kinds 0..3; slots 4..15 have a frame but no icon.
    L.powerups = {
        {{0.0f, 0.453f, 0.258f, 0.727f}, Blend::Add, false},
        {{0.774f, 0.727f, 0.998f, 1.0f}, Blend::Alpha, false},
        {{0.258f, 0.727f, 0.516f, 1.0f}, Blend::Alpha, false},
        {{0.516f, 0.727f, 0.774f, 1.0f}, Blend::Alpha, false},
    };
    L.typewriterSkipsBraces = false;
    L.hint = HintStyle::V170Box;

    using E = HudElement;
    // One player (frontend.md 4.2).
    HudSide& s = L.onePlayer;
    s.order = {E::HealthFrame, E::HealthFill, E::WeaponBox, E::WeaponIcon, E::Missiles,
               E::ScoreFrame,  E::Score,      E::Powerups,  E::Lives};
    s.livesX = 15; s.livesStep = 32; s.livesY = 555;
    s.barX = 10; s.barY = 10;
    s.fillX = 12; s.fillY = 10;
    s.scoreFrame = true; s.scoreFrameX = 610; s.scoreFrameY = 10; s.scoreFrameMirror = true;
    s.scoreX = 630; s.scoreY = 12;
    s.weaponBoxX = 10; s.weaponBoxY = 32;
    s.weaponIconX = 11; s.weaponIconY = 35;
    s.missiles = {10, 73, 41, false, false, 11, 3, 76, 3, 0, 0, true, false};
    s.powerups = {720, 32, 41, true, false, 721, 3, 786, 3, 0, 0, true, false};

    // Two players (frontend.md 4.3).
    const std::vector<HudElement> order2 = {E::Lives,     E::HealthFrame, E::HealthFill, E::ScoreFrame, E::Score,
                                            E::WeaponBox, E::WeaponIcon,  E::Missiles,   E::Powerups};
    HudSide& a = L.twoPlayers[0];
    a.order = order2;
    a.livesX = 15; a.livesStep = 32; a.livesY = 555;
    a.barX = 10; a.barY = 10;
    a.fillX = 12; a.fillY = 10;
    a.scoreFrame = true; a.scoreFrameX = 10; a.scoreFrameY = 32;
    a.scoreX = 170; a.scoreY = 34; a.scoreRightAligned = true;
    a.weaponBoxX = 10; a.weaponBoxY = 54; a.weaponBoxMirror = true;
    a.weaponIconX = 11; a.weaponIconY = 57;
    a.missiles = {10, 95, 41, true, false, 11, 3, 76, 3, 0, 0, true, false};
    a.powerups = {82, 54, 41, true, false, 86, 3, 148, 3, 0, 0, true, false};
    HudSide& b = L.twoPlayers[1];
    b.order = order2;
    b.livesX = 753; b.livesStep = -32; b.livesY = 555;
    b.barX = 610; b.barY = 10; b.barMirror = true;
    b.fillX = 788; b.fillY = 10; b.fillFromRight = true;
    b.scoreFrame = true; b.scoreFrameX = 610; b.scoreFrameY = 32; b.scoreFrameMirror = true;
    b.scoreX = 630; b.scoreY = 34;
    b.weaponBoxX = 720; b.weaponBoxY = 54;
    b.weaponIconX = 721; b.weaponIconY = 57;
    b.missiles = {720, 95, 41, false, false, 721, 3, 786, 3, 0, 0, true, false};
    b.powerups = {648, 54, 41, true, false, 649, 3, 714, 3, 0, 0, true, false};
    return L;
}

// ---------------------------------------------------------------------------
// AirStrike 2 v2.51 (as2/frontend.md 4, corrections: issues/280).
// ---------------------------------------------------------------------------
HudLayout makeAs2() {
    HudLayout L;
    L.game = GameId::AirStrike2;
    L.name = "as2";
    L.barAtlas = "gfx\\ui\\mainbar2.tga";
    L.barAtlasW = 256; L.barAtlasH = 256;
    L.lifeTexture = "gfx\\ui\\life.tga";
    L.weaponsAtlas = "gfx\\ui\\weapons.tga";
    L.missilesAtlas = "gfx\\ui\\missiles.tga";
    L.itemsAtlas = "gfx\\ui\\items.tga";
    L.iconAtlasW = 256; L.iconAtlasH = 128;
    L.mouseCursor = nullptr; // no mouse-control cursor any more (4.1)
    L.panelAtlas = "gfx\\ui\\interface.tga";
    L.panelNoise = "gfx\\ui\\snow.tga";

    auto bar = [](float x, float y, float w, float h) { return texelUv(x, y, w, h, 256, 256); };
    auto icon = [](float x, float y, float w, float h) { return texelUv(x, y, w, h, 256, 128); };
    // Frames, fill and boxes white, ALPHA; pips and the line ADD (their alpha is 0).
    L.barFrame = {bar(0, 34, 232, 34), 232, 34, Color{}, Blend::Alpha};
    L.scoreFrame = {bar(0, 68, 232, 34), 232, 34, Color{}, Blend::Alpha};
    L.box = {bar(0, 102, 87, 60), 87, 60, Color{}, Blend::Alpha};
    L.fill = {bar(0, 0, 227, 34), 227, 34, Color{}, Blend::Alpha};
    L.fillStyle = HudFill::Segmented;
    L.fillCap = 17;
    L.fillSegment = 5;
    L.fillSegments = 42;
    L.levelOn = {bar(16, 164, 7, 6), 7, 6, Color{}, Blend::Add};
    L.levelMax = {bar(16, 176, 7, 6), 7, 6, Color{}, Blend::Add};
    L.selLine = {bar(21, 190, 53, 1), 53, 1, Color{}, Blend::Add};
    // life.tga whole through R_Add2DPic, which insets the UVs by half a texel (conventions).
    L.life = {texelUv(0.5f, 0.5f, 31, 31, 32, 32), 32, 32, kLifeGrey, Blend::Add};
    L.lifeIconsMax = 10;

    L.scoreColor = kScoreColor;
    L.countSelected = kCountSelected;
    L.countOther = kCountOther;
    L.missileCountMin = 1;
    L.powerupCountMin = 1; // counts show from 1 upwards
    L.selection = HudSelection::LineAndAlpha;
    L.dimUnselected = 0x40 / 255.0f; // 0.251

    // Weapons, UV table as2@0x49e1e8 (4.4), ADD.
    L.weapons = {
        {{0.0f, 0.727f, 0.258f, 1.0f}, Blend::Add, false},     // 0 machine gun
        {{0.516f, 0.727f, 0.774f, 1.0f}, Blend::Add, false},   // 1 impulse gun
        {{0.774f, 0.453f, 1.0f, 0.727f}, Blend::Add, false},   // 2 plasma cannon
        {{0.0f, 0.453f, 0.258f, 0.727f}, Blend::Add, false},   // 3 quantum gun (laser)
        {{0.258f, 0.453f, 0.516f, 0.727f}, Blend::Add, false}, // 4 big laser
        {{0.516f, 0.453f, 0.774f, 0.727f}, Blend::Add, false}, // 5 lightning gun
        {{0.258f, 0.727f, 0.516f, 1.0f}, Blend::Add, false},   // 6 wave gun
        {{0.774f, 0.727f, 1.0f, 1.0f}, Blend::Add, false},     // 7 missile gun
        {{0.0f, 0.18f, 0.258f, 0.453f}, Blend::Add, false},    // 8 flamethrower
    };
    L.weaponLevelMax = {4, 5, 7, 8, 5, 5, 4, 5, 3}; // table as2@0x49e1c4
    L.missiles = missileIcons();
    // Power-ups (switch as2@0x408838); slots 10..15 have no icon.
    L.powerups = {
        {icon(0, 35, 66, 35), Blend::Add, false},    // 0 lightning bomb (alpha 0: ADD)
        {icon(198, 0, 57, 35), Blend::Alpha, false}, // 1 nuclear bomb
        {icon(66, 0, 66, 35), Blend::Alpha, false},  // 2 rocket strike (the cross-hair)
        {icon(132, 0, 66, 35), Blend::Alpha, false}, // 3 cluster bomb (the canister)
        {icon(66, 35, 66, 35), Blend::Alpha, false}, // 4 annihilator
        {icon(132, 35, 66, 35), Blend::Alpha, false}, // 5 satellite strike
        {icon(0, 0, 66, 35), Blend::Alpha, true},    // 6 speed-up (timer)
        {icon(0, 70, 66, 35), Blend::Alpha, true},   // 7 slow-down (timer)
        {icon(198, 35, 57, 35), Blend::Alpha, false}, // 8 air support
        {icon(66, 70, 66, 35), Blend::Alpha, true},  // 9 shield (timer)
    };
    L.typewriterSkipsBraces = true;
    L.hint = HintStyle::SequelPanel;

    using E = HudElement;
    // One player (4.2). The power-up column shows no frames and no selection line: the
    // original's screenshots show none (issue 280).
    HudSide& s = L.onePlayer;
    s.order = {E::Lives,    E::HealthFrame, E::HealthFill, E::WeaponBox, E::WeaponLevel,
               E::WeaponIcon, E::Missiles,  E::ScoreFrame, E::Score,     E::Powerups};
    s.livesX = 15; s.livesStep = 32; s.livesY = 555;
    s.barX = 0; s.barY = 6;
    s.fillX = 0; s.fillY = 6;
    s.scoreFrame = true; s.scoreFrameX = 568; s.scoreFrameY = 6; s.scoreFrameMirror = true;
    s.scoreX = 600; s.scoreY = 17;
    s.weaponBoxX = 0; s.weaponBoxY = 40;
    s.weaponIconX = 15; s.weaponIconY = 57;
    s.levelX = 18; s.levelY = 51;
    s.missiles = {0, 100, 60, false, false, 15, 17, 80, 13, 22, 5, true, true};
    s.powerups = {713, 44, 60, true, false, 721, 17, 782, 13, 725, 5, false, false, true};

    // Two players (4.3).
    const std::vector<HudElement> order2 = {E::Lives,     E::HealthFrame, E::HealthFill, E::ScoreFrame, E::Score,
                                            E::WeaponBox, E::WeaponLevel, E::WeaponIcon, E::Missiles,   E::Powerups};
    HudSide& a = L.twoPlayers[0];
    a.order = order2;
    a.livesX = 15; a.livesStep = 32; a.livesY = 555;
    a.barX = 0; a.barY = 6;
    a.fillX = 0; a.fillY = 6;
    a.scoreFrame = true; a.scoreFrameX = 0; a.scoreFrameY = 40;
    a.scoreX = 200; a.scoreY = 51; a.scoreRightAligned = true;
    a.weaponBoxX = 0; a.weaponBoxY = 74;
    a.weaponIconX = 15; a.weaponIconY = 91;
    a.levelX = 18; a.levelY = 85;
    a.missiles = {0, 134, 60, false, false, 15, 13, 80, 13, 22, 5, true, true};
    a.powerups = {0, 0, 60, false, true, 15, 17, 80, 13, 22, 5, true, true, true};
    HudSide& b = L.twoPlayers[1];
    b.order = order2;
    b.livesX = 753; b.livesStep = -32; b.livesY = 555;
    b.barX = 568; b.barY = 6; b.barMirror = true;
    b.fillX = 800; b.fillY = 6; b.fillFromRight = true;
    b.scoreFrame = true; b.scoreFrameX = 568; b.scoreFrameY = 40; b.scoreFrameMirror = true;
    b.scoreX = 600; b.scoreY = 51;
    b.weaponBoxX = 713; b.weaponBoxY = 74; b.weaponBoxMirror = true;
    b.weaponIconX = 719; b.weaponIconY = 91;
    b.levelX = 782; b.levelY = 85; b.levelFromRight = true;
    b.missiles = {713, 134, 60, true, false, 719, 13, 780, 13, 725, 5, true, true};
    b.powerups = {713, 0, 60, true, true, 721, 17, 782, 13, 725, 5, true, true, true};
    return L;
}

// Gulf Thunder v2.71: the same HUD code as AirStrike 2 (as2/frontend.md, Gulf column) and the
// same mainbar2.tga, life.tga, missiles.tga and items.tga files. UNVERIFIED: its weapons.tga
// has two more cells (texel row 70) whose table entries we do not know, so only AirStrike 2's
// nine are used; its panel atlas interface_gulf.tga has a different arrangement than
// interface.tga (AirStrike 2's rectangles do not fit it), so the hint box stays the first
// game's plain box until the atlas is mapped.
HudLayout makeGulf() {
    HudLayout L = makeAs2();
    L.game = GameId::GulfThunder;
    L.name = "gulf";
    L.panelAtlas = "gfx\\ui\\interface_gulf.tga";
    L.panelNoise = "gfx\\ui\\snow.tga";
    L.hint = HintStyle::V170Box;
    return L;
}

} // namespace

const HudLayout& hudLayout(GameId game) {
    static const HudLayout v170 = makeV170();
    static const HudLayout as2 = makeAs2();
    static const HudLayout gulf = makeGulf();
    switch (game) {
        case GameId::AirStrike2: return as2;
        case GameId::GulfThunder: return gulf;
        default: return v170;
    }
}

} // namespace as3d::ui
