// Which games are installed under sdmc:/3ds/openairstrike/<key>/ (M5.4). Plain file checks, no
// 3DS API.
#pragma once

#include <string>
#include <vector>

#include "as3d/game_profile.h"

namespace ctrapp {

// One game's files: the paks of its profile, Settings.xml and its texts file (the extracted
// texts the front end needs). Installed = none missing.
struct GameInstall {
    const as3d::GameProfile* game = nullptr;
    std::vector<std::string> missing; // file names that are not there
    int present = 0;                  // how many of the files are there
    bool paksPresent = false;         // every pak is there (the selector can take its font)
    bool complete() const { return missing.empty(); }
    bool any() const { return present > 0; }
};

// `root`: the data directory (no trailing slash). All games of the family, in GameId order,
// whether present or not.
std::vector<GameInstall> detectInstalls(const std::string& root);
// "m1.pak, Settings.xml" for the log and the card.
std::string missingList(const GameInstall& g);

} // namespace ctrapp
