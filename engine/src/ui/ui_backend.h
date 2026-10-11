// What the 2D layer's backend (gles/renderer2d_gl.cpp, pica/renderer2d_pica.cpp) adds to the
// neutral UI code: private to the ui module.
#pragma once

#include "as3d/ui.h"

namespace as3d::ui {

// UiAssets::consoleText's default: the console text sizes on the PICA (the 3DS), off on GL
// (desktop, Android, web: their text and goldens unchanged).
bool consoleTextBackendDefault();
// After UiAssets::load: the PICA logs where the font textures landed (AS3D_UI_FONT: VRAM or
// linear memory, size, format); GL does nothing.
void noteFontAtlases(const UiAssets& assets);

} // namespace as3d::ui
