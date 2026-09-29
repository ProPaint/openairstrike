// Tests for the RCSL VM (as3d::script). See docs/script-vm.md and docs/spec/rcsl-*.md.
//
//  - golden corpus: the standard run of every shipped script under the mock host must
//    reproduce testdata/golden/rcsl_trace_hashes.json (sha1 of the trace, instruction and
//    builtin call counts), produced by the Python reference interpreter;
//  - performance sanity;
//  - unit tests on small synthetic programs built with a tiny assembler;
//  - loader robustness including a mutation fuzz loop.
#include "doctest.h"

#include "as3d/core.h"
#include "as3d/script.h"
#include "test_data.h"

// apps/rcsl_tool is not linked into as3d_tests; its small sources are compiled into this
// translation unit instead (the mock host must be identical for the tool and the tests).
#include "../rcsl_tool/mock_host.cpp"
#include "../rcsl_tool/runner.cpp"
#include "../rcsl_tool/sha1.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using namespace as3d;
using namespace as3d::script;

namespace {

u32 fb(float f) {
    u32 b;
    std::memcpy(&b, &f, 4);
    return b;
}
float bf(u32 b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}
i32 imm(float f) { return static_cast<i32>(fb(f)); }
i32 glob(int i) { return -(i + 1); }
i32 func(int i) { return -(i + 1); }

// ---------------------------------------------------------------------------------
// Tiny assembler: builds a valid RCSL file (DEFS FUNC DATA STRG CODE) in memory.
// ---------------------------------------------------------------------------------
class ScrBuilder {
public:
    u32 frameSlots = 24;
    void setEntry(EntryPoint ep, u32 pc) { entries_[static_cast<int>(ep)] = pc; }
    int defs(const std::string& n) { defs_.push_back(n); return static_cast<int>(defs_.size()) - 1; }
    int funcs(const std::string& n) { funcs_.push_back(n); return static_cast<int>(funcs_.size()) - 1; }
    u32 str(const std::string& s) {
        u32 off = static_cast<u32>(strg_.size());
        strg_.insert(strg_.end(), s.begin(), s.end());
        strg_.push_back(0);
        return off;
    }
    void data(u16 kind, i16 slot, u32 value) { data_.push_back({kind, slot, value}); }
    int emit(u8 op, u8 mode = 0, i32 a = 0, i32 b = 0, i32 c = 0) {
        code_.push_back({op, mode, a, b, c});
        return static_cast<int>(code_.size()) - 1;
    }
    int here() const { return static_cast<int>(code_.size()); }

    std::vector<u8> build() const {
        std::vector<u8> out;
        auto u32le = [](std::vector<u8>& v, u32 x) {
            for (int i = 0; i < 4; ++i) v.push_back(static_cast<u8>(x >> (8 * i)));
        };
        u32le(out, 0x4C534352u);
        u32le(out, 16);
        u32le(out, 0);
        u32le(out, static_cast<u32>(defs_.size()));
        u32le(out, static_cast<u32>(funcs_.size()));
        u32le(out, static_cast<u32>(data_.size()));
        u32le(out, frameSlots);
        u32le(out, static_cast<u32>(strg_.size()));
        u32le(out, static_cast<u32>(code_.size()));
        for (int k = 0; k < 5; ++k) u32le(out, entries_[k]);
        auto section = [&](const char* tag, const std::vector<u8>& payload) {
            out.insert(out.end(), tag, tag + 4);
            u32le(out, static_cast<u32>(payload.size()));
            out.insert(out.end(), payload.begin(), payload.end());
        };
        auto names = [](const std::vector<std::string>& v) {
            std::vector<u8> p;
            for (const auto& n : v) {
                p.push_back(static_cast<u8>(n.size() + 1));
                p.insert(p.end(), n.begin(), n.end());
                p.push_back(0);
            }
            return p;
        };
        if (!defs_.empty()) section("DEFS", names(defs_));
        if (!funcs_.empty()) section("FUNC", names(funcs_));
        std::vector<u8> d;
        for (const auto& e : data_) {
            d.push_back(static_cast<u8>(e.kind));
            d.push_back(static_cast<u8>(e.kind >> 8));
            u16 s = static_cast<u16>(e.slot);
            d.push_back(static_cast<u8>(s));
            d.push_back(static_cast<u8>(s >> 8));
            u32le(d, e.value);
        }
        section("DATA", d);
        if (!strg_.empty()) section("STRG", strg_);
        std::vector<u8> c;
        for (const auto& i : code_) {
            c.push_back(i.op);
            c.push_back(i.mode);
            u32le(c, static_cast<u32>(i.a));
            u32le(c, static_cast<u32>(i.b));
            u32le(c, static_cast<u32>(i.c));
        }
        section("CODE", c);
        return out;
    }

private:
    struct D { u16 kind; i16 slot; u32 value; };
    struct I { u8 op, mode; i32 a, b, c; };
    u32 entries_[5] = {kNoEntry, kNoEntry, kNoEntry, kNoEntry, kNoEntry};
    std::vector<std::string> defs_, funcs_;
    std::vector<D> data_;
    std::vector<u8> strg_;
    std::vector<I> code_;
};

// Flat, fully mapped memory host with per-test globals and builtins.
class TestHost final : public IScriptHost {
public:
    Addr resolveGlobal(const char* name) override {
        auto it = globals_.find(name);
        if (it != globals_.end()) return it->second;
        Addr a = 0x9000'0000u + 4u * static_cast<u32>(globals_.size());
        globals_[name] = a;
        return a;
    }
    const BuiltinDesc* resolveBuiltin(const char* name) override {
        auto it = builtins_.find(name);
        return it == builtins_.end() ? nullptr : &it->second;
    }
    bool readWord(Addr a, u32& out) override {
        if (unmapped_.count(a)) return false;
        auto it = mem_.find(a);
        out = it == mem_.end() ? 0 : it->second;
        return true;
    }
    bool writeWord(Addr a, u32 v) override {
        if (unmapped_.count(a)) return false;
        mem_[a] = v;
        return true;
    }
    void addBuiltin(const char* name, int argc, BuiltinFn fn, void* ud = nullptr) {
        BuiltinDesc d;
        d.name = name;
        d.argCount = argc;
        d.fn = fn;
        d.userData = ud;
        builtins_[name] = d;
    }
    u32 peek(Addr a) const {
        auto it = mem_.find(a);
        return it == mem_.end() ? 0 : it->second;
    }
    void poke(Addr a, u32 v) { mem_[a] = v; }
    void unmap(Addr a) { unmapped_.insert(a); }
    Addr global(const char* n) { return resolveGlobal(n); }

private:
    std::unordered_map<std::string, Addr> globals_;
    std::unordered_map<std::string, BuiltinDesc> builtins_;
    std::unordered_map<u32, u32> mem_;
    std::unordered_set<u32> unmapped_;
};

void bDone(BuiltinArgs& a, void*) {
    a.setReturnFloat(42.0f);
    if (a.latent()) a.setDone(true);
}
void bNever(BuiltinArgs& a, void*) { a.setReturnFloat(7.0f); }
void bCount(BuiltinArgs& a, void* ud) {
    ++*static_cast<int*>(ud);
    a.setReturnFloat(a.f32(0) + a.f32(1));
}

struct Rig {
    ScrBuilder b;
    ScriptProgram prog;
    TestHost host;
    TextTraceSink sink;
    std::unique_ptr<ScriptThread> th;
    BuiltinReport report;
    u32 frame = 0;

