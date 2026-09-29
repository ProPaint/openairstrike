// RCSL interpreter: operand decoding, the instruction loop, event dispatch. Ported
// instruction-for-instruction from tools/ref/rcsl_vm.py's VM/Thread classes so traces
// match bit for bit; see docs/spec/rcsl-opcodes-v0.md and rcsl-vm.md for the semantics
// this implements.
#include "as3d/script.h"

#include <cstring>

#include "script_internal.h"

namespace as3d::script {

namespace {

using detail::farith;
using detail::floatToBits;
using detail::bitsToFloat;
using detail::ftol;
using detail::itof;
using detail::quiet;
using detail::Role;
using detail::RoleSet;
using detail::roleSetFor;

constexpr int kStallBudget = 10000;
constexpr size_t kStackSize = 512;

// BuiltinArgs backed directly by a ScriptThread's frame and host. Argument k is exactly
// frame slot t(k), matching the original's argument pointer (always the calling
// thread's own slot 0) -- see rcsl-builtins-table.md, "Calling convention recap".
class ThreadBuiltinArgs final : public BuiltinArgs {
public:
    ThreadBuiltinArgs(ScriptThread& owner, IScriptHost& host, int argCount, bool latent)
        : owner_(owner), host_(host), count_(argCount), latent_(latent) {}

    int count() const override { return count_; }
    u32 bits(int index) const override {
        return (index >= 0 && index < count_) ? owner_.frameSlot(static_cast<u32>(index)) : 0;
    }
    bool readVec3(int index, float out[3]) const override {
        if (index < 0 || index >= count_) return false;
        Addr base = bits(index);
        u32 words[3];
        for (int k = 0; k < 3; ++k) {
            if (!owner_.readWord(base + 4u * static_cast<u32>(k), words[k])) return false;
        }
        std::memcpy(out, words, sizeof words);
        return true;
    }
    bool writeVec3(int index, const float in[3]) const override {
        if (index < 0 || index >= count_) return false;
        Addr base = bits(index);
        u32 words[3];
        std::memcpy(words, in, sizeof words);
        for (int k = 0; k < 3; ++k) {
            if (!owner_.writeWord(base + 4u * static_cast<u32>(k), words[k])) return false;
        }
        return true;
    }
    bool isStringArg(int index) const override {
        return index >= 0 && index < count_ && owner_.program().isStringStart(bits(index));
    }
    const char* cstr(int index) const override {
        if (!isStringArg(index)) return nullptr;
        return owner_.program().stringAt(bits(index));
    }
    // Written through at once: a nested handler run later by the same builtin can still
    // replace it (rcsl-vm.md quirk 9).
    void setReturnBits(u32 b) override {
        retreg_ = b;
        wroteReturn_ = true;
        owner_.setReturnRegisterBits(b);
    }
    bool latent() const override { return latent_; }
    void setDone(bool d) override { done_ = d; }
    u32 timeoutBits() const override { return owner_.timeoutBits(); }
    IScriptHost& host() const override { return host_; }
    bool readWord(Addr addr, u32& outBits) const override { return owner_.readWord(addr, outBits); }
    bool writeWord(Addr addr, u32 b) const override {
        return const_cast<ScriptThread&>(owner_).writeWord(addr, b);
    }
    bool failed() const override { return failed_; }
    void fail(const char* message) override {
        failed_ = true;
        failMessage_ = message ? message : "builtin failed";
    }

