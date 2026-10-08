#include "touch_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "as3d/touch_controls.h"

namespace as3d_game {

using namespace as3d;
using ui::Blend;
using ui::Color;

void drawFpsCounter(ui::Renderer2D& r, const ui::UiAssets& a, const FpsCounter& fps, const TouchLayout* touch,
                    const SafeInsets& in, const char* note) {
    if (!a.fontLoaded) return;
    const ui::Mapping& m = r.mapping();
    const float fw = static_cast<float>(m.fbWidth);
    char line1[32], line2[48];
    if (fps.valid()) {
        std::snprintf(line1, sizeof line1, "%d FPS", static_cast<int>(fps.fps() + 0.5));
        std::snprintf(line2, sizeof line2, "worst %d ms  drop %d", static_cast<int>(fps.worstMs() + 0.5), fps.dropped());
    } else {
        std::snprintf(line1, sizeof line1, "-- FPS");
        line2[0] = 0;
    }
    const float s1 = 0.7f, s2 = 0.5f;            // text scales (virtual pixels)
    const float h1 = 15.0f * s1, h2 = 15.0f * s2;
    const ui::FontMetrics& fm = ui::FontMetrics::original();
    const bool hasNote = note && *note;
    float w = std::max(ui::measureText(fm, line1, s1), ui::measureText(fm, line2, s2));
    if (hasNote) w = std::max(w, ui::measureText(fm, note, s2));
    const float pad = 3.0f;
    const float margin = 6.0f;
    // Anchor: a top corner in virtual pixels, text right- or left-aligned from it.
    float x = 0, y = 0;
    ui::Align align = ui::Align::Right;
    const float topInset = m.toVirtY(static_cast<float>(in.top)) - m.toVirtY(0);
    if (touch && touch->outside) {
        const TouchCircle& p = touch->circles[static_cast<int>(TouchButton::Pause)];
        const bool pauseLeft = p.x < fw * 0.5f;
        y = m.top() + topInset + margin;
        if (pauseLeft) {
            x = m.toVirtX(fw - static_cast<float>(in.right)) - margin;
        } else {
            x = m.toVirtX(static_cast<float>(in.left)) + margin;
            align = ui::Align::Left;
        }
    } else if (touch) {
        // Beside the pause button at the top centre of the field.
        const TouchCircle& p = touch->circles[static_cast<int>(TouchButton::Pause)];
        x = m.toVirtX(p.x + p.r) + margin;
        y = m.toVirtY(p.y - p.r);
        align = ui::Align::Left;
    } else if (m.left() < -margin * 2) {
        x = m.right() - margin; // the screen's top right corner, outside the field
        y = m.top() + margin;
    } else {
        x = 400.0f + w * 0.5f; // top centre, between the health and score bars
        y = 3.0f;
    }
    const float bx = align == ui::Align::Right ? x - w - pad : x - pad;
    const float bh = h1 + (line2[0] ? h2 + 2.0f : 0.0f) + (hasNote ? h2 + 2.0f : 0.0f) + 2 * pad;
    r.rect(bx, y - pad, w + 2 * pad, bh, Color{0, 0, 0, 0.45f});
    ui::TextStyle st;
    st.scale = s1;
    st.align = align;
    st.color = Color{1.0f, 0.63f, 0.0f, 1.0f};
    ui::drawText(r, a.uiFont(), x, y, line1, st);
    if (line2[0]) {
        st.scale = s2;
        st.color = Color{0.75f, 0.75f, 0.78f, 1.0f};
        ui::drawText(r, a.uiFont(), x, y + h1 + 2.0f, line2, st);
    }
    if (hasNote) {
        st.scale = s2;
        st.color = Color{1.0f, 0.35f, 0.3f, 1.0f};
        ui::drawText(r, a.uiFont(), x, y + h1 + 2.0f + (line2[0] ? h2 + 2.0f : 0.0f), note, st);
    }
}

void drawPauseOverlay(ui::Renderer2D& r, bool touch) {
    r.fullscreen(Color{0, 0, 0, 0.5f});
    const float play[] = {0.1f, 0.05f, 0.95f, 0.5f, 0.1f, 0.95f};
    ui::fillConvex(r, 340, 220, 120, 160, play, 3, Color{1, 1, 1, 0.85f});
    (void)touch;
}

void drawLoadingScreen(ui::Renderer2D& r, float progress) {
    progress = std::min(std::max(progress, 0.0f), 1.0f);
    r.fullscreen(Color{0, 0, 0, 1});
    Color frame{0.6f, 0.6f, 0.6f, 1};
    r.rect(248, 288, 304, 2, frame);
    r.rect(248, 310, 304, 2, frame);
    r.rect(248, 288, 2, 24, frame);
    r.rect(550, 288, 2, 24, frame);
    r.rect(252, 292, 296 * progress, 16, Color{0.82f, 0.25f, 0.0f, 1});
}

void drawTouchControlsIn(ui::Renderer2D& r, int x, int y, int width, int height, const TouchMapper& touch,
                         const TouchOverlayState& state) {
    r.begin(width, height, x, y);
    drawTouchControls(r, touch, state);
    r.flush();
}

} // namespace as3d_game
