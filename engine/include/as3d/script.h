// RCSL (compiled AirStrike 3D script) virtual machine.
//
// This is the public interface between the RCSL loader/interpreter and the rest of the
// engine. It follows docs/spec/rcsl-container.md (container format), rcsl-opcodes-v0.md
// (instruction set) and rcsl-vm.md (runtime contract, mock host, trace format) exactly;
// those specs and tools/ref/rcsl_vm.py are the ground truth. See docs/script-vm.md for an
// implementer's overview (architecture, registering builtins, attaching a trace sink,
// debugging a trace mismatch).
//
// No exceptions, no RTTI, no allocation per executed instruction: every failure is
// reported through a return value or an out-parameter, and ScriptThread pre-allocates its
// frame and stack at construction time.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "as3d/core.h"

namespace as3d::script {

// core.h does not define a 64-bit alias; only this header needs one (call counters).
using u64 = std::uint64_t;

// ---------------------------------------------------------------------------------------
// Container: sections, entry points, instructions.
// ---------------------------------------------------------------------------------------

// Header field 9..13 select what the engine runs for each event. See rcsl-container.md,
// "Entry points and events".
enum class EntryPoint : u8 { Init = 0, Main = 1, Damage = 2, Touch = 3, Callback = 4 };
constexpr int kNumEntryPoints = 5;
constexpr u32 kNoEntry = 0xFFFFFFFFu;

const char* entryPointName(EntryPoint ep);

// CASH entry kinds (rcsl-container.md, "CASH: precache list").
enum class CashKind : u8 { Object = 0, Sound = 3 };

struct CashEntry {
    u8 kind = 0;
    std::string name;
};

// DATA entry kinds (rcsl-container.md, "DATA: slot initialisers").
struct DataEntry {
    u16 kind = 0;
    i16 slot = 0;
    u32 value = 0;
};

// One CODE instruction (rcsl-container.md, "CODE: instructions").
struct Instruction {
    u8 op = 0;
    u8 mode = 0;
    i32 a = 0;
    i32 b = 0;
    i32 c = 0;
};

// Opcodes (rcsl-opcodes-v0.md, "Opcode table").
enum Opcode : u8 {
    OP_END = 0x00,
    OP_MUL = 0x01,
    OP_DIV = 0x02,
    OP_ADD = 0x03,
    OP_SUB = 0x04,
    OP_BAND = 0x05,
    OP_BOR = 0x06,
    OP_LOR = 0x07,
    OP_LAND = 0x08,
    OP_EQ = 0x09,
    OP_NE = 0x0A,
    OP_GT = 0x0B,
    OP_LT = 0x0C,
    OP_GE = 0x0D,
    OP_LE = 0x0E,
    OP_NOT = 0x0F,
    OP_NEG = 0x10,
    OP_MOV = 0x11,
    OP_LEA = 0x12,
    OP_PUSH = 0x13,
    OP_POP = 0x14,
    OP_ALLOC = 0x15,
    OP_NOP16 = 0x16,
    OP_NOP17 = 0x17,
    OP_RET = 0x18,
    OP_JNZ = 0x19,
    OP_JZ = 0x1A,
    OP_JMP = 0x1B,
    OP_CALL = 0x1C,
    OP_LCALL = 0x1D,
    OP_TMO = 0x1E,
    kMaxOpcode = 0x1E,
};

// Operand mode bits (rcsl-container.md, "Operand encoding").
enum ModeBits : u8 {
    M_IMM1 = 0x01,
    M_IND1 = 0x02,
    M_IMM2 = 0x10,
    M_IND2 = 0x20,
    M_IND3 = 0x80,
};

// An immutable, statically-validated loaded script. Owns every table it needs at run
// time; a ScriptThread is created directly from one (plus an IScriptHost) and never
// touches the raw bytes again.
class ScriptProgram {
public:
    ScriptProgram() = default;