    bool wroteReturn() const { return wroteReturn_; }
    u32 retreg() const { return retreg_; }
    bool done() const { return done_; }
    const std::string& failMessage() const { return failMessage_; }

private:
    ScriptThread& owner_;
    IScriptHost& host_;
    int count_;
    bool latent_;
    bool wroteReturn_ = false;
    u32 retreg_ = 0;
    bool done_ = false;
    bool failed_ = false;
    std::string failMessage_;
};

} // namespace

// ---------------------------------------------------------------------------------------
// Construction and binding.
// ---------------------------------------------------------------------------------------

ScriptThread::ScriptThread(const ScriptProgram& program, IScriptHost& host, BuiltinReport* report,
                            ITraceSink* sink, const char* debugName)
    : program_(program),
      host_(host),
      report_(report),
      sink_(sink),
      debugName_(debugName ? debugName : "") {
    frame_.assign(kMaxFrameSlots, 0);
    stack_.reserve(kStackSize);
    valid_ = bind();
    if (!valid_) return;

    for (const DataEntry& e : program_.data()) {
        if (e.kind == 2) {
            frame_[static_cast<u32>(e.slot)] = e.value;
        } else if (e.kind == 3) {
            frame_[static_cast<u32>(e.slot)] = kFrameBase + 4u * e.value;
        }
    }
    pc_ = program_.hasEntry(EntryPoint::Main) ? static_cast<i32>(program_.entry(EntryPoint::Main)) : -1;
}

bool ScriptThread::bind() {
    defsAddr_.clear();
    defsAddr_.reserve(program_.defs().size());
    for (const std::string& name : program_.defs()) defsAddr_.push_back(host_.resolveGlobal(name.c_str()));

    autoStubs_.clear();
    autoStubs_.reserve(program_.funcs().size());
    funcDesc_.clear();
    funcDesc_.reserve(program_.funcs().size());
    for (const std::string& name : program_.funcs()) {
        const BuiltinDesc* desc = host_.resolveBuiltin(name.c_str());
        if (desc != nullptr) {
            funcDesc_.push_back(desc);
            continue;
        }
        const BuiltinMeta* meta = findBuiltinMeta(name.c_str());
        BuiltinDesc stub;
        stub.name = name.c_str(); // program_ outlives this thread
        stub.argCount = meta ? meta->arity : 0;
        stub.status = BuiltinStatus::Stub;
        stub.userData = const_cast<void*>(static_cast<const void*>(meta));
        stub.fn = [](BuiltinArgs& args, void* userData) {
            const BuiltinMeta* m = static_cast<const BuiltinMeta*>(userData);
            ReturnKind rk = m ? m->returns : ReturnKind::None;
            DoneFlagKind dk = m ? m->doneFlag : DoneFlagKind::None;
            if (rk != ReturnKind::None) args.setReturnBits(0);
            if (args.latent() && dk == DoneFlagKind::Zero) args.setDone(false);
            // DoneFlagKind::None/Computed: leave the done flag as LCALL cleared it (0),
            // so a stub of a "computed" builtin (e.g. an unimplemented RotateTo) ends
            // only by timeout, matching rcsl-vm.md's rule for every non-sleep, non-
            // motion builtin.
        };
        autoStubs_.push_back(stub);
        funcDesc_.push_back(&autoStubs_.back());
    }
    // autoStubs_ addresses must stay valid: reserved above to its final size, so no
    // reallocation happens as we push_back.

    frametimeAddr_ = host_.resolveGlobal("frametime");
    return true;
}

// ---------------------------------------------------------------------------------------
// Memory access.
// ---------------------------------------------------------------------------------------

bool ScriptThread::readWord(Addr addr, u32& outBits) const {
    if (addr >= kFrameBase && addr < kFrameWindowEnd) {
        outBits = frame_[(addr - kFrameBase) / 4];
        return true;
    }
    return const_cast<IScriptHost&>(host_).readWord(addr, outBits);
}

bool ScriptThread::writeWord(Addr addr, u32 bits) {
    if (addr >= kFrameBase && addr < kFrameWindowEnd) {
        frame_[(addr - kFrameBase) / 4] = bits;
        return true;
    }
    return host_.writeWord(addr, bits);
}

Addr ScriptThread::globalAddr(i32 raw) const {
    u32 i = static_cast<u32>(-raw - 1);
    if (i >= defsAddr_.size()) return 0; // out-of-range index on an unused operand: never
                                          // actually dereferenced meaningfully (rcsl-vm.md
                                          // quirk 7); a used one is caught at load time.
    return defsAddr_[i];
}

Addr ScriptThread::slotAddr(i32 v) { return kFrameBase + 4u * static_cast<u32>(v); }

bool ScriptThread::loadOp(const Loc& loc, u32& outBits) {
    if (loc.kind == Loc::Kind::Imm) {
        outBits = loc.value;
        return true;
    }
    if (readWord(loc.value, outBits)) return true;
    fail(static_cast<u32>(pc_), "access to unmapped address");
    return false;
}

bool ScriptThread::storeOp(const Loc& loc, u32 bits) {
    if (loc.kind == Loc::Kind::Imm) return true; // original writes a scratch copy; no effect
    if (writeWord(loc.value, bits)) return true;
    fail(static_cast<u32>(pc_), "access to unmapped address");
    return false;
}

bool ScriptThread::derefOperand(const Loc& in, bool used, Loc& out) {
    if (used) {
        u32 v;
        if (!loadOp(in, v)) return false;
        out = Loc{Loc::Kind::Addr, v};
        return true;
    }
    // Soft read for an operand the handler does not use: never faults, reads 0 from
    // unmapped memory (rcsl-vm.md quirk 7).
    if (in.kind == Loc::Kind::Imm) {
        out = Loc{Loc::Kind::Addr, in.value};
        return true;
    }
    u32 v = 0;
    readWord(in.value, v); // ignore failure: v stays 0
    out = Loc{Loc::Kind::Addr, v};
    return true;
}

bool ScriptThread::decode(const Instruction& ins, Loc& aOut, Loc& bOut, Loc& cOut) {
    RoleSet roles{Role::None, Role::None, Role::None};
    roleSetFor(ins.op, roles); // always succeeds: the loader rejects unknown opcodes

    // Operand A.
    Loc a;
    if (ins.mode & M_IMM1) {
        a = Loc{Loc::Kind::Imm, quiet(static_cast<u32>(ins.a))};
    } else if (ins.a < 0 && ins.op != OP_ALLOC && ins.op != OP_JMP && ins.op != OP_CALL &&
               ins.op != OP_LCALL) {
        a = Loc{Loc::Kind::Addr, globalAddr(ins.a)};
    } else {
        a = Loc{Loc::Kind::Addr, slotAddr(ins.a)};
    }
    if (ins.mode & M_IND1) {
        Loc tmp;
        if (!derefOperand(a, roles.a != Role::None, tmp)) return false;
        a = tmp;
    }

    // Operand B.
    Loc b;
    if (ins.mode & M_IMM2) {
        b = Loc{Loc::Kind::Imm, quiet(static_cast<u32>(ins.b))};
    } else if (ins.b < 0 && ins.op != OP_JNZ && ins.op != OP_JZ) {
        b = Loc{Loc::Kind::Addr, globalAddr(ins.b)};
    } else {
        b = Loc{Loc::Kind::Addr, slotAddr(ins.b)};
    }
    if (ins.mode & M_IND2) {
        bool used = (roles.b == Role::Read || roles.b == Role::Write);
        Loc tmp;
        if (!derefOperand(b, used, tmp)) return false;
        b = tmp;
    }

    // Operand C: never immediate.
    Loc c;
    if (ins.c < 0) {
        c = Loc{Loc::Kind::Addr, globalAddr(ins.c)};
    } else {
        c = Loc{Loc::Kind::Addr, slotAddr(ins.c)};
    }
    if (ins.mode & M_IND3) {
        Loc tmp;
        if (!derefOperand(c, roles.c != Role::None, tmp)) return false;
        c = tmp;
    }

    aOut = a;
    bOut = b;
    cOut = c;
    return true;
}

// ---------------------------------------------------------------------------------------
// Trace / error helpers.
// ---------------------------------------------------------------------------------------

void ScriptThread::traceInstruction(u32 pc, u8 op, bool hasValue, u32 value) {
    if (sink_) sink_->instruction(currentFrame_, currentEntry_, depth_, pc, op, hasValue, value);
}

void ScriptThread::fail(u32 pc, const std::string& message) {
    if (sink_) sink_->vmError(currentFrame_, currentEntry_, pc, message.c_str());
    host_.onScriptError(debugName_.c_str(), message.c_str());
}

// ---------------------------------------------------------------------------------------
// Builtin and subroutine calls.
// ---------------------------------------------------------------------------------------

bool ScriptThread::execBuiltin(i32 a, bool latent, u32& doneOrRet) {
    u32 idx = static_cast<u32>(-a - 1);
    if (idx >= funcDesc_.size()) {
        fail(static_cast<u32>(pc_), "FUNC index out of range");
        return false;
    }
    const BuiltinDesc* desc = funcDesc_[idx];
    const std::string& name = program_.funcs()[idx];

    u32 argWords[64];
    int n = desc->argCount;
    if (n < 0) n = 0;
    if (n > 64) n = 64; // no documented builtin has anywhere near this many arguments
    for (int i = 0; i < n; ++i) argWords[i] = frame_[static_cast<u32>(i)];

    ThreadBuiltinArgs args(*this, host_, desc->argCount, latent);
    if (report_) report_->noteCall(name.c_str(), desc->status);
    if (desc->fn) desc->fn(args, desc->userData);
    if (args.failed()) {
        fail(static_cast<u32>(pc_), args.failMessage());
        return false;
    }
    doneOrRet = args.done() ? 1u : 0u;

    if (sink_) {
        sink_->builtinCall(currentFrame_, currentEntry_, static_cast<u32>(pc_), name.c_str(),
                            argWords, n, retreg());
    }
    return true;
}

bool ScriptThread::execSubroutine(i32 target, int& status) {
    if (target < 0 || static_cast<u32>(target) >= program_.code().size()) {
        fail(static_cast<u32>(pc_), "call target out of range");
        return false;
    }
    i32 savedPc = pc_;
    pc_ = target;
    ++depth_;
    bool ok = runInvocation(status);
    --depth_;
    if (!ok) return false;
    pc_ = savedPc; // caller resumes right after its own CALL/LCALL instruction
    return true;
}

// ---------------------------------------------------------------------------------------
// The interpreter loop (rcsl-opcodes-v0.md, opcode table; rcsl-vm.md for LCALL/TMO).
// ---------------------------------------------------------------------------------------

bool ScriptThread::runInvocation(int& status) {
    int budget = kStallBudget;
    for (;;) {
        if (pc_ < 0 || static_cast<u32>(pc_) >= program_.code().size()) {
            fail(static_cast<u32>(pc_ < 0 ? 0 : pc_), "pc out of range");
            return false;
        }
        u32 pc = static_cast<u32>(pc_);
        const Instruction ins = program_.code()[pc];
        --budget;

        Loc a, b, c;
        if (!decode(ins, a, b, c)) return false;

        u32 val = 0;
        bool hasVal = false;
        i32 nxt = pc_ + 1;

        switch (ins.op) {
            case OP_END: {
                traceInstruction(pc, ins.op, false, 0);
                status = 0;
                return true;
            }
            case OP_MUL:
            case OP_DIV:
            case OP_ADD:
            case OP_SUB: {
                u32 av, bv;
                if (!loadOp(a, av) || !loadOp(b, bv)) return false;
                val = farith(ins.op, av, bv);
                hasVal = true;
                if (!storeOp(c, val)) return false;
                break;
            }
            case OP_BAND:
            case OP_BOR:
            case OP_LOR:
            case OP_LAND:
            case OP_EQ:
            case OP_NE: {
                u32 av, bv;
                if (!loadOp(a, av) || !loadOp(b, bv)) return false;
                i32 x = ftol(av), y = ftol(bv);
                i32 r = 0;
                switch (ins.op) {
                    case OP_BAND: r = x & y; break;
                    case OP_BOR: r = x | y; break;
                    case OP_LOR: r = (x != 0 || y != 0) ? 1 : 0; break;
                    case OP_LAND: r = (x != 0 && y != 0) ? 1 : 0; break;
                    case OP_EQ: r = (x == y) ? 1 : 0; break;
                    default: r = (x != y) ? 1 : 0; break; // OP_NE
                }
                val = itof(r);
                hasVal = true;
                if (!storeOp(c, val)) return false;
                break;
            }
            case OP_GT:
            case OP_LT:
            case OP_GE:
            case OP_LE: {
                u32 av, bv;
                if (!loadOp(a, av) || !loadOp(b, bv)) return false;
                float x = bitsToFloat(av), y = bitsToFloat(bv);
                bool r;
                switch (ins.op) {
                    case OP_GT: r = x > y; break;
                    case OP_LT: r = x < y; break;
                    case OP_GE: r = x >= y; break;
                    default: r = x <= y; break; // OP_LE
                }
                val = itof(r ? 1 : 0);
                hasVal = true;
                if (!storeOp(c, val)) return false;
                break;
            }
            case OP_NOT: {
                u32 av;
                if (!loadOp(a, av)) return false;
                val = itof(ftol(av) == 0 ? 1 : 0);
                hasVal = true;
                if (!storeOp(c, val)) return false;
                break;
            }
            case OP_NEG: {
                u32 av;
                if (!loadOp(a, av)) return false;
                val = quiet(av) ^ 0x80000000u;
                hasVal = true;
                if (!storeOp(c, val)) return false;
                break;
            }
            case OP_MOV: {
                u32 bv;
                if (!loadOp(b, bv)) return false;
                val = quiet(bv);
                hasVal = true;
                if (!storeOp(a, val)) return false;
                break;
            }
            case OP_LEA: {
                u32 av;
                if (!loadOp(a, av)) return false;
                val = av + 4u * static_cast<u32>(ins.b);
                hasVal = true;
                if (!storeOp(c, val)) return false;
                break;
            }
            case OP_PUSH: {
                if (stack_.size() >= kStackSize) {
                    fail(pc, "thread stack overflow");
                    return false;
                }
                u32 av;
                if (!loadOp(a, av)) return false;
                val = quiet(av);
                hasVal = true;
                stack_.push_back(val);
                break;
            }
            case OP_POP: {
                if (stack_.empty()) {
                    fail(pc, "thread stack underflow");
                    return false;
                }
                val = stack_.back();
                stack_.pop_back();
                hasVal = true;
                if (!storeOp(b, val)) return false; // the POP quirk: writes B, not A/C
                break;
            }
            case OP_ALLOC: {
                fail(pc, "ALLOC is not supported");
                return false;
            }
            case OP_NOP16:
            case OP_NOP17:
                break;
            case OP_RET: {
                u32 av;
                if (!loadOp(a, av)) return false;
                val = quiet(av);
                retreg() = val;
                traceInstruction(pc, ins.op, true, val);
                status = 1;
                return true;
            }
            case OP_JNZ:
            case OP_JZ: {
                u32 av;
                if (!loadOp(a, av)) return false;
                bool zero = bitsToFloat(av) == 0.0f;
                if ((ins.op == OP_JNZ && !zero) || (ins.op == OP_JZ && zero)) nxt = pc_ + ins.b;
                break;
            }
            case OP_JMP: {
                nxt = pc_ + ins.a;
                break;
            }
            case OP_CALL: {
                if (ins.a < 0) {
                    u32 doneOrRet;
                    if (!execBuiltin(ins.a, false, doneOrRet)) return false;
                } else {
                    int subStatus = 0;
                    if (!execSubroutine(ins.a, subStatus)) return false;
                }
                val = quiet(retreg());
                hasVal = true;
                if (!storeOp(b, val)) return false;
                break;
            }
            case OP_LCALL: {
                u32 doneFlag = 0;
                if (ins.a < 0) {
                    if (!execBuiltin(ins.a, true, doneFlag)) return false;
                } else {
                    int subStatus = 0;
                    if (!execSubroutine(ins.a, subStatus)) return false;
                    doneFlag = static_cast<u32>(subStatus);
                }
                bool complete;
                if (doneFlag != 0) {
                    complete = true;
                } else if (timeoutBits_ == 0) {
                    complete = false;
                } else {
                    u32 tbits;
                    if (!readWord(frametimeAddr_, tbits)) {
                        fail(pc, "access to unmapped address"); // frametime, for the LCALL timeout
                        return false;
                    }
                    float t = bitsToFloat(timeoutBits_) - bitsToFloat(tbits);
                    timeoutBits_ = floatToBits(t);
                    complete = !(bitsToFloat(timeoutBits_) > 0.0f);
                }
                if (complete) {
                    pc_ = static_cast<i32>(pc) + 1;
                    timeoutBits_ = 0;
                    val = quiet(retreg());
                    if (!storeOp(b, val)) return false;
                    traceInstruction(pc, ins.op, true, val);
                } else {
                    pc_ = static_cast<i32>(pc);
                    traceInstruction(pc, ins.op, false, 0);
                }
                status = 0;
                return true;
            }
            case OP_TMO: {
                u32 av;
                if (!loadOp(a, av)) return false;
                val = quiet(av);
                hasVal = true;
                timeoutBits_ = val;
                break;
            }
            default:
                fail(pc, "unknown opcode");
                return false;
        }

        if (ins.op != OP_LCALL) traceInstruction(pc, ins.op, hasVal, val);
        if (ins.op == OP_JNZ || ins.op == OP_JZ || ins.op == OP_JMP) {
            if (nxt < 0 || static_cast<u32>(nxt) >= program_.code().size()) {
                fail(pc, "jump target out of range");
                return false;
            }
        }
        pc_ = nxt;
        if (budget <= 0) {
            fail(static_cast<u32>(pc_), "Script stall detected.");
            return false;
        }
    }
}

// ---------------------------------------------------------------------------------------
// Dispatch.
// ---------------------------------------------------------------------------------------

DispatchResult ScriptThread::runMain(u32 frame, float dt) {
    DispatchResult r;
    if (!program_.hasEntry(EntryPoint::Main)) return r;
    currentFrame_ = frame;
    currentEntry_ = EntryPoint::Main;
    writeWord(frametimeAddr_, floatToBits(dt));
    if (pc_ < 0) return r; // unreachable in practice: hasEntry(Main) implies pc_ >= 0
    r.ranHandler = true;
    int depth0 = static_cast<int>(stack_.size());
    if (sink_) sink_->dispatchStart(frame, EntryPoint::Main);
    int status = 0;
    if (!runInvocation(status)) {
        r.ok = false;
        return r;
    }
    if (pc_ >= 0 && static_cast<u32>(pc_) < program_.code().size()) {
        u8 op = program_.code()[static_cast<u32>(pc_)].op;
        if (op == OP_END || op == OP_RET) pc_ = static_cast<i32>(program_.entry(EntryPoint::Main));
    }
    r.status = status;
    r.stackDelta = static_cast<int>(stack_.size()) - depth0;
    if (sink_) sink_->dispatchEnd(frame, EntryPoint::Main, status, r.stackDelta);
    return r;
}

DispatchResult ScriptThread::runEvent(EntryPoint ep, u32 frame) {
    // Intended for init/damage/touch/callback (use runMain for Main; see script.h).
    DispatchResult r;
    if (!program_.hasEntry(ep)) return r;
    currentFrame_ = frame;
    currentEntry_ = ep;
    r.ranHandler = true;
    int depth0 = static_cast<int>(stack_.size());
    if (sink_) sink_->dispatchStart(frame, ep);

    i32 savedPc = pc_;
    u32 saved[kNumTemps];
    for (u32 i = 0; i < kNumTemps; ++i) saved[i] = frame_[i];
    pc_ = static_cast<i32>(program_.entry(ep));

    int status = 0;
    if (!runInvocation(status)) {
        r.ok = false;
        return r;
    }
    pc_ = savedPc;
    for (u32 i = 0; i < kNumTemps; ++i) frame_[i] = saved[i];

    r.status = status;
    r.stackDelta = static_cast<int>(stack_.size()) - depth0;
    if (sink_) sink_->dispatchEnd(frame, ep, status, r.stackDelta);
    return r;
}

} // namespace as3d::script
