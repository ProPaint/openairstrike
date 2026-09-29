// 2D layer, font and in-game HUD. Spec: docs/spec/render-pipeline.md 8 (2D drawing, font) and
// docs/spec/engine-behaviour.md 11 (HUD, tutorial hints); the gaps are in docs/spec/issues/060-*.md.
//
// Everything is drawn in the original's virtual 800x600 screen, origin top-left, y down.
// UVs given to Renderer2D follow the engine convention of gfx.h: v = 0 is the TOP of the
// texture, so a quad with (s0,t0)-(s1,t1) shows the texture upright.
//
// Renderer2D collects quads on the CPU (begin/quad/rect/..., testable without GL) and
// draws them in submission order on flush() (needs a current GLES 3.0 context).
// HudState has no dependency on the game world; the game loop fills it every frame.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "as3d/core.h"
#include "as3d/gfx.h"
#include "as3d/vfs.h"

namespace as3d::ui {

constexpr float kVirtualWidth = 800.0f;
constexpr float kVirtualHeight = 600.0f;
constexpr int kMaxQuads = 4096; // per frame, like the original's 2D list

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
};
inline Color grey(float v) { return {v, v, v, 1}; }

// 2D blend modes (render-pipeline.md 4.1): Alpha = SRC_ALPHA/ONE_MINUS_SRC_ALPHA, Add = ONE/ONE
// (the alpha of the colour scales the intensity), Filter = DST_COLOR/ZERO.
enum class Blend { Alpha, Add, Filter };

// ---------------------------------------------------------------------------
// Virtual screen to framebuffer mapping.
// Aspect >= 4:3: uniform scale by height, the 4:3 play field is centred (bars left and right).
// 5:4 up to 4:3: stretched to the whole framebuffer, as the original does.
// Narrower than 5:4: uniform scale by width, centred vertically.
// ---------------------------------------------------------------------------
struct Mapping {
    int fbWidth = 800, fbHeight = 600;
    float scaleX = 1, scaleY = 1;
    float offsetX = 0, offsetY = 0; // framebuffer pixels of virtual (0, 0)

    float toFbX(float vx) const { return offsetX + vx * scaleX; }
    float toFbY(float vy) const { return offsetY + vy * scaleY; }
    float toVirtX(float px) const { return (px - offsetX) / scaleX; }
    float toVirtY(float py) const { return (py - offsetY) / scaleY; }
    // Virtual rectangle covered by the whole framebuffer (for full-screen overlays).
    float left() const { return toVirtX(0); }
    float top() const { return toVirtY(0); }
    float right() const { return toVirtX(static_cast<float>(fbWidth)); }
    float bottom() const { return toVirtY(static_cast<float>(fbHeight)); }
};
Mapping computeMapping(int fbWidth, int fbHeight);

// ---------------------------------------------------------------------------
// Quad list
// ---------------------------------------------------------------------------
struct Quad {
    float x = 0, y = 0, w = 0, h = 0;      // virtual pixels
    float s0 = 0, t0 = 0, s1 = 1, t1 = 1;  // (s0,t0) at the top-left corner
    const Texture2D* texture = nullptr;    // null = untextured (white)
    Color color;
    Blend blend = Blend::Alpha;
    bool line = false;                     // segment (x,y)-(x+w,y+h), one framebuffer pixel thick
};

class Renderer2D {
public:
    Renderer2D();
    ~Renderer2D();
    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    // Compiles the shader and creates the buffers. Needs a current GLES 3.0 context.
    bool init(std::string* error = nullptr);

    // Starts a frame for a framebuffer of this size: clears the list.
    void begin(int fbWidth, int fbHeight);
    const Mapping& mapping() const { return mapping_; }

    // Returns false (and counts a drop) once kMaxQuads is reached.
    bool add(const Quad& q);
    bool quad(float x, float y, float w, float h, float s0, float t0, float s1, float t1,
              const Texture2D* tex, Color c, Blend blend);
    // Whole texture at one texel per virtual pixel ("draw pic").
    bool pic(float x, float y, const Texture2D& tex, Color c, Blend blend);
    bool rect(float x, float y, float w, float h, Color c, Blend blend = Blend::Alpha);
    bool outline(float x, float y, float w, float h, Color c, Blend blend = Blend::Alpha);
    bool line(float x0, float y0, float x1, float y1, Color c, Blend blend = Blend::Alpha);
    // Covers the whole framebuffer, including the bars outside the 4:3 field.
    bool fullscreen(Color c, Blend blend = Blend::Alpha);

    const std::vector<Quad>& quads() const { return quads_; }
    int dropped() const { return dropped_; }

    // Draws the list into the currently bound framebuffer (viewport = whole framebuffer).
    // Returns the number of draw calls (batches of equal texture and blend mode).
    int flush();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Mapping mapping_;
    std::vector<Quad> quads_;
    int dropped_ = 0;
};

