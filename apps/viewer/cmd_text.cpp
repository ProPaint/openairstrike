// `as3d_viewer text "<string>" --out f.png [--size WxH] [--scale f] [--color r,g,b] [--align l|c|r]
//  [--markup] [--bg r,g,b]`: draws a string with the game font on a dark background, headless.
// '^' in the string starts a new line. Without --size the image is cropped around the text
// (rendered at 800x600, one virtual pixel per pixel).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

#include "as3d/gfx.h"
#include "as3d/image.h"
#include "as3d/platform.h"
#include "as3d/ui.h"
#include "common.h"
#include "registry.h"

namespace viewer {

// Shared with cmd_hud.cpp: renders `draw` into a w x h target on a solid background and reads
// it back. Prints the error and returns false on failure.
bool renderUiHeadless(int w, int h, as3d::ui::Color bg,
                      const std::function<void(as3d::ui::Renderer2D&, const as3d::ui::UiAssets&)>& draw,
                      as3d::Image& out) {
    as3d::Vfs vfs;
    if (!mountGameData(vfs)) { std::fprintf(stderr, "error: no game data (set AS3D_DATA_ROOT)\n"); return false; }
    as3d::GraphicsConfig cfg;
    cfg.headless = true;
    auto gl = as3d::createGraphicsContext(cfg);
    if (!gl) { std::fprintf(stderr, "error: could not create a headless graphics context\n"); return false; }
    gl->makeCurrent();
    std::string err;
    as3d::ui::UiAssets assets;
    if (!assets.load(vfs, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return false; }
    if (assets.missing) std::fprintf(stderr, "warning: %d UI texture(s) missing\n", assets.missing);
    as3d::ui::Renderer2D r2;
    if (!r2.init(&err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return false; }
    as3d::RenderTarget target;
    if (!target.create(w, h, 4)) { std::fprintf(stderr, "error: cannot create %dx%d render target\n", w, h); return false; }
    target.bind();
    as3d::clear({bg.r, bg.g, bg.b, 1.0f}, true);
    r2.begin(w, h);
    draw(r2, assets);
    int quads = static_cast<int>(r2.quads().size());
    int calls = r2.flush();
    std::printf("quads=%d draw calls=%d dropped=%d\n", quads, calls, r2.dropped());
    if (!target.readPixels(out)) { std::fprintf(stderr, "error: readback failed\n"); return false; }
    return true;
}

bool parseTriple(const char* s, float& a, float& b, float& c) {
    return std::sscanf(s, "%f,%f,%f", &a, &b, &c) == 3;
}

} // namespace viewer

namespace {

int run(int argc, char** argv) {
    std::string text, out;
    int w = 0, h = 0;
    float scale = 1.0f;
    as3d::ui::Color color, bg{0.12f, 0.16f, 0.12f, 1};
    as3d::ui::Align align = as3d::ui::Align::Center;
    bool markup = false;
    for (int i = 0; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        const char* v = nullptr;
        if (a == "--out" && (v = val())) out = v;
        else if (a == "--size" && (v = val())) { if (std::sscanf(v, "%dx%d", &w, &h) != 2 || w < 1 || h < 1 || w > 8192 || h > 8192) return 1; }
        else if (a == "--scale" && (v = val())) scale = static_cast<float>(std::atof(v));
        else if (a == "--color" && (v = val())) { if (!viewer::parseTriple(v, color.r, color.g, color.b)) return 1; }
        else if (a == "--bg" && (v = val())) { if (!viewer::parseTriple(v, bg.r, bg.g, bg.b)) return 1; }
        else if (a == "--align" && (v = val())) align = v[0] == 'l' ? as3d::ui::Align::Left : (v[0] == 'r' ? as3d::ui::Align::Right : as3d::ui::Align::Center);
        else if (a == "--markup") markup = true;
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 1; }
        else text = a;
    }
    if (text.empty() || out.empty() || !(scale > 0.0f) || scale > 20.0f) {
        std::fprintf(stderr, "usage: as3d_viewer text \"<string>\" --out f.png [--size WxH] [--scale f] [--color r,g,b] "
                             "[--align l|c|r] [--markup] [--bg r,g,b]   ('^' = new line)\n");
        return 1;
    }
    const bool crop = w == 0;
    if (crop) { w = 800; h = 600; }

    std::vector<std::string> lines;
    for (size_t i = 0;;) {
        size_t j = text.find('^', i);
        lines.push_back(text.substr(i, j == std::string::npos ? std::string::npos : j - i));
        if (j == std::string::npos) break;
        i = j + 1;
    }
    const float lineH = 18.0f * scale;
    float widest = 0;
    for (const auto& l : lines) widest = std::max(widest, as3d::ui::measureText(as3d::ui::FontMetrics::original(), l, scale, markup));
    const float totalH = lineH * static_cast<float>(lines.size());

    as3d::Image img;
    bool ok = viewer::renderUiHeadless(w, h, bg, [&](as3d::ui::Renderer2D& r, const as3d::ui::UiAssets& a) {
        as3d::ui::TextStyle st;
        st.scale = scale;
        st.color = color;
        st.align = align;
        st.markup = markup;
        float x = align == as3d::ui::Align::Left ? 400.0f - widest * 0.5f
                  : align == as3d::ui::Align::Right ? 400.0f + widest * 0.5f : 400.0f;
        float y = 300.0f - totalH * 0.5f;
        for (const auto& l : lines) {
            as3d::ui::drawText(r, a.uiFont(), x, y, l, st);
            y += lineH;
        }
    }, img);
    if (!ok) return 1;
    if (crop) {
        int pad = 20;
        int x0 = std::max(0, static_cast<int>(400.0f - widest * 0.5f) - pad);
        int x1 = std::min(800, static_cast<int>(400.0f + widest * 0.5f) + pad);
        int y0 = std::max(0, static_cast<int>(300.0f - totalH * 0.5f) - pad);
        int y1 = std::min(600, static_cast<int>(300.0f + totalH * 0.5f) + pad);
        as3d::Image c;
        c.width = x1 - x0;
        c.height = y1 - y0;
        c.hasAlpha = true;
        c.rgba.resize(static_cast<size_t>(c.width) * c.height * 4);
        for (int y = 0; y < c.height; y++)
            std::memcpy(&c.rgba[static_cast<size_t>(y) * c.width * 4], &img.rgba[(static_cast<size_t>(y + y0) * img.width + x0) * 4],
                        static_cast<size_t>(c.width) * 4);
        img = std::move(c);
    }
    if (!as3d::writePng(out.c_str(), img)) { std::fprintf(stderr, "error: cannot write %s\n", out.c_str()); return 1; }
    std::printf("wrote %s (%dx%d), text width %.0f\n", out.c_str(), img.width, img.height, widest);
    return 0;
}

} // namespace

AS3D_VIEWER_COMMAND("text", "draw a string with the game font (--scale, --color, --align, --markup)", run);