    void start() {
        std::vector<u8> bytes = b.build();
        std::string err;
        bool ok = ScriptProgram::load(bytes.data(), bytes.size(), prog, &err);
        INFO(err);
        REQUIRE(ok);
        th.reset(new ScriptThread(prog, host, &report, &sink, "test"));
        REQUIRE(th->valid());
    }
    DispatchResult main(float dt = 1.0f) { return th->runMain(frame++, dt); }
    float slot(u32 i) const { return bf(th->frameSlot(i)); }
    void set(u32 i, float f) { th->setFrameSlot(i, fb(f)); }
    bool traceHas(const char* s) const { return sink.text().find(s) != std::string::npos; }
};

// Single binary op program: C(t2) = A(t0) op B(t1); END.
u32 binop(u8 op, u32 a, u32 b) {
    Rig r;
    r.b.emit(op, 0, 0, 1, 2);
    r.b.emit(OP_END);
    r.b.setEntry(EntryPoint::Main, 0);
    r.start();
    r.th->setFrameSlot(0, a);
    r.th->setFrameSlot(1, b);
    r.main();
    return r.th->frameSlot(2);
}

} // namespace

// ---------------------------------------------------------------------------------
// Golden corpus and performance.
// ---------------------------------------------------------------------------------

namespace {
struct Golden {
    u64 instructions = 0, builtins = 0;
    std::string sha1;
};

// Extracts (name -> counts, sha1) from the fixed-shape golden JSON.
bool parseGoldenHashes(const std::string& t, std::vector<std::pair<std::string, Golden>>& out) {
    size_t p = 0;
    auto str = [&](std::string& s) {
        p = t.find('"', p);
        if (p == std::string::npos) return false;
        size_t e = ++p;
        s.clear();
        while (e < t.size() && t[e] != '"') {
            if (t[e] == '\\' && e + 1 < t.size()) ++e;
            s.push_back(t[e++]);
        }
        p = e + 1;
        return e < t.size();
    };
    auto num = [&](u64& v) {
        p = t.find(':', p);
        if (p == std::string::npos) return false;
        ++p;
        while (p < t.size() && t[p] == ' ') ++p;
        v = std::strtoull(t.c_str() + p, nullptr, 10);
        return true;
    };
    std::string key, field;
    while (true) {
        size_t save = p;
        if (!str(key)) break;
        if (key.rfind("scripts", 0) != 0) { p = save; break; }
        Golden g;
        if (!str(field) || field != "builtin_calls" || !num(g.builtins)) return false;
        if (!str(field) || field != "instructions" || !num(g.instructions)) return false;
        if (!str(field) || field != "sha1") return false;
        if (!str(g.sha1)) return false;
        out.push_back({key, g});
    }
    return !out.empty();
}
} // namespace

