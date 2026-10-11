// 2D layer, fonts and in-game HUD. Spec: docs/spec/frontend.md (authority: section 2.8 for the
// text routines, section 4 for the HUD, section 3.15 for the hint box) and
// docs/spec/render-pipeline.md 8 (2D list, font metrics).
//
// Everything is drawn in the original's virtual 800x600 screen, origin top-left, y down.
// UVs given to Renderer2D::quad follow the engine convention of texture.h: v = 0 is the TOP of the
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
#include "as3d/game_profile.h"
#include "as3d/texture.h"
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
// One unit = one framebuffer pixel (scale 1, offset 0): the console layouts' mapping.
Mapping pixelMapping(int fbWidth, int fbHeight);

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
    // The quads are laid out for a target of fbWidth x fbHeight pixels, which flush() draws into
    // the rectangle of the bound framebuffer whose lower-left corner is (originX, originY)
    // (GL convention, y from the bottom): the default is the whole framebuffer. A dual-screen or
    // sub-rectangle host passes the rectangle's size and origin.
    void begin(int fbWidth, int fbHeight, int originX = 0, int originY = 0);
    // The same with the console pixel mapping (pixelMapping below): the quads are laid out in the
    // target's own pixels, scale 1, no 4:3 field (the 3DS console layouts: 320x240 bottom, 400x240
    // top; docs/3ds-ui-design.md section 4).
    void beginPixels(int fbWidth, int fbHeight, int originX = 0, int originY = 0);
    // Ours (the 3DS console screens, R4.8): a list laid out through any mapping `m` (a picture of
    // the virtual 800x600 screen at a chosen scale and offset), and the quads of such a list
    // appended to this one, re-expressed in this list's mapping (the same framebuffer pixels). The
    // two lists' targets are the same size.
    void beginMapped(int fbWidth, int fbHeight, const Mapping& m);
    void appendMapped(const Renderer2D& src);
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
    // A cache boundary for the PICA backend (renderer2d_pica.cpp): the quads added since the last
    // boundary are converted and cached as a unit of their own, so that a change elsewhere in the
    // list does not rebuild them (the 3DS touch band: its tessellated discs stay cached while a
    // count on a button face changes). Drawing order and pixels are unchanged; the GL backend
    // ignores it. begin() clears the boundaries.
    void cacheBreak() {
        if (!quads_.empty() && (breaks_.empty() || breaks_.back() != quads_.size())) breaks_.push_back(quads_.size());
    }
    const std::vector<size_t>& cacheBreaks() const { return breaks_; }

    // The sequels shift every quad by -0.5 virtual pixel in x and y before drawing (Direct3D
    // pixel centres, as2/render-pipeline.delta.md 8.2; invisible at 800x600). Off by default:
    // the first game's layer is unchanged.
    void setHalfPixelShift(bool on) { halfPixelShift_ = on; }
    bool halfPixelShift() const { return halfPixelShift_; }
    // Draw circles and rings as CPU-tessellated triangles with a one-pixel alpha ramp instead of
    // the shader's fwidth anti-aliasing (as3d/batch2d.h; the 3DS backend has no such shader).
    // Off by default.
    void setTessellateShapes(bool on, int segments = 48) { tessellate_ = on; segments_ = segments; }
    bool tessellateShapes() const { return tessellate_; }
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
    int originX_ = 0, originY_ = 0;
    std::vector<Quad> quads_;
    std::vector<size_t> breaks_; // cacheBreak()
    int dropped_ = 0;
    bool halfPixelShift_ = false;
    bool tessellate_ = false;
    int segments_ = 48;
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

// The console text sizes (docs/3ds-ui-design.md "Typography"; 3DS only, see Font::consoleSizes):
// the game's font drawn one atlas texel to one screen pixel, never at a fractional scale.
//   S: font.tga resampled once at load to 0.8 (its own atlas, smallFontAtlas), cap 8 px
//   L: font.tga as is, cap 10 px
//   T: font.tga at 2x nearest, cap 20 px
// Auto picks the size nearest the text's scale on screen (consoleSizeFor). The desktop, Android
// and the web ignore the size (Font::consoleSizes is off there): their text is unchanged.
enum class FontSize : u8 { Auto, S, L, T };
// What is drawn under the glyphs. Auto: an outline for additive T text on a console font (the
// banners drawn over the world), nothing otherwise. Outline: the glyphs in black (the alpha
// font) at the eight one-pixel offsets first; Shadow: once at (+1, +1) pixel. Console fonts only.
enum class TextEffect : u8 { Auto, None, Outline, Shadow };

