// The setup screen (setup_screen.h).
#include "setup_screen.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace as3d_game {

namespace {

// The built-in font: printable ASCII (32 to 126), 5 columns by 8 rows, row 6 the baseline and
// row 7 the descenders. Drawn for this file ('#' set, '.' clear), no font file is used.
constexpr int kGlyphW = 5, kGlyphH = 8, kCellW = 6, kCellH = 10;
const char* const kGlyphs[95] = {
    /*   */ "....." "....." "....." "....." "....." "....." "....." ".....",
    /* ! */ "..#.." "..#.." "..#.." "..#.." "..#.." "....." "..#.." ".....",
    /* " */ ".#.#." ".#.#." "....." "....." "....." "....." "....." ".....",
    /* # */ ".#.#." ".#.#." "#####" ".#.#." "#####" ".#.#." ".#.#." ".....",
    /* $ */ "..#.." ".####" "#.#.." ".###." "..#.#" "####." "..#.." ".....",
    /* % */ "##..." "##..#" "...#." "..#.." ".#..." "#..##" "...##" ".....",
    /* & */ ".##.." "#..#." "#.#.." ".#..." "#.#.#" "#..#." ".##.#" ".....",
    /* ' */ "..#.." "..#.." "....." "....." "....." "....." "....." ".....",
    /* ( */ "...#." "..#.." ".#..." ".#..." ".#..." "..#.." "...#." ".....",
    /* ) */ ".#..." "..#.." "...#." "...#." "...#." "..#.." ".#..." ".....",
    /* * */ "....." "..#.." "#.#.#" ".###." "#.#.#" "..#.." "....." ".....",
    /* + */ "....." "..#.." "..#.." "#####" "..#.." "..#.." "....." ".....",
    /* , */ "....." "....." "....." "....." "....." "..#.." "..#.." ".#...",
    /* - */ "....." "....." "....." "#####" "....." "....." "....." ".....",
    /* . */ "....." "....." "....." "....." "....." "....." "..#.." ".....",
    /* / */ "....." "....#" "...#." "..#.." ".#..." "#...." "....." ".....",
    /* 0 */ ".###." "#...#" "#..##" "#.#.#" "##..#" "#...#" ".###." ".....",
    /* 1 */ "..#.." ".##.." "..#.." "..#.." "..#.." "..#.." ".###." ".....",
    /* 2 */ ".###." "#...#" "....#" "...#." "..#.." ".#..." "#####" ".....",
    /* 3 */ "#####" "...#." "..#.." "...#." "....#" "#...#" ".###." ".....",
    /* 4 */ "...#." "..##." ".#.#." "#..#." "#####" "...#." "...#." ".....",
    /* 5 */ "#####" "#...." "####." "....#" "....#" "#...#" ".###." ".....",
    /* 6 */ "..##." ".#..." "#...." "####." "#...#" "#...#" ".###." ".....",
    /* 7 */ "#####" "....#" "...#." "..#.." ".#..." ".#..." ".#..." ".....",
    /* 8 */ ".###." "#...#" "#...#" ".###." "#...#" "#...#" ".###." ".....",
    /* 9 */ ".###." "#...#" "#...#" ".####" "....#" "...#." ".##.." ".....",
    /* : */ "....." "....." "..#.." "....." "....." "..#.." "....." ".....",
    /* ; */ "....." "....." "..#.." "....." "....." "..#.." "..#.." ".#...",
    /* < */ "...#." "..#.." ".#..." "#...." ".#..." "..#.." "...#." ".....",
    /* = */ "....." "....." "#####" "....." "#####" "....." "....." ".....",
    /* > */ ".#..." "..#.." "...#." "....#" "...#." "..#.." ".#..." ".....",
    /* ? */ ".###." "#...#" "....#" "...#." "..#.." "....." "..#.." ".....",
    /* @ */ ".###." "#...#" "....#" ".##.#" "#.#.#" "#.#.#" ".###." ".....",
    /* A */ ".###." "#...#" "#...#" "#####" "#...#" "#...#" "#...#" ".....",
    /* B */ "####." "#...#" "#...#" "####." "#...#" "#...#" "####." ".....",
    /* C */ ".###." "#...#" "#...." "#...." "#...." "#...#" ".###." ".....",
    /* D */ "###.." "#..#." "#...#" "#...#" "#...#" "#..#." "###.." ".....",
    /* E */ "#####" "#...." "#...." "####." "#...." "#...." "#####" ".....",
    /* F */ "#####" "#...." "#...." "####." "#...." "#...." "#...." ".....",
    /* G */ ".###." "#...#" "#...." "#.###" "#...#" "#...#" ".####" ".....",
    /* H */ "#...#" "#...#" "#...#" "#####" "#...#" "#...#" "#...#" ".....",
    /* I */ ".###." "..#.." "..#.." "..#.." "..#.." "..#.." ".###." ".....",
    /* J */ "..###" "...#." "...#." "...#." "...#." "#..#." ".##.." ".....",
    /* K */ "#...#" "#..#." "#.#.." "##..." "#.#.." "#..#." "#...#" ".....",
    /* L */ "#...." "#...." "#...." "#...." "#...." "#...." "#####" ".....",
    /* M */ "#...#" "##.##" "#.#.#" "#.#.#" "#...#" "#...#" "#...#" ".....",
    /* N */ "#...#" "#...#" "##..#" "#.#.#" "#..##" "#...#" "#...#" ".....",
    /* O */ ".###." "#...#" "#...#" "#...#" "#...#" "#...#" ".###." ".....",
    /* P */ "####." "#...#" "#...#" "####." "#...." "#...." "#...." ".....",
    /* Q */ ".###." "#...#" "#...#" "#...#" "#.#.#" "#..#." ".##.#" ".....",
    /* R */ "####." "#...#" "#...#" "####." "#.#.." "#..#." "#...#" ".....",
    /* S */ ".####" "#...." "#...." ".###." "....#" "....#" "####." ".....",
    /* T */ "#####" "..#.." "..#.." "..#.." "..#.." "..#.." "..#.." ".....",
    /* U */ "#...#" "#...#" "#...#" "#...#" "#...#" "#...#" ".###." ".....",
    /* V */ "#...#" "#...#" "#...#" "#...#" "#...#" ".#.#." "..#.." ".....",
    /* W */ "#...#" "#...#" "#...#" "#.#.#" "#.#.#" "#.#.#" ".#.#." ".....",
    /* X */ "#...#" "#...#" ".#.#." "..#.." ".#.#." "#...#" "#...#" ".....",
    /* Y */ "#...#" "#...#" ".#.#." "..#.." "..#.." "..#.." "..#.." ".....",
    /* Z */ "#####" "....#" "...#." "..#.." ".#..." "#...." "#####" ".....",
    /* [ */ ".###." ".#..." ".#..." ".#..." ".#..." ".#..." ".###." ".....",
    /* \ */ "....." "#...." ".#..." "..#.." "...#." "....#" "....." ".....",
    /* ] */ ".###." "...#." "...#." "...#." "...#." "...#." ".###." ".....",
    /* ^ */ "..#.." ".#.#." "#...#" "....." "....." "....." "....." ".....",
    /* _ */ "....." "....." "....." "....." "....." "....." "....." "#####",
    /* ` */ ".#..." "..#.." "....." "....." "....." "....." "....." ".....",
    /* a */ "....." "....." ".###." "....#" ".####" "#...#" ".####" ".....",
    /* b */ "#...." "#...." "####." "#...#" "#...#" "#...#" "####." ".....",
    /* c */ "....." "....." ".###." "#...." "#...." "#...#" ".###." ".....",
    /* d */ "....#" "....#" ".####" "#...#" "#...#" "#...#" ".####" ".....",
    /* e */ "....." "....." ".###." "#...#" "#####" "#...." ".###." ".....",
    /* f */ "..##." ".#..#" ".#..." "###.." ".#..." ".#..." ".#..." ".....",
    /* g */ "....." "....." ".####" "#...#" "#...#" ".####" "....#" ".###.",
    /* h */ "#...." "#...." "#.##." "##..#" "#...#" "#...#" "#...#" ".....",
    /* i */ "..#.." "....." ".##.." "..#.." "..#.." "..#.." ".###." ".....",
    /* j */ "...#." "....." "..##." "...#." "...#." "...#." "#..#." ".##..",
    /* k */ "#...." "#...." "#..#." "#.#.." "##..." "#.#.." "#..#." ".....",
    /* l */ ".##.." "..#.." "..#.." "..#.." "..#.." "..#.." ".###." ".....",
    /* m */ "....." "....." "##.#." "#.#.#" "#.#.#" "#...#" "#...#" ".....",
    /* n */ "....." "....." "#.##." "##..#" "#...#" "#...#" "#...#" ".....",
    /* o */ "....." "....." ".###." "#...#" "#...#" "#...#" ".###." ".....",
    /* p */ "....." "....." "####." "#...#" "#...#" "####." "#...." "#....",
    /* q */ "....." "....." ".####" "#...#" "#...#" ".####" "....#" "....#",
    /* r */ "....." "....." "#.##." "##..#" "#...." "#...." "#...." ".....",
    /* s */ "....." "....." ".###." "#...." ".###." "....#" "####." ".....",
    /* t */ ".#..." ".#..." "###.." ".#..." ".#..." ".#..#" "..##." ".....",
    /* u */ "....." "....." "#...#" "#...#" "#...#" "#..##" ".##.#" ".....",
    /* v */ "....." "....." "#...#" "#...#" "#...#" ".#.#." "..#.." ".....",
    /* w */ "....." "....." "#...#" "#...#" "#.#.#" "#.#.#" ".#.#." ".....",
    /* x */ "....." "....." "#...#" ".#.#." "..#.." ".#.#." "#...#" ".....",
    /* y */ "....." "....." "#...#" "#...#" "#...#" ".####" "....#" ".###.",
    /* z */ "....." "....." "#####" "...#." "..#.." ".#..." "#####" ".....",
    /* { */ "...#." "..#.." "..#.." ".#..." "..#.." "..#.." "...#." ".....",
    /* | */ "..#.." "..#.." "..#.." "..#.." "..#.." "..#.." "..#.." ".....",
    /* } */ ".#..." "..#.." "..#.." "...#." "..#.." "..#.." ".#..." ".....",
    /* ~ */ "....." "....." ".#..." "#.#.#" "...#." "....." "....." ".....",
};
constexpr const char* kBox = "#####" "#...#" "#...#" "#...#" "#...#" "#...#" "#####" ".....";

using Rows = std::array<unsigned char, kGlyphH>;

Rows rowsOf(const char* g) {
    Rows r{};
    for (int y = 0; y < kGlyphH; ++y)
        for (int x = 0; x < kGlyphW; ++x)
            if (g[y * kGlyphW + x] == '#') r[y] |= static_cast<unsigned char>(1u << (kGlyphW - 1 - x));
    return r;
}

const std::vector<Rows>& font() {
    static const std::vector<Rows> f = [] {
        std::vector<Rows> v;
        for (const char* g : kGlyphs) v.push_back(rowsOf(g));
        v.push_back(rowsOf(kBox));
        return v;
    }();
    return f;
}

struct Line {
    std::string text;
    bool title = false;
};

// The text as lines of at most `cols` characters: split at '\n', "# " marks a title, long
// lines wrap at the last space (or hard at `cols`), the continuation keeps the indentation.
std::vector<Line> layout(const std::string& text, int cols) {
    std::vector<Line> out;
    size_t start = 0;
    cols = std::max(cols, 8);
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string s = text.substr(start, end - start);
        Line l;
        if (s.rfind("# ", 0) == 0) {
            l.title = true;
            s.erase(0, 2);
        }
        const size_t lead = s.find_first_not_of(' ');
        // A "- " item hangs: its continuation lines start under its text.
        const size_t hang = lead == std::string::npos ? 0 : lead + (s.compare(lead, 2, "- ") == 0 ? 2 : 0);
        const std::string pad(std::min(hang, static_cast<size_t>(cols / 2)), ' ');
        do {
            std::string piece = s;
            if (static_cast<int>(s.size()) > cols) {
                size_t cut = s.rfind(' ', static_cast<size_t>(cols));
                if (cut == std::string::npos || cut <= pad.size()) cut = static_cast<size_t>(cols);
                piece = s.substr(0, cut);
                s = pad + s.substr(cut + (s[cut] == ' ' ? 1 : 0));
            } else {
                s.clear();
            }
            out.push_back({piece, l.title});
        } while (!s.empty() && s.find_first_not_of(' ') != std::string::npos);
        if (end == text.size()) break;
        start = end + 1;
    }
    return out;
}

} // namespace