    // Parses and statically validates `data[0..size)`. On success, fills `out` and
    // returns true. On any malformed input, returns false and (if `error` is non-null)
    // writes a short diagnostic; `out` is left in an unspecified but safe state. Never
    // crashes, never reads out of bounds, regardless of how `data` is corrupted.
    //
    // Validates everything that can be checked without a host: section framing and
    // counts, name encodings, DATA/STRG/CODE lengths, entry points, and every operand
    // that refers to a DEFS index, FUNC index, frame slot, jump target or call target
    // (rcsl-opcodes-v0.md, "Operand encoding" and the opcode table). Field access
    // through LEA (entity/global field indices) cannot be range-checked statically
    // (the valid range depends on what the field addresses; see rcsl-vm.md) and is
    // checked by the host at run time instead.
    static bool load(const u8* data, size_t size, ScriptProgram& out, std::string* error);

    u32 header1() const { return header1_; }
    u32 frameSlots() const { return frameSlots_; }
    u32 entry(EntryPoint ep) const { return entries_[static_cast<int>(ep)]; }
    bool hasEntry(EntryPoint ep) const { return entry(ep) != kNoEntry; }

    const std::vector<CashEntry>& cash() const { return cash_; }
    const std::vector<std::string>& defs() const { return defs_; }
    const std::vector<std::string>& funcs() const { return funcs_; }
    const std::vector<DataEntry>& data() const { return data_; }
    const std::vector<Instruction>& code() const { return code_; }
    const std::vector<u8>& strg() const { return strg_; }

    // True if `offset` is exactly the start of a NUL-terminated string in STRG: offset 0
    // (if STRG is non-empty), or right after some NUL byte, and inside range. Matches
    // rcsl_disasm.py's `string_starts()`.
    bool isStringStart(u32 offset) const;
    // NUL-terminated bytes at a validated string start, or nullptr if `offset` is not one
    // (see isStringStart). The returned pointer is valid for the lifetime of *this.
    const char* stringAt(u32 offset) const;

    // Notifies the host of every CASH entry once, e.g. right after loading, so it can
    // precache object definitions and sounds. Safe to call more than once; a host should
    // treat repeats as no-ops it if already precached the name.
    void notifyPrecache(class IScriptHost& host) const;

private:
    u32 header1_ = 0;
    u32 frameSlots_ = 16;
    u32 entries_[kNumEntryPoints] = {kNoEntry, kNoEntry, kNoEntry, kNoEntry, kNoEntry};
    std::vector<CashEntry> cash_;
    std::vector<std::string> defs_;
    std::vector<std::string> funcs_;
    std::vector<DataEntry> data_;
    std::vector<u8> strg_;
    std::vector<Instruction> code_;
    std::vector<u32> stringStarts_; // sorted
};

// ---------------------------------------------------------------------------------------
// Host interface.
// ---------------------------------------------------------------------------------------

// A 32-bit address in the host's address space: the same raw bits the original engine
// stored in slots for pointers (entity references, field addresses, ...). Frame slots
// live in a small reserved window starting at kFrameBase (see below) that a host must
// never use for anything else; every other address is opaque to the VM and only ever
// round-tripped through IScriptHost.
using Addr = u32;

// Reserved address window for a thread's own frame slots, handled internally by
// ScriptThread (never routed through IScriptHost). A script has no way to obtain another
// thread's frame address, so every ScriptThread may safely reuse the same window; a host
// must not resolve any global, entity field or other address inside it. Frame size is at
// most 81 slots in the shipped corpus (rcsl-container.md), so the window is kept well
// clear of that with room to spare.
constexpr Addr kFrameBase = 0x1000'0000u;
constexpr u32 kMaxFrameSlots = 256;
constexpr Addr kFrameWindowEnd = kFrameBase + 4 * kMaxFrameSlots;
// Frame slots 0..15 are temporaries: saved and restored around event handlers other than
// main (rcsl-container.md, "Frame slots").
constexpr u32 kNumTemps = 16;

enum class ReturnKind : u8 { None, Float, IntAsFloat, Entity, Pointer };
enum class DoneFlagKind : u8 { None, Zero, Computed };
enum class BuiltinStatus : u8 { Implemented, Approximate, Stub };

// Accessor a builtin implementation uses to read its arguments and produce its result.
// Passed to a BuiltinFn by ScriptThread; the implementation must not retain it past the
// call. Argument k is frame slot t(k) as it stood immediately before the call.
class BuiltinArgs {
public:
    virtual ~BuiltinArgs() = default;

