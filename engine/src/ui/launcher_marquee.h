// The marquees of the game selector's cards (docs/spec/issues/164). Internal to engine/src/ui.
#pragma once

#include "as3d/frontend.h"

namespace as3d::ui {

// Draws a card's marquee inside `box` (virtual pixels), its pictures animated as on the game's
// title screen at the selector's `clock`: AirStrike 2 through as2::titleLogoAt (the very code of
// its title screen), Gulf Thunder's logo with the clouds through it. `loopFit` (the web
// page's build-time render): the clouds' clock is scaled so that the clouds' loop (10 title
// clock units) and the emblem's (2 pi) repeat together after 2 pi units.
// Returns false when the card has nothing to draw here (a Banner card, whose mesh the window
// draws after the 2D layer, or pictures that are missing): the caller shows the title in text.
bool drawMarquee(Renderer2D& r, const GameCard& card, const RectF& box, float clock, bool loopFit = false);

} // namespace as3d::ui
