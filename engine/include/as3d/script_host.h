// The real script host: maps the VM's address space onto the game world (entity fields,
// engine globals, the camera structure), resolves builtins from the game's builtin table
// and caches loaded script programs per path. See docs/spec/rcsl-vm.md ("Globals",
// "Entity references and fields") and as3d/world.h for the address layout.
#pragma once

#include <map>
#include <memory>
#include <string>

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

private:
    World& world_;
    std::map<std::string, std::unique_ptr<script::ScriptProgram>> programs_; // null = failed
};

// Engine global table order (rcsl-vm.md "Globals", table at 0x457220).
constexpr int kNumScriptGlobals = 24;
const char* scriptGlobalName(int index);
int scriptGlobalIndex(const char* name);

// The game's builtin table (builtins_*.cpp): the implemented builtins only. A name not in
// it binds to the VM's counted auto-stub (as3d/script.h).
const script::BuiltinDesc* findGameBuiltin(const char* name);
size_t gameBuiltinCount();
const script::BuiltinDesc& gameBuiltinAt(size_t index);

} // namespace as3d
