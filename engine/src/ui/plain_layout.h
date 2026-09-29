// Shared layout of the plain front end (docs/spec/as2/issues/260): the button slots of the
// bottom bar, in virtual 800x600 pixels. Internal to engine/src/ui.
#pragma once

#include "as3d/ui.h"

namespace as3d::ui {

// Bottom bar (y 500..600, the letterbox bar): Back on the left, the screen's main action on
// the right, a third button in the middle.
constexpr RectF kPlainLeft{40, 526, 150, 40};
constexpr RectF kPlainMiddle{325, 526, 150, 40};
constexpr RectF kPlainRight{610, 526, 150, 40};
constexpr float kPlainButtonScale = 1.25f;

} // namespace as3d::ui