struct TextStyle {
    float scale = 1.0f;
    Color color;
    Align align = Align::Left;
    bool markup = false;      // '{' switches to white, '}' back to `color`; neither is drawn
    FontKind kind = FontKind::Additive;
    FontSize size = FontSize::Auto; // console fonts only
    TextEffect effect = TextEffect::Auto; // console fonts only
};

// Screen pixels per font texel of each size (0.8, 1, 2; Auto: 1), the cap height in pixels
// (8, 10, 20) and the line pitch the console layouts use (13, 16, 32).
float fontSizeScale(FontSize size);
int fontCapPx(FontSize size);
int fontLinePitchPx(FontSize size);
// The size for a text drawn at `pxPerTexel` screen pixels per font texel: below 0.7 S, below 1.1
// L, else T (today's 3DS body text at 0.4 and the HUD counts at 0.6 become S, items at 0.8 L,
// PAUSED and the titles at 1.2 T).
FontSize consoleSizeFor(float pxPerTexel);
// The pen advance of byte c at a size, whole pixels: L the table's, T twice it, S the table's x 0.8
// rounded (a non-zero advance stays at least 1).
int glyphAdvancePx(const FontMetrics& m, FontSize size, unsigned char c);
// Width in pixels of the string at a size (sum of glyphAdvancePx; markup braces not counted).
int measureTextPx(const FontMetrics& m, std::string_view text, FontSize size, bool markup = false);
// The metrics of the S atlas: the same 8 x 16 grid of 32 x 16 cells in 256 x 256, the glyph the
// left 24 and lower 12 texels of its cell, the advances of glyphAdvancePx(S).
FontMetrics smallFontMetrics(const FontMetrics& m);
// The S atlas from font.tga (`font`: the atlas as loaded, 256 x 256, glyph intensity in RGB):
// every drawable glyph's 30 x 15 texels area-averaged onto 24 x 12 (exactly 0.8, each output
// texel the coverage-weighted mean of the 1.25 x 1.25 texels it covers), contrast x 1.25 so a
// one-texel stroke keeps full intensity, placed as smallFontMetrics says. RGB and alpha both
// hold the value: one LA8 texture serves the additive font (RGB) and the alpha font (A).
Image smallFontAtlas(const Image& font, const FontMetrics& m);

// Width of the string in virtual pixels (sum of advances). With markup the braces are not
// counted (the original counts them, 6 px each; frontend.md 8 question 4: we centre exactly).
float measureText(const FontMetrics& m, std::string_view text, float scale = 1.0f, bool markup = false);

struct Font {
    const FontMetrics* metrics = nullptr;
    const Texture2D* texture = nullptr;      // gfx\ui\font.tga
    const Texture2D* alphaTexture = nullptr; // gfx\ui\font_alpha.tga
    // The console sizes (FontSize): text is drawn at S, L or T in whole screen pixels instead of
    // at its scale. Set by UiAssets::uiFont when UiAssets::consoleText is on (the 3DS).
    bool consoleSizes = false;
    const Texture2D* smallTexture = nullptr;     // the S atlas (smallFontAtlas)
    const FontMetrics* smallMetrics = nullptr;   // smallFontMetrics
};
// Draws one line; (x, y) is the top of the glyph boxes, x is the left, centre or right edge
// depending on style.align. Returns the number of quads added.
// A console font (Font::consoleSizes) draws the size style.size (Auto: consoleSizeFor of the
// scale on screen) in whole pixels of the Renderer2D's mapping: the anchor (left, centre or right
// edge per style.align) and the vertical centre of the glyph box stay where the scaled text
// would have them, the pen starts on a whole pixel and advances by glyphAdvancePx. An Auto size
// that would leave the screen steps down (T to L to S); text still too wide at S is cut at the
// screen edge (glyphs wholly outside are not drawn).
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
// A console font draws the number routine's cells at consoleSizeFor(scale on screen) as
// drawText does, the glyphs' vertical centre kept, the pen advancing by numberAdvancePx (14 px at
// L, 28 at T, 11 at S).
int numberAdvancePx(FontSize size);