TEST_CASE("script: every shipped script reproduces the reference trace") {
    AS3D_REQUIRE_DATA();
    std::ifstream f(testdata::goldenDir() + "/rcsl_trace_hashes.json", std::ios::binary);
    REQUIRE(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    std::vector<std::pair<std::string, Golden>> golden;
    REQUIRE(parseGoldenHashes(ss.str(), golden));
    CHECK(golden.size() == 339);

    int matched = 0;
    for (const auto& kv : golden) {
        INFO("script: ", kv.first);
        Blob blob;
        REQUIRE(testdata::readExtracted(kv.first, blob));
        ScriptProgram prog;
        std::string err;
        REQUIRE_MESSAGE(ScriptProgram::load(blob.data(), blob.size(), prog, &err), err);
        TextTraceSink sink;
        rcsl_tool::RunOutcome o =
            rcsl_tool::runStandard(prog, 600, 1.0f / 60.0f, rcsl_tool::kStandardEvents, true, &sink, nullptr);
        bool ok = o.ok && rcsl_tool::sha1Hex(sink.text()) == kv.second.sha1 &&
                  o.instructions == kv.second.instructions && o.builtinCalls == kv.second.builtins;
        CHECK(o.ok);
        CHECK(o.instructions == kv.second.instructions);
        CHECK(o.builtinCalls == kv.second.builtins);
        CHECK(rcsl_tool::sha1Hex(sink.text()) == kv.second.sha1);
        for (const auto& d : o.dispatches) {
            if (d.ranHandler) CHECK(d.stackDelta == 0);
        }
        if (ok) ++matched;
    }
    MESSAGE("script traces matching the reference: ", matched, " of ", golden.size());
    CHECK(matched == static_cast<int>(golden.size()));
}

TEST_CASE("script: performance of the standard run over all scripts (no trace sink)") {
    AS3D_REQUIRE_DATA();
    std::vector<std::unique_ptr<ScriptProgram>> progs;
    std::ifstream f(testdata::goldenDir() + "/rcsl_trace_hashes.json", std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::vector<std::pair<std::string, Golden>> golden;
    REQUIRE(parseGoldenHashes(ss.str(), golden));
    for (const auto& kv : golden) {
        Blob blob;
        REQUIRE(testdata::readExtracted(kv.first, blob));
        auto p = std::make_unique<ScriptProgram>();
        REQUIRE(ScriptProgram::load(blob.data(), blob.size(), *p, nullptr));
        progs.push_back(std::move(p));
    }
    auto t0 = std::chrono::steady_clock::now();
    u64 instr = 0, calls = 0;
    for (const auto& p : progs) {
        rcsl_tool::RunOutcome o = rcsl_tool::runStandard(*p, 600, 1.0f / 60.0f, rcsl_tool::kStandardEvents, true,
                                                         nullptr, nullptr);
        instr += o.instructions;
        calls += o.builtinCalls;
    }
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    MESSAGE("standard run over ", progs.size(), " scripts: ", instr, " instructions, ", calls,
            " builtin calls in ", sec * 1000.0, " ms");
    CHECK(instr > 2000000);
    CHECK(sec < 5.0); // the real bound is well under a second in RelWithDebInfo; slack for sanitizers
}

// ---------------------------------------------------------------------------------
// Arithmetic and logic.
// ---------------------------------------------------------------------------------

TEST_CASE("script: MUL DIV ADD SUB and float corner cases") {
    CHECK(bf(binop(OP_MUL, fb(6), fb(7))) == 42.0f);
    CHECK(bf(binop(OP_ADD, fb(1.5f), fb(2.25f))) == 3.75f);
    CHECK(bf(binop(OP_SUB, fb(1), fb(3))) == -2.0f);
    CHECK(bf(binop(OP_DIV, fb(1), fb(4))) == 0.25f);
    const u32 inf = 0x7F800000u, ninf = 0xFF800000u;
    CHECK(binop(OP_DIV, fb(1), 0) == inf);
    CHECK(binop(OP_DIV, fb(-1), 0) == ninf);
    CHECK(binop(OP_DIV, fb(1), 0x80000000u) == ninf);
    CHECK(binop(OP_DIV, 0, 0) == 0xFFC00000u);
    CHECK(binop(OP_SUB, inf, inf) == 0xFFC00000u);
    CHECK(binop(OP_MUL, inf, 0) == 0xFFC00000u);
    CHECK(binop(OP_ADD, inf, fb(1)) == inf);
    CHECK(binop(OP_MUL, 0, fb(-1)) == 0x80000000u);  // negative zero
    CHECK(binop(OP_ADD, 0x80000000u, 0x80000000u) == 0x80000000u);
    // NaN operands propagate, quieted; the larger significand wins between two NaNs.
    CHECK(binop(OP_ADD, 0x7F800001u, fb(1)) == 0x7FC00001u);
    CHECK(binop(OP_ADD, fb(1), 0x7FC00005u) == 0x7FC00005u);
    CHECK(binop(OP_MUL, 0x7FC00002u, 0x7FC00009u) == 0x7FC00009u);
}

TEST_CASE("script: integer ops truncate toward zero; comparisons are float") {
    CHECK(bf(binop(OP_BAND, fb(3), fb(5))) == 1.0f);
    CHECK(bf(binop(OP_BOR, fb(3), fb(5))) == 7.0f);
    CHECK(bf(binop(OP_LOR, fb(0.5f), fb(0))) == 0.0f);  // int(0.5) == 0
    CHECK(bf(binop(OP_LOR, fb(1.5f), fb(0))) == 1.0f);
    CHECK(bf(binop(OP_LAND, fb(2), fb(0))) == 0.0f);
    CHECK(bf(binop(OP_LAND, fb(2), fb(3))) == 1.0f);
    CHECK(bf(binop(OP_EQ, fb(2.9f), fb(2.1f))) == 1.0f);   // both truncate to 2
    CHECK(bf(binop(OP_GT, fb(2.9f), fb(2.1f))) == 1.0f);   // float compare differs
    CHECK(bf(binop(OP_EQ, fb(-2.7f), fb(-2.0f))) == 1.0f);
    CHECK(bf(binop(OP_NE, fb(-2.7f), fb(-3.0f))) == 1.0f);
    // Out of range, NaN and infinities all convert to INT32_MIN.
    CHECK(bf(binop(OP_EQ, fb(3e9f), fb(-3e9f))) == 1.0f);
    CHECK(bf(binop(OP_EQ, 0x7FC00000u, 0x7F800000u)) == 1.0f);
    CHECK(bf(binop(OP_EQ, fb(-2147483648.0f), fb(-3e9f))) == 1.0f);
    CHECK(bf(binop(OP_EQ, fb(2147483520.0f), fb(2147483000.0f))) == 0.0f);
    CHECK(bf(binop(OP_LT, fb(1), fb(2))) == 1.0f);
    CHECK(bf(binop(OP_GE, fb(2), fb(2))) == 1.0f);
    CHECK(bf(binop(OP_LE, fb(3), fb(2))) == 0.0f);
    // Unordered: every comparison is false; NaN != NaN in the float sense.
    for (u8 op : {OP_GT, OP_LT, OP_GE, OP_LE}) CHECK(bf(binop(op, 0x7FC00000u, fb(1))) == 0.0f);
}

TEST_CASE("script: NOT NEG MOV LEA and operand modes") {
    Rig r;
    r.b.emit(OP_NOT, 0, 0, 0, 3);            // t3 = !int(t0)
    r.b.emit(OP_NEG, M_IMM1, imm(500), 0, 4); // t4 = -#500
    r.b.emit(OP_NEG, 0, 1, 0, 5);            // t5 = -t1
    r.b.emit(OP_MOV, M_IMM2, 6, imm(3.5f));   // t6 = #3.5
    r.b.emit(OP_MOV, 0, 7, 1);               // t7 = t1
    r.b.emit(OP_MOV, M_IND1, 8, 1);          // [t8] = t1  (t8 holds a host address)
    r.b.emit(OP_MOV, M_IND2, 9, 10);         // t9 = [t10]
    r.b.emit(OP_ADD, M_IMM1 | M_IND2, imm(1), 11, 12); // t12 = #1 + [t11]
    r.b.emit(OP_END);
    r.b.setEntry(EntryPoint::Main, 0);
    r.start();
    r.host.poke(0xA000, fb(10));
    r.host.poke(0xA010, fb(20));
    r.th->setFrameSlot(8, 0xA000);
    r.th->setFrameSlot(10, 0xA010);
    r.th->setFrameSlot(11, 0xA010);
    r.set(0, 0.0f);
    r.set(1, 2.0f);
    r.main();
    CHECK(r.slot(3) == 1.0f);
    CHECK(r.slot(4) == -500.0f);
    CHECK(r.slot(5) == -2.0f);
    CHECK(r.slot(6) == 3.5f);
    CHECK(r.slot(7) == 2.0f);
    CHECK(bf(r.host.peek(0xA000)) == 2.0f);
    CHECK(r.slot(9) == 20.0f);
    CHECK(r.slot(12) == 21.0f);

    // NEG flips the sign bit after quieting; -0 and NaN keep their payload.
    Rig n;
    n.b.emit(OP_NEG, 0, 0, 0, 1);
    n.b.emit(OP_END);
    n.b.setEntry(EntryPoint::Main, 0);
    n.start();
    n.th->setFrameSlot(0, 0);
    n.main();
    CHECK(n.th->frameSlot(1) == 0x80000000u);
    n.th->setFrameSlot(0, 0x7F800003u);  // signalling NaN
    n.main();
    CHECK(n.th->frameSlot(1) == 0xFFC00003u);
}

TEST_CASE("script: LEA, globals and entity fields through references") {
    Rig r;
    int g = r.b.defs("obj");
    int h = r.b.defs("val");
    r.b.emit(OP_LEA, 0, glob(g), 5, 0);        // t0 = &obj[5]
    r.b.emit(OP_MOV, M_IND1 | M_IMM2, 0, imm(50.0f)); // [t0] = #50
    r.b.emit(OP_LEA, 0, 0, 2, 1);              // t1 = &t0[2]  (pointer arithmetic on a pointer)
    r.b.emit(OP_MOV, M_IND2, 2, 1);            // t2 = [t1]
    r.b.emit(OP_MOV, 0, glob(h), 2);           // val = t2
    r.b.emit(OP_END);
    r.b.setEntry(EntryPoint::Main, 0);
    r.start();
    Addr obj = r.host.global("obj");
    r.host.poke(obj, 0xB000);                  // the reference stored in the global
    r.host.poke(0xB000 + 4 * 7, fb(9.0f));     // field 7
    r.main();
    CHECK(r.th->frameSlot(0) == 0xB000 + 20);
    CHECK(bf(r.host.peek(0xB000 + 20)) == 50.0f);
    CHECK(r.th->frameSlot(1) == 0xB000 + 28);
    CHECK(r.slot(2) == 9.0f);
    CHECK(bf(r.host.peek(r.host.global("val"))) == 9.0f);
}

TEST_CASE("script: DATA initialises scalar and vector variables") {
    Rig r;
    r.b.data(2, 16, fb(1.5f));
    r.b.data(3, 17, 18);   // pointer slot followed by storage
    r.b.data(2, 18, fb(4.0f));
    r.b.data(2, 19, 0x00000010u);
    r.b.emit(OP_LEA, 0, 17, 1, 0);   // t0 = &v17[1]: second component
    r.b.emit(OP_END);
    r.b.setEntry(EntryPoint::Main, 0);
    r.start();
    CHECK(r.slot(16) == 1.5f);
    CHECK(r.th->frameSlot(17) == kFrameBase + 4 * 18);
    CHECK(r.slot(18) == 4.0f);
    CHECK(r.th->frameSlot(19) == 0x10u);
    CHECK(r.th->frameSlot(0) == 0);
    r.main();
    CHECK(r.th->frameSlot(0) == kFrameBase + 4 * 18 + 4);
}

// ---------------------------------------------------------------------------------
// Stack, jumps, calls, RET/END.
// ---------------------------------------------------------------------------------

TEST_CASE("script: PUSH POP and the POP quirk (POP writes operand B)") {
    Rig r;
    r.b.emit(OP_PUSH, 0, 1);
    r.b.emit(OP_PUSH, M_IND3, 2);     // mode bit 0x80 is set by the compiler on some PUSH/POP
    r.b.emit(OP_POP, 0, 3, 0, 3);     // compiler: A = C = 3, B = 0  -> writes t0
    r.b.emit(OP_POP, 0, 4, 0, 4);
    r.b.emit(OP_END);
    r.b.setEntry(EntryPoint::Main, 0);
    r.start();
    r.set(1, 7.0f);
    r.set(2, 9.0f);
    r.set(0, -1.0f);
    DispatchResult d = r.main();
    CHECK(d.ok);
    CHECK(d.stackDelta == 0);
    CHECK(r.slot(3) == 0.0f);   // not written: the restore went to t0 instead
    CHECK(r.slot(4) == 0.0f);
    CHECK(r.slot(0) == 7.0f);   // the last POP wrote t0 (first push)
}

TEST_CASE("script: stack overflow, underflow and the stall detector are errors") {
    {
        Rig r;
        r.b.emit(OP_PUSH, 0, 1);
        r.b.emit(OP_JMP, 0, -1);
        r.b.setEntry(EntryPoint::Main, 0);
        r.start();
        DispatchResult d = r.main();
        CHECK(!d.ok);
        CHECK(r.traceHas("thread stack overflow"));
        CHECK(r.th->stackDepth() == 512);
    }
    {
        Rig r;
        r.b.emit(OP_POP, 0, 0, 0, 0);
        r.b.emit(OP_END);
        r.b.setEntry(EntryPoint::Main, 0);
        r.start();
        CHECK(!r.main().ok);
        CHECK(r.traceHas("thread stack underflow"));
    }
    {
        Rig r;
        r.b.emit(OP_JMP, 0, 0);   // jump to itself forever
        r.b.setEntry(EntryPoint::Main, 0);
        r.start();
        CHECK(!r.main().ok);
        CHECK(r.traceHas("Script stall detected."));
        // exactly 10000 instructions ran
        size_t lines = 0;
        for (char c : r.sink.text()) lines += (c == '\n');
        CHECK(lines == 1 + 10000 + 1);   // E line, 10000 I lines, X line
    }
}

TEST_CASE("script: jumps") {
    Rig r;
    r.b.emit(OP_JZ, 0, 0, 3);          // 0: t0 == 0 -> jump to 3
    r.b.emit(OP_MOV, M_IMM2, 16, imm(1));  // 1: skipped
    r.b.emit(OP_JMP, 0, 2);            // 2: skipped
    r.b.emit(OP_JNZ, 0, 1, 3);         // 3: t1 != 0 (NaN counts) -> jump to 6
    r.b.emit(OP_MOV, M_IMM2, 17, imm(1));  // 4: skipped
    r.b.emit(OP_JMP, 0, 2);            // 5: skipped
    r.b.emit(OP_JZ, 0, 2, 2);          // 6: t2 = -0.0 is zero -> jump to 8
    r.b.emit(OP_MOV, M_IMM2, 18, imm(1));  // 7: skipped
    r.b.emit(OP_MOV, M_IMM2, 19, imm(1));  // 8
    r.b.emit(OP_END);
    r.b.setEntry(EntryPoint::Main, 0);
    r.start();
    r.set(0, 0.0f);
    r.th->setFrameSlot(1, 0x7FC00000u);
    r.th->setFrameSlot(2, 0x80000000u);
    r.main();
    CHECK(r.slot(16) == 0.0f);
    CHECK(r.slot(17) == 0.0f);
    CHECK(r.slot(18) == 0.0f);
    CHECK(r.slot(19) == 1.0f);
}

TEST_CASE("script: CALL of subroutines (nested), RET and END") {
    Rig r;
    r.b.emit(OP_CALL, 0, 3, 0, 0);     // 0: CALL sub3 -> t5
    r.b.emit(OP_END);                  // 1
    r.b.emit(OP_END);                  // 2
    r.b.emit(OP_MOV, M_IMM2, 1, imm(5.0f));  // 3: sub3
    r.b.emit(OP_CALL, 0, 6, 2);        // 4: CALL sub6 -> t2 (shares the frame)
    r.b.emit(OP_RET, 0, 1);            // 5: RET t1
    r.b.emit(OP_MOV, M_IMM2, 3, imm(9.0f));  // 6: sub6
    r.b.emit(OP_RET, 0, 3);            // 7
    r.b.setEntry(EntryPoint::Main, 0);
    // patch: first CALL must store into t5
    ScrBuilder& b = r.b;
    (void)b;
    r.start();
    r.main();
    CHECK(r.slot(2) == 9.0f);           // sub6's RET reached t2
    CHECK(r.slot(0) == 5.0f);           // the CALL's B is t0 here (operand b == 0)
    CHECK(r.traceHas("I 0 main 1 4 1c"));   // nested instruction at depth 1
    CHECK(r.traceHas("I 0 main 2 6 11"));   // and depth 2
    CHECK(r.traceHas("R 0 main 0 0"));

    // RET in main ends the invocation with status 1 and main restarts.
    Rig m;
    m.b.emit(OP_RET, M_IMM1, imm(3.0f));
    m.b.setEntry(EntryPoint::Main, 0);
    m.start();
    DispatchResult d = m.main();
    CHECK(d.status == 1);
    CHECK(m.th->pc() == 0);
}

TEST_CASE("script: builtin CALL sees its arguments and shares one return register") {
    Rig r;
    int cnt = 0;
    r.host.addBuiltin("add2", 2, bCount, &cnt);
    int f = r.b.funcs("add2");
    r.b.emit(OP_CALL, 0, func(f), 2);   // t2 = add2(t0, t1)
    r.b.emit(OP_END);
    r.b.setEntry(EntryPoint::Main, 0);
    r.start();
    r.set(0, 1.5f);
    r.set(1, 2.0f);
    r.main();
    CHECK(cnt == 1);
    CHECK(r.slot(2) == 3.5f);
    CHECK(r.traceHas("B 0 main 0 add2 3fc00000 40000000 -> 40600000"));
    auto rows = r.report.rows();
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].calls == 1);
    CHECK(rows[0].status == BuiltinStatus::Implemented);
}

