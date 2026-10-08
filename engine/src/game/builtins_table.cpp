// The builtin registry: every family's implementations, selected per game. A game's table
// follows its builtin metadata (as3d/script.h, builtinMetaTable of the profile's builtin
// set): for each name, the sequels' own version (builtins_sequel.cpp) when the game is a
// sequel and one exists, else the family implementation shared by all games (which reads
// the rules' flags where the games differ). Names not listed bind to the VM's counted
// auto-stub.
#include <cstring>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "builtins_common.h"

namespace as3d {

namespace {

const script::BuiltinDesc* findIn(const builtins::Family& f, const char* name) {
    for (size_t i = 0; i < f.count; ++i) {
        if (std::strcmp(f.table[i].name, name) == 0) return &f.table[i];
    }
    return nullptr;
}

std::vector<script::BuiltinDesc> build(GameId game) {
    const builtins::Family families[] = {
        builtins::mathBuiltins(),     builtins::vectorBuiltins(), builtins::entityBuiltins(),
        builtins::movementBuiltins(), builtins::combatBuiltins(), builtins::playerBuiltins(),
        builtins::effectsBuiltins(),
    };
    const builtins::Family sequel = builtins::sequelBuiltins();
    const script::BuiltinMetaTable meta = script::builtinMetaTable(gameProfile(game).builtinSet);
    std::vector<script::BuiltinDesc> v;
    v.reserve(meta.count);
    for (size_t i = 0; i < meta.count; ++i) {
        const char* name = meta.rows[i].name;
        const script::BuiltinDesc* d = game != GameId::AirStrike3D ? findIn(sequel, name) : nullptr;
        for (const builtins::Family& f : families) {
            if (d) break;
            d = findIn(f, name);
        }
        if (d) v.push_back(*d);
    }
    return v;
}

const std::vector<script::BuiltinDesc>& table(GameId game) {
    static const std::vector<script::BuiltinDesc> t[kGameCount] = {
        build(GameId::AirStrike3D), build(GameId::AirStrike2), build(GameId::GulfThunder)};
    int i = static_cast<int>(game);
    return t[i >= 0 && i < kGameCount ? i : 0];
}

// Name -> the first table entry with that name (the result of a front-to-back strcmp
// search), hashed: every script thread binds each of its FUNC names here when it is
// created (several per frame on a busy mission).
using NameIndex = std::unordered_map<std::string_view, const script::BuiltinDesc*>;

NameIndex buildIndex(GameId game) {
    NameIndex m;
    const std::vector<script::BuiltinDesc>& t = table(game);
    m.reserve(t.size());
    for (const script::BuiltinDesc& d : t) m.emplace(std::string_view(d.name), &d); // keeps the first
    return m;
}

const NameIndex& builtinIndex(GameId game) {
    static const NameIndex idx[kGameCount] = {buildIndex(GameId::AirStrike3D), buildIndex(GameId::AirStrike2),
                                              buildIndex(GameId::GulfThunder)};
    int i = static_cast<int>(game);
    return idx[i >= 0 && i < kGameCount ? i : 0];
}

} // namespace

const script::BuiltinDesc* findGameBuiltin(GameId game, const char* name) {
    if (!name) return nullptr;
    const NameIndex& m = builtinIndex(game);
    auto it = m.find(std::string_view(name));
    return it == m.end() ? nullptr : it->second;
}

size_t gameBuiltinCount(GameId game) { return table(game).size(); }
const script::BuiltinDesc& gameBuiltinAt(GameId game, size_t index) { return table(game)[index]; }

const script::BuiltinDesc* findGameBuiltin(const char* name) { return findGameBuiltin(GameId::AirStrike3D, name); }
size_t gameBuiltinCount() { return gameBuiltinCount(GameId::AirStrike3D); }
const script::BuiltinDesc& gameBuiltinAt(size_t index) { return gameBuiltinAt(GameId::AirStrike3D, index); }

} // namespace as3d
