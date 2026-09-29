// GameScriptHost: the world-backed IScriptHost (docs/spec/rcsl-vm.md,
// docs/spec/as2/rcsl-vm.delta.md "Globals").
#include "as3d/script_host.h"

#include <cstring>

#include "as3d/vfs.h"
#include "as3d/world.h"
#include "world_internal.h"

namespace as3d {

using script::Addr;

namespace {

// Host slots: a global's address is kGlobalAddrBase + 0x10 * slot, whatever the game. The
// first game's 24 in its table order, then the sequels' four.
enum GlobalSlot {
    G_SELF, G_OTHER, G_CB_MSG, G_CB_PARM1, G_CB_PARM2, G_PLAYER, G_P_ACTION, G_P_SCORES,
    G_P_LIVES, G_P_STARS, G_P_SPEEDFACTOR, G_P_COUNTER1, G_P_COUNTER2, G_P_COUNTER3,
    G_P_WEAPON, G_L_NIGHT, G_L_WATER, G_L_WATERLEVEL, G_FRAMETIME, G_TIME, G_CAMERA,
    G_MAP_POS, G_DAMAGE_FACTOR, G_HEALTH_FACTOR,
    G_PLAYER1, G_PLAYER2, G_P_MAXHEALTH, G_CAMERAMODE,
    kGlobalSlots
};
static_assert(kGlobalSlots == kMaxScriptGlobals, "slot table");
static_assert(kUnknownGlobalSlot >= kGlobalSlots, "scratch cells after the globals");

const char* const kSlotNames[kGlobalSlots] = {
    "self",          "other",         "cb_msg",       "cb_parm1",  "cb_parm2",
    "player",        "p_action",      "p_scores",     "p_lives",   "p_stars",
    "p_speedfactor", "p_counter1",    "p_counter2",   "p_counter3", "p_weapon",
    "l_night",       "l_water",       "l_waterlevel", "frametime", "time",
    "camera",        "g_map_pos",     "g_damage_factor", "g_health_factor",
    "player1",       "player2",       "p_maxHealth",  "cameramode",
};

// Each game's own table, as host slots in the executable's order.
const int kTableV170[kNumScriptGlobals] = {
    G_SELF, G_OTHER, G_CB_MSG, G_CB_PARM1, G_CB_PARM2, G_PLAYER, G_P_ACTION, G_P_SCORES,
    G_P_LIVES, G_P_STARS, G_P_SPEEDFACTOR, G_P_COUNTER1, G_P_COUNTER2, G_P_COUNTER3,
    G_P_WEAPON, G_L_NIGHT, G_L_WATER, G_L_WATERLEVEL, G_FRAMETIME, G_TIME, G_CAMERA,
    G_MAP_POS, G_DAMAGE_FACTOR, G_HEALTH_FACTOR,
};
// as2/rcsl-vm.delta.md "Globals" (as2@0x49d908); Gulf Thunder has the same 28.
const int kTableSequel[kMaxScriptGlobals] = {
    G_SELF, G_OTHER, G_CB_MSG, G_CB_PARM1, G_CB_PARM2, G_PLAYER, G_PLAYER1, G_PLAYER2,
    G_P_ACTION, G_P_MAXHEALTH, G_P_SCORES, G_P_LIVES, G_P_STARS, G_P_SPEEDFACTOR,
    G_P_COUNTER1, G_P_COUNTER2, G_P_COUNTER3, G_P_WEAPON, G_L_NIGHT, G_L_WATER,
    G_L_WATERLEVEL, G_FRAMETIME, G_TIME, G_CAMERA, G_CAMERAMODE, G_MAP_POS,
    G_DAMAGE_FACTOR, G_HEALTH_FACTOR,
};

struct GlobalTable {
    const int* slots;
    int count;
};

GlobalTable tableOf(GameId game) {
    if (game == GameId::AirStrike3D) return {kTableV170, kNumScriptGlobals};
    return {kTableSequel, kMaxScriptGlobals};
}

int slotOf(GameId game, const char* name) {
    int i = scriptGlobalIndex(game, name);
    return i < 0 ? -1 : tableOf(game).slots[i];
}

} // namespace

int scriptGlobalCount(GameId game) { return tableOf(game).count; }

const char* scriptGlobalName(GameId game, int index) {
    GlobalTable t = tableOf(game);
    return index >= 0 && index < t.count ? kSlotNames[t.slots[index]] : nullptr;
}

int scriptGlobalIndex(GameId game, const char* name) {
    if (!name) return -1;
    GlobalTable t = tableOf(game);
    for (int i = 0; i < t.count; ++i) {
        if (std::strcmp(kSlotNames[t.slots[i]], name) == 0) return i;
    }
    return -1;
}

u32 scriptGlobalAddress(GameId game, const char* name) {
    int s = slotOf(game, name);
    return s < 0 ? 0u : kGlobalAddrBase + 0x10u * static_cast<u32>(s);
}

const char* scriptGlobalName(int index) { return scriptGlobalName(GameId::AirStrike3D, index); }
int scriptGlobalIndex(const char* name) { return scriptGlobalIndex(GameId::AirStrike3D, name); }

Addr GameScriptHost::resolveGlobal(const char* name) {
    if (!name) return 0;
    if (u32 a = scriptGlobalAddress(world_.game(), name)) return a;
    // Unknown to this game: a scratch cell per name, reported once (like an unknown builtin).
    for (size_t k = 0; k < unknown_.size(); ++k) {
        if (unknown_[k].name == name) {
            ++unknown_[k].binds;
            return kGlobalAddrBase + 0x10u * static_cast<u32>(kUnknownGlobalSlot + static_cast<int>(k));
        }
    }
    if (static_cast<int>(unknown_.size()) >= kMaxUnknownGlobals) return 0;
    AS3D_WARN("script: global '%s' is not one of the game's; bound to a scratch cell", name);
    UnknownGlobal u;
    u.name = name;
    u.binds = 1;
    unknown_.push_back(u);
    return kGlobalAddrBase + 0x10u * static_cast<u32>(kUnknownGlobalSlot + static_cast<int>(unknown_.size()) - 1);
}

const script::BuiltinDesc* GameScriptHost::resolveBuiltin(const char* name) {
    return findGameBuiltin(world_.game(), name);
}

namespace {
// Field index of an entity-window address, and the reference it belongs to.
bool entityField(Addr addr, u32& ref, int& k) {
    int slot;
    u32 gen, off;
    if (!World::decodeEntityAddr(addr, slot, gen, off)) return false;
    if (off < kEntityRefOffset || (off - kEntityRefOffset) % 4 != 0) return false;
    u32 f = (off - kEntityRefOffset) / 4;
    if (f >= static_cast<u32>(kEntityFieldCount)) return false;
    k = static_cast<int>(f);
    ref = addr - 4u * f;
    return true;
}

// Host slot of a global-window address that the running game maps, else -1 (the first
// game's four missing globals are unmapped for it; they are never resolved anyway).
int globalSlot(GameId game, Addr addr) {
    if (addr < kGlobalAddrBase || (addr - kGlobalAddrBase) % 0x10 != 0) return -1;
    u32 g = (addr - kGlobalAddrBase) / 0x10;
    int limit = game == GameId::AirStrike3D ? kNumScriptGlobals : kMaxScriptGlobals;
    return g < static_cast<u32>(limit) ? static_cast<int>(g) : -1;
}

int unknownSlot(Addr addr, size_t count) {
    if (addr < kGlobalAddrBase || (addr - kGlobalAddrBase) % 0x10 != 0) return -1;
    u32 g = (addr - kGlobalAddrBase) / 0x10;
    if (g < static_cast<u32>(kUnknownGlobalSlot) || g >= static_cast<u32>(kUnknownGlobalSlot) + count) return -1;
    return static_cast<int>(g) - kUnknownGlobalSlot;
}

// `player2` of an entity of player index p (as2/rcsl-vm.delta.md): the other player's cell.
int otherPlayer(int p) { return p == 1 ? 0 : 1; }
} // namespace

bool GameScriptHost::readWord(Addr addr, u32& out) {
    World& w = world_;
    u32 ref;
    int k;
    if (entityField(addr, ref, k)) return w.readRefField(ref, k, out); // stale: tombstone
    if (addr >= kEntityAddrBase && addr < kEntityAddrEnd) return false;
    if (addr >= kCameraAddrBase && addr < kCameraAddrBase + 4u * kCameraFieldCount) {
        if ((addr - kCameraAddrBase) % 4 != 0) return false;
        out = fbits(w.camera().field[(addr - kCameraAddrBase) / 4]);
        return true;
    }
    int u = unknownSlot(addr, unknown_.size());
    if (u >= 0) {
        out = unknown_[static_cast<size_t>(u)].value;
        return true;
    }
    int g = globalSlot(w.game(), addr);
    if (g < 0) return false;
    const int pi = w.currentPlayerIndex();
    const PlayerRecord& pr = w.player(pi);
    switch (g) {
        case G_SELF: out = w.selfBits; return true;
        case G_OTHER: out = w.otherBits; return true;
        case G_CB_MSG: out = w.cbMsgBits; return true;
        case G_CB_PARM1: out = w.cbParm1Bits; return true;
        case G_CB_PARM2: out = w.cbParm2Bits; return true;
        case G_PLAYER: out = pr.entityRef; return true;
        case G_P_ACTION: out = fbits(pr.action); return true;
        case G_P_SCORES: out = fbits(pr.scores); return true;
        case G_P_LIVES: out = fbits(pr.lives); return true;
        case G_P_STARS: out = fbits(pr.stars); return true;
        case G_P_SPEEDFACTOR: out = fbits(pr.speedFactor); return true;
        case G_P_COUNTER1: out = fbits(pr.counter[0]); return true;
        case G_P_COUNTER2: out = fbits(pr.counter[1]); return true;
        case G_P_COUNTER3: out = fbits(pr.counter[2]); return true;
        case G_P_WEAPON: out = fbits(pr.weapon); return true;
        case G_L_NIGHT: out = fbits(w.lNight); return true;
        case G_L_WATER: out = fbits(w.lWater); return true;
        case G_L_WATERLEVEL: out = fbits(w.lWaterLevel); return true;
        case G_FRAMETIME: out = fbits(w.frametimeGlobal); return true;
        case G_TIME: out = fbits(w.timeGlobal); return true;
        case G_CAMERA: out = kCameraAddrBase; return true;
        case G_MAP_POS: out = fbits(w.mapPos()); return true;
        case G_DAMAGE_FACTOR: out = fbits(w.damageFactor()); return true;
        case G_HEALTH_FACTOR: out = fbits(w.healthFactor()); return true;
        case G_PLAYER1: out = w.player(0).entityRef; return true;
        case G_PLAYER2: out = w.player(otherPlayer(pi)).entityRef; return true;
        case G_P_MAXHEALTH: out = fbits(pr.maxHealthGlobal); return true;
        case G_CAMERAMODE: out = fbits(w.cameraModeGlobal); return true;
        default: return false;
    }
}

bool GameScriptHost::writeWord(Addr addr, u32 bits) {
    World& w = world_;
    u32 ref;
    int k;
    if (entityField(addr, ref, k)) {
        int i = w.liveIndexFromRef(ref);
        if (i >= 0) w.entity(i).fields[k] = bits; // writes through a stale reference are ignored
        return true;
    }
    if (addr >= kEntityAddrBase && addr < kEntityAddrEnd) return false;
    if (addr >= kCameraAddrBase && addr < kCameraAddrBase + 4u * kCameraFieldCount) {
        if ((addr - kCameraAddrBase) % 4 != 0) return false;
        w.camera().field[(addr - kCameraAddrBase) / 4] = bitsf(bits);
        return true;
    }
    int u = unknownSlot(addr, unknown_.size());
    if (u >= 0) {
        unknown_[static_cast<size_t>(u)].value = bits;
        return true;
    }
    int g = globalSlot(w.game(), addr);
    if (g < 0) return false;
    const int pi = w.currentPlayerIndex();
    PlayerRecord& pr = w.player(pi);
    float v = bitsf(bits);
    switch (g) {
        case G_SELF: w.selfBits = bits; return true;
        case G_OTHER: w.otherBits = bits; return true;
        case G_CB_MSG: w.cbMsgBits = bits; return true;
        case G_CB_PARM1: w.cbParm1Bits = bits; return true;
        case G_CB_PARM2: w.cbParm2Bits = bits; return true;
        case G_PLAYER: pr.entityRef = bits; return true;
        case G_P_ACTION: pr.action = v; return true;
        case G_P_SCORES: pr.scores = v; return true;
        case G_P_LIVES: pr.lives = v; return true;
        case G_P_STARS: pr.stars = v; return true;
        case G_P_SPEEDFACTOR: pr.speedFactor = v; return true;
        case G_P_COUNTER1: pr.counter[0] = v; return true;
        case G_P_COUNTER2: pr.counter[1] = v; return true;
        case G_P_COUNTER3: pr.counter[2] = v; return true;
        case G_P_WEAPON: pr.weapon = v; return true;
        case G_L_NIGHT: w.lNight = v; return true;
        case G_L_WATER: w.lWater = v; return true;
        case G_L_WATERLEVEL: w.lWaterLevel = v; return true;
        case G_FRAMETIME: w.frametimeGlobal = v; return true;
        case G_TIME: w.timeGlobal = v; return true;
        case G_CAMERA: return true; // constant pointer; a write has no lasting effect here
        case G_MAP_POS: w.setMapPos(v); return true;
        case G_DAMAGE_FACTOR: w.damageFactor_ = v; return true;
        case G_HEALTH_FACTOR: w.healthFactor_ = v; return true;
        // The sequels' cells: `player1` is player 1's `player` cell, `player2` the other
        // player's; `p_maxHealth` and `cameramode` are plain stored cells.
        case G_PLAYER1: w.player(0).entityRef = bits; return true;
        case G_PLAYER2: w.player(otherPlayer(pi)).entityRef = bits; return true;
        case G_P_MAXHEALTH: pr.maxHealthGlobal = v; return true;
        case G_CAMERAMODE: w.cameraModeGlobal = v; return true;
        default: return false;
    }
}

void GameScriptHost::onScriptError(const char* scriptName, const char* message) {
    world_.noteScriptError(scriptName, message);
}

const script::ScriptProgram* GameScriptHost::program(const std::string& rawPath) {
    std::string path = normalizePath(rawPath);
    auto it = programs_.find(path);
    if (it != programs_.end()) return it->second.get();
    std::unique_ptr<script::ScriptProgram> prog;
    Blob blob;
    if (!world_.vfs().read(path, blob)) {
        AS3D_WARN("script '%s' not found", path.c_str());
    } else {
        prog.reset(new script::ScriptProgram());
        std::string err;
        if (!script::ScriptProgram::load(blob.data(), blob.size(), *prog, &err)) {
            AS3D_WARN("script '%s' failed to load: %s", path.c_str(), err.c_str());
            prog.reset();
        }
    }
    const script::ScriptProgram* p = prog.get();
    programs_[path] = std::move(prog);
    return p;
}

} // namespace as3d