namespace {
// A builtin that runs another thread's callback handler, like `callback` or `create`
// (whose result is set before the nested init runs when `setFirst`).
struct NestCtx {
    ScriptThread* other = nullptr;
    bool setFirst = false;
};
void bNest(BuiltinArgs& a, void* ud) {
    NestCtx& c = *static_cast<NestCtx*>(ud);
    if (c.setFirst) a.setReturnFloat(1.0f);
    c.other->runEvent(EntryPoint::Callback, 0);
}
} // namespace

TEST_CASE("script: a shared return register leaks nested handler results (issue 032)") {
    // Callee: callback handler = RET 5.0.
    ScrBuilder cb;
    cb.emit(OP_RET, M_IMM1, imm(5.0f));
    cb.setEntry(EntryPoint::Callback, 0);
    std::vector<u8> cbytes = cb.build();
    ScriptProgram cprog;
    REQUIRE(ScriptProgram::load(cbytes.data(), cbytes.size(), cprog, nullptr));

    for (int shared = 0; shared < 2; ++shared) {
        for (int setFirst = 0; setFirst < 2; ++setFirst) {
            Rig r;
            NestCtx ctx;
            ctx.setFirst = setFirst != 0;
            r.host.addBuiltin("nest", 0, bNest, &ctx);
            int f = r.b.funcs("nest");
            r.b.emit(OP_CALL, 0, func(f), 2); // t2 = nest()
            r.b.emit(OP_END);
            r.b.setEntry(EntryPoint::Main, 0);
            r.start();
            ScriptThread callee(cprog, r.host);
            REQUIRE(callee.valid());
            ctx.other = &callee;
            u32 reg = fb(-1.0f);
            if (shared) {
                r.th->setSharedReturnRegister(&reg);
                callee.setSharedReturnRegister(&reg);
            }
            r.main();
            INFO("shared " << shared << " setFirst " << setFirst);
            if (shared) {
                // The original: the callee's RET replaced whatever the caller would receive.
                CHECK(r.slot(2) == 5.0f);
                CHECK(reg == fb(5.0f));
                CHECK(r.th->returnRegisterBits() == fb(5.0f));
            } else {
                // Per-thread registers: the caller sees its own builtin's value (or its own
                // stale register, 0, when the builtin returns nothing).
                CHECK(r.slot(2) == (setFirst ? 1.0f : 0.0f));
                CHECK(callee.returnRegisterBits() == fb(5.0f));
            }
        }
    }
}

