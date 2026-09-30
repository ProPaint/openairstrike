// The sequels' comic screens (docs/spec/as2/frontend.md 3.2, 3.16): the four intro comic pages
// and the loading comic, and the touch-mode buttons that stand for keys there.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "as2_screens.h"

namespace as3d::ui {

namespace {

using namespace as2;

std::string tile(const char* name) { return std::string("gfx\\ui\\comix\\intro\\frame#") + name + ".tga"; }

// A tile at its natural size (R_Add2DPic), opaque unless stated.
void tileAt(Renderer2D& r, const UiAssets& a, const char* name, float x, float y, Color c = Color{},
            Blend b = Blend::Opaque) {
    if (const Texture2D* t = a.texture(tile(name))) pic(r, *t, x, y, c, b);
}

// A row of four tiles `<prefix>0..3` at x 0, 256, 512, 768 (the last column is 64 wide).
void row4(Renderer2D& r, const UiAssets& a, const char* prefix, float y) {
    char name[32];
    for (int k = 0; k < 4; k++) {
        std::snprintf(name, sizeof name, "%s%d", prefix, k);
        tileAt(r, a, name, 256.0f * static_cast<float>(k), y);
    }
}

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

void fill(Renderer2D& r, Color c, float alpha) {
    if (alpha > 0) r.fullscreen({c.r, c.g, c.b, clamp01(alpha)}, Blend::Alpha);
}

// A piece of page 3 or 4 fading in over [start, start + 1] (white, alpha t - start), ALPHA.
void fadeIn(Renderer2D& r, const UiAssets& a, const char* name, float x, float y, float t, float start) {
    if (t <= start) return;
    tileAt(r, a, name, x, y, {1, 1, 1, clamp01(t - start)}, Blend::Alpha);
}

} // namespace

float SequelScreens::comicDuration(int page) {
    switch (page) {
        case 1: case 2: return 7.0f;
        case 3: return 20.0f;
        default: return 11.0f;
    }
}

void SequelScreens::drawComicPage(Renderer2D& r, const UiAssets& a, int page, float t) {
    const Color white{1, 1, 1, 1}, black{0, 0, 0, 1};
    r.fullscreen(black, Blend::Opaque);
    switch (page) {
        case 1:
        case 2: {
            // One 832 x 320 panorama at (0, 150): rows at y 150 and 406.
            row4(r, a, page == 1 ? "1_0_" : "2_0_", 150);
            row4(r, a, page == 1 ? "1_1_" : "2_1_", 406);
            if (t < 2) fill(r, white, (2 - t) / 2);
            if (t > 5) fill(r, page == 1 ? white : black, (t - 5) / 2);
            break;
        }
        case 3: {
            // Panel A slides up from 200 px down; B to E fade in over it.
            const float ay = t < 2 ? static_cast<float>(static_cast<int>((1 - t / 2) * 200)) : 0.0f;
            row4(r, a, "3_1_0_", ay);
            fadeIn(r, a, "3_2_0_0", 0, 162, t, 3);
            fadeIn(r, a, "3_2_0_1", 256, 162, t, 3);
            fadeIn(r, a, "3_2_1_0", 0, 418, t, 3);
            fadeIn(r, a, "3_2_1_1", 256, 418, t, 3);
            fadeIn(r, a, "3_3_0_0", 190, 147, t, 5);
            fadeIn(r, a, "3_3_0_1", 446, 147, t, 5);
            fadeIn(r, a, "3_4_0_0", 472, 100, t, 10);
            fadeIn(r, a, "3_4_0_1", 728, 100, t, 10);
            fadeIn(r, a, "3_4_1_0", 472, 356, t, 10);
            fadeIn(r, a, "3_4_1_1", 728, 356, t, 10);
            fadeIn(r, a, "3_5_0_0", 264, 384, t, 12);
            fadeIn(r, a, "3_5_0_1", 520, 384, t, 12);
            if (t < 2) fill(r, black, (2 - t) / 2);
            if (t >= 18 && t < 20) fill(r, black, (t - 18) / 2);
            break;
        }
        default: {
            row4(r, a, "4_1_0_", 90);
            row4(r, a, "4_1_1_", 346);
            fadeIn(r, a, "4_2_0_0", 39, 120, t, 3);
            fadeIn(r, a, "4_2_0_1", 295, 120, t, 3);
            if (t < 2) fill(r, black, (2 - t) / 2);
            if (t >= 9 && t < 11) fill(r, black, (t - 9) / 2);
            break;
        }
    }
}

RectF SequelScreens::touchSkipRect() {
    const float w = measureText(FontMetrics::original(), " Skip ", 1.0f, true) + 46.0f;
    return {780.0f - w, 560.0f, w, 30.0f};
}

void SequelScreens::drawTouchSkip(Renderer2D& r, const UiAssets& a) {
    const RectF b = touchSkipRect();
    textButton(r, a, b.x, b.y, b.w - 46.0f, " Skip ", green());
}

RectF SequelScreens::touchMenuRect() {
    const float w = measureText(FontMetrics::original(), " MENU ", 1.0f, true) + 46.0f;
    return {400.0f - w * 0.5f, 4.0f, w, 30.0f};
}

void SequelScreens::drawLoading(Renderer2D& r, const UiAssets& a, float progress, bool intermission, int mission) {
    r.fullscreen({0, 0, 0, 1}, Blend::Opaque);
    if (!intermission) {
        // The comic of the mission's third of the campaign (as2/frontend.md 3.16): one picture of
        // about 801 x 330 in three columns 267 wide.
        const int set = mission <= 6 ? 1 : mission <= 12 ? 2 : 3;
        char name[64];
        for (int k = 1; k <= 3; k++) {
            const float x = 267.0f * static_cast<float>(k - 1);
            std::snprintf(name, sizeof name, "gfx\\ui\\comix\\loading%d_%d_1.tga", set, k);
            if (const Texture2D* t = a.texture(name)) picStretched(r, *t, x, 135, 267, 264, Color{}, Blend::Opaque);
            std::snprintf(name, sizeof name, "gfx\\ui\\comix\\loading%d_%d_2.tga", set, k);
            if (const Texture2D* t = a.texture(name)) picStretched(r, *t, x, 399, 267, 66, Color{}, Blend::Opaque);
        }
    }
    r.outline(340, 500, 200, 10, orange(), Blend::Opaque);
    r.rect(340, 500, std::clamp(progress, 0.0f, 1.0f) * 200.0f, 10, orange(), Blend::Opaque);
    text(r, a, 260, 496, "Loading", orange());
}

} // namespace as3d::ui