// ---------------------------------------------------------------------------
// Key hints (the console layouts' footers: "[A] Select  [B] Back")
// ---------------------------------------------------------------------------
// The pad keys a hint can name.
enum class HintKey : u8 { A, B, X, Y, L, R, ZL, ZR, Start, Select, DPad, Count };
const char* hintKeyName(HintKey k); // "A", "B", ..., "START", "SELECT", "+"
// A swappable picture set for the keys: one small texture and a rectangle per key (texels, the
// texture's v = 0 at the top). No set is built in: the owner chooses the art; a host loads it
// and fills `rects`. A key whose rectangle is empty, or a null texture, falls back to text.
struct KeyGlyphAtlas {
    const Texture2D* texture = nullptr;
    struct Rect {
        int x = 0, y = 0, w = 0, h = 0;
        bool empty() const { return w <= 0 || h <= 0; }
    };
    Rect rects[static_cast<int>(HintKey::Count)];
    const Rect& rect(HintKey k) const { return rects[static_cast<int>(k)]; }
};
struct KeyHintStyle {
    FontSize size = FontSize::S;
    Color keyColor;            // the key's name (text fallback) and frame
    Color labelColor;          // the label after it
    int gap = 4;               // pixels between the key and its label
};
// One hint at (x, y) (pixels of a beginPixels mapping, top of the line): the key's picture from
// `atlas` drawn 1:1 and centred on the line, or (no picture) its name in the font inside a
// one-pixel frame of keyColor; then the label. Returns the hint's width in pixels.
int drawKeyHint(Renderer2D& r, const Font& font, const KeyGlyphAtlas* atlas, int x, int y, HintKey key,
                std::string_view label, const KeyHintStyle& style = {});

// ---------------------------------------------------------------------------
// Half-size pictures (console layouts: a menu picture at exactly 1/2 of its texels)
// ---------------------------------------------------------------------------
// A picture's half-size copy: each texel the mean of a 2 x 2 block (colour weighted by alpha, so
// transparent texels do not darken the edges; odd last rows and columns dropped), padded with
// transparent texels to power-of-two sides of at least 8 (the PICA's texture sizes: no stretch on
// upload). `w` and `h` get the picture's size, the image is the padded one.
Image halfSizePicture(const Image& src, int* w, int* h);
struct HalfPicture {
    const Texture2D* texture = nullptr; // nearest filtering
    int width = 0, height = 0;          // pixels of the picture (half its texels)
    float s1 = 1, t1 = 1;               // the picture's corner in the padded texture
    bool valid() const { return texture && width > 0 && height > 0; }
};
// The picture at one texel per screen pixel of the mapping (beginPixels: its width x height
// pixels), its top-left corner at (x, y).
bool drawHalfPicture(Renderer2D& r, float x, float y, const HalfPicture& p, Color c, Blend blend = Blend::Alpha);

// ---------------------------------------------------------------------------
// Assets
// ---------------------------------------------------------------------------
struct HudLayout;
// Which layout the HUD takes. Main: the 800x600 virtual screen scaled into the target (desktop,
// Android, web). Bottom320x240: the 3DS bottom screen, which holds nothing but the HUD and the
// touch buttons; its tables are laid out for the 0.4 scale of a 320x240 target and keep the lower
// band (kHudBottomBandTop..600) free for the touch pickers (hud_layout.h).
enum class HudTarget { Main, Bottom320x240 };

struct UiAssets {
    // `mainbar` is the HUD layout's bar atlas (mainbar.tga; the sequels' mainbar2.tga);
    // `panel` and `panelNoise` the sequels' hint panel pieces (interface.tga, and snow.tga
    // with repeat wrapping). A texture the game's layout does not name stays invalid.
    Texture2D font, fontAlpha, mainbar, life, weapons, missiles, items, cursor1, cursor2, mcCursor;
    Texture2D panel, panelNoise;
    // The console text (FontSize, docs/3ds-ui-design.md): when on before load(), the S atlas is
    // built from font.tga (fontSmall, LA8 256 x 256, 128 KB, loaded with the font, before any
    // level texture) and the font textures are nearest-filtered; uiFont() then draws the console
    // sizes. Default: consoleTextDefault() (on in the 3DS build, off elsewhere).
    bool consoleText = consoleTextDefault();
    Texture2D fontSmall;
    bool fontLoaded = false;
    int missing = 0; // textures that could not be loaded (their draws are skipped)
    GameId game = GameId::AirStrike3D;
    // The layout variant the HUD and the hint box are drawn with (set by the host around its
    // HUD and 2D-layer draws).
    HudTarget hudTarget = HudTarget::Main;

