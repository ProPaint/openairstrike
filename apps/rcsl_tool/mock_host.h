// Deterministic mock host for the RCSL VM, mirroring tools/ref/rcsl_vm.py's MockHost
// exactly: same mock address layout, same xorshift32 generator (as3d::Rng, seed 1), same
// generic (documented-type-driven) builtin behaviour, same entity/global bootstrap. See
// docs/spec/rcsl-vm.md, "Mock host". Shared by rcsl_tool (trace/check/info) and
// apps/tests/script_test.cpp so both exercise the exact same host.
#pragma once

#include <cstring>
#include <string>
#include <unordered_map>

#include "as3d/core.h"
#include "as3d/script.h"

namespace rcsl_tool {

using as3d::script::Addr;
using as3d::u32;

// Small float/bits helpers shared by mock_host.cpp and runner.cpp. Both files may end up
// compiled into the same translation unit (apps/tests/script_test.cpp #includes them
// directly rather than linking a separate library -- see docs/script-vm.md), so these
// live here, once, as ordinary inline functions instead of being duplicated in each
// file's own anonymous namespace.
inline u32 floatToBits(float f) {
    u32 b;
    std::memcpy(&b, &f, 4);
    return b;
}
inline float bitsToFloat(u32 b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

// Mock entities 0..3 exist from the start: 0 = self, 1 = player, 2 = camera, 3 = other.
enum MockEntity { kMockSelf = 0, kMockPlayer = 1, kMockCamera = 2, kMockOther = 3 };
constexpr int kEntityFields = 90;
constexpr int kHealthField = 34;
constexpr int kDeadField = 4;

class MockHost final : public as3d::script::IScriptHost {
public:
    explicit MockHost(float dt);

    Addr resolveGlobal(const char* name) override;
    const as3d::script::BuiltinDesc* resolveBuiltin(const char* name) override;
    bool readWord(Addr addr, u32& outBits) override;
    bool writeWord(Addr addr, u32 bits) override;
    void onScriptError(const char* scriptName, const char* message) override;

    Addr entityRef(int n) const;
    Addr newEntity();
    void setGlobal(const char* name, u32 bits);
    u32 getGlobal(const char* name) const;

    // Mock damage routine on self (entity 0), rcsl-vm.md "Mock host": skipped if field 4
    // (dead) != 0; else field 34 (health) -= amount, the damage handler runs, and field 4
    // is set to 1.0 if health <= 0 afterward. Emits the "D" trace line itself (sink may be
    // null). Returns the handler dispatch result (ranHandler=false if skipped).
    as3d::script::DispatchResult damage(as3d::script::ScriptThread& thread, as3d::script::ITraceSink* sink,
                                         u32 frame, float amount);

    as3d::Rng rng{1};
    bool hadError() const { return hadError_; }
    const std::string& errorMessage() const { return errorMessage_; }

private:
    bool mapped(Addr addr) const;

    std::unordered_map<u32, u32> cells_;
    int entityCount_ = 0;
    bool hadError_ = false;
    std::string errorMessage_;
};

} // namespace rcsl_tool