const unsigned char* SetupScreen::glyphRows(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    const size_t i = (u >= 32 && u <= 126) ? u - 32u : 95u;
    return font()[i].data();
}

bool SetupScreen::open() {
    if (opened_) return true;
    if (failed_) return false;
    failed_ = true;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "as3d_game: setup screen: %s\n", SDL_GetError());
        return false;
    }
    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
    if (fullscreen_) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    window_ = SDL_CreateWindow("OpenAirStrike", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width_, height_, flags);
    if (window_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_PRESENTVSYNC);
    if (window_ && !renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) {
        std::fprintf(stderr, "as3d_game: setup screen: %s\n", SDL_GetError());
        if (window_) SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
        return false;
    }
    failed_ = false;
    opened_ = true;
    return true;
}

void SetupScreen::close() {
    if (!opened_) return;
    SDL_DestroyRenderer(renderer_);
    SDL_DestroyWindow(window_);
    renderer_ = nullptr;
    window_ = nullptr;
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    opened_ = false;
}

void SetupScreen::draw(const std::string& text) {
    int w = 0, h = 0;
    if (SDL_GetRendererOutputSize(renderer_, &w, &h) != 0 || w <= 0 || h <= 0) SDL_GetWindowSize(window_, &w, &h);
    // The scale: 50 columns where they fit at two pixels per font pixel or more (a 1280x800
    // Deck: 4), one pixel on the smallest screens (480x320: 76 columns).
    const int margin = std::max(4, w / 40);
    int scale = std::max(1, std::min((w - 2 * margin) / (kCellW * 50), h / (kCellH * 24)));
    const int cols = (w - 2 * margin) / (kCellW * scale);
    const std::vector<Line> lines = layout(text, cols);
    // Fewer pixels per font pixel when the text would not fit in height.
    while (scale > 1 && static_cast<int>(lines.size()) * kCellH * scale > h - 2 * margin) --scale;
    SDL_SetRenderDrawColor(renderer_, 16, 20, 28, 255);
    SDL_RenderClear(renderer_);
    std::vector<SDL_Rect> rects;
    int y = margin;
    for (const Line& l : lines) {
        rects.clear();
        int x = margin;
        for (char c : l.text) {
            const unsigned char* g = glyphRows(c);
            for (int gy = 0; gy < kGlyphH; ++gy)
                for (int gx = 0; gx < kGlyphW; ++gx) {
                    if (!(g[gy] & (1u << (kGlyphW - 1 - gx)))) continue;
                    // Runs of set pixels as one rectangle.
                    int run = 1;
                    while (gx + run < kGlyphW && (g[gy] & (1u << (kGlyphW - 1 - gx - run)))) ++run;
                    rects.push_back(SDL_Rect{x + gx * scale, y + gy * scale, run * scale, scale});
                    gx += run - 1;
                }
            x += kCellW * scale;
        }
        if (l.title) SDL_SetRenderDrawColor(renderer_, 255, 170, 60, 255);
        else SDL_SetRenderDrawColor(renderer_, 225, 228, 235, 255);
        if (!rects.empty()) SDL_RenderFillRects(renderer_, rects.data(), static_cast<int>(rects.size()));
        y += kCellH * scale;
        if (y > h) break;
    }
    // $AS3D_SETUP_SHOT=FILE.bmp (tests): the screen is saved there and
    // waitForPress returns at once, as if a button was pressed.
    if (const char* shot = std::getenv("AS3D_SETUP_SHOT"); shot && *shot) {
        if (SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888)) {
            if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) SDL_SaveBMP(s, shot);
            SDL_FreeSurface(s);
        }
    }
    SDL_RenderPresent(renderer_);
}

bool SetupScreen::show(const std::string& text) {
    if (!open()) return false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
    }
    draw(text);
    return true;
}

bool SetupScreen::waitForPress(const std::string& text) {
    if (!open()) return false;
    if (const char* shot = std::getenv("AS3D_SETUP_SHOT"); shot && *shot) {
        draw(text);
        return true;
    }
    const Uint32 start = SDL_GetTicks();
    std::vector<SDL_GameController*> pads;
    bool result = false, done = false;
    while (!done) {
        draw(text);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            const bool early = SDL_GetTicks() - start < 500;
            switch (e.type) {
            case SDL_QUIT: done = true; break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_CLOSE) done = true;
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (SDL_GameController* c = SDL_GameControllerOpen(e.cdevice.which)) pads.push_back(c);
                break;
            case SDL_KEYDOWN:
                if (!early && !e.key.repeat) done = result = true;
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_MOUSEBUTTONDOWN:
            case SDL_FINGERDOWN:
                if (!early) done = result = true;
                break;
            default: break;
            }
        }
        if (!done) SDL_Delay(30);
    }
    for (SDL_GameController* c : pads) SDL_GameControllerClose(c);
    return result;
}

} // namespace as3d_game