    // Loads the font, the atlases the game's HUD layout names (as3d/hud_layout.h) and
    // menu\cursor_*.tga from the VFS and keeps the VFS for texture() below. Returns false
    // only if the font is missing (nothing can be drawn without it). The VFS must outlive
    // this object.
    bool load(Vfs& vfs, std::string* error = nullptr, GameId game = GameId::AirStrike3D);
    Font uiFont() const;
    // The HUD layout of the assets' game.
    const HudLayout& hudLayout() const; // of `game` and `hudTarget`

    // Any other picture by game path (e.g. "menu\\mmenu_1.tga"), loaded on first use and
    // cached; null if it cannot be loaded or no VFS was given (CPU-only tests). The first
    // call for a path needs a current GL context.
    const Texture2D* texture(std::string_view path) const;
    // The same with repeat wrapping (a picture sampled over more than one width: the sequels'
    // title-logo clouds and panel noise), cached apart from the clamped one.
    const Texture2D* textureRepeat(std::string_view path) const;
    // Its half-size copy (halfSizePicture), nearest-filtered, made on first use and cached apart;
    // invalid if the picture cannot be loaded. Not made at load: only the console layouts that
    // draw a picture at 1/2 ask for it (about 0.2 MB for the first game's menus).
    HalfPicture textureHalf(std::string_view path) const;
    // The 3DS build's default for consoleText (the PICA backend: true; GL: false), and an override
    // for the hosts (run.ini ui_text=0 on the 3DS: today's scaled text, for comparisons).
    static bool consoleTextDefault();
    static void setConsoleTextDefault(bool on);
    // Level-start precache (as3d/precache.h): loads into the texture() cache the pictures play
    // can show besides the atlases load() takes, so the first hint or level end loads nothing:
    // the first game's hint OK button and its mission complete / game over pictures (the
    // sequels' hint panel is one of load()'s atlases). Returns how many it looked up.
    int precachePlay() const;
    // The pictures of the front end's menus (3DS round 2, P1: 21 menu pictures loaded on first
    // use on the console): every screen the main menu reaches and the in-play ones (in-game menu,
    // its Options, the exit dialog, mission complete, game over, the name entry), normal and
    // focused states. The first game's menu\*.tga; the sequels' atlas, title logo and widget
    // pictures (their comic pages are per mission and stay on first use). Missing files are
    // skipped quietly. Returns how many it looked up.
    int precacheMenus() const;
    // The sounds the menus play (MenuSystem::playSound), for the level-start sound precache.
    static const std::vector<std::string>& menuSounds();

private:
    Vfs* vfs_ = nullptr;
    mutable std::map<std::string, std::unique_ptr<Texture2D>> cache_;
    mutable std::map<std::string, HalfPicture> halfInfo_; // textureHalf: the sizes, by cache key
};

// ---------------------------------------------------------------------------
// HUD (frontend.md 4; the sequels as2/frontend.md 4). What is drawn where comes from the
// HudLayout of the assets' game (as3d/hud_layout.h).
// ---------------------------------------------------------------------------
constexpr int kMissileTypes = 5;
constexpr int kPowerupSlots = 16;
constexpr int kPowerupIconKinds = 4;  // slots 4..15 have a frame but no icon
constexpr int kWeaponSlots = 20;      // entries 10..19 of the icon table have no picture
constexpr float kFullHealth = 400.0f; // the first game's health bar scale

