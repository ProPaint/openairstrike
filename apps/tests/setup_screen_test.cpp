// The setup screen of --import-dir (apps/game/setup_screen.h): its built-in font and its line
// layout. No window is opened.
#include "doctest.h"

#include <set>
#include <string>
#include <vector>

#include "../game/setup_screen.cpp"

using as3d_game::SetupScreen;

namespace {
std::vector<unsigned char> glyph(char c) {
    const unsigned char* g = SetupScreen::glyphRows(c);
    return std::vector<unsigned char>(g, g + 8);
}
} // namespace

TEST_CASE("setup screen font: every printable character has its own glyph, others a box") {
    std::set<std::vector<unsigned char>> seen;
    for (int c = 33; c <= 126; ++c) {
        const std::vector<unsigned char> g = glyph(static_cast<char>(c));
        bool any = false;
        for (unsigned char row : g) {
            CHECK(row < 32); // five columns
            any |= row != 0;
        }
        CHECK_MESSAGE(any, "empty glyph for ", static_cast<char>(c));
        CHECK_MESSAGE(seen.insert(g).second, "glyph shared by ", static_cast<char>(c));
    }
    for (unsigned char row : glyph(' ')) CHECK(row == 0);
    CHECK(glyph('A')[0] == 0x0E); // .###.
    CHECK(glyph('A')[3] == 0x1F); // #####
    CHECK(glyph('\t') == glyph('\x7f'));
    CHECK(glyph('\t')[0] == 0x1F);
}

TEST_CASE("setup screen layout: titles, wrapping at spaces, hanging items") {
    const auto lines = as3d_game::layout("# Title\nshort\none two three four\n- item with words", 10);
    REQUIRE(lines.size() == 7);
    CHECK(lines[0].title);
    CHECK(lines[0].text == "Title");
    CHECK(lines[1].text == "short");
    CHECK(lines[2].text == "one two");
    CHECK(lines[3].text == "three four");
    CHECK(lines[4].text == "- item");
    CHECK(lines[5].text == "  with");
    CHECK(lines[6].text == "  words");
    // A word longer than the line is cut.
    const auto cut = as3d_game::layout("/a/very/long/path/name", 8);
    REQUIRE(cut.size() == 3);
    CHECK(cut[0].text == "/a/very/");
}