    virtual int count() const = 0;
    virtual u32 bits(int index) const = 0;
    float f32(int index) const;

    // Dereferences a vec/vec_out argument (a pointer to 3 floats). Returns false (and
    // leaves `out` unchanged) if any of the 3 words cannot be read.
    virtual bool readVec3(int index, float out[3]) const = 0;
    virtual bool writeVec3(int index, const float in[3]) const = 0;

    // True if the argument's raw bits are a valid STRG offset (rcsl-container.md).
    virtual bool isStringArg(int index) const = 0;
    // NUL-terminated string at a valid string argument, else nullptr.
    virtual const char* cstr(int index) const = 0;

    virtual void setReturnBits(u32 bits) = 0;
    void setReturnFloat(float v);

    // True if this call is a latent call (opcode LCALL); only then does setDone matter.
    virtual bool latent() const = 0;
    virtual void setDone(bool done) = 0;
    // The thread's pending latent timeout (raw bits of a float, thread +0x820), as set by
    // the most recent TMO and not yet touched by this LCALL's own countdown (rcsl-vm.md,
    // "LCALL and TMO"). Lets a host replicate the mock's rule that a latent call with a
    // zero timeout completes immediately.
    virtual u32 timeoutBits() const = 0;

    virtual class IScriptHost& host() const = 0;
    // Convenience for a builtin that needs to read/write an arbitrary address (e.g. an
    // entity field reached through an argument that is itself an entity reference).
    virtual bool readWord(Addr addr, u32& outBits) const = 0;
    virtual bool writeWord(Addr addr, u32 bits) const = 0;

    // Lets a builtin implementation abort the whole invocation with a VM error (e.g. an
    // argument that fails the mock host's type validation), matching the reference
    // interpreter's mock host, which can raise mid-call. Unused by a builtin that never
    // fails; ScriptThread checks failed() right after the call and, if set, stops the
    // invocation the same way any other VM error does (an X trace line, no B line).
    virtual bool failed() const = 0;
    virtual void fail(const char* message) = 0;
};

using BuiltinFn = void (*)(BuiltinArgs& args, void* userData);

// One entry of a host's builtin table. `argCount` is the number of frame slots (t0..)
// the implementation reads; it must match the documented arity in
// docs/spec/rcsl-builtins-table.md for a builtin with that name (the VM uses the
// documented arity, not this field, to decide how many argument words appear in a trace
// line and to size an auto-generated stub -- see findBuiltinMeta).
struct BuiltinDesc {
    const char* name = nullptr;
    int argCount = 0;
    BuiltinFn fn = nullptr;
    void* userData = nullptr;
    BuiltinStatus status = BuiltinStatus::Implemented;
};

// The engine side of the VM: naming, memory and the builtin table. A host implementation
// (the mock host in apps/rcsl_tool, eventually a real game host) subclasses this.
class IScriptHost {
public:
    virtual ~IScriptHost() = default;

    // Resolves a DEFS global name to its storage address, once per ScriptThread at
    // construction (mirrors the original's per-thread DEFS resolution table,
    // rcsl-container.md). An unknown name should return 0 (unmapped); this matches the
    // original silently resolving to NULL (rcsl-vm.md quirk 8) -- any later access then
    // fails cleanly through readWord/writeWord rather than crashing. Must be a pure
    // function of `name` (repeated calls with the same name must return the same
    // address), since it is also used internally for the VM's own access to
    // "frametime" independent of any particular script's DEFS table.
    virtual Addr resolveGlobal(const char* name) = 0;

    // Resolves a builtin name to a callable entry, once per ScriptThread at
    // construction. Returning nullptr for a name the host does not implement is fine:
    // the VM installs a stub automatically using the documented arity (findBuiltinMeta),
    // so binding always succeeds (see script-vm.md).
    virtual const BuiltinDesc* resolveBuiltin(const char* name) = 0;

