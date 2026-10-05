// The front-end texts compiled into the player's own game executable (Information pages,
// ranks, the sequels' menus and dialogues): a C++ port of tools/extract_exe_texts.py and of the
// web page's copy (apps/web/site/files.js); keep the three in step. The address table is chosen
// by the executable's SHA-256. The output is the texts file as3d::ui::Texts reads
// (engine/src/ui/frontend_texts.cpp), byte for byte what the Python tool writes. No game data
// is in here: hashes and addresses only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "as3d/game_profile.h"

namespace as3d {

// The game whose executable has this SHA-256 (lower-case hex), or nullptr.
const GameProfile* gameOfExecutable(const std::string& sha256);

// Reads the texts out of `exe` (the whole file), whose digest is `sha256`. False and a message
// for an unknown executable or one whose strings are not where the table says.
bool extractExeTexts(const std::vector<std::uint8_t>& exe, const std::string& sha256, const GameProfile** game,
                     std::string* text, std::string* error);

// The address list of a sequel (as2, gulf) as "key|address|kind" (kind 0 text, 1 text_ml,
// 2 u32), for the test that compares it with tools/exe_texts/<key>.json. Empty for as3d.
std::vector<std::string> exeTextListForTest(const char* key);

} // namespace as3d
