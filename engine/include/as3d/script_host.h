// The real script host: maps the VM's address space onto the game world (entity fields,
// engine globals, the camera structure), resolves builtins from the game's builtin table
// and caches loaded script programs per path. See docs/spec/rcsl-vm.md ("Globals",
// "Entity references and fields"), docs/spec/as2/rcsl-vm.delta.md ("Globals") and
// as3d/world.h for the address layout.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "as3d/game_profile.h"
#include "as3d/script.h"

namespace as3d {

class World;

class GameScriptHost final : public script::IScriptHost {
public:
    explicit GameScriptHost(World& world) : world_(world) {}

    World& world() { return world_; }

    script::Addr resolveGlobal(const char* name) override;
    const script::BuiltinDesc* resolveBuiltin(const char* name) override;
    bool readWord(script::Addr addr, u32& outBits) override;
    bool writeWord(script::Addr addr, u32 bits) override;
    void onScriptError(const char* scriptName, const char* message) override;

    // Loads (once) and returns the program at a game path, or null if it is missing or
    // malformed (logged once).
    const script::ScriptProgram* program(const std::string& path);
    void clearPrograms() { programs_.clear(); }
    size_t programCount() const { return programs_.size(); }

    // Globals the game does not have: each name binds to a scratch cell of its own (a plain
    // stored word, 0 at first) instead of an unmapped address, is logged once and counted
    // per script thread that binds it. At most kMaxUnknownGlobals names; more resolve to 0.
    struct UnknownGlobal {
        std::string name;
        script::u64 binds = 0;
        u32 value = 0;
    };
    const std::vector<UnknownGlobal>& unknownGlobals() const { return unknown_; }

private:
    World& world_;
    std::map<std::string, std::unique_ptr<script::ScriptProgram>> programs_; // null = failed
    std::vector<UnknownGlobal> unknown_;
};

// Engine global tables per game (rcsl-vm.md "Globals", table at 0x457220; the sequels:
// as2/rcsl-vm.delta.md, table at as2@0x49d908). `index` is the position in the game's own
// table; the host's address of a global does not depend on the game: the first game's 24
// keep theirs and the four the sequels add (`player1`, `player2`, `p_maxHealth`,
// `cameramode`) come after them.
constexpr int kNumScriptGlobals = 24;    // the first game's
constexpr int kMaxScriptGlobals = 28;    // any game's
constexpr int kMaxUnknownGlobals = 64;
constexpr int kUnknownGlobalSlot = 64;   // first scratch slot (address kGlobalAddrBase + 16 * slot)
int scriptGlobalCount(GameId game);
const char* scriptGlobalName(GameId game, int index);
int scriptGlobalIndex(GameId game, const char* name); // -1 if the game has no such global
// The host's address of a global of `game`, 0 if the game has none of that name.
u32 scriptGlobalAddress(GameId game, const char* name);
// The first game's table (the functions above for GameId::AirStrike3D).
const char* scriptGlobalName(int index);
int scriptGlobalIndex(const char* name);

// The builtin table of a game (builtins_*.cpp): the implemented builtins in the order of the
// executable's table (the game's builtin metadata, as3d/script.h). A name not in it binds to
// the VM's counted auto-stub.
const script::BuiltinDesc* findGameBuiltin(GameId game, const char* name);
size_t gameBuiltinCount(GameId game);
const script::BuiltinDesc& gameBuiltinAt(GameId game, size_t index);
// The first game's table.
const script::BuiltinDesc* findGameBuiltin(const char* name);
size_t gameBuiltinCount();
const script::BuiltinDesc& gameBuiltinAt(size_t index);

} // namespace as3d