    // Reads/writes one 32-bit word outside the calling thread's own frame (a global, an
    // entity field, heap, ...). Returns false if `addr` is not mapped; the VM turns that
    // into a script error only when the access was one the instruction actually needed
    // (rcsl-vm.md quirk 7: unused operands may read garbage/unmapped silently).
    virtual bool readWord(Addr addr, u32& outBits) = 0;
    virtual bool writeWord(Addr addr, u32 bits) = 0;

    // Called once per CASH entry, e.g. from ScriptProgram::notifyPrecache after loading.
    virtual void precache(u8 kind, const char* name) { (void)kind; (void)name; }

    // Reports a VM-level error (see also ITraceSink::vmError for the structured trace
    // line). `scriptName` may be empty if the host did not provide one.
    virtual void onScriptError(const char* scriptName, const char* message) {
        (void)scriptName;
        (void)message;
    }
};

// Static metadata for the 85 engine builtins (docs/spec/rcsl-builtins-table.md): just
// enough for the VM to size and classify an auto-generated stub when a host's
// resolveBuiltin returns nullptr for a name it does not implement. Not a substitute for
// the host's own table; a host needing full argument-type information (as the mock host
// does, to validate calls the way the reference VM's mock host does) keeps its own copy.
struct BuiltinMeta {
    const char* name;
    u8 arity;
    ReturnKind returns;
    DoneFlagKind doneFlag;
};
const BuiltinMeta* findBuiltinMeta(const char* name);
size_t builtinMetaCount();
const BuiltinMeta& builtinMetaAt(size_t index);

// The same per game, by the builtin set of its profile (GameProfile::builtinSet): "v170" (the
// 85 above, also for an unknown or null set), "v251" and "v271" (the 101 of AirStrike 2 and
// Gulf Thunder, docs/spec/as2/rcsl-builtins-table.delta.md), in the executable's table order.
struct BuiltinMetaTable {
    const BuiltinMeta* rows;
    size_t count;
};
BuiltinMetaTable builtinMetaTable(const char* builtinSet);
const BuiltinMeta* findBuiltinMeta(const char* builtinSet, const char* name);

// Aggregates per-builtin call counts and implementation status across a run, and logs
// the first call to any auto-generated stub once. A host or tool owns one and passes it
// (optionally -- pass nullptr to ScriptThread for zero overhead) to every ScriptThread
// it creates.
class BuiltinReport {
public:
    struct Row {
        std::string name;
        u64 calls = 0;
        BuiltinStatus status = BuiltinStatus::Implemented;
    };

    void noteCall(const char* name, BuiltinStatus status);
    std::vector<Row> rows() const;

private:
    struct Entry {
        u64 calls = 0;
        BuiltinStatus status = BuiltinStatus::Implemented;
    };
    std::vector<std::pair<std::string, Entry>> entries_; // small N (<=85); linear scan is fine
};

// ---------------------------------------------------------------------------------------
// Trace sink.
// ---------------------------------------------------------------------------------------

// Structured trace events, one method per line kind of the reference trace format
// (rcsl-vm.md, "Trace format"). Attaching a sink costs nothing when not attached
// (ScriptThread holds a possibly-null pointer and checks it before every call).
class ITraceSink {
public:
    virtual ~ITraceSink() = default;

