// 2D layer, fonts and in-game HUD. Spec: docs/spec/frontend.md (authority: section 2.8 for the
// text routines, section 4 for the HUD, section 3.15 for the hint box) and
// docs/spec/render-pipeline.md 8 (2D list, font metrics).
//
// Everything is drawn in the original's virtual 800x600 screen, origin top-left, y down.
// UVs given to Renderer2D::quad follow the engine convention of gfx.h: v = 0 is the TOP of the
// texture, so a quad with (s0,t0)-(s1,t1) shows the texture upright. The specs write UVs the
// other way up (t = 1 is the top row); Renderer2D::quadSpec takes them as written there.
//
// Renderer2D collects quads on the CPU (begin/quad/rect/..., testable without GL) and
// draws them in submission order on flush() (needs a current GLES 3.0 context).
// HudState has no dependency on the game world; the game loop fills it every frame.
#pragma once

#include <cstdint>
#include <map>
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
// The original's packed colours, 0xAABBGGRR.
inline Color packed(u32 v) {
    return {static_cast<float>(v & 0xFF) / 255.0f, static_cast<float>((v >> 8) & 0xFF) / 255.0f,
            static_cast<float>((v >> 16) & 0xFF) / 255.0f, static_cast<float>((v >> 24) & 0xFF) / 255.0f};
}
// Named colours of frontend.md (conventions paragraph).
inline Color orange() { return packed(0xFF00A0FFu); }
inline Color rust() { return packed(0xFF0030C0u); }
// Pulse(f, phi) of frontend.md: grey level 0.5 + 0.5 sin(f pi mt - phi), alpha 1.
Color pulse(float f, float phi, float mt);

// 2D blend modes (render-pipeline.md 4.1, frontend.md conventions): Alpha =
// SRC_ALPHA/ONE_MINUS_SRC_ALPHA, Add = ONE/ONE (the alpha of the colour scales the intensity),
// Filter = DST_COLOR/ZERO, Opaque = no blending (the original's mode 0).
enum class Blend { Alpha, Add, Filter, Opaque };

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
    // Framebuffer pixels (origin top-left) covered by the virtual 800x600 screen, rounded and
    // clipped to the framebuffer: the play-field, the area of the 4:3 screen mode.
    void fieldPixels(int& x, int& y, int& w, int& h) const;
};
Mapping computeMapping(int fbWidth, int fbHeight);

// ---------------------------------------------------------------------------
// Quad list
// ---------------------------------------------------------------------------
// Ours (the touch controls): a quad can also be the disc or ring inscribed in it, with
// anti-aliased edges computed per pixel (no texture needed).
enum class QuadShape : u8 { Rect, Ellipse };

struct Quad {
    float x = 0, y = 0, w = 0, h = 0;      // virtual pixels
    float s0 = 0, t0 = 0, s1 = 1, t1 = 1;  // (s0,t0) at the top-left corner
    const Texture2D* texture = nullptr;    // null = untextured (white)
    Color color;
    Blend blend = Blend::Alpha;
    bool line = false;                     // segment (x,y)-(x+w,y+h), one framebuffer pixel thick
    QuadShape shape = QuadShape::Rect;     // Ellipse: only the inscribed ellipse is drawn
    float inner = 0;                       // Ellipse: hole radius as a fraction of the radius (a ring)
    float feather = 0;                     // Ellipse: extra edge softness, fraction of the radius
    // The sequels' 2D list (as2/render-pipeline.delta.md 8.2), unused by the first game:
    // a rotation in degrees about the quad's centre (positive turns clockwise on screen, y
    // being down: GUESS, the delta gives no sign), and a second texture with its own
    // coordinates combined with the first stage (texture × colour) by `combine2`:
    // 1 blend by the second texture's alpha, 2 add, 3 modulate, any other value replaces.
    // No second texture when texture2 is null.
    float rotation = 0;
    const Texture2D* texture2 = nullptr;
    float s0b = 0, t0b = 0, s1b = 1, t1b = 1; // second texture, (s0b, t0b) at the top-left
    int combine2 = 0;
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
    // Same, with UVs as the specs write them (t = 1 is the top row of the image, the quad's
    // top-left corner samples (s0, t1)). Swapping s0/s1 or t0/t1 mirrors the picture.
    bool quadSpec(float x, float y, float w, float h, float s0, float t0, float s1, float t1,
                  const Texture2D* tex, Color c, Blend blend);
    // Whole texture at one texel per virtual pixel ("draw pic").
    bool pic(float x, float y, const Texture2D& tex, Color c, Blend blend);
    bool rect(float x, float y, float w, float h, Color c, Blend blend = Blend::Alpha);
    bool outline(float x, float y, float w, float h, Color c, Blend blend = Blend::Alpha);
    bool line(float x0, float y0, float x1, float y1, Color c, Blend blend = Blend::Alpha);
    // Covers the whole framebuffer, including the bars outside the 4:3 field.
    bool fullscreen(Color c, Blend blend = Blend::Alpha);
    // Ours: a filled disc and a ring centred on (cx, cy), anti-aliased. `radius` is in virtual
    // pixels measured vertically; the shape stays round on screen whatever the mapping (the
    // stretched 5:4 to 4:3 case too). `thickness` is the ring's width inwards from `radius`;
    // `feather` (virtual pixels) softens the edges further. Alpha and Add blending.
    bool circle(float cx, float cy, float radius, Color c, Blend blend = Blend::Alpha, float feather = 0);
    bool ring(float cx, float cy, float radius, float thickness, Color c, Blend blend = Blend::Alpha,
              float feather = 0);