// ---------------------------------------------------------------------------
// Font (render-pipeline.md 8.3)
// ---------------------------------------------------------------------------
struct FontMetrics {
    u8 advance[256] = {};          // pen advance per byte, virtual pixels at scale 1
    int columns = 8, rows = 16;    // glyph grid
    float atlasSize = 256;         // texels, square
    float cellW = 32, cellH = 16;  // texels
    float glyphW = 30, glyphH = 15; // drawn size, virtual pixels at scale 1 (= texels used)
    int drawableLimit = 0x80;      // bytes at or above this have no texture (font_rus.tga is not shipped)

    static const FontMetrics& original();
};

struct GlyphUv {
    float s0, t0, s1, t1; // v = 0 at the top
};
// UV rectangle of byte c: the left glyphW texels and the lower glyphH texel rows of its cell.
GlyphUv glyphUv(const FontMetrics& m, unsigned char c);
// True if byte c is drawn (has a glyph texture, non-zero advance, not a blank space).
bool glyphDrawable(const FontMetrics& m, unsigned char c);

enum class Align { Left, Center, Right };

struct TextStyle {
    float scale = 1.0f;
    Color color;              // alpha scales the intensity (additive blend); 1 = full
    Align align = Align::Left;
    bool markup = false;      // '{' switches to white, '}' back to `color`; neither is drawn
};

// Width of the string in virtual pixels (sum of advances, markup characters excluded).
float measureText(const FontMetrics& m, std::string_view text, float scale = 1.0f, bool markup = false);

struct Font {
    const FontMetrics* metrics = nullptr;
    const Texture2D* texture = nullptr;
};
// Draws one line; (x, y) is the top of the glyph boxes, x is the left, centre or right edge
// depending on style.align. Returns the number of quads added.
int drawText(Renderer2D& r, const Font& font, float x, float y, std::string_view text,
             const TextStyle& style = {});

// ---------------------------------------------------------------------------
// Assets
// ---------------------------------------------------------------------------
struct UiAssets {
    Texture2D font, mainbar, life, weapons, missiles, items, cursor1, cursor2;
    bool fontLoaded = false;
    int missing = 0; // textures that could not be loaded (their draws are skipped)

    // Loads gfx\ui\*.tga and menu\cursor_*.tga from the VFS. Returns false only if the
    // font is missing (nothing can be drawn without it).
    bool load(Vfs& vfs, std::string* error = nullptr);
    Font uiFont() const;
};

// ---------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------
constexpr int kMissileTypes = 5;
constexpr int kPowerupKinds = 4;
constexpr float kFullHealth = 400.0f; // the health bar scale

struct HudPlayer {
    float health = kFullHealth;
    int lives = 3;
    std::int64_t score = 0;          // as shown: level score plus banked campaign score
    int weapon = 0;                  // index into weapons.tga (4 columns of 64x34 cells)
    int weaponLevel = 0;             // upgrade level of the weapon, 0 = none (drawn as pips)
    int missileSelected = 0;
    int missiles[kMissileTypes] = {-1, -1, -1, -1, -1}; // rounds per type, -1 = type not owned
    int powerups[kPowerupKinds] = {0, 0, 0, 0};         // counts, 0 = not owned
    int stars = 0;
};

struct HudState {
    int playerCount = 1; // 1 or 2
    HudPlayer players[2];
    float bossHealth = -1.0f;   // 0..1 fraction, negative = no boss bar
    std::string levelName;      // typewriter text
    float levelTime = -1.0f;    // seconds since the level started; negative = hide the typewriter
    std::string message;        // cheat message and similar
    float messageAge = -1.0f;   // seconds since it was posted; negative or >= 3 = hidden
};

// Visible part and intensity of the level-name typewriter (starts 1 s in, 8 characters per
// second, holds 3 s, fades over 1 s).
struct Typewriter {
    std::string text;
    float alpha = 0;
};
Typewriter typewriterText(std::string_view name, float levelTime);
float messageAlpha(float age); // 1 until 2 s, fades to 0 at 3 s

// Draws the whole HUD (one or two players by state.playerCount) plus the level name and
// message lines. Call between Renderer2D::begin() and flush().
void drawHud(Renderer2D& r, const UiAssets& assets, const HudState& state);

// ---------------------------------------------------------------------------
// Tutorial hint box (ShowTutorialHint, engine-behaviour.md 11.3)
// ---------------------------------------------------------------------------
struct RectF {
    float x = 0, y = 0, w = 0, h = 0;
};
struct HintLayout {
    std::vector<std::string> lines; // at most 16
    RectF box;                      // width max(360, widest + 40), height max(160, 18 n + 80)
    RectF okButton;
    float textTop = 0;              // y of the first line
};
// Lines are split on '^'; a line wider than `maxLineWidth` is word-wrapped.
HintLayout layoutHint(const FontMetrics& m, std::string_view text, float maxLineWidth = 680.0f);
void drawHint(Renderer2D& r, const UiAssets& assets, std::string_view text);
// Greedy word wrap of one paragraph.
std::vector<std::string> wrapText(const FontMetrics& m, std::string_view text, float maxWidth,
                                  float scale = 1.0f);

} // namespace as3d::ui