    virtual void dispatchStart(u32 frame, EntryPoint entry) = 0;
    // hasValue selects between a formatted `value` and `-`.
    virtual void instruction(u32 frame, EntryPoint entry, int depth, u32 pc, u8 op,
                              bool hasValue, u32 value) = 0;
    virtual void builtinCall(u32 frame, EntryPoint entry, u32 pc, const char* name,
                              const u32* args, int argCount, u32 retBits) = 0;
    virtual void dispatchEnd(u32 frame, EntryPoint entry, int status, int stackDelta) = 0;
    // For a host's own damage routine (as the mock host's does); the core VM never calls
    // this itself.
    virtual void damage(u32 frame, bool skipped, u32 amountBits) = 0;
    virtual void vmError(u32 frame, EntryPoint entry, u32 pc, const char* message) = 0;
};

// A trace sink that renders the exact ASCII text format of tools/ref/rcsl_vm.py (see
// rcsl-vm.md, "Trace format"), appending to an internal buffer. Used by rcsl_tool and by
// the golden-hash test.
class TextTraceSink : public ITraceSink {
public:
    void dispatchStart(u32 frame, EntryPoint entry) override;
    void instruction(u32 frame, EntryPoint entry, int depth, u32 pc, u8 op, bool hasValue,
                      u32 value) override;
    void builtinCall(u32 frame, EntryPoint entry, u32 pc, const char* name, const u32* args,
                      int argCount, u32 retBits) override;
    void dispatchEnd(u32 frame, EntryPoint entry, int status, int stackDelta) override;
    void damage(u32 frame, bool skipped, u32 amountBits) override;
    void vmError(u32 frame, EntryPoint entry, u32 pc, const char* message) override;

    const std::string& text() const { return buf_; }
    void clear() { buf_.clear(); }

private:
    std::string buf_;
};

// ---------------------------------------------------------------------------------------
// Thread: per-entity script state and the interpreter.
// ---------------------------------------------------------------------------------------

// Result of one dispatch (runMain or runEvent).
struct DispatchResult {
    bool ranHandler = false; // false if the entry point is absent (kNoEntry): no-op
    bool ok = true;          // false if a VM error stopped execution
    int status = 0;          // 0 (END / waiting or completed LCALL) or 1 (RET)
    int stackDelta = 0;      // stack depth change across the dispatch
};

// Per-instance script state: pc, frame, stack, latent timeout. Created directly from a
// ScriptProgram and an IScriptHost; binds DEFS and FUNC once at construction (mirroring
// the original's per-thread resolution tables) and applies DATA to a freshly zeroed
// frame. Fixed-size after construction: no further allocation happens while running.
class ScriptThread {
public:
    // `report` and `sink` may be null. `debugName` is used only for error messages/logs.
    ScriptThread(const ScriptProgram& program, IScriptHost& host, BuiltinReport* report = nullptr,
                 ITraceSink* sink = nullptr, const char* debugName = "");

    bool valid() const { return valid_; }
    const std::string& bindError() const { return bindError_; }

    void setTraceSink(ITraceSink* sink) { sink_ = sink; }
    void setBuiltinReport(BuiltinReport* report) { report_ = report; }

    // Runs one update of `main`, resuming where it last stopped (or doing nothing if the
    // program has no `main`). Writes `dt`'s bits to the host's "frametime" global first
    // (frame-independent of any particular script's own DEFS table -- see
    // IScriptHost::resolveGlobal), so scripts reading $frametime and the VM's own LCALL
    // timeout math agree. `frame` is only used for trace output.
    DispatchResult runMain(u32 frame, float dt);

    // Runs one of the other four entry points synchronously to completion, saving and
    // restoring pc and frame slots t0..t15 around it (not the PUSH/POP stack); a no-op if
    // the entry point is absent. The caller is responsible for setting up whatever
    // globals the handler expects first (e.g. `other` before Touch, `cb_msg`/`cb_parm1`/
    // `cb_parm2` before Callback) -- see rcsl-vm.md, "Event dispatch".
    DispatchResult runEvent(EntryPoint ep, u32 frame);

    u32 pc() const { return pc_; }
    int stackDepth() const { return static_cast<int>(stack_.size()); }
    u32 frameSlot(u32 index) const { return index < frame_.size() ? frame_[index] : 0; }
    void setFrameSlot(u32 index, u32 bits) {
        if (index < frame_.size()) frame_[index] = bits;
    }

