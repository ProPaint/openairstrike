// The setup screen of the desktop game (Steam Deck AppImage, PortMaster): plain text in a window
// of its own, drawn without any game file (an SDL renderer and a built-in 5x8 pixel font), for
// what happens before a game can start: "Importing <file>..." while --import-dir installs the
// player's files, and, when no game is there afterwards, where to put them and what the importer
// said. A device with no keyboard and no terminal (a handheld, the Deck's Game Mode) then shows
// something instead of closing at once.
#pragma once

#include <string>

struct SDL_Window;
struct SDL_Renderer;

namespace as3d_game {

class SetupScreen {
public:
    SetupScreen(bool fullscreen, int width, int height) : fullscreen_(fullscreen), width_(width), height_(height) {}
    ~SetupScreen() { close(); }
    SetupScreen(const SetupScreen&) = delete;
    SetupScreen& operator=(const SetupScreen&) = delete;

    // Draws the text once (opening the window on first use) and returns. Lines starting with
    // "# " are titles (orange, without the mark); long lines are wrapped at spaces. False when
    // no window can be opened (no display): the caller prints the text instead.
    bool show(const std::string& text);
    // Draws the text and waits for a key, a pad button, a click or a touch (presses within the
    // first half second are ignored: the button that started the program). False when the
    // window was closed or cannot be opened.
    bool waitForPress(const std::string& text);
    // Closes the window and the SDL subsystems it started (the game then opens its own).
    void close();

    // The built-in font, for the test: the 8 rows of the glyph of `c` (5 bits each, bit 4 the
    // leftmost column); a box for characters outside printable ASCII.
    static const unsigned char* glyphRows(char c);

private:
    bool open();
    void draw(const std::string& text);

    bool fullscreen_;
    int width_, height_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool opened_ = false, failed_ = false;
};

} // namespace as3d_game
