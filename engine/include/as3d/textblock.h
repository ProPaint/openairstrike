// Generic parser for the brace-block text files (.obj, .wpn, .ps, levels.txt).
// See docs/spec/text-blocks.md. Owned by the orchestrator.
#pragma once

#include <string>
#include <vector>

#include "as3d/core.h"

namespace as3d {

struct TextToken {
    std::string text;      // without quotes; cp1251 bytes are passed through
    bool quoted = false;
    bool isNumber() const;
    float asFloat() const; // 0 when not a number
    int asInt() const;
};

// One line inside a block: a key followed by its arguments.
// `enemy` has no arguments; `attach abs id "GUNS" "x" "tag_guns"` has five.
struct TextStatement {
    std::string key;
    std::vector<TextToken> args;
    int line = 0;
};

struct TextBlock {
    std::string name;      // empty for anonymous blocks, as in levels.txt
    std::vector<TextStatement> statements;
    int line = 0;

    // First statement with this key (case-insensitive), or null.
    const TextStatement* find(const char* key) const;
};

struct TextFile {
    std::vector<TextBlock> blocks;
    std::vector<std::string> errors; // "line N: message"
};

TextFile parseTextBlocks(const u8* data, size_t size);

} // namespace as3d