    // Reads/writes one 32-bit word anywhere in this thread's view of memory: its own
    // frame window (kFrameBase..) is handled locally, everything else is forwarded to
    // the host. Used internally for operand access and exposed publicly because a
    // builtin's BuiltinArgs (see script.h) needs exactly this to dereference vec/vec_out
    // arguments that may point into the frame (a vector script variable) or into host
    // memory (an entity field).
    bool readWord(Addr addr, u32& outBits) const;
    bool writeWord(Addr addr, u32 bits);

    // The pending latent timeout, unmodified by whatever LCALL is currently executing a
    // builtin (see BuiltinArgs::timeoutBits).
    u32 timeoutBits() const { return timeoutBits_; }

    // The return register. In the original it is one engine global shared by every thread
    // (rcsl-vm.md "Thread state", quirks 5 and 9; builtins semantics D3): a RET or a
    // value-returning builtin in a nested handler changes what the calling CALL receives.
    // A host reproduces that by pointing every thread at one register it owns (it must
    // outlive the threads); without it each thread keeps its own. A builtin's
    // BuiltinArgs::setReturnBits writes the register at once, so a builtin that runs
    // handlers after setting its result (`create`) sees them overwrite it.
    void setSharedReturnRegister(u32* reg) { sharedRetreg_ = reg; }
    u32 returnRegisterBits() const { return sharedRetreg_ ? *sharedRetreg_ : retregBits_; }
    void setReturnRegisterBits(u32 bits) { retreg() = bits; }

    const ScriptProgram& program() const { return program_; }

private:
    // -- decode/execute (see rcsl-opcodes-v0.md, "Operand encoding" and opcode table) --
    // A decoded operand location: either an immediate value, or an address (in the frame
    // window or in the host's address space) to read/write through.
    struct Loc {
        enum class Kind : u8 { Imm, Addr } kind;
        u32 value;
    };

    bool bind();
    bool loadOp(const Loc& loc, u32& outBits);
    bool storeOp(const Loc& loc, u32 bits);
    bool decode(const Instruction& ins, Loc& a, Loc& b, Loc& c);
    Addr globalAddr(i32 raw) const;
    static Addr slotAddr(i32 v);
    bool derefOperand(const Loc& in, bool used, Loc& out);

    // Runs from the current pc until END/RET/an LCALL stops the invocation, or an error
    // occurs. Returns false on error (an X line has already been emitted if a sink is
    // attached). `status` is set to 0 or 1. Uses currentEntry_/currentFrame_, set by
    // runMain/runEvent and unchanged across any nested subroutine calls within one
    // dispatch.
    bool runInvocation(int& status);
    bool execBuiltin(i32 a, bool latent, u32& doneOrRet);
    bool execSubroutine(i32 target, int& status);
    void fail(u32 pc, const std::string& message);
    void traceInstruction(u32 pc, u8 op, bool hasValue, u32 value);

    const ScriptProgram& program_;
    IScriptHost& host_;
    BuiltinReport* report_;
    ITraceSink* sink_;
    std::string debugName_;

    bool valid_ = false;
    std::string bindError_;

    std::vector<Addr> defsAddr_;               // resolved once, indexed like program_.defs()
    std::vector<const BuiltinDesc*> funcDesc_;  // resolved once, indexed like program_.funcs()
    std::vector<BuiltinDesc> autoStubs_;        // storage for generated stubs
    Addr frametimeAddr_ = 0;

    std::vector<u32> frame_;   // frameSlots() words, frame_[i] lives at kFrameBase + 4*i
    std::vector<u32> stack_;   // PUSH/POP stack, up to 512 entries
    i32 pc_ = -1;              // -1: no main entry (thread never runs main)
    u32 timeoutBits_ = 0;      // latent timeout (thread +0x820)
    u32 retregBits_ = 0;       // return register when not shared
    u32* sharedRetreg_ = nullptr; // the host's shared register (see setSharedReturnRegister)
    u32& retreg() { return sharedRetreg_ ? *sharedRetreg_ : retregBits_; }
    int depth_ = 0;            // trace nesting depth (subroutine calls)

    EntryPoint currentEntry_ = EntryPoint::Main;
    u32 currentFrame_ = 0;
};

} // namespace as3d::script