TEST_CASE("script: unknown and unimplemented builtins bind to a counted stub") {
    Rig r;
    int f1 = r.b.funcs("random");     // documented, not registered by this host
    int f2 = r.b.funcs("Lightning");
    int f3 = r.b.funcs("NotABuiltin");
    r.b.emit(OP_CALL, 0, func(f1), 3);
    r.b.emit(OP_CALL, 0, func(f2), 4);
    r.b.emit(OP_CALL, 0, func(f3), 5);
    r.b.emit(OP_CALL, 0, func(f2), 6);
    r.b.emit(OP_END);
    r.b.setEntry(EntryPoint::Main, 0);
    r.start();
    r.set(3, 5.0f);
    r.main();
    CHECK(r.slot(3) == 0.0f);        // value-returning stub returns 0
    CHECK(r.slot(4) == 0.0f);        // "returns none": register untouched (still 0 here)
    auto rows = r.report.rows();
    REQUIRE(rows.size() == 3);
    for (const auto& row : rows) {
        CHECK(row.status == BuiltinStatus::Stub);
        CHECK(row.calls == (row.name == "Lightning" ? 2u : 1u));
    }
}

// ---------------------------------------------------------------------------------
// LCALL / TMO, handlers.
// ---------------------------------------------------------------------------------

