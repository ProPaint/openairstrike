// Shared helpers for the builtin families (builtins_*.cpp). Each family exports a table
// of script::BuiltinDesc that builtins_table.cpp concatenates into the game's single
// builtin table (as3d/script_host.h, findGameBuiltin).
#pragma once

#include <cstddef>

#include "as3d/script.h"
#include "as3d/script_host.h"
#include "as3d/world.h"
#include "world_internal.h"

namespace as3d {
namespace builtins {

using script::BuiltinArgs;
using script::BuiltinDesc;
using script::BuiltinStatus;

inline World& worldOf(BuiltinArgs& a) { return static_cast<GameScriptHost&>(a.host()).world(); }

// The entity `self` refers to (the running entity), or -1.
inline int selfOf(World& w) { return w.liveIndexFromRef(w.selfBits); }

inline i32 intArg(const BuiltinArgs& a, int k) { return ftol(a.f32(k)); }

// Entity argument: the in-use slot a reference points at, or -1.
inline int entityArg(World& w, const BuiltinArgs& a, int k) { return w.liveIndexFromRef(a.bits(k)); }

inline bool readVecAt(const BuiltinArgs& a, u32 addr, float out[3]) {
    u32 w[3];
    for (int k = 0; k < 3; ++k) {
        if (!a.readWord(addr + 4u * static_cast<u32>(k), w[k])) return false;
    }
    for (int k = 0; k < 3; ++k) out[k] = bitsf(w[k]);
    return true;
}

// Reads a vec argument; fails the call (VM error) if the pointer is not readable, like
// the mock host's validation.
inline bool vecArg(BuiltinArgs& a, int k, float out[3]) {
    if (a.readVec3(k, out)) return true;
    a.fail("access to unmapped address");
    return false;
}

inline bool writeComponent(BuiltinArgs& a, u32 base, int k, float v) {
    if (a.writeWord(base + 4u * static_cast<u32>(k), fbits(v))) return true;
    a.fail("access to unmapped address");
    return false;
}

inline bool readComponent(BuiltinArgs& a, u32 base, int k, float& v) {
    u32 b;
    if (!a.readWord(base + 4u * static_cast<u32>(k), b)) {
        a.fail("access to unmapped address");
        return false;
    }
    v = bitsf(b);
    return true;
}

// String argument (a STRG offset), or null (no VM error: the original would read
// whatever lies at STRG + value; we treat it as a missing name).
inline const char* strArg(const BuiltinArgs& a, int k) { return a.cstr(k); }

struct Family {
    const BuiltinDesc* table;
    size_t count;
};

Family mathBuiltins();
Family vectorBuiltins();
Family entityBuiltins();
Family movementBuiltins();
Family combatBuiltins();
Family playerBuiltins();

} // namespace builtins
} // namespace as3d
