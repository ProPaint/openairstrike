#include "runner.h"

#include <algorithm>
#include <cstring>

#include "mock_host.h"

using namespace as3d::script;

namespace rcsl_tool {

namespace {

// Counts executed instructions and builtin calls (matching tools/ref/rcsl_vm.py's
// vm.counts) while forwarding every event to an optional inner sink. Attaching only this
// (inner == nullptr) keeps per-frame overhead to two integer increments, which is what
// the performance-sanity run uses.
class CountingSink : public ITraceSink {
public:
    explicit CountingSink(ITraceSink* inner) : inner_(inner) {}

    void dispatchStart(u32 frame, EntryPoint entry) override {
        if (inner_) inner_->dispatchStart(frame, entry);
    }
    void instruction(u32 frame, EntryPoint entry, int depth, u32 pc, as3d::u8 op, bool hasValue,
                      u32 value) override {
        instructions_++;
        if (inner_) inner_->instruction(frame, entry, depth, pc, op, hasValue, value);
    }
    void builtinCall(u32 frame, EntryPoint entry, u32 pc, const char* name, const u32* args,
                      int argCount, u32 retBits) override {
        builtinCalls_++;
        if (inner_) inner_->builtinCall(frame, entry, pc, name, args, argCount, retBits);
    }
    void dispatchEnd(u32 frame, EntryPoint entry, int status, int stackDelta) override {
        if (inner_) inner_->dispatchEnd(frame, entry, status, stackDelta);
    }
    void damage(u32 frame, bool skipped, u32 amountBits) override {
        if (inner_) inner_->damage(frame, skipped, amountBits);
    }
    void vmError(u32 frame, EntryPoint entry, u32 pc, const char* message) override {
        if (inner_) inner_->vmError(frame, entry, pc, message);
    }

    as3d::script::u64 instructions() const { return instructions_; }
    as3d::script::u64 builtinCalls() const { return builtinCalls_; }

private:
    ITraceSink* inner_;
    as3d::script::u64 instructions_ = 0;
    as3d::script::u64 builtinCalls_ = 0;
};

} // namespace

std::vector<ScheduledEvent> parseEvents(const std::string& spec) {
    std::vector<ScheduledEvent> out;
    size_t pos = 0;
    while (pos < spec.size()) {
        size_t comma = spec.find(',', pos);
        std::string item = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        pos = (comma == std::string::npos) ? spec.size() : comma + 1;
        if (item.empty()) continue;
        size_t at = item.find('@');
        if (at == std::string::npos) continue;
        ScheduledEvent ev;
        ev.kind = item.substr(0, at);
        std::string rest = item.substr(at + 1);
        size_t p2 = 0;
        bool first = true;
        while (p2 <= rest.size()) {
            size_t colon = rest.find(':', p2);
            std::string field =
                rest.substr(p2, colon == std::string::npos ? std::string::npos : colon - p2);
            if (first) {
                ev.frame = std::atoi(field.c_str());
                first = false;
            } else {
                ev.args.push_back(static_cast<float>(std::atof(field.c_str())));
            }
            if (colon == std::string::npos) break;
            p2 = colon + 1;
        }
        out.push_back(std::move(ev));
    }
    return out;
}

RunOutcome runStandard(const ScriptProgram& program, int frames, float dt, const std::string& eventsSpec,
                        bool init, ITraceSink* sink, BuiltinReport* report, const char* debugName) {
    RunOutcome outcome;
    MockHost host(dt);
    CountingSink counting(sink);
    ScriptThread thread(program, host, report, &counting, debugName);
    if (!thread.valid()) {
        outcome.ok = false;
        outcome.errorMessage = "bind failed: " + thread.bindError();
        return outcome;
    }

    std::vector<ScheduledEvent> events = parseEvents(eventsSpec);
    float timeAcc = 0.0f;

    auto order = [](const std::string& kind) {
        if (kind == "init") return 0;
        if (kind == "touch") return 1;
        if (kind == "damage") return 2;
        if (kind == "callback") return 3;
        return 9;
    };

    for (int frame = 0; frame < frames; ++frame) {
        host.setGlobal("time", floatToBits(timeAcc));

        if (frame == 0 && init) {
            DispatchResult d = thread.runEvent(EntryPoint::Init, static_cast<u32>(frame));
            outcome.dispatches.push_back({"init", d.ranHandler, d.ok, d.stackDelta});
            if (!d.ok) {
                outcome.ok = false;
                break;
            }
        }
        {
            DispatchResult d = thread.runMain(static_cast<u32>(frame), dt);
            outcome.dispatches.push_back({"main", d.ranHandler, d.ok, d.stackDelta});
            if (!d.ok) {
                outcome.ok = false;
                break;
            }
        }

        std::vector<const ScheduledEvent*> thisFrame;
        for (const ScheduledEvent& e : events) {
            if (e.frame == frame) thisFrame.push_back(&e);
        }
        std::stable_sort(thisFrame.begin(), thisFrame.end(),
                          [&](const ScheduledEvent* a, const ScheduledEvent* b) {
                              return order(a->kind) < order(b->kind);
                          });

        bool stop = false;
        for (const ScheduledEvent* e : thisFrame) {
            if (e->kind == "touch") {
                u32 prev = host.getGlobal("other");
                host.setGlobal("other", host.entityRef(kMockOther));
                DispatchResult d = thread.runEvent(EntryPoint::Touch, static_cast<u32>(frame));
                host.setGlobal("other", prev);
                outcome.dispatches.push_back({"touch", d.ranHandler, d.ok, d.stackDelta});
                if (!d.ok) {
                    outcome.ok = false;
                    stop = true;
                    break;
                }
            } else if (e->kind == "damage") {
                float amount = e->args.empty() ? 10.0f : e->args[0];
                DispatchResult d = host.damage(thread, &counting, static_cast<u32>(frame), amount);
                outcome.dispatches.push_back({"damage", d.ranHandler, d.ok, d.stackDelta});
                if (!d.ok) {
                    outcome.ok = false;
                    stop = true;
                    break;
                }
            } else if (e->kind == "callback") {
                float vals[3] = {0.0f, 0.0f, 0.0f};
                for (size_t i = 0; i < e->args.size() && i < 3; ++i) vals[i] = e->args[i];
                host.setGlobal("cb_msg", floatToBits(vals[0]));
                host.setGlobal("cb_parm1", floatToBits(vals[1]));
                host.setGlobal("cb_parm2", floatToBits(vals[2]));
                DispatchResult d = thread.runEvent(EntryPoint::Callback, static_cast<u32>(frame));
                outcome.dispatches.push_back({"callback", d.ranHandler, d.ok, d.stackDelta});
                if (!d.ok) {
                    outcome.ok = false;
                    stop = true;
                    break;
                }
            } else if (e->kind == "init") {
                DispatchResult d = thread.runEvent(EntryPoint::Init, static_cast<u32>(frame));
                outcome.dispatches.push_back({"init", d.ranHandler, d.ok, d.stackDelta});
                if (!d.ok) {
                    outcome.ok = false;
                    stop = true;
                    break;
                }
            }
        }
        if (stop) break;
        timeAcc += dt;
    }

    outcome.instructions = counting.instructions();
    outcome.builtinCalls = counting.builtinCalls();
    if (!outcome.ok && outcome.errorMessage.empty()) outcome.errorMessage = host.errorMessage();
    return outcome;
}

} // namespace rcsl_tool
