// What of the web version (apps/web, docs/spec/issues/150) is testable natively: the fresh
// profile's web key bindings, and the list of known game files the page checks the player's
// files against (sizes and pak headers of the owner's data, when it is there).
#include "doctest.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "as3d/profile.h"
#include "test_data.h"

using namespace as3d;

TEST_CASE("web key bindings: no Ctrl or Alt, Space fires, X missile, C item") {
    Settings s = Settings::defaults();
    applyWebKeyBindings(s);
    const Settings d = Settings::defaults();
    for (int p = 0; p < 2; p++) {
        CHECK(s.keys[p][static_cast<int>(Action::PrimaryAttack)][0] == 32);
        CHECK(s.keys[p][static_cast<int>(Action::MissileAttack)][0] == 'X');
        CHECK(s.keys[p][static_cast<int>(Action::UseItem)][0] == 'C');
        std::set<int> first;
        for (int a = 0; a < kActionCount; a++) {
            for (int k = 0; k < 2; k++) {
                const int code = s.keys[p][a][k];
                CHECK(code != 17); // Ctrl: Ctrl+W would close the tab
                CHECK(code != 18); // Alt: Alt+Left navigates back
                CHECK(code != 'F'); // the full screen key of the page
            }
            // Every action keeps a key, and the joystick slot of the built-in table.
            CHECK(s.keys[p][a][0] > 0);
            CHECK(s.keys[p][a][1] == d.keys[p][a][1]);
            // Moves, switches: unchanged.
            if (a != static_cast<int>(Action::PrimaryAttack) && a != static_cast<int>(Action::MissileAttack) &&
                a != static_cast<int>(Action::UseItem))
                CHECK(s.keys[p][a][0] == d.keys[p][a][0]);
            first.insert(s.keys[p][a][0]);
        }
        // Only the two "switch" actions share a key in the original's table ('2').
        CHECK(first.size() == kActionCount - 1);
    }
    // Survives the profile round trip and the range check.
    Profile prof;
    prof.settings = s;
    const std::vector<u8> bytes = serializeProfile(prof);
    Profile back;
    REQUIRE(deserializeProfile(bytes.data(), bytes.size(), back));
    for (int a = 0; a < kActionCount; a++)
        for (int k = 0; k < 2; k++) CHECK(back.settings.keys[0][a][k] == s.keys[0][a][k]);
}

TEST_CASE("web: known_files.json matches the owner's game files") {
    const std::string list = std::string(AS3D_REPO_ROOT) + "/apps/web/site/known_files.json";
    std::ifstream in(list);
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string json = ss.str();
    // "name": { ... "size": N, "sha256": "...", "where": "..." }
    const std::regex entry("\"([^\"]+)\": \\{ \"required\": (true|false), \"size\": (\\d+), \"sha256\": \"([0-9a-f]{64})\", "
                           "\"where\": \"([^\"]+)\"");
    int entries = 0, checked = 0, paks = 0;
    const std::string orig = testdata::originalDir() + "/";
    for (auto it = std::sregex_iterator(json.begin(), json.end(), entry); it != std::sregex_iterator(); ++it) {
        const std::smatch& m = *it;
        ++entries;
        const bool required = m[2] == "true";
        if (required) ++paks;
        CHECK(required == (m[1].str().size() == 8 && m[1].str().rfind("pak", 0) == 0));
        std::FILE* f = std::fopen((orig + m[5].str()).c_str(), "rb");
        if (!f) continue;
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        unsigned char head[8] = {};
        const size_t got = std::fread(head, 1, 8, f);
        std::fclose(f);
        CHECK(size == std::stol(m[3].str()));
        if (required) {
            // docs/spec/pak.md: every pak starts with this magic.
            const unsigned char magic[8] = {0x00, 0x00, 0x80, 0x3F, 0x99, 0x99, 0x00, 0x00};
            CHECK(got == 8);
            CHECK(std::equal(head, head + 8, magic));
        }
        ++checked;
    }
    CHECK(entries == 6);
    CHECK(paks == 3);
    if (checked == 0) MESSAGE("no original game files under " << orig << ": sizes not checked");
}
