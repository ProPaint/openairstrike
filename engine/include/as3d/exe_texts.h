// The front-end texts compiled into the player's own game executable (Information pages,
// ranks, the sequels' menus and dialogues): a C++ port of tools/extract_exe_texts.py and of the
// web page's copy (apps/web/site/files.js); keep the three in step. Two ways
// (docs/spec/issues/170, section 3):
//   1. by address, for the builds the specs were taken from, chosen by the executable's
//      SHA-256 (the address tables below);
//   2. by hash dictionary, for any other build: tools/exe_texts/<key>.hashes.json, compiled in,
//      maps every key to the SHA-256 and length of its string (a u32 to its value); the file is
//      scanned for NUL-terminated runs of printable cp1251 bytes, each run's hash looked up,
//      and the game is the one with the best share of its string keys found (60 % at least).
//      A key whose string is not found gets no line; the two ways give the same file for the
//      builds the tables know.
// The output is the texts file as3d::ui::Texts reads (engine/src/ui/frontend_texts.cpp), byte
// for byte what the Python tool writes. No game data is in here: hashes, lengths and
// addresses only.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "as3d/game_profile.h"

namespace as3d {

// The game whose executable has this SHA-256 (lower-case hex) in the address tables, or
// nullptr (another build of a game may still give its texts through extractExeTexts).
const GameProfile* gameOfExecutable(const std::string& sha256);

// Reads the texts out of `exe` (the whole file), whose digest is `sha256`: by address when
// the digest is known, else by hash dictionary. `game` (optional) gets the game found. False
// and a message for a file that is no game's, or a known build whose strings are not where
// the table says.
bool extractExeTexts(const std::vector<std::uint8_t>& exe, const std::string& sha256, const GameProfile** game,
                     std::string* text, std::string* error);
// The hash-dictionary way alone, whatever the build. `share` (optional) gets the share of the
// best game's string keys found, even on failure.
bool extractExeTextsByHash(const std::vector<std::uint8_t>& exe, const GameProfile** game, std::string* text,
                           std::string* error, double* share);

// The address list of a sequel (as2, gulf) as "key|address|kind" (kind 0 text, 1 text_ml,
// 2 u32), for the test that compares it with tools/exe_texts/<key>.json. Empty for as3d.
std::vector<std::string> exeTextListForTest(const char* key);
// A game's hash dictionary as "key|sha256|len|value" (a u32: empty sha256, len 0), in its
// order, for the test that compares it with tools/exe_texts/<key>.hashes.json.
std::vector<std::string> exeTextHashListForTest(const char* key);
// Test hook: replaces one game's dictionary with `count` entries of the HashedText layout of
// exe_texts.cpp {const char* key; const char* sha256; std::uint16_t len; std::uint32_t value;}
// (a struct of the same layout in the test); (any game, nullptr, 0) restores every built-in one.
void setExeTextHashesForTest(GameId game, const void* list, size_t count);

} // namespace as3d