TEST_CASE("script: LCALL completes by done flag, code after it runs on a later update") {
    Rig r;
    r.host.addBuiltin("done", 0, bDone);
    int f = r.b.funcs("done");
    r.b.emit(OP_LCALL, 0, func(f), 1);       // t1 = latent done()
    r.b.emit(OP_MOV, M_IMM2, 16, imm(1.0f)); // after the LCALL
    r.b.emit(OP_END);
    r.b.setEntry(EntryPoint::Main, 0);
    r.start();
    DispatchResult d = r.main();
    CHECK(d.ok);
    CHECK(r.slot(1) == 42.0f);
    CHECK(r.slot(16) == 0.0f);   // not yet: a completed LCALL ends the invocation
    CHECK(r.th->pc() == 1);
    r.main();
    CHECK(r.slot(16) == 1.0f);
    CHECK(r.th->pc() == 0);      // END restarted main
}

TEST_CASE("script: LCALL waits on its timeout, waits forever without one") {
    {
        Rig r;
        r.host.addBuiltin("never", 0, bNever);
        int f = r.b.funcs("never");
        r.b.emit(OP_TMO, M_IMM1, imm(2.0f));
        r.b.emit(OP_LCALL, 0, func(f), 1);
        r.b.emit(OP_MOV, M_IMM2, 16, imm(1.0f));
        r.b.emit(OP_END);
        r.b.setEntry(EntryPoint::Main, 0);
        r.start();
        r.main(1.0f);                    // timeout 2 -> 1: waiting
        CHECK(r.th->pc() == 1);
        CHECK(r.th->timeoutBits() == fb(1.0f));
        r.main(1.0f);                    // 1 -> 0: complete
        CHECK(r.th->pc() == 2);
        CHECK(r.th->timeoutBits() == 0);
        CHECK(r.slot(1) == 7.0f);
        r.main(1.0f);
        CHECK(r.slot(16) == 1.0f);
    }
    {
        Rig r;                           // no TMO: timeout 0 waits forever
        r.host.addBuiltin("never", 0, bNever);
        int f = r.b.funcs("never");
        r.b.emit(OP_LCALL, 0, func(f), 1);
        r.b.emit(OP_MOV, M_IMM2, 16, imm(1.0f));
        r.b.emit(OP_END);
        r.b.setEntry(EntryPoint::Main, 0);
        r.start();
        for (int i = 0; i < 20; ++i) r.main(1.0f);
        CHECK(r.th->pc() == 0);
        CHECK(r.slot(16) == 0.0f);
    }
    {
        Rig r;                           // NaN timeout: completes at once (docs/spec/issues/001)
        r.host.addBuiltin("never", 0, bNever);
        int f = r.b.funcs("never");
        r.b.emit(OP_TMO, M_IMM1, static_cast<i32>(0x7FC00000u));
        r.b.emit(OP_LCALL, 0, func(f), 1);
        r.b.emit(OP_MOV, M_IMM2, 16, imm(1.0f)); // not END: main would restart from its entry
        r.b.emit(OP_END);
        r.b.setEntry(EntryPoint::Main, 0);
        r.start();
        r.main(1.0f);
        INFO(r.sink.text());
        CHECK(r.th->pc() == 2);
        CHECK(r.th->timeoutBits() == 0);
    }
}

