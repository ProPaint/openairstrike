// Standard-run driver: reproduces tools/ref/rcsl_vm.py's `run_script` event schedule
// exactly (docs/spec/rcsl-vm.md, "Mock host"), so both rcsl_tool and
// apps/tests/script_test.cpp exercise the identical frame loop. See script.h's note on
// this file being shared by direct #include (no separate library wiring needed).
#pragma once

#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/script.h"

namespace rcsl_tool {

struct ScheduledEvent {
    int frame = 0;
    std::string kind; // "touch" | "damage" | "callback" | "init"
    std::vector<float> args;
};

// Parses "touch@120,damage@240,callback@360:1" (rcsl_vm.py's --events syntax).
std::vector<ScheduledEvent> parseEvents(const std::string& spec);

inline const char* kStandardEvents = "touch@120,damage@240,callback@360:1";

struct DispatchRecord {
    std::string kind;
    bool ranHandler = false;
    bool ok = true;
    int stackDelta = 0;
};

struct RunOutcome {
    bool ok = true;
    std::string errorMessage;
    as3d::script::u64 instructions = 0;
    as3d::script::u64 builtinCalls = 0;
    std::vector<DispatchRecord> dispatches;
};

// Runs `program` under a fresh MockHost for `frames` frames of `dt` seconds, dispatching
// `init` at frame 0 (if `init` is true and the program has an init entry) before the
// first main update, then main, then the frame's scheduled events in the order
// touch/damage/callback (matching the engine's touch-then-projectile passes coming after
// the entity pass). `sink` (may be null) receives the full structured trace; `report`
// (may be null) aggregates builtin call counts.
RunOutcome runStandard(const as3d::script::ScriptProgram& program, int frames, float dt,
                        const std::string& eventsSpec, bool init, as3d::script::ITraceSink* sink,
                        as3d::script::BuiltinReport* report, const char* debugName = "");

} // namespace rcsl_tool
