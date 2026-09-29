// `as3d_viewer sheet <pattern|list file> --out file.png [--cols N] [--cell WxH]`: a contact
// sheet of many objects. Patterns use * and ? (case-insensitive), comma separated; a
// pattern naming an existing file is read as one object name per line.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>

#include "as3d/gfx.h"
#include "registry.h"
#include "viewer_scene.h"

namespace {
bool globMatch(const char* p, const char* s) {
    for (; *p; p++, s++) {
        if (*p == '*') {
            while (p[1] == '*') p++;
            if (!p[1]) return true;
            for (; *s; s++) if (globMatch(p + 1, s)) return true;
            return globMatch(p + 1, s);
        }
        if (!*s) return false;
        if (*p != '?' && std::tolower((unsigned char)*p) != std::tolower((unsigned char)*s)) return false;
    }
    return !*s;
}

int run(int argc, char** argv) {
    viewer::ViewOpts opts;
    std::vector<std::string> pos;
    if (!viewer::parseViewOpts(argc, argv, opts, pos)) return 1;
    if (pos.empty() || opts.out.empty()) {
        std::fprintf(stderr, "usage: as3d_viewer sheet <pattern[,pattern]|list file> --out file.png [--cols N] [--cell WxH] "
                             "[--yaw d] [--pitch d] [--dist f] [--night]\n");
        return 1;
    }
    viewer::ViewerContext ctx;
    std::string err;
    if (!ctx.init(opts.cellW, opts.cellH, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }

    std::vector<std::string> patterns;
    std::ifstream f(pos[0]);
    if (f.good()) {
        std::string line;
        while (std::getline(f, line)) {
            while (!line.empty() && std::isspace((unsigned char)line.back())) line.pop_back();
            if (!line.empty() && line[0] != '#') patterns.push_back(line);
        }
    } else {
        size_t start = 0;
        for (;;) {
            size_t c = pos[0].find(',', start);
            patterns.push_back(pos[0].substr(start, c == std::string::npos ? c : c - start));
            if (c == std::string::npos) break;
            start = c + 1;
        }
    }
    std::vector<std::string> names;
    std::set<std::string> seen;
    for (const std::string& pat : patterns)
        for (const as3d::ObjectDef& o : ctx.db.objects())
            if (!o.name.empty() && globMatch(pat.c_str(), o.name.c_str()) && seen.insert(o.name).second) names.push_back(o.name);
    if (names.empty()) { std::fprintf(stderr, "error: no objects match '%s'\n", pos[0].c_str()); return 1; }

    int n = static_cast<int>(names.size());
    int cols = opts.cols > 0 ? opts.cols : std::max(1, static_cast<int>(std::ceil(std::sqrt(n * 1.3))));
    int rows = (n + cols - 1) / cols;
    const int labelH = 14;
    as3d::Image sheet;
    sheet.width = cols * opts.cellW;
    sheet.height = rows * (opts.cellH + labelH);
    sheet.hasAlpha = false;
    sheet.rgba.assign(static_cast<size_t>(sheet.width) * sheet.height * 4, 255);
    for (size_t i = 0; i + 3 < sheet.rgba.size(); i += 4) { sheet.rgba[i] = 24; sheet.rgba[i + 1] = 24; sheet.rgba[i + 2] = 28; }

    const unsigned char white[3] = {235, 235, 235}, red[3] = {255, 70, 70};
    int failures = 0;
    for (int i = 0; i < n; i++) {
        int cx = (i % cols) * opts.cellW, cy = (i / cols) * (opts.cellH + labelH);
        viewer::SceneDesc scene;
        as3d::Image cell;
        std::string e;
        bool ok = viewer::buildObjectScene(ctx, names[i], opts.night, scene, nullptr, e) &&
                  ctx.renderToImage(opts.cellW, opts.cellH, cell, scene, opts, false, e);
        if (ok) {
            for (int y = 0; y < opts.cellH; y++)
                for (int x = 0; x < opts.cellW; x++) {
                    size_t s = (static_cast<size_t>(y) * cell.width + x) * 4;
                    size_t d = (static_cast<size_t>(cy + y) * sheet.width + cx + x) * 4;
                    for (int k = 0; k < 4; k++) sheet.rgba[d + k] = cell.rgba[s + k];
                }
        } else {
            failures++;
            std::fprintf(stderr, "sheet: %s: %s\n", names[i].c_str(), e.c_str());
            for (int y = 0; y < opts.cellH; y++)
                for (int x = 0; x < opts.cellW; x++) {
                    size_t d = (static_cast<size_t>(cy + y) * sheet.width + cx + x) * 4;
                    bool diag = std::abs(x * opts.cellH - y * opts.cellW) < opts.cellW * 2 ||
                                std::abs((opts.cellW - x) * opts.cellH - y * opts.cellW) < opts.cellW * 2;
                    sheet.rgba[d] = diag ? 200 : 60;
                    sheet.rgba[d + 1] = diag ? 40 : 30;
                    sheet.rgba[d + 2] = diag ? 40 : 30;
                }
        }
        std::string label = names[i];
        size_t maxChars = static_cast<size_t>(opts.cellW / 6);
        if (label.size() > maxChars) label = label.substr(0, maxChars);
        viewer::drawText(sheet, cx + 2, cy + opts.cellH + 3, label, ok ? white : red, 1);
    }
    if (!as3d::writePng(opts.out.c_str(), sheet)) { std::fprintf(stderr, "error: cannot write %s\n", opts.out.c_str()); return 1; }
    std::printf("wrote %s (%dx%d): %d objects, %d failed\n", opts.out.c_str(), sheet.width, sheet.height, n, failures);
    return 0;
}
} // namespace

AS3D_VIEWER_COMMAND("sheet", "contact sheet of objects matching a pattern or list file", run);