TEST_CASE("script: LCALL of a script subroutine repeats until it RETs") {
    Rig r;
    r.b.emit(OP_LCALL, 0, 4, 1);            // 0: LCALL sub4 -> t1
    r.b.emit(OP_MOV, M_IMM2, 17, imm(1.0f));// 1
    r.b.emit(OP_END);                       // 2
    r.b.emit(OP_END);                       // 3
    r.b.emit(OP_ADD, M_IMM2, 16, imm(1.0f), 16);  // 4: v16 += 1 (A == C == 16)
    r.b.emit(OP_GE, M_IMM2, 16, imm(3.0f), 0);    // 5: t0 = v16 >= 3
    r.b.emit(OP_JZ, 0, 0, 2);               // 6: not yet -> END at 8
    r.b.emit(OP_RET, M_IMM1, imm(5.0f));    // 7
    r.b.emit(OP_END);                       // 8
    r.b.setEntry(EntryPoint::Main, 0);
    r.start();
    for (int i = 0; i < 2; ++i) {
        r.main();
        CHECK(r.th->pc() == 0);             // sub ended with END: still waiting
    }
    r.main();
    CHECK(r.slot(16) == 3.0f);
    CHECK(r.th->pc() == 1);                 // RET -> done flag 1 -> complete
    CHECK(r.slot(1) == 5.0f);
}

TEST_CASE("script: event handlers save and restore pc and t0..t15, keep variables") {
    Rig r;
    r.b.emit(OP_END);                                  // 0: main
    r.b.emit(OP_MOV, M_IMM2, 0, imm(99.0f));           // 1: touch: t0 = 99
    r.b.emit(OP_MOV, M_IMM2, 16, imm(3.0f));           // 2: v16 = 3
    r.b.emit(OP_PUSH, 0, 0);                           // 3: unbalanced on purpose
    r.b.emit(OP_END);                                  // 4
    r.b.setEntry(EntryPoint::Main, 0);
    r.b.setEntry(EntryPoint::Touch, 1);
    r.start();
    r.set(0, 1.0f);
    DispatchResult d = r.th->runEvent(EntryPoint::Touch, 0);
    CHECK(d.ok);
    CHECK(d.ranHandler);
    CHECK(d.stackDelta == 1);      // the stack is not saved or restored
    CHECK(r.slot(0) == 1.0f);
    CHECK(r.slot(16) == 3.0f);
    CHECK(r.th->pc() == 0);
    CHECK(!r.th->runEvent(EntryPoint::Callback, 0).ranHandler);   // absent entry: no-op
    CHECK(!r.th->runEvent(EntryPoint::Damage, 0).ranHandler);
}

TEST_CASE("script: an event handler interrupts a waiting main without disturbing it") {
    Rig r;
    r.host.addBuiltin("never", 0, bNever);
    int f = r.b.funcs("never");
    r.b.emit(OP_TMO, M_IMM1, imm(100.0f));       // 0
    r.b.emit(OP_LCALL, 0, func(f), 1);           // 1
    r.b.emit(OP_END);                            // 2
    r.b.emit(OP_ADD, M_IMM2, 16, imm(1.0f), 16); // 3: damage: v16 += 1
    r.b.emit(OP_END);                            // 4
    r.b.setEntry(EntryPoint::Main, 0);
    r.b.setEntry(EntryPoint::Damage, 3);
    r.start();
    r.main(1.0f);
    CHECK(r.th->pc() == 1);
    r.th->runEvent(EntryPoint::Damage, 0);
    CHECK(r.th->pc() == 1);
    CHECK(r.slot(16) == 1.0f);
    r.main(1.0f);
    CHECK(r.th->pc() == 1);   // still waiting on the same LCALL
    CHECK(r.slot(16) == 1.0f);
}

TEST_CASE("script: a waiting LCALL inside a handler ends it; the timeout stays") {
    Rig r;
    r.host.addBuiltin("never", 0, bNever);
    int f = r.b.funcs("never");
    r.b.emit(OP_END);                             // 0: main
    r.b.emit(OP_TMO, M_IMM1, imm(1.5f));          // 1: init
    r.b.emit(OP_LCALL, 0, func(f), 1);            // 2
    r.b.emit(OP_MOV, M_IMM2, 16, imm(1.0f));      // 3: never reached
    r.b.emit(OP_END);
    r.b.setEntry(EntryPoint::Main, 0);
    r.b.setEntry(EntryPoint::Init, 1);
    r.start();
    r.host.poke(r.host.global("frametime"), fb(0.5f));
    DispatchResult d = r.th->runEvent(EntryPoint::Init, 0);
    CHECK(d.ok);
    CHECK(r.slot(16) == 0.0f);
    CHECK(r.th->pc() == 0);
    CHECK(r.th->timeoutBits() == fb(1.0f));   // 1.5 - host frametime 0.5
}

TEST_CASE("script: errors are reported, not thrown") {
    Rig r;
    r.b.defs("g");
    r.b.emit(OP_MOV, 0, 0, glob(0));
    r.b.emit(OP_END);
    r.b.setEntry(EntryPoint::Main, 0);
    r.start();
    r.host.unmap(r.host.global("g"));
    DispatchResult d = r.main();
    CHECK(!d.ok);
    CHECK(r.traceHas("X 0 main 0 access to unmapped address"));
    CHECK(!r.traceHas("R 0 main"));   // no R line after an error

    Rig none;                         // no main entry: nothing runs, nothing traced
    none.b.emit(OP_END);
    none.start();
    CHECK(!none.main().ranHandler);
    CHECK(none.sink.text().empty());
}

// ---------------------------------------------------------------------------------
// Loader.
// ---------------------------------------------------------------------------------

namespace {
std::vector<u8> sampleScript() {
    ScrBuilder b;
    b.defs("self");
    b.funcs("sin");
    u32 s = b.str("hello");
    b.data(2, 16, fb(1.0f));
    b.data(3, 17, 18);
    b.emit(OP_MOV, M_IMM2, 0, static_cast<i32>(s));
    b.emit(OP_CALL, 0, func(0), 1);
    b.emit(OP_JZ, 0, 0, 2);
    b.emit(OP_MOV, 0, 16, glob(0));
    b.emit(OP_END);
    b.setEntry(EntryPoint::Main, 0);
    b.setEntry(EntryPoint::Damage, 3);
    return b.build();
}
bool loads(const std::vector<u8>& v, std::string* err = nullptr) {
    ScriptProgram p;
    return ScriptProgram::load(v.data(), v.size(), p, err);
}
void put32(std::vector<u8>& v, size_t off, u32 x) {
    for (int i = 0; i < 4; ++i) v[off + i] = static_cast<u8>(x >> (8 * i));
}
} // namespace

