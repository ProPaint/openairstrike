#include "mock_host.h"

#include <cstdio>
#include <cstring>

using namespace as3d::script;

namespace rcsl_tool {

namespace {

constexpr Addr kGlobalBase = 0x2000'0000u;
constexpr Addr kEntityBase = 0x3000'0000u;
constexpr int kNumGlobals = 24;

// Engine global table order (0x457220), rcsl-vm.md "Globals". Index order matters: it is
// how a global's address is computed (kGlobalBase + 0x10 * index).
const char* const kEngineGlobals[kNumGlobals] = {
    "self",          "other",         "cb_msg",     "cb_parm1", "cb_parm2",
    "player",        "p_action",      "p_scores",   "p_lives",  "p_stars",
    "p_speedfactor", "p_counter1",    "p_counter2", "p_counter3", "p_weapon",
    "l_night",       "l_water",       "l_waterlevel", "frametime", "time",
    "camera",        "g_map_pos",     "g_damage_factor", "g_health_factor",
};

int globalIndex(const char* name) {
    for (int i = 0; i < kNumGlobals; ++i) {
        if (std::strcmp(kEngineGlobals[i], name) == 0) return i;
    }
    return -1;
}

Addr addrForGlobal(const char* name) {
    int i = globalIndex(name);
    return i < 0 ? 0 : kGlobalBase + 0x10u * static_cast<u32>(i);
}

// Full builtin metadata for the mock host's generic dispatch: name, arity, argument
// types (for the reference mock's validation) and return kind (docs/spec/
// rcsl-builtins-table.md, "Table"). Transcribed once here since only the mock host needs
// full argument-type information; engine/src/script/builtins_meta.cpp carries the
// smaller subset (name/arity/returns/done-flag) the VM itself needs for stubs.
enum class ArgKind : as3d::u8 { Float, Int, Vec, VecOut, Entity, StringArg, Raw };

struct MockBuiltinMeta {
    const char* name;
    as3d::u8 arity;
    ArgKind args[4];
    ReturnKind returns;
};

using AK = ArgKind;
using RK = ReturnKind;

constexpr MockBuiltinMeta kMockBuiltins[] = {
    {"debug", 0, {}, RK::None},
    {"RespawnPlayer", 0, {}, RK::None},
    {"EndLevel", 0, {}, RK::None},
    {"random", 0, {}, RK::Float},
    {"crandom", 0, {}, RK::Float},
    {"sin", 1, {AK::Float}, RK::Float},
    {"cos", 1, {AK::Float}, RK::Float},
    {"tan", 1, {AK::Float}, RK::Float},
    {"atan", 1, {AK::Float}, RK::Float},
    {"abs", 1, {AK::Float}, RK::Float},
    {"min", 2, {AK::Float, AK::Float}, RK::Float},
    {"max", 2, {AK::Float, AK::Float}, RK::Float},
    {"lerp", 3, {AK::Float, AK::Float, AK::Float}, RK::Float},
    {"vec_copy", 2, {AK::Vec, AK::VecOut}, RK::None},
    {"vec_add", 3, {AK::Vec, AK::Vec, AK::VecOut}, RK::None},
    {"vec_sub", 3, {AK::Vec, AK::Vec, AK::VecOut}, RK::None},
    {"vec_ma", 4, {AK::Vec, AK::Float, AK::Vec, AK::VecOut}, RK::None},
    {"vec_length", 1, {AK::Vec}, RK::Float},
    {"vec_norm", 1, {AK::VecOut}, RK::Float},
    {"vec_scale", 2, {AK::VecOut, AK::Float}, RK::None},
    {"vec_setlen", 2, {AK::VecOut, AK::Float}, RK::None},
    {"vec_toyaw", 1, {AK::Vec}, RK::Float},
    {"vec_toangles", 2, {AK::Vec, AK::VecOut}, RK::None},
    {"ClearAxis", 1, {AK::Raw}, RK::None},
    {"AnglesToAxis", 2, {AK::Vec, AK::Raw}, RK::None},
    {"create", 2, {AK::StringArg, AK::Vec}, RK::Entity},
    {"remove", 1, {AK::Entity}, RK::None},
    {"activate", 1, {AK::Entity}, RK::None},
    {"deactivate", 1, {AK::Entity}, RK::None},
    {"getentity", 1, {AK::Raw}, RK::Entity},
    {"setskin", 2, {AK::Entity, AK::StringArg}, RK::None},
    {"callback", 4, {AK::Entity, AK::Float, AK::Float, AK::Float}, RK::None},
    {"AttachEntity", 4, {AK::Entity, AK::Entity, AK::StringArg, AK::Int}, RK::None},
    {"AttachActivate", 1, {AK::StringArg}, RK::None},
    {"AttachDeactivate", 1, {AK::StringArg}, RK::None},
    {"AttachCallback", 4, {AK::StringArg, AK::Float, AK::Float, AK::Float}, RK::None},
    {"ParentCallback", 3, {AK::Float, AK::Float, AK::Float}, RK::None},
    {"move", 1, {AK::Vec}, RK::None},
    {"movex", 1, {AK::Float}, RK::None},
    {"movey", 1, {AK::Float}, RK::None},
    {"movez", 1, {AK::Float}, RK::None},
    {"rotate", 1, {AK::Vec}, RK::None},
    {"rotatex", 1, {AK::Float}, RK::None},
    {"rotatey", 1, {AK::Float}, RK::None},
    {"rotatez", 1, {AK::Float}, RK::None},
    {"sleep", 0, {}, RK::None},
    {"Shoot", 3, {AK::StringArg, AK::StringArg, AK::Vec}, RK::None},
    {"Damage", 2, {AK::Entity, AK::Float}, RK::None},
    {"RadialDamage", 3, {AK::Vec, AK::Float, AK::Float}, RK::None},
    {"RotateTo", 2, {AK::Entity, AK::Int}, RK::None},
    {"lRotateTo", 2, {AK::Entity, AK::Int}, RK::None},
    {"RotateToClamp", 4, {AK::Entity, AK::Int, AK::Float, AK::Float}, RK::None},
    {"lRotateToClamp", 4, {AK::Entity, AK::Int, AK::Float, AK::Float}, RK::None},
    {"MoveToNextWP", 1, {AK::Int}, RK::None},
    {"lMoveToNextWP", 1, {AK::Int}, RK::None},
    {"RotateToNextWP", 0, {}, RK::None},
    {"lRotateToNextWP", 0, {}, RK::None},
    {"GetWaypointDelay", 1, {AK::Int}, RK::Float},
    {"TerrainHeight", 2, {AK::Float, AK::Float}, RK::Float},
    {"PlaceLight", 3, {AK::Vec, AK::Vec, AK::Float}, RK::None},
    {"CameraQuake", 1, {AK::Float}, RK::None},
    {"LockTarget", 0, {}, RK::Entity},
    {"IsValidTarget", 1, {AK::Entity}, RK::IntAsFloat},
    {"StartSound", 1, {AK::StringArg}, RK::None},
    {"StartLoopingSound", 1, {AK::StringArg}, RK::None},
    {"StopLoopingSound", 0, {}, RK::None},
    {"TraceLine", 3, {AK::Vec, AK::Vec, AK::Int}, RK::Entity},
    {"TraceLineDamage", 3, {AK::Vec, AK::Vec, AK::Float}, RK::None},
    {"Lightning", 0, {}, RK::None},
    {"PushPlayer", 0, {}, RK::None},
    {"G_AddPowerUp", 2, {AK::Int, AK::Int}, RK::None},
    {"G_UsePowerUp", 0, {}, RK::IntAsFloat},
    {"G_GetPowerUp", 0, {}, RK::IntAsFloat},
    {"G_AddMissiles", 2, {AK::Int, AK::Int}, RK::None},
    {"G_UseMissile", 0, {}, RK::IntAsFloat},
    {"G_GetMissiles", 0, {}, RK::IntAsFloat},
    {"G_GetUpgrade", 1, {AK::Int}, RK::IntAsFloat},
    {"G_SetUpgrade", 2, {AK::Int, AK::Int}, RK::None},
    {"ShowTutorialHint", 1, {AK::StringArg}, RK::None},
    {"PlayerFreezeHealth", 1, {AK::Float}, RK::None},
    {"PlayerDisableAction", 1, {AK::Float}, RK::None},
    {"FreezeHealth", 2, {AK::Entity, AK::Float}, RK::None},
    {"SetFlag", 2, {AK::Int, AK::Int}, RK::IntAsFloat},
    {"ClearFlag", 2, {AK::Int, AK::Int}, RK::IntAsFloat},
    {"SetModel", 1, {AK::StringArg}, RK::None},
};
constexpr size_t kMockBuiltinCount = sizeof(kMockBuiltins) / sizeof(kMockBuiltins[0]);

// One generic implementation drives every documented builtin, exactly as the reference
// VM's MockHost.call_builtin does (docs/spec/rcsl-vm.md, "Mock host"): the mock does not
// simulate gameplay, it only validates argument shapes and produces a value from the
// documented return kind (random/crandom excepted, which draw from the shared
// generator).
void genericMockBuiltin(BuiltinArgs& args, void* userData) {
    const MockBuiltinMeta* m = static_cast<const MockBuiltinMeta*>(userData);
    MockHost& host = static_cast<MockHost&>(args.host());

    for (int k = 0; k < m->arity; ++k) {
        ArgKind t = m->args[k];
        if (t == ArgKind::Vec || t == ArgKind::VecOut) {
            float tmp[3];
            if (!args.readVec3(k, tmp)) {
                args.fail("access to unmapped address");
                return;
            }
        } else if (t == ArgKind::StringArg) {
            if (!args.isStringArg(k)) {
                char buf[160];
                std::snprintf(buf, sizeof buf,
                               "%s: argument %d is not a STRG string offset (%#x)", m->name, k,
                               args.bits(k));
                args.fail(buf);
                return;
            }
        }
    }

    if (std::strcmp(m->name, "random") == 0) {
        args.setReturnFloat(host.rng.uniform());
    } else if (std::strcmp(m->name, "crandom") == 0) {
        // Mirrors tools/ref/rcsl_vm.py exactly: u rounded to float32, doubled and
        // rounded again, then 1.0 subtracted and rounded a third time -- each step
        // matching a specific f2b/b2f round-trip in the reference.
        float u = host.rng.uniform();
        float v = static_cast<float>(static_cast<double>(u) * 2.0);
        float result = static_cast<float>(static_cast<double>(v) - 1.0);
        args.setReturnFloat(result);
    } else if (m->returns == ReturnKind::Entity) {
        args.setReturnBits(host.newEntity());
    } else if (m->returns == ReturnKind::Float || m->returns == ReturnKind::IntAsFloat ||
               m->returns == ReturnKind::Pointer) {
        args.setReturnBits(0);
    }
    // ReturnKind::None: register left untouched, as the spec requires.

    if (args.latent()) {
        // "Under LCALL a mock builtin is done immediately when the timeout is 0,
        // otherwise it is never done by itself and the timeout ends the wait."
        args.setDone(args.timeoutBits() == 0);
    }
}

const std::vector<BuiltinDesc>& mockDescs() {
    static const std::vector<BuiltinDesc> descs = [] {
        std::vector<BuiltinDesc> v;
        v.reserve(kMockBuiltinCount);
        for (const MockBuiltinMeta& m : kMockBuiltins) {
            BuiltinDesc d;
            d.name = m.name;
            d.argCount = m.arity;
            d.fn = &genericMockBuiltin;
            d.userData = const_cast<void*>(static_cast<const void*>(&m));
            d.status = BuiltinStatus::Implemented;
            v.push_back(d);
        }
        return v;
    }();
    return descs;
}

} // namespace

MockHost::MockHost(float dt) {
    newEntity(); // 0 = self
    newEntity(); // 1 = player
    newEntity(); // 2 = camera
    newEntity(); // 3 = other
    setGlobal("self", entityRef(kMockSelf));
    setGlobal("player", entityRef(kMockPlayer));
    setGlobal("camera", entityRef(kMockCamera));
    setGlobal("frametime", floatToBits(dt));
}

bool MockHost::mapped(Addr addr) const {
    if (addr & 3u) return false;
    if (addr >= kGlobalBase && addr < kGlobalBase + 0x10u * kNumGlobals) return true;
    if (addr >= kEntityBase) {
        u32 rel = addr - kEntityBase;
        u32 n = rel / 0x1000u;
        u32 off = rel % 0x1000u;
        if (n < static_cast<u32>(entityCount_) && off < 4u * kEntityFields) return true;
    }
    return false;
}

Addr MockHost::resolveGlobal(const char* name) { return addrForGlobal(name); }

const BuiltinDesc* MockHost::resolveBuiltin(const char* name) {
    for (const BuiltinDesc& d : mockDescs()) {
        if (std::strcmp(d.name, name) == 0) return &d;
    }
    return nullptr;
}

bool MockHost::readWord(Addr addr, u32& outBits) {
    if (!mapped(addr)) return false;
    auto it = cells_.find(addr);
    outBits = (it != cells_.end()) ? it->second : 0;
    return true;
}

bool MockHost::writeWord(Addr addr, u32 bits) {
    if (!mapped(addr)) return false;
    cells_[addr] = bits;
    return true;
}

void MockHost::onScriptError(const char* scriptName, const char* message) {
    hadError_ = true;
    errorMessage_ = message ? message : "";
    (void)scriptName;
}

Addr MockHost::entityRef(int n) const { return kEntityBase + 0x1000u * static_cast<u32>(n); }

Addr MockHost::newEntity() {
    int n = entityCount_++;
    Addr ref = entityRef(n);
    cells_[ref] = ref - 0x7Bu; // field 0: back-pointer, like the real entity's self-pointer
    return ref;
}

void MockHost::setGlobal(const char* name, u32 bits) {
    Addr a = addrForGlobal(name);
    cells_[a] = bits;
}

u32 MockHost::getGlobal(const char* name) const {
    Addr a = addrForGlobal(name);
    auto it = cells_.find(a);
    return it != cells_.end() ? it->second : 0;
}

DispatchResult MockHost::damage(ScriptThread& thread, ITraceSink* sink, u32 frame, float amount) {
    Addr self = entityRef(kMockSelf);
    u32 deadBits = 0;
    readWord(self + 4u * kDeadField, deadBits);
    if (bitsToFloat(deadBits) != 0.0f) {
        if (sink) sink->damage(frame, true, 0);
        return DispatchResult{};
    }
    Addr healthAddr = self + 4u * kHealthField;
    u32 healthBits = 0;
    readWord(healthAddr, healthBits);
    float health = bitsToFloat(healthBits) - amount;
    writeWord(healthAddr, floatToBits(health));
    if (sink) sink->damage(frame, false, floatToBits(amount));
    DispatchResult r = thread.runEvent(EntryPoint::Damage, frame);
    if (!r.ok) return r; // an error propagates immediately in the reference too
    readWord(healthAddr, healthBits);
    if (bitsToFloat(healthBits) <= 0.0f) writeWord(self + 4u * kDeadField, floatToBits(1.0f));
    return r;
}

} // namespace rcsl_tool