struct HudPlayer {
    float health = kFullHealth;       // entity health; the fill is health / maxHealth clamped to [0, 1]
    float maxHealth = kFullHealth;    // the bar's scale: 400 in the first game, the helicopter's
                                      // maximum health in the sequels (GameRules::healthBarScaleFromMax)
    int lives = 2;                    // p_lives; at most HudLayout::lifeIconsMax icons (5; sequels 10)
    std::int64_t score = 0;           // as shown: ftol(p_scores) + banked score
    int weapon = 0;                   // p_weapon, index into the icon table (4.4)
    int upgrades[kWeaponSlots] = {};  // upgrade level per weapon slot (0 = not owned): the sequels'
                                      // level pips show upgrades[weapon]; the touch overlay's
                                      // next-weapon preview reads it too
    int missiles[kMissileTypes] = {}; // rounds per type; types with 0 are not shown
    int missileSelected = 0;          // type index
    int powerups[kPowerupSlots] = {}; // count per slot (slot = kind); 0 = not shown. The sequels'
                                      // timer power-ups (slots 6, 7, 9) hold the seconds left, set
                                      // by their scripts every frame: a count-down on the HUD
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

// Icon UVs (spec convention, t = 1 at the top) of the HUD atlases of `game` (frontend.md 4.4,
// as2/frontend.md 4.4), from its HudLayout; the atlases are the UiAssets weapons, missiles
// and items loaded for that game. Empty where the game's table has no picture.
struct SpecUv {
    float s0 = 0, t0 = 0, s1 = 0, t1 = 0;
    bool empty() const { return s0 == s1 || t0 == t1; }
};
SpecUv weaponIconUv(int weapon, GameId game = GameId::AirStrike3D); // first game 0..9 (10..19 empty); sequels 0..8
SpecUv missileIconUv(int type, GameId game = GameId::AirStrike3D);  // 0..4
SpecUv powerupIconUv(int kind, GameId game = GameId::AirStrike3D);  // first game 0..3; sequels 0..9

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
// line and mouse-control cursor, in the layout of the assets' game. Call between
// Renderer2D::begin() and flush().
void drawHud(Renderer2D& r, const UiAssets& assets, const HudState& state);

// ---------------------------------------------------------------------------
// Tutorial hint box (frontend.md 3.15; the sequels as2/frontend.md 3.15). The interactive box
// is a front-end screen (menu.h); these functions hold its layout and drawing so the HUD
// viewer can show it too. The look follows the assets' game (HudLayout::hint).
// ---------------------------------------------------------------------------
enum class HintStyle : u8 {
    V170Box,     // frontend.md 3.15: a growing black box and the OK picture
    SequelPanel, // as2/frontend.md 3.15: UI_DrawPanel titled "Tutorial Tip" and a text button
};

struct RectF {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
struct HintLayout {
    std::vector<std::string> lines; // split at '^' only, at most 16, each at most 63 bytes
    RectF box;                      // width max(360, widest + 40), height max(160, 18 n + 80)
    RectF okButton;                 // (350, top + H - 60, 100, 64)
    float scale = 1.0f;             // text and box scale (HudLayout::hintScale; 1 = the original)
    float textTop = 0;              // y of the first line (top + 20)
};
constexpr float kHintOpenSeconds = 0.3f;
// SequelPanel: height max(160, 18 n + 60), the lines centred vertically in it, the Ok button
// ((W + 46) x 30) centred at (400, 520) whatever the box size.
// `scale` (>= 1) enlarges text, line pitch, minimum box and Ok button for small targets; it is
// reduced when the widest line would not fit the 800 virtual pixels.
HintLayout layoutHint(const FontMetrics& m, std::string_view text, HintStyle style = HintStyle::V170Box,
                      float scale = 1.0f);
// The panel and text at box time u (seconds since it opened). First game: while u < 0.3 only
// the growing fill is drawn. Sequels: the panel opens over 0.125 s, the lines show once it is
// open. Returns true once the opening animation is over.
bool drawHintPanel(Renderer2D& r, const UiAssets& assets, const HintLayout& layout, float boxTime);
// The OK picture of the box (right 100 texels of menu\apply_ok_1.tga), highlighted with
// apply_ok_2.tga in Pulse(2, 0) when focused. Sequels: the "  Ok  " text button of
// interface.tga, its caption green (orange when focused).
void drawHintOk(Renderer2D& r, const UiAssets& assets, const HintLayout& layout, bool focused, float mt);
// Fully opened box with its OK button (for previews).
void drawHint(Renderer2D& r, const UiAssets& assets, std::string_view text);

} // namespace as3d::ui
