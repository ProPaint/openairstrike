// The pad-key pictures of the console menus' footers (docs/3ds-ui-design.md "Owner decisions":
// set 4, "HUD plate"): apps/3ds/assets/keys.png, original art of this project (not game data),
// embedded as apps/3ds/assets/keys.inc (tools/3ds_key_glyphs.py) and made into a texture here.
#pragma once

#include "as3d/texture.h"
#include "as3d/ui.h"

namespace ctrapp {

// Makes the embedded pictures into `tex` (padded to 256 x 32, nearest filtering) and fills `atlas`
// with consoleKeyRect's rectangles. False (an atlas without a texture: the footers draw the keys'
// names instead) if the texture cannot be made.
bool loadKeyGlyphs(as3d::Texture2D& tex, as3d::ui::KeyGlyphAtlas& atlas);

} // namespace ctrapp