    const std::vector<Quad>& quads() const { return quads_; }
    int dropped() const { return dropped_; }

    // The sequels shift every quad by -0.5 virtual pixel in x and y before drawing (Direct3D
    // pixel centres, as2/render-pipeline.delta.md 8.2; invisible at 800x600). Off by default:
    // the first game's layer is unchanged.
    void setHalfPixelShift(bool on) { halfPixelShift_ = on; }
    bool halfPixelShift() const { return halfPixelShift_; }
    // Framebuffer corners of a quad as flush() draws it, in the order top-left, bottom-left,
    // bottom-right, top-right (lines: the segment's two sides); for tests.
    void quadCorners(const Quad& q, float px[4], float py[4]) const;

    // Draws the list into the currently bound framebuffer (viewport = whole framebuffer).
    // Returns the number of draw calls (batches of equal texture and blend mode).
    int flush();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Mapping mapping_;
    std::vector<Quad> quads_;
    int dropped_ = 0;
    bool halfPixelShift_ = false;
};

// ---------------------------------------------------------------------------
// Font (render-pipeline.md 8.3, frontend.md 2.8)
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
// True if byte c has a textured glyph (below drawableLimit, non-zero advance, not a space).
bool glyphDrawable(const FontMetrics& m, unsigned char c);

enum class Align { Left, Center, Right };

// The two string routines of frontend.md 2.8.
//   Additive: font.tga, blend ADD, the colour's alpha scales the intensity (the original forces
//             alpha to 1; callers wanting a fade pass the grey level instead). Markup allowed.
//             Bytes >= 0x80 draw a solid rectangle in the text colour (the original's missing
//             font_rus.tga), advancing by the table.
//   Alpha:    font_alpha.tga, blend ALPHA, the colour's alpha is honoured; no markup; bytes
//             >= 0x80 draw nothing. Used for black text and shadows.
enum class FontKind { Additive, Alpha };

struct TextStyle {
    float scale = 1.0f;
    Color color;
    Align align = Align::Left;
    bool markup = false;      // '{' switches to white, '}' back to `color`; neither is drawn
    FontKind kind = FontKind::Additive;
};

// Width of the string in virtual pixels (sum of advances). With markup the braces are not
// counted (the original counts them, 6 px each; frontend.md 8 question 4: we centre exactly).
float measureText(const FontMetrics& m, std::string_view text, float scale = 1.0f, bool markup = false);

struct Font {
    const FontMetrics* metrics = nullptr;
    const Texture2D* texture = nullptr;      // gfx\ui\font.tga
    const Texture2D* alphaTexture = nullptr; // gfx\ui\font_alpha.tga
};
// Draws one line; (x, y) is the top of the glyph boxes, x is the left, centre or right edge
// depending on style.align. Returns the number of quads added.
int drawText(Renderer2D& r, const Font& font, float x, float y, std::string_view text,
             const TextStyle& style = {});
// Text with the original's black alpha-font shadow at (+2, +2) drawn first.
int drawTextShadowed(Renderer2D& r, const Font& font, float x, float y, std::string_view text,
                     const TextStyle& style, float shadowAlpha = 1.0f);

// The number routine (0x4258f0): whole 32x16 cells of font.tga, quad 32s x 16s, pen advance
// 14s, x truncated after each glyph, blend ADD, left-aligned. Returns the quads added.
int drawNumber(Renderer2D& r, const Font& font, float x, float y, std::string_view digits,
               float scale, Color c);
// Width drawNumber advances over `digits` (14 s per character, truncated per glyph).
float numberWidth(std::string_view digits, float scale);

// ---------------------------------------------------------------------------
// Assets
// ---------------------------------------------------------------------------
struct UiAssets {
    Texture2D font, fontAlpha, mainbar, life, weapons, missiles, items, cursor1, cursor2, mcCursor;
    bool fontLoaded = false;
    int missing = 0; // textures that could not be loaded (their draws are skipped)

    // Loads gfx\ui\*.tga, gfx\mc_cur.tga and menu\cursor_*.tga from the VFS and keeps the VFS
    // for texture() below. Returns false only if the font is missing (nothing can be drawn
    // without it). The VFS must outlive this object.
    bool load(Vfs& vfs, std::string* error = nullptr);
    Font uiFont() const;

