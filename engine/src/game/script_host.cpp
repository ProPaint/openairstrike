// GameScriptHost: the world-backed IScriptHost (docs/spec/rcsl-vm.md).
#include "as3d/script_host.h"

#include <cstring>

#include "as3d/vfs.h"
#include "as3d/world.h"
#include "world_internal.h"

namespace as3d {

using script::Addr;

namespace {

const char* const kGlobals[kNumScriptGlobals] = {
    "self",          "other",         "cb_msg",       "cb_parm1",  "cb_parm2",
    "player",        "p_action",      "p_scores",     "p_lives",   "p_stars",
    "p_speedfactor", "p_counter1",    "p_counter2",   "p_counter3", "p_weapon",
    "l_night",       "l_water",       "l_waterlevel", "frametime", "time",
    "camera",        "g_map_pos",     "g_damage_factor", "g_health_factor",
};

enum GlobalIndex {
    G_SELF, G_OTHER, G_CB_MSG, G_CB_PARM1, G_CB_PARM2, G_PLAYER, G_P_ACTION, G_P_SCORES,
    G_P_LIVES, G_P_STARS, G_P_SPEEDFACTOR, G_P_COUNTER1, G_P_COUNTER2, G_P_COUNTER3,
    G_P_WEAPON, G_L_NIGHT, G_L_WATER, G_L_WATERLEVEL, G_FRAMETIME, G_TIME, G_CAMERA,
    G_MAP_POS, G_DAMAGE_FACTOR, G_HEALTH_FACTOR,
};

} // namespace

const char* scriptGlobalName(int index) {
    return index >= 0 && index < kNumScriptGlobals ? kGlobals[index] : nullptr;
}

int scriptGlobalIndex(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < kNumScriptGlobals; ++i) {
        if (std::strcmp(kGlobals[i], name) == 0) return i;
    }
    return -1;
}

Addr GameScriptHost::resolveGlobal(const char* name) {
    int i = scriptGlobalIndex(name);
    return i < 0 ? 0 : kGlobalAddrBase + 0x10u * static_cast<u32>(i);
}

const script::BuiltinDesc* GameScriptHost::resolveBuiltin(const char* name) { return findGameBuiltin(name); }

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
    if (addr >= kGlobalAddrBase && addr < kGlobalAddrBase + 0x10u * kNumScriptGlobals) {
        if ((addr - kGlobalAddrBase) % 0x10 != 0) return false;
        int g = static_cast<int>((addr - kGlobalAddrBase) / 0x10);
        const PlayerRecord& pr = w.player(w.currentPlayerIndex());
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
            default: return false;
        }
    }
    return false;
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
    if (addr >= kGlobalAddrBase && addr < kGlobalAddrBase + 0x10u * kNumScriptGlobals) {
        if ((addr - kGlobalAddrBase) % 0x10 != 0) return false;
        int g = static_cast<int>((addr - kGlobalAddrBase) / 0x10);
        PlayerRecord& pr = w.player(w.currentPlayerIndex());
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
            default: return false;
        }
    }
    return false;
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
