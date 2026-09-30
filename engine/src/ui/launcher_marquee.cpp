// The marquees of the game selector's cards (docs/spec/issues/164).
#include <algorithm>
#include <cmath>

#include "as2_draw.h"
#include "launcher_marquee.h"

namespace as3d::ui {

bool drawMarquee(Renderer2D& r, const GameCard& card, const RectF& box, float clock, bool loopFit) {
    // Both games' title clocks run at half the menu time (as2/frontend.md 3.1).
    const float T = 0.5f * clock;
    const float cloudT = loopFit ? T * (10.0f / 6.28318531f) : T;
    switch (card.marquee) {
        case Marquee::TitleLogo: {
            if (!card.logo || !card.logo->valid()) return false;
            // The logo (x 124..636) and the emblem at its end (up to 738 at its largest): 614
            // virtual pixels wide by 190 high (the emblem swells 30 pixels past the logo's 128),
            // centred on the letters' middle line.
            const float k = std::min(box.w / 614.0f, box.h / 190.0f);
            as2::TitleLogoPictures p;
            p.glow = card.glow && card.glow->valid() ? card.glow : nullptr;
            p.two = card.two && card.two->valid() ? card.two : nullptr;
            p.logo = card.logo;
            p.clouds = card.clouds && card.clouds->valid() ? card.clouds : nullptr;
            as2::titleLogoAt(r, p, T, cloudT, box.x + (box.w - 614.0f * k) * 0.5f - 124.0f * k,
                             box.y + box.h * 0.5f - 64.0f * k, k, 1.0f);
            return true;
        }
        case Marquee::Emblem: {
            if (!card.logo || !card.logo->valid()) return false;
            const float k = std::min(box.w / 512.0f, box.h / 256.0f);
            Quad q;
            q.w = 512.0f * k;
            q.h = 256.0f * k;
            q.x = box.x + (box.w - q.w) * 0.5f;
            q.y = box.y + (box.h - q.h) * 0.5f;
            q.texture = card.logo;
            q.blend = Blend::Alpha;
            if (card.clouds && card.clouds->valid()) {
                q.texture2 = card.clouds;
                q.s0b = 0.1f * cloudT;
                q.s1b = 0.1f * cloudT + 2.0f;
                q.t0b = 0;
                q.t1b = 1;
                q.combine2 = 2;
            }
            r.add(q);
            return true;
        }
        default: return false;
    }
}

} // namespace as3d::ui