    // Any other picture by game path (e.g. "menu\\mmenu_1.tga"), loaded on first use and
    // cached; null if it cannot be loaded or no VFS was given (CPU-only tests). The first
    // call for a path needs a current GL context.
    const Texture2D* texture(std::string_view path) const;

private:
    Vfs* vfs_ = nullptr;
    mutable std::map<std::string, std::unique_ptr<Texture2D>> cache_;
};

// ---------------------------------------------------------------------------
// HUD (frontend.md 4)
// ---------------------------------------------------------------------------
constexpr int kMissileTypes = 5;
constexpr int kPowerupSlots = 16;
constexpr int kPowerupIconKinds = 4;  // slots 4..15 have a frame but no icon
constexpr int kWeaponSlots = 20;      // entries 10..19 of the icon table have no picture
constexpr float kFullHealth = 400.0f; // the health bar scale

struct HudPlayer {
    float health = kFullHealth;       // entity health; the fill is health / 400 clamped to [0, 1]
    int lives = 2;                    // p_lives; at most 5 icons are drawn
    std::int64_t score = 0;           // as shown: ftol(p_scores) + banked score
    int weapon = 0;                   // p_weapon, index into the icon table (4.4)
    int upgrades[kWeaponSlots] = {};  // the weapon table (0 = not owned); only the touch overlay's next-weapon preview reads it
    int missiles[kMissileTypes] = {}; // rounds per type; types with 0 are not shown
    int missileSelected = 0;          // type index
    int powerups[kPowerupSlots] = {}; // count per slot (slot = kind); 0 = not shown
    int powerupSelected = 0;          // slot index
};

struct HudState {
    int playerCount = 1; // 1 or 2
    HudPlayer players[2];
    std::string levelName;      // typewriter text (the level's `name`)
    float levelTime = -1.0f;    // the level clock (stops while paused); typewriter tau = clock - 1
    std::string message;        // cheat message
    float messageAge = -1.0f;   // seconds since it was posted; negative or >= 3 = hidden
    bool mouseCursor = false;   // MouseControl = 1 and no menu cursor: draw gfx\mc_cur.tga
    float mouseX = 0, mouseY = 0;
};

// Icon UVs (spec convention, t = 1 at the top) of the HUD atlases, frontend.md 4.4.
struct SpecUv {
    float s0 = 0, t0 = 0, s1 = 0, t1 = 0;
    bool empty() const { return s0 == s1 || t0 == t1; }
};
SpecUv weaponIconUv(int weapon);   // empty for 10..19 and out of range
SpecUv missileIconUv(int type);    // 0..4
SpecUv powerupIconUv(int kind);    // 0..3

// Visible part and grey level of the level-name typewriter (frontend.md 4.5): starts 1 s in,
// 8 characters per second, holds 3 s, fades over 1 s.
struct Typewriter {
    std::string text;
    float alpha = 0;
    int shown = 0; // characters shown (0 = nothing)
};
Typewriter typewriterText(std::string_view name, float levelTime);
// True if `sounds\type.wav` should play when the level clock moves from tPrev to tNow: the
// number of shown characters changed and the newest one is not a space (the first character
// makes no sound).
bool typewriterTypes(std::string_view name, float tPrev, float tNow);
float messageAlpha(float age); // 1 until 2 s, fades to 0 at 3 s

// Draws the whole HUD (one or two players by state.playerCount) plus the level name, message
// line and mouse-control cursor. Call between Renderer2D::begin() and flush().
void drawHud(Renderer2D& r, const UiAssets& assets, const HudState& state);

// ---------------------------------------------------------------------------
// Tutorial hint box (frontend.md 3.15). The interactive box is a front-end screen (menu.h);
// these functions hold its layout and drawing so the HUD viewer can show it too.
// ---------------------------------------------------------------------------
struct RectF {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
struct HintLayout {
    std::vector<std::string> lines; // split at '^' only, at most 16, each at most 63 bytes
    RectF box;                      // width max(360, widest + 40), height max(160, 18 n + 80)
    RectF okButton;                 // (350, top + H - 60, 100, 64)
    float textTop = 0;              // y of the first line (top + 20)
};
constexpr float kHintOpenSeconds = 0.3f;
HintLayout layoutHint(const FontMetrics& m, std::string_view text);
// The panel and text at box time u (seconds since it opened): while u < 0.3 only the growing
// fill is drawn. Returns true once the opening animation is over.
bool drawHintPanel(Renderer2D& r, const UiAssets& assets, const HintLayout& layout, float boxTime);
// The OK picture of the box (right 100 texels of menu\apply_ok_1.tga), highlighted with
// apply_ok_2.tga in Pulse(2, 0) when focused.
void drawHintOk(Renderer2D& r, const UiAssets& assets, const HintLayout& layout, bool focused, float mt);
// Fully opened box with its OK button (for previews).
void drawHint(Renderer2D& r, const UiAssets& assets, std::string_view text);

} // namespace as3d::ui