TEST_CASE("script loader: accepts a valid file and exposes its tables") {
    std::vector<u8> v = sampleScript();
    ScriptProgram p;
    std::string err;
    REQUIRE_MESSAGE(ScriptProgram::load(v.data(), v.size(), p, &err), err);
    CHECK(p.code().size() == 5);
    CHECK(p.defs().size() == 1);
    CHECK(p.funcs()[0] == "sin");
    CHECK(p.frameSlots() == 24);
    CHECK(p.hasEntry(EntryPoint::Main));
    CHECK(!p.hasEntry(EntryPoint::Touch));
    CHECK(p.entry(EntryPoint::Damage) == 3);
    CHECK(p.isStringStart(0));
    CHECK(!p.isStringStart(1));
    CHECK(std::string(p.stringAt(0)) == "hello");
    CHECK(p.data().size() == 2);
}

TEST_CASE("script loader: truncation at every length is rejected") {
    std::vector<u8> v = sampleScript();
    for (size_t n = 0; n < v.size(); ++n) {
        std::vector<u8> t(v.begin(), v.begin() + static_cast<long>(n));
        INFO("length ", n);
        CHECK(!loads(t));
    }
    CHECK(!loads({}));
}

TEST_CASE("script loader: structural errors are rejected") {
    std::vector<u8> good = sampleScript();
    auto bad = [&](const char* what, auto mutate) {
        std::vector<u8> v = good;
        mutate(v);
        INFO(what);
        CHECK(!loads(v));
    };
    bad("magic", [](auto& v) { v[0] = 'X'; });
    bad("defs count", [](auto& v) { put32(v, 12, 2); });
    bad("func count", [](auto& v) { put32(v, 16, 0); });
    bad("data count", [](auto& v) { put32(v, 20, 3); });
    // 8 * 0x20000002 wraps to 16 in 32 bits, the length of the two real entries.
    bad("data count wrapping 32 bits", [](auto& v) { put32(v, 20, 0x20000002u); });
    bad("frame size", [](auto& v) { put32(v, 24, 4); });
    bad("strg size", [](auto& v) { put32(v, 28, 99); });
    bad("instr count", [](auto& v) { put32(v, 32, 6); });
    bad("entry out of range", [](auto& v) { put32(v, 0x28, 5); });
    bad("entry out of range 2", [](auto& v) { put32(v, 0x2C, 0x7FFFFFFF); });
    // instruction 2 (JZ) starts at CODE payload + 28: find it from the end of the file
    bad("jump target", [](auto& v) { put32(v, v.size() - 14 * 3 + 6, 100); });
    bad("unknown opcode", [](auto& v) { v[v.size() - 14] = 0x40; });
    bad("slot out of range", [](auto& v) { put32(v, v.size() - 14 * 5 + 2, 200); });
    bad("global out of range", [](auto& v) { put32(v, v.size() - 14 * 2 + 6, static_cast<u32>(-9)); });
    bad("func out of range", [](auto& v) { put32(v, v.size() - 14 * 4 + 2, static_cast<u32>(-9)); });
    bad("trailing section overrun", [](auto& v) { put32(v, 0x38 + 4, 0x10000); });
    bad("duplicate section", [](auto& v) {
        std::vector<u8> extra(v.end() - 8 - 14 * 5, v.end());
        v.insert(v.end(), extra.begin(), extra.end());
    });
    // An unknown extra section is skipped, as in the original loader.
    std::vector<u8> extra = good;
    const char tag[] = "ZZZZ";
    extra.insert(extra.end(), tag, tag + 4);
    for (int i = 0; i < 4; ++i) extra.push_back(i == 0 ? 2 : 0);
    extra.push_back(1);
    extra.push_back(2);
    CHECK(loads(extra));
}

namespace {
void fuzzOne(const std::vector<u8>& base, u32 seed, int iterations) {
    Rng rng(seed);
    for (int it = 0; it < iterations; ++it) {
        std::vector<u8> v = base;
        int edits = 1 + static_cast<int>(rng.next() % 4);
        for (int e = 0; e < edits; ++e) {
            size_t pos = rng.next() % v.size();
            switch (rng.next() % 3) {
                case 0: v[pos] = static_cast<u8>(rng.next()); break;
                case 1: v[pos] ^= static_cast<u8>(1u << (rng.next() % 8)); break;
                default: v[pos] = (rng.next() & 1) ? 0xFF : 0x00; break;
            }
        }
        if (rng.next() % 16 == 0) v.resize(rng.next() % v.size());
        ScriptProgram p;
        if (!ScriptProgram::load(v.data(), v.size(), p, nullptr)) continue;
        rcsl_tool::RunOutcome o = rcsl_tool::runStandard(p, 8, 1.0f / 60.0f, "touch@2,damage@3,callback@4:1", true,
                                                         nullptr, nullptr);
        (void)o; // an error is fine; a crash or hang is not (the stall detector bounds runs)
    }
}
} // namespace

TEST_CASE("script loader: mutation fuzz of a synthetic program never crashes or hangs") {
    fuzzOne(sampleScript(), 12345, 4000);
}

TEST_CASE("script loader: mutation fuzz of real scripts never crashes or hangs") {
    AS3D_REQUIRE_DATA();
    const char* names[] = {"scripts\\tanks\\tank.scr", "scripts\\boss1\\boss1_rl.scr", "scripts\\barrel\\barrel_benzin_dead.scr",
                           "scripts\\train.scr"};
    u32 seed = 1;
    for (const char* n : names) {
        Blob blob;
        if (!testdata::readExtracted(n, blob)) continue;
        fuzzOne(blob, seed++, 1500);
    }
}
